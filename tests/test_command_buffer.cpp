#include "test_framework.hpp"

#include "gamev2/ecs/command_buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::Diagnostic;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 0;
};

}  // namespace

TEST(command_buffer_defers_changes_until_flush)
{
    World world;
    const auto e = world.create();

    CommandBuffer cmd;
    cmd.add<Position>(e, Position{5});
    CHECK_EQ(cmd.pending(), std::size_t{1});
    CHECK(world.get_if<Position>(e) == nullptr);

    cmd.flush(world);
    CHECK_EQ(cmd.pending(), std::size_t{0});
    CHECK(world.get_if<Position>(e) != nullptr);
}

TEST(command_buffer_preserves_enqueue_order)
{
    World world;
    const auto e = world.create();

    // 交错而非连续 add：World::add 拒绝重复组件，连续 add 只有第一条生效，
    // 那样读不到执行顺序。本用例覆盖的是「整体倒序执行」与「相邻 add/remove
    // 交换」两类变异都会改掉最终值；它并不穷尽全部排列（(remove, add3,
    // add1) 同样得 3）。
    CommandBuffer cmd;
    cmd.add<Position>(e, Position{1});
    cmd.remove<Position>(e);
    cmd.add<Position>(e, Position{3});
    cmd.flush(world);

    const auto* p = world.get_if<Position>(e);
    CHECK(p != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 3);
    }
}

TEST(command_buffer_rejects_add_after_destroy)
{
    World world;
    const auto e = world.create();

    std::vector<std::string> codes;
    world.set_diagnostic_handler(
        [&codes](const Diagnostic& d) { codes.emplace_back(d.code); });

    CommandBuffer cmd;
    cmd.destroy(e);
    cmd.add<Position>(e, Position{1});
    cmd.flush(world);

    CHECK(!world.is_alive(e));
    CHECK_EQ(codes.size(), std::size_t{1});
    if (!codes.empty())
    {
        CHECK(codes[0] == "ecs.entity_invalid");
    }
}

TEST(command_buffer_remove_applies_at_flush)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{1});
    (void)world.add<Velocity>(e, Velocity{2});

    CommandBuffer cmd;
    cmd.remove<Velocity>(e);
    CHECK(world.has<Velocity>(e));

    cmd.flush(world);
    CHECK(!world.has<Velocity>(e));
    CHECK(world.has<Position>(e));
}

TEST(command_buffer_append_keeps_order)
{
    World world;
    const auto a = world.create();
    const auto b = world.create();

    CommandBuffer first;
    first.add<Position>(a, Position{10});

    CommandBuffer second;
    second.add<Position>(b, Position{20});

    first.append(std::move(second));
    first.flush(world);

    const auto* pa = world.get_if<Position>(a);
    const auto* pb = world.get_if<Position>(b);
    CHECK(pa != nullptr);
    CHECK(pb != nullptr);
    if (pa != nullptr)
    {
        CHECK_EQ(pa->x, 10);
    }
    if (pb != nullptr)
    {
        CHECK_EQ(pb->x, 20);
    }
}

TEST(command_buffer_append_drains_source)
{
    World world;
    const auto a = world.create();
    const auto b = world.create();

    CommandBuffer first;
    first.add<Position>(a, Position{1});

    CommandBuffer second;
    second.add<Position>(b, Position{2});

    first.append(std::move(second));
    // 只断言 pending：源队列若仍留着被搬空的 std::function，这里会打出
    // 一行 FAIL 就够了。刻意不再 flush 它 —— 那会把确定的失败升级成
    // 异常逃出 main 的挂死，失去指向性。
    CHECK_EQ(second.pending(), std::size_t{0});

    first.flush(world);
    const auto* pb = world.get_if<Position>(b);
    CHECK(pb != nullptr);
    if (pb != nullptr)
    {
        CHECK_EQ(pb->x, 2);
    }
    CHECK_EQ(first.pending(), std::size_t{0});
}

TEST(command_buffer_flush_of_empty_buffer_is_noop)
{
    World world;
    std::size_t diagnostics = 0;
    world.set_diagnostic_handler(
        [&diagnostics](const Diagnostic&) { ++diagnostics; });

    CommandBuffer cmd;
    cmd.flush(world);
    CHECK_EQ(world.entity_count(), std::size_t{0});
    CHECK_EQ(cmd.pending(), std::size_t{0});
    // noop 不只是不动实体：flush 一条命令都不该产生诊断。
    CHECK_EQ(diagnostics, std::size_t{0});
}

TEST(command_buffer_create_makes_entity_with_components)
{
    World world;
    gamev2::ecs::EntityId fresh{};
    // flush 之前实体还不存在：create 是唯一一种目标在 flush 期才诞生的命令。
    CHECK_EQ(world.entity_count(), std::size_t{0});
    CHECK(!fresh);

    CommandBuffer cmd;
    cmd.create(fresh, Position{7}, Velocity{3});
    CHECK_EQ(cmd.pending(), std::size_t{1});
    CHECK_EQ(world.entity_count(), std::size_t{0});

    cmd.flush(world);

    // 回写目标是实打实的新句柄：alive，且版本号非 0（0 是非法哨兵）。
    CHECK(world.is_alive(fresh));
    CHECK_EQ(fresh.version(), std::uint32_t{1});
    CHECK_EQ(world.entity_count(), std::size_t{1});

    const auto* p = world.get_if<Position>(fresh);
    const auto* v = world.get_if<Velocity>(fresh);
    CHECK(p != nullptr);
    CHECK(v != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 7);
    }
    if (v != nullptr)
    {
        CHECK_EQ(v->dx, 3);
    }
}

TEST(command_buffer_create_without_components_still_makes_entity)
{
    // 零组件分支：变参包为空时下标展开是空包展开，不是「漏挂了一个组件」。
    World world;
    gamev2::ecs::EntityId fresh{};

    CommandBuffer cmd;
    cmd.create(fresh);
    cmd.flush(world);

    CHECK(world.is_alive(fresh));
    CHECK_EQ(world.entity_count(), std::size_t{1});
    CHECK(world.get_if<Position>(fresh) == nullptr);
}

TEST(command_buffer_create_reports_no_diagnostic)
{
    // create 的 target 是「还没有」而不是「已失效」，因此 flush 的
    // 「目标已失效则拒绝」判据对它不适用：不该报任何诊断。
    // （放宽这条判据的正是 Command::target 的 optional 化，见其注释。）
    World world;
    std::size_t diagnostics = 0;
    world.set_diagnostic_handler(
        [&diagnostics](const Diagnostic&) { ++diagnostics; });

    gamev2::ecs::EntityId fresh{};
    CommandBuffer cmd;
    cmd.create(fresh, Position{1});
    cmd.flush(world);

    CHECK_EQ(diagnostics, std::size_t{0});
    CHECK(world.is_alive(fresh));
}

TEST(command_buffer_rejects_commands_aimed_at_empty_handle)
{
    // 误传 EntityId{} 给 add / destroy / remove 是一个已失效的目标，而不是
    // 「无目标」：三者都必须照常报 ecs.entity_invalid。这条判据在 create 落地
    // 之前一直成立，落地之后一度随「空 target 当 create 标记」一起失效。
    World world;
    std::vector<std::string> codes;
    world.set_diagnostic_handler(
        [&codes](const Diagnostic& d) { codes.emplace_back(d.code); });

    CommandBuffer cmd;
    cmd.add<Position>(gamev2::ecs::EntityId{}, Position{1});
    cmd.remove<Position>(gamev2::ecs::EntityId{});
    cmd.destroy(gamev2::ecs::EntityId{});
    cmd.flush(world);

    CHECK_EQ(codes.size(), std::size_t{3});
    for (const auto& code : codes)
    {
        CHECK(code == "ecs.entity_invalid");
    }
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(command_buffer_many_commands)
{
    World world;
    std::vector<gamev2::ecs::EntityId> entities;
    for (int i = 0; i < 500; ++i)
    {
        entities.push_back(world.create());
    }

    CommandBuffer cmd;
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        cmd.add<Position>(entities[i], Position{static_cast<int>(i)});
    }
    cmd.flush(world);

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const auto* p = world.get_if<Position>(entities[i]);
        CHECK(p != nullptr);
        if (p != nullptr)
        {
            CHECK_EQ(p->x, static_cast<int>(i));
        }
    }
}
