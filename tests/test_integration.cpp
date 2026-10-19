#include "test_framework.hpp"

#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/view.hpp"

#include <tuple>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::Scheduler;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 1;
};

struct Health
{
    int hp = 100;
};

struct Spawner
    : gamev2::ecs::System
{
    int spawned = 0;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer& cmd) override
    {
        const auto e = world.create();
        cmd.add<Position>(e, Position{0});
        cmd.add<Velocity>(e, Velocity{1});
        cmd.add<Health>(e, Health{100});
        ++spawned;
    }
};

struct Movement
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Velocity, Position>().each(
            [](const Velocity& v, Position& p) { p.x += v.dx; });
    }
};

struct Damage
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Health>().each([](Health& h) { h.hp -= 1; });
    }
};

/// 两个写同一组件的 System，都按「当前值」推导新值，因此彼此不可交换。
/// 最终值能反推出两者的生效先后，而不只是说明「两个都跑过了」。
struct AppendDigit
    : gamev2::ecs::System
{
    int digit = 0;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each(
            [this](Position& p) { p.x = p.x * 10 + digit; });
    }
};

struct DoubleValue
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each([](Position& p) { p.x = p.x * 2; });
    }
};

/// 统计世界里带 Ts 的实体数。延迟落地类断言只能靠它观察中间态。
template<typename T>
[[nodiscard]] std::size_t count_components(World& world)
{
    std::size_t n = 0;
    world.view<const T>().each([&n](const T&) { ++n; });
    return n;
}

}  // namespace

TEST(integration_frame_loop_is_stable)
{
    World world;
    Scheduler scheduler{0};
    Spawner spawner;
    Movement movement;
    Damage damage;

    scheduler.add(spawner);
    scheduler.add(movement);
    scheduler.add(damage);

    CommandBuffer cmd;
    for (int frame = 0; frame < 200; ++frame)
    {
        scheduler.run(world, cmd);
    }

    CHECK_EQ(spawner.spawned, 200);
    CHECK_EQ(world.entity_count(), std::size_t{200});

    // Spawner 无读写集，与另两个 System 同批。worker 数为 0 时退化为
    // 按注册顺序串行：第 k 帧创建的实体在 flush 之后才拿到组件，
    // 从第 k+1 帧起每帧移动 1 点，累计 199 + 198 + ... + 0 = 19900。
    int moved = 0;
    world.view<const Position>().each(
        [&moved](const Position& p) { moved += p.x; });
    CHECK_EQ(moved, 19900);
}

TEST(integration_independent_systems_share_a_batch)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{0});
    // Movement 的访问集是 <const Velocity, Position>：缺 Velocity 就一个实体
    // 都匹配不到，Position 会原地不动。
    (void)world.add<Velocity>(e, Velocity{1});
    (void)world.add<Health>(e, Health{100});

    Scheduler scheduler{0};
    Movement movement;
    Damage damage;
    scheduler.add(movement);
    scheduler.add(damage);
    scheduler.plan();

    // Movement 与 Damage 的访问集不相交，应同批。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    const auto* p = world.get_if<Position>(e);
    const auto* h = world.get_if<Health>(e);
    CHECK(p != nullptr && h != nullptr);
    if (p != nullptr && h != nullptr)
    {
        CHECK_EQ(p->x, 1);
        CHECK_EQ(h->hp, 99);
    }
}

TEST(integration_survives_mass_destruction)
{
    World world;
    std::vector<EntityId> entities;

    CommandBuffer cmd;
    for (int i = 0; i < 1000; ++i)
    {
        const auto e = world.create();
        cmd.add<Position>(e, Position{i});
        cmd.add<Health>(e, Health{i});
        entities.push_back(e);
    }
    cmd.flush(world);
    CHECK_EQ(world.entity_count(), std::size_t{1000});

    for (std::size_t i = 0; i < entities.size(); i += 2)
    {
        world.destroy(entities[i]);
    }
    CHECK_EQ(world.entity_count(), std::size_t{500});

    std::size_t intact = 0;
    for (std::size_t i = 1; i < entities.size(); i += 2)
    {
        const auto* p = world.get_if<Position>(entities[i]);
        if (p != nullptr && p->x == static_cast<int>(i))
        {
            ++intact;
        }
    }
    CHECK_EQ(intact, std::size_t{500});
}

TEST(integration_command_buffer_defers_to_frame_end)
{
    World world;
    Scheduler scheduler{0};
    Spawner spawner;
    scheduler.add(spawner);

    CommandBuffer cmd;

    // Scheduler::run 自己在末尾 flush，中间态从它身上看不到，只能直接调
    // execute 把帧末那一拍停在手上。
    spawner.execute(world, cmd);

    // create 立即生效：实体数已经涨了。组件还堵在 CommandBuffer 里没落地。
    CHECK_EQ(world.entity_count(), std::size_t{1});
    CHECK_EQ(cmd.pending(), std::size_t{3});
    CHECK_EQ(count_components<Position>(world), std::size_t{0});
    CHECK_EQ(count_components<Health>(world), std::size_t{0});

    cmd.flush(world);

    // 帧末 flush 之后，两个组件才各自落地一次。
    CHECK_EQ(cmd.pending(), std::size_t{0});
    CHECK_EQ(count_components<Position>(world), std::size_t{1});
    CHECK_EQ(count_components<Health>(world), std::size_t{1});

    // 走完整帧循环：第二个实体同样只在本帧的 flush 拿到组件。
    scheduler.run(world, cmd);
    CHECK_EQ(world.entity_count(), std::size_t{2});
    CHECK_EQ(count_components<Position>(world), std::size_t{2});
    CHECK_EQ(count_components<Health>(world), std::size_t{2});
}

/// 写同一组件的两个 System 必须被拆批，且按分批顺序生效。
///
/// 简报只钉了「不相交则同批」这一侧，没钉「冲突则串行化」这一侧，
/// 而后者才是分批机制存在的理由。两个 System 都按当前值推导新值，
/// 顺序换了结果就变，因此最终值可反推生效先后。
TEST(integration_conflicting_systems_serialize_in_batch_order)
{
    World world;
    for (int i = 0; i < 16; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{0});
    }

    Scheduler scheduler{0};
    AppendDigit appender;
    appender.digit = 3;
    DoubleValue doubler;
    scheduler.add(appender);
    scheduler.add(doubler);
    scheduler.plan();

    // 都写 Position：写-写冲突必须拆成两批，否则批内并发写同一列是竞争。
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // 注册顺序即批次顺序：先 appender（0 -> 3），再 doubler（3 -> 6）。
    // 顺序反过来是 0 -> 0 -> 3，与 6 完全不同，断言因此可证伪。
    int ok = 0;
    world.view<const Position>().each(
        [&ok](const Position& p) { ok += (p.x == 6); });
    CHECK_EQ(ok, 16);
}
