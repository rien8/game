#include "test_framework.hpp"

#include "gamev2/ecs/view.hpp"

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

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

struct Health
{
    int hp = 0;
};

}  // namespace

TEST(view_matches_only_full_component_sets)
{
    World world;
    const auto a = world.create();
    const auto b = world.create();
    (void)world.add<Position>(a, Position{1});
    (void)world.add<Position>(b, Position{2});
    (void)world.add<Velocity>(b, Velocity{3});

    std::vector<int> seen;
    world.view<Position, Velocity>().each(
        [&seen](Position& p, Velocity& v) { seen.push_back(p.x + v.dx); });

    CHECK_EQ(seen.size(), std::size_t{1});
    if (!seen.empty())
    {
        CHECK_EQ(seen[0], 5);
    }
}

TEST(view_readonly_param_yields_values)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{42});

    int total = 0;
    world.view<const Position>().each(
        [&total](const Position& p) { total += p.x; });
    CHECK_EQ(total, 42);
}

TEST(view_visits_every_entity_across_chunks)
{
    World world;
    for (int i = 0; i < 5000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
    }

    std::vector<int> seen;
    seen.reserve(5000);
    world.view<Health>().each([&seen](Health& h) { seen.push_back(h.hp); });
    CHECK_EQ(seen.size(), std::size_t{5000});

    long long sum = 0;
    for (const auto v : seen)
    {
        sum += v;
    }
    CHECK_EQ(sum, 4999LL * 5000 / 2);
}

TEST(view_parallel_matches_sequential)
{
    World world;
    for (int i = 0; i < 20000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i});
        (void)world.add<Velocity>(e, Velocity{2 * i});
    }

    std::vector<int> seq;
    std::vector<int> par;
    seq.reserve(20000);
    par.reserve(20000);

    world.view<Position, Velocity>().each(
        [&seq](Position& p, Velocity& v) { seq.push_back(p.x + v.dx); });

    // 逐项比对之前先确认没有静默走串行退化分支，否则这条用例会变成
    // 「串行 == 串行」的空转断言。只判 parallelize_allowed() 不够：它只看
    // in_system_ 标志，而 hardware_concurrency<=2 时 default_worker_count()
    // 返回 0，池里没有 worker，each_parallel 仍会全串行跑完。守卫按池是否
    // 真有 worker 收紧，写法与 test_scheduler.cpp 同形。
    if (world.parallel_pool().size() > 0)
    {
        CHECK(world.parallelize_allowed());
    }

    // chunk 由多个 worker 分领，回调内的共享状态必须自行同步。
    // 无锁 push_back 是数据竞争：实测 size 直接少于 20000（丢元素），
    // 且元素被释放两次。
    std::mutex mtx;
    world.view<Position, Velocity>().each_parallel(
        [&par, &mtx](Position& p, Velocity& v)
        {
            const int sum = p.x + v.dx;
            const std::lock_guard<std::mutex> lock{mtx};
            par.push_back(sum);
        });

    // 遍历顺序不是 each_parallel 的契约：chunk 落到哪个 worker 由竞争决定，
    // 并行结果本就允许与串行顺序不同。逐项比对前先归一化顺序，否则断言的
    // 是实现从未承诺过的性质，而不是「有没有漏访 / 多访」。
    std::ranges::sort(seq);
    std::ranges::sort(par);

    CHECK_EQ(seq.size(), par.size());
    if (seq.size() == par.size())
    {
        bool same = true;
        for (std::size_t i = 0; i < seq.size(); ++i)
        {
            if (seq[i] != par[i])
            {
                same = false;
                break;
            }
        }
        CHECK(same);
    }
}

TEST(view_parallel_writes_accumulate)
{
    World world;
    for (int i = 0; i < 10000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{1});
    }

    // 并行的前提有两个，缺一不可：in_system_ 未置位（否则静默走串行分支），
    // 且池里真有 worker（parallelize_allowed() 只看前一个，cores<=2 时
    // default_worker_count() 返回 0，run() 会全串行跑完）。两个前提绑在
    // 一起断言：池为空时如实跳过，不把环境差异报成假红。
    if (world.parallel_pool().size() > 0)
    {
        CHECK(world.parallelize_allowed());
    }

    world.view<Health>().each_parallel([](Health& h) { h.hp += 100; });

    std::size_t updated = 0;
    world.view<Health>().each([&updated](const Health& h)
                               { updated += (h.hp == 101) ? 1u : 0u; });
    CHECK_EQ(updated, std::size_t{10000});
}

/// 确定性退化探针：断言执行线程身份，不断言求和值。
///
/// 与 test_world.cpp 的 view_parallel_falls_back_to_serial_inside_system 正交：
/// 那条断言求和值，退化分支失效后要靠无锁 sum += 撞上数据竞争才可能变红，
/// 实测 30 次跑出 2 次假绿；求和一旦改成原子写法，那条就彻底测不到退化。
/// 本条断言 distinct thread 数，与「求和是否恰好算对」无关，求和实现换成
/// 无竞争的原子写法后依然能检出退化失效。
///
/// 实体数刻意取大：Health 是 4 字节，chunk 容量 1024，20000 个实体产生 20 个
/// chunk / 20 个 job。ThreadPool::run 通知 worker 后调用线程会立刻开始 drain，
/// job 太少时它可能在 worker 被唤醒前就把队列排空，反而退化成零并发而漏检。
TEST(view_parallel_in_system_stays_on_calling_thread)
{
    constexpr int kEntities = 20000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{1});
    }

    std::mutex mtx;
    std::vector<std::thread::id> thread_ids;
    thread_ids.reserve(kEntities);
    const std::thread::id caller = std::this_thread::get_id();

    world.enter_system_execution();
    world.view<Health>().each_parallel(
        [&thread_ids, &mtx](Health&)
        {
            const std::lock_guard<std::mutex> lock{mtx};
            thread_ids.push_back(std::this_thread::get_id());
        });
    world.exit_system_execution();

    CHECK_EQ(thread_ids.size(), static_cast<std::size_t>(kEntities));

    std::vector<std::thread::id> distinct = thread_ids;
    std::ranges::sort(distinct);
    const auto deduped = std::ranges::unique(distinct);
    distinct.erase(deduped.begin(), distinct.end());

    CHECK_EQ(distinct.size(), std::size_t{1});
    CHECK(!distinct.empty() && distinct.front() == caller);
}

TEST(view_empty_when_no_match)
{
    World world;
    const auto e = world.create();
    (void)world.add<Health>(e, Health{1});
    std::size_t count = 0;
    world.view<Position>().each([&count](Position&) { ++count; });
    CHECK_EQ(count, std::size_t{0});
}

TEST(view_spans_multiple_archetypes)
{
    World world;
    for (int i = 0; i < 100; ++i)
    {
        const auto a = world.create();
        (void)world.add<Position>(a, Position{i});
        (void)world.add<Velocity>(a, Velocity{i});

        const auto b = world.create();
        (void)world.add<Position>(b, Position{i});
        (void)world.add<Velocity>(b, Velocity{i});
        (void)world.add<Health>(b, Health{i});
    }

    std::size_t both = 0;
    std::size_t with_health = 0;
    world.view<Position, Velocity>().each([&both](Position&, Velocity&)
                                          { ++both; });
    world.view<Position, Velocity, Health>().each(
        [&with_health](Position&, Velocity&, Health&) { ++with_health; });

    CHECK_EQ(both, std::size_t{200});
    CHECK_EQ(with_health, std::size_t{100});
}
