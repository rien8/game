#include "test_framework.hpp"

#include "gamev2/ecs/thread_pool.hpp"
#include "gamev2/ecs/view.hpp"
#include "gamev2/ecs/world.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using gamev2::ecs::Diagnostic;
using gamev2::ecs::DiagnosticLevel;
using gamev2::ecs::EcsError;
using gamev2::ecs::EntityId;
using gamev2::ecs::ThreadPool;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
    int y = 0;
};

struct Velocity
{
    int dx = 0;
    int dy = 0;
};

struct Health
{
    int hp = 0;
};

std::atomic<int>& live_resources()
{
    static std::atomic<int> n{0};
    return n;
}

/// 析构可观测的资源。声明了析构就没有隐式移动构造，存储层的移动构造会
/// 落到拷贝构造函数上，因此它必须 noexcept 才过得了注册的静态断言。
template<int Tag>
struct Resource
{
    Resource()
    {
        live_resources().fetch_add(1);
    }

    Resource(const Resource&) noexcept
    {
        live_resources().fetch_add(1);
    }

    ~Resource()
    {
        live_resources().fetch_sub(1);
    }

    int v = 0;
};

/// 自定义移动构造与析构，各带一个自增计数器。存储层搬运行走移动构造，
/// moves 会跟着涨；退化成字节搬运（对 RAW 存储做移动赋值）则一次都不涨，
/// 而且源行那一份还会漏析构。两者只在这里才分得开。
struct Tracked
{
    static inline int moves = 0;
    static inline int copies = 0;
    static inline int destructions = 0;
    int value = 0;

    Tracked() = default;
    explicit Tracked(int v) : value{v} {}
    Tracked(const Tracked& other) : value{other.value} { ++copies; }
    Tracked(Tracked&& other) noexcept : value{other.value} { ++moves; }
    ~Tracked() { ++destructions; }

    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) = default;
};

/// 每个构造函数都计入 creations、每次析构计入 destructions：一批实体
/// 建了又销毁后两者必须相等，否则就是漏析构或多析构。
struct Owned
{
    static inline int creations = 0;
    static inline int destructions = 0;
    int value = 0;

    Owned() { ++creations; }
    Owned(const Owned&) { ++creations; }
    Owned(Owned&&) noexcept { ++creations; }
    ~Owned() { ++destructions; }

    Owned& operator=(const Owned&) = default;
    Owned& operator=(Owned&&) = default;
};

}  // namespace

TEST(world_report_forwards_to_installed_handler)
{
    // 不装 handler 时默认处理器写 stderr，会破坏「stderr 恒为 0 字节」
    // 的验收前提，所以这条用例同时把默认路径换成可断言的收集器。
    World world;
    std::vector<Diagnostic> seen;
    world.set_diagnostic_handler(
        [&seen](const Diagnostic& d) { seen.push_back(d); });

    world.report(Diagnostic{DiagnosticLevel::error, "E_TEST", "boom"});

    CHECK_EQ(seen.size(), std::size_t{1});
    if (seen.size() == 1)
    {
        CHECK(seen[0].code == std::string_view{"E_TEST"});
        CHECK(seen[0].level == DiagnosticLevel::error);
        CHECK_EQ(seen[0].message, std::string{"boom"});
    }
}

TEST(world_create_yields_alive_entity)
{
    World world;
    const auto e = world.create();
    CHECK(static_cast<bool>(e));
    CHECK(world.is_alive(e));
    CHECK_EQ(world.entity_count(), std::size_t{1});
}

TEST(world_first_entity_does_not_collide_with_sentinel)
{
    World world;
    const auto e = world.create();
    CHECK_EQ(e.index(), 0u);
    CHECK_EQ(e.version(), 1u);
    CHECK(static_cast<bool>(e));
    CHECK(world.is_alive(e));
}

TEST(world_add_then_get)
{
    World world;
    const auto e = world.create();
    const auto r = world.add<Position>(e, Position{3, 4});
    CHECK(r.has_value());

    const auto* p = world.get_if<Position>(e);
    CHECK(p != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 3);
        CHECK_EQ(p->y, 4);
    }
}

TEST(world_add_duplicate_returns_component_exists)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{});
    const auto r = world.add<Position>(e, Position{});
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::component_exists);
}

TEST(world_add_on_dead_entity_returns_entity_invalid)
{
    World world;
    const auto r = world.add<Position>(EntityId::make(99, 1), Position{});
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::entity_invalid);
}

TEST(world_get_missing_component_returns_null)
{
    World world;
    const auto e = world.create();
    CHECK(world.get_if<Position>(e) == nullptr);
    CHECK(!world.has<Position>(e));
}

TEST(world_remove_missing_returns_component_missing)
{
    World world;
    const auto e = world.create();
    const auto r = world.remove<Position>(e);
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::component_missing);
}

TEST(world_migration_preserves_other_components)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{10, 20});
    (void)world.add<Velocity>(e, Velocity{1, 2});
    (void)world.add<Health>(e, Health{77});

    (void)world.remove<Velocity>(e);

    CHECK(!world.has<Velocity>(e));
    const auto* p = world.get_if<Position>(e);
    const auto* h = world.get_if<Health>(e);
    CHECK(p != nullptr && h != nullptr);
    if (p != nullptr && h != nullptr)
    {
        CHECK_EQ(p->x, 10);
        CHECK_EQ(p->y, 20);
        CHECK_EQ(h->hp, 77);
    }
}

TEST(world_destroy_invalidates_stale_handle)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{5, 6});
    world.destroy(e);

    CHECK(!world.is_alive(e));
    CHECK(world.get_if<Position>(e) == nullptr);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(world_repeated_destroy_is_silent)
{
    World world;
    const auto e = world.create();
    world.destroy(e);
    world.destroy(e);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(world_recycled_slot_bumps_version)
{
    World world;
    const auto first = world.create();
    world.destroy(first);
    const auto second = world.create();

    CHECK(second.index() == first.index());
    CHECK(second.version() != first.version());
    CHECK(!world.is_alive(first));
    CHECK(world.is_alive(second));
}

TEST(world_add_relocates_tail_entity_slot)
{
    // relocate 只在源 archetype 有多个实体、且被删的不是末位时才走到。
    // 每条用例都「create 后立刻 add」的话源 archetype 永远只有 1 个实体，
    // fill_hole 走 entity_count()==1 的 pop_back 分支，relocate 是死代码。
    constexpr int kInitial = 8;
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < kInitial; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    // entities[1] 不是末位：fill_hole 会把 entities[7] 换上来，
    // 它的 slot 必须跟着改写到新位置。
    (void)world.add<Position>(entities[1], Position{1, 2});

    // 再补几个实体把腾空的第 7 行重新占掉。映射没跟着更新的实体读到
    // 的是别人的数据而不是自己的 —— 尾部实体正是最容易丢映射的那个。
    for (int i = 0; i < 4; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{static_cast<int>(entities.size())});
        entities.push_back(e);
    }

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        CHECK(h != nullptr);
        if (h != nullptr)
        {
            CHECK_EQ(h->hp, static_cast<int>(i));
        }
    }
    CHECK(world.has<Position>(entities[1]));
    CHECK(!world.has<Position>(entities[7]));
}

TEST(world_remove_relocates_tail_entity_slot)
{
    constexpr int kInitial = 8;
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < kInitial; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        (void)world.add<Position>(e, Position{i, i * 2});
        entities.push_back(e);
    }

    // 同样的道理：源 archetype 有 8 个实体，被删的 entities[1] 不是末位。
    (void)world.remove<Position>(entities[1]);

    // 新实体把腾空的第 7 行重新占掉，并且 copy_shared 会把 Health 一并
    // 写进去，映射错位的实体立刻读到错的行。
    const auto fresh = world.create();
    (void)world.add<Health>(fresh, Health{100});
    (void)world.add<Position>(fresh, Position{100, 100});

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        CHECK(h != nullptr);
        if (h != nullptr)
        {
            CHECK_EQ(h->hp, static_cast<int>(i));
        }
    }
    CHECK(!world.has<Position>(entities[1]));

    const auto* p = world.get_if<Position>(entities[7]);
    CHECK(p != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 7);
        CHECK_EQ(p->y, 14);
    }
}

TEST(world_destroy_neighbour_keeps_its_components)
{
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < 8; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    world.destroy(entities[0]);

    for (std::size_t i = 1; i < entities.size(); ++i)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        CHECK(h != nullptr);
        if (h != nullptr)
        {
            CHECK_EQ(h->hp, static_cast<int>(i));
        }
    }
}

TEST(world_many_entities_survive_churn)
{
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < 2000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    for (std::size_t i = 0; i < entities.size(); i += 2)
    {
        world.destroy(entities[i]);
    }

    std::size_t alive = 0;
    for (std::size_t i = 1; i < entities.size(); i += 2)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        if (h != nullptr && h->hp == static_cast<int>(i))
        {
            ++alive;
        }
    }
    CHECK_EQ(alive, std::size_t{1000});
    CHECK_EQ(world.entity_count(), std::size_t{1000});
}

TEST(world_destroy_runs_destructor_for_every_component)
{
    // Column 没有存活行位图，无从自行析构，逐列 destroy_row 的责任
    // 全压在 World::destroy 上：删掉那个循环本用例就会红。
    World world;
    const auto before = live_resources().load();

    const auto e = world.create();
    (void)world.add<Resource<0>>(e);
    (void)world.add<Resource<1>>(e);
    CHECK_EQ(live_resources().load(), before + 2);

    world.destroy(e);
    CHECK_EQ(live_resources().load(), before);
}

/// 本次重构的核心证据：迁移必须走移动构造，并把源行析构回 RAW。
///
/// 少了 add 里那步析构，moves 照样会涨（fill_hole 仍在移动构造），
/// destructions 却停住 —— 两个断言缺一不可。
TEST(world_add_move_constructs_shared_components)
{
    World world;
    const auto e = world.create();
    (void)world.add<Tracked>(e, Tracked{5});

    const auto moves = Tracked::moves;
    const auto destructions = Tracked::destructions;
    (void)world.add<Health>(e, Health{1});

    // Tracked 作为共有组件被移动构造进目标 archetype 的 RAW 行。
    CHECK_EQ(Tracked::moves, moves + 1);
    // 源行随即析构，空洞才能安全地接受移动构造。
    CHECK_EQ(Tracked::destructions, destructions + 1);
    const auto* t = world.get_if<Tracked>(e);
    CHECK(t != nullptr);
    if (t != nullptr)
    {
        CHECK_EQ(t->value, 5);
    }
}

TEST(world_remove_destroys_every_component_of_source_row)
{
    // remove 与 add 对称：源行必须整行析构，而不只是被移除的那一列。
    constexpr int kInitial = 8;
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < kInitial; ++i)
    {
        const auto e = world.create();
        (void)world.add<Tracked>(e, Tracked{i});
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    const auto destructions = Tracked::destructions;
    (void)world.remove<Health>(entities[1]);

    // 被迁移实体的源行一次，尾行搬进空洞后留在原位的那一次。
    CHECK_EQ(Tracked::destructions, destructions + 2);
    CHECK(!world.has<Health>(entities[1]));
    const auto* moved = world.get_if<Tracked>(entities[7]);
    CHECK(moved != nullptr);
    if (moved != nullptr)
    {
        CHECK_EQ(moved->value, 7);
    }
    const auto* kept = world.get_if<Tracked>(entities[3]);
    CHECK(kept != nullptr);
    if (kept != nullptr)
    {
        CHECK_EQ(kept->value, 3);
    }
}

TEST(world_destruction_balance_across_full_churn)
{
    constexpr int kEntities = 500;
    World world;
    const auto creations = Owned::creations;
    const auto destructions = Owned::destructions;

    std::vector<EntityId> entities;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Owned>(e);
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }
    for (const auto e : entities)
    {
        world.destroy(e);
    }

    CHECK_EQ(Owned::creations - creations,
             Owned::destructions - destructions);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

/// world_destruction_balance_across_full_churn 的断言发生在 World 析构之前，
/// 照不到「没人离开 archetype 的行」这条路径。World 析构是组件析构的最后
/// 一道机会：这些行随 World 一起消失，之后再没有任何显式调用会走到它们。
TEST(world_destruction_destroys_rows_still_alive)
{
    // 2500 个 4 字节组件按 1024 行/chunk 切成 3 块，避免只测到单 chunk。
    constexpr int kEntities = 2500;
    const auto creations = Owned::creations;
    const auto destructions = Owned::destructions;

    {
        World world;
        for (int i = 0; i < kEntities; ++i)
        {
            const auto e = world.create();
            (void)world.add<Owned>(e);
            // 每三行制造一次跨 archetype 迁移：迁移会搬走并析构自己的两行，
            // 剩下的行只剩 World 析构这一条收尾路径。同时它也逼出
            // Storage::archetypes_ 的扩容，钉住 Archetype 的移动语义。
            if (i % 3 == 0)
            {
                (void)world.add<Health>(e, Health{i});
            }
        }

        // 前提：此刻 kEntities 个 Owned 全部存活，一个都没被析构过。
        const auto live = Owned::creations - creations -
                          (Owned::destructions - destructions);
        CHECK_EQ(live, kEntities);
    }

    // World 已出作用域：仍存活的行必须已全部析构完，一次不多一次不少。
    CHECK_EQ(Owned::creations - creations,
             Owned::destructions - destructions);
}

TEST(thread_pool_runs_every_job_exactly_once)
{
    ThreadPool pool{4};
    CHECK_EQ(pool.size(), std::size_t{4});

    constexpr std::size_t kJobs = 5000;
    std::vector<std::atomic<int>> hits(kJobs);
    std::atomic<int> total{0};

    std::vector<std::function<void()>> jobs;
    jobs.reserve(kJobs);
    for (std::size_t i = 0; i < kJobs; ++i)
    {
        jobs.emplace_back([&hits, &total, i]
                          {
                              hits[i].fetch_add(1);
                              total.fetch_add(1);
                          });
    }

    pool.run(jobs);

    // 漏跑会让 total 偏低，重复跑会让某个 hits 超过 1，两者都要抓住。
    CHECK_EQ(total.load(), static_cast<int>(kJobs));
    for (std::size_t i = 0; i < kJobs; ++i)
    {
        CHECK_EQ(hits[i].load(), 1);
    }
}

TEST(thread_pool_settles_every_batch_before_returning)
{
    ThreadPool pool{4};
    constexpr int kBatches = 40;
    constexpr int kPerBatch = 64;
    std::atomic<int> executed{0};

    for (int b = 0; b < kBatches; ++b)
    {
        std::vector<std::function<void()>> jobs;
        jobs.reserve(static_cast<std::size_t>(kPerBatch));
        for (int i = 0; i < kPerBatch; ++i)
        {
            jobs.emplace_back(
                [&executed]
                {
                    // 空任务体太快，worker 还没醒来就被调用线程吃光，
                    // 分发路径等于没被走到。这里花掉一点真实时间。
                    for (int s = 0; s < 200; ++s)
                    {
                        std::atomic_signal_fence(std::memory_order_seq_cst);
                    }
                    executed.fetch_add(1);
                });
        }
        pool.run(jobs);
        // 每批都必须结清：欠账留给下一批，或上一批的句柄被重复执行，
        // 都会让这一行对不上。
        CHECK_EQ(executed.load(), kPerBatch * (b + 1));
    }
}

TEST(thread_pool_empty_batch_returns_without_work)
{
    ThreadPool pool{2};
    const std::vector<std::function<void()>> none;
    pool.run(none);
    CHECK_EQ(pool.size(), std::size_t{2});
}

TEST(thread_pool_without_workers_runs_on_calling_thread)
{
    ThreadPool pool{0};
    CHECK_EQ(pool.size(), std::size_t{0});

    std::atomic<bool> ran{false};
    const auto caller = std::this_thread::get_id();
    std::thread::id seen{};
    const std::vector<std::function<void()>> jobs{
        [&]
        {
            seen = std::this_thread::get_id();
            ran.store(true);
        }};

    pool.run(jobs);

    CHECK(ran.load());
    CHECK(seen == caller);
}

TEST(view_each_visits_every_matching_entity)
{
    // Position 为 8 字节，chunk 容量 4096/8 = 512，3000 个实体必然跨
    // chunk，遍历要覆盖「除尾块外全满」的布局。
    constexpr int kEntities = 3000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i, i * 2});
    }

    long long sum = 0;
    std::size_t visited = 0;
    world.view<Position>().each(
        [&sum, &visited](Position& p)
        {
            sum += p.x;
            ++visited;
        });

    CHECK_EQ(visited, static_cast<std::size_t>(kEntities));
    CHECK_EQ(sum, 1LL * kEntities * (kEntities - 1) / 2);
}

TEST(view_filters_on_every_component)
{
    constexpr int kEntities = 3000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i, 0});
        if (i % 3 == 0)
        {
            (void)world.add<Velocity>(e, Velocity{1, 1});
        }
    }

    long long pos_sum = 0;
    long long vel_sum = 0;
    std::size_t visited = 0;
    // 两个参数必须分别落在各自的列上。这是编译期约束而非静默行为错误：
    // 把基址全绑到第 0 列（column_base<0>(...) ...）会直接报
    // pack expansion does not contain any unexpanded parameter packs。
    world.view<Position, Velocity>().each(
        [&pos_sum, &vel_sum, &visited](Position& p, Velocity& v)
        {
            pos_sum += p.x;
            vel_sum += v.dx;
            ++visited;
        });

    CHECK_EQ(visited, std::size_t{1000});
    CHECK_EQ(vel_sum, 1000LL);
    CHECK_EQ(pos_sum, 1498500LL);
}

TEST(view_each_parallel_matches_serial_result)
{
    constexpr int kEntities = 3000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i, 0});
    }

    std::atomic<long long> sum{0};
    std::atomic<std::size_t> visited{0};
    // 守卫必须连池一起看：parallelize_allowed() 只读 in_system_ 标志，而
    // hardware_concurrency<=2 时 default_worker_count() 返回 0，池里没有
    // worker，each_parallel 必然全串行跑完 —— 此时「走了并行分支」是空话。
    // 写法与 test_scheduler.cpp 同形：池为空时如实跳过，不报假红。
    if (world.parallel_pool().size() > 0)
    {
        CHECK(world.parallelize_allowed());
    }
    world.view<Position>().each_parallel(
        [&sum, &visited](Position& p)
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            sum.fetch_add(p.x);
            visited.fetch_add(1);
        });

    CHECK_EQ(visited.load(), static_cast<std::size_t>(kEntities));
    CHECK_EQ(sum.load(), 1LL * kEntities * (kEntities - 1) / 2);
}

TEST(view_parallel_falls_back_to_serial_inside_system)
{
    constexpr int kEntities = 3000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i, 0});
    }

    world.enter_system_execution();
    CHECK(!world.parallelize_allowed());

    long long sum = 0;
    world.view<Position>().each_parallel([&sum](Position& p) { sum += p.x; });

    world.exit_system_execution();
    CHECK(world.parallelize_allowed());
    CHECK_EQ(sum, 1LL * kEntities * (kEntities - 1) / 2);
}
