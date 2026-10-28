#include "test_framework.hpp"

#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/view.hpp"

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::Scheduler;
using gamev2::ecs::World;
using gamev2::ecs::read_set_of;
using gamev2::ecs::write_set_of;

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

struct WritesPosition
    : gamev2::ecs::System
{
    int delta = 1;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each([this](Position& p) { p.x += delta; });
    }
};

struct WritesPositionOther
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each([](Position& p) { p.x += 100; });
    }
};

struct ReadsPosition
    : gamev2::ecs::System
{
    int observed = 0;

    using ReadSet = std::tuple<const Position>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Position>().each(
            [this](const Position& p) { observed += p.x; });
    }
};

struct ReadsVelocity
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Velocity>().each(
            [](const Velocity&) {});
    }
};

struct WritesHealth
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Health>().each([](Health& h) { h.hp -= 1; });
    }
};

struct Mixed
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

/// ReadSet 里不带 const 的元素：声明为读，但没承诺只读，因此计入写集。
/// 与 Mixed 正好构成一对，把「const 决定读还是写」钉死。
struct MutatesViaReadSet
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<Velocity>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Velocity>().each([](Velocity& v) { v.dx += 1; });
    }
};

/// 记录自己跑在哪个线程上，并顺手探测 in_system_ 标志。
struct ThreadProbe
    : gamev2::ecs::System
{
    /// 自旋上界。worker 唤醒是微秒级，给足余量即可；
    /// 上界的作用是「池里没有并行」时也能退出，而不是挂死。
    static constexpr int kSpinLimit = 200000;

    std::mutex* mtx = nullptr;
    std::vector<std::thread::id>* ids = nullptr;
    std::atomic<bool>* saw_parallelize_allowed = nullptr;
    std::atomic<int>* started = nullptr;
    std::atomic<bool>* overlapped = nullptr;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        const int n = started->fetch_add(1) + 1;
        // 第一个进来的 System 自旋到「第二个进来」为止。它卡在这里时，
        // 所属线程无法去执行第二个 job，所以 started 涨到 2 只可能是另一条
        // 线程跑到了第二个 System。于是「池里是否真有并行」成了确定性断言，
        // 而不是「调用线程有没有抢在 worker 醒来前排空队列」的概率赌注。
        if (n == 1)
        {
            for (int spin = 0; spin < kSpinLimit; ++spin)
            {
                if (started->load() >= 2)
                {
                    overlapped->store(true);
                    break;
                }
                std::this_thread::yield();
            }
        }

        if (saw_parallelize_allowed != nullptr)
        {
            saw_parallelize_allowed->store(world.parallelize_allowed());
        }
        const std::lock_guard<std::mutex> lock{*mtx};
        ids->push_back(std::this_thread::get_id());
    }
};

/// 只声明 Position 读+写，在自己的 execute 里用 each_parallel 遍历。
/// 用于钉住「单 System 批次仍能内部按 chunk 并行」。
struct InnerParallelProbe
    : gamev2::ecs::System
{
    static constexpr int kSpinLimit = 200000;

    std::atomic<int>* started = nullptr;
    std::atomic<bool>* overlapped = nullptr;
    std::atomic<int>* visited = nullptr;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each_parallel(
            [this](Position& p)
            {
                p.x += 1;
                const int n = started->fetch_add(1) + 1;
                visited->fetch_add(1);
                // 第一个回调自旋到「第二个回调进来」为止。它卡住时所属线程
                // 无法处理下一个 chunk，所以 started 涨到 2 只可能是另一条
                // 线程在跑第二个 chunk —— 这正是「内部并行真的发生了」。
                if (n == 1)
                {
                    for (int spin = 0; spin < kSpinLimit; ++spin)
                    {
                        if (started->load() >= 2)
                        {
                            overlapped->store(true);
                            break;
                        }
                        std::this_thread::yield();
                    }
                }
            });
    }
};

/// 同批并发的两个 System 之一：各自持有互不相交的组件写集，
/// 且都真的构造 View 并遍历大量实体，用于钉住批内并发 View 路径。
struct ConcurrentViewA
    : gamev2::ecs::System
{
    std::atomic<bool>* saw_parallelize_allowed = nullptr;
    int delta = 1;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        saw_parallelize_allowed->store(world.parallelize_allowed());
        world.view<Position>().each([this](Position& p) { p.x += delta; });
    }
};

struct ConcurrentViewB
    : gamev2::ecs::System
{
    std::atomic<bool>* saw_parallelize_allowed = nullptr;
    int delta = 100;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World& world, CommandBuffer&) override
    {
        saw_parallelize_allowed->store(world.parallelize_allowed());
        world.view<Health>().each([this](Health& h) { h.hp -= delta; });
    }
};

}  // namespace

TEST(access_sets_derive_from_declaration)
{
    CHECK_EQ(write_set_of<WritesPosition>().size(), std::size_t{1});
    CHECK_EQ(read_set_of<WritesPosition>().size(), std::size_t{1});
    CHECK_EQ(write_set_of<ReadsPosition>().size(), std::size_t{0});
    CHECK_EQ(read_set_of<ReadsPosition>().size(), std::size_t{1});
}

TEST(access_sets_count_const_reads_as_read_only)
{
    // Mixed 的 ReadSet 带 const，Velocity 计入读集但不计入写集。
    const auto w = write_set_of<Mixed>();
    CHECK_EQ(w.size(), std::size_t{1});
    const auto r = read_set_of<Mixed>();
    CHECK_EQ(r.size(), std::size_t{2});
}

TEST(scheduler_merges_disjoint_systems_into_one_batch)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsVelocity b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
}

TEST(scheduler_separates_write_write_conflicts)
{
    Scheduler scheduler{0};
    WritesPosition a;
    WritesPositionOther b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_separates_write_read_conflicts)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsPosition b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_replans_after_add)
{
    Scheduler scheduler{0};
    WritesPosition a;
    scheduler.add(a);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    WritesPositionOther b;
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_reader_runs_before_conflicting_writer)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{10});

    Scheduler scheduler{0};
    ReadsPosition reader;
    WritesPosition writer;
    scheduler.add(reader);
    scheduler.add(writer);
    scheduler.plan();

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // reader 注册在前且无冲突可入第一批，必须先读到初始值 10。
    CHECK_EQ(reader.observed, 10);

    scheduler.run(world, cmd);

    // 第二帧 writer 已完成迁移并更新 slot，reader 仍读到 11。
    CHECK_EQ(reader.observed, 21);
}

TEST(scheduler_runs_all_systems_in_one_frame)
{
    World world;
    for (int i = 0; i < 1000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{0});
    }

    Scheduler scheduler{0};
    WritesPosition w1;
    WritesPositionOther w2;
    scheduler.add(w1);
    scheduler.add(w2);

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    int total = 0;
    world.view<const Position>().each(
        [&total](const Position& p) { total += p.x; });
    CHECK_EQ(total, 1000 * 101);
}

TEST(scheduler_three_way_split)
{
    Scheduler scheduler{0};
    WritesPosition a;
    WritesPositionOther b;
    ReadsPosition c;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.add(c);
    scheduler.plan();

    // a 与 b 写 Position 冲突，c 读 Position，三者必须分三批。
    CHECK_EQ(scheduler.batch_count(), std::size_t{3});
}

TEST(scheduler_independent_chains_stay_separate)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsVelocity b;
    WritesHealth c;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.add(c);
    scheduler.plan();

    // 三者访问集两两不交，应同批并行。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
}

TEST(access_sets_count_non_const_read_as_write)
{
    // MutatesViaReadSet 的 ReadSet 不带 const：Velocity 同时进读集与写集。
    const auto w = write_set_of<MutatesViaReadSet>();
    CHECK_EQ(w.size(), std::size_t{1});
    const auto r = read_set_of<MutatesViaReadSet>();
    CHECK_EQ(r.size(), std::size_t{1});
    // 与 Mixed 对照：同一个 Velocity，带 const 进 2，不带 const 写集非空。
    CHECK_EQ(write_set_of<Mixed>().size(), std::size_t{1});
    CHECK_EQ(read_set_of<Mixed>().size(), std::size_t{2});
}

/// 补 brief 缺的一侧：读-写方向。brief 只有「写-写」「写在前读在后」，
/// 这里钉「读在前写在后」，也就是谓词里的 intersects(writes, read) 分支。
TEST(scheduler_separates_read_write_conflicts)
{
    Scheduler scheduler{0};
    ReadsPosition reader;
    WritesPosition writer;
    scheduler.add(reader);
    scheduler.add(writer);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

/// ReadSet 不带 const 的系统必须被当成写者参与冲突判定。
///
/// 对手方必须是**真冲突**的：b 的 ReadSet 显式含 Velocity，于是
/// 「a 算写者」时 a 与 b 读写冲突 → 必须拆批；「a 不算写者」时两者都只是
/// 读 Velocity → 同批。断言 batch_count 就能区分这两种派生规则。
/// （旧版本拿只写 Health 的 b 当对手，两侧访问集本就两两不交，
///   无论规则怎么变都是 1 批 —— 可证伪的真空用例。）
TEST(scheduler_treats_mutable_read_set_as_writer)
{
    Scheduler scheduler{0};
    MutatesViaReadSet a;   // 读写 Velocity
    ReadsVelocity b;       // 显式读 Velocity
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

/// 确定性并发探针：断言「确实在另一条线程上跑过 System」，不断言任何求和值。
///
/// 简报 10 条用例全部是 Scheduler{0}，ThreadPool::run 走 workers_.empty()
/// 的串行早返回分支，并发路径一次都没跑到。这里用真实 worker 跑同批 32 个
/// 空访问集 System，并由第一个进来的 System 自旋等待第二个到来 —— 该线程
/// 卡在自己的 job 上，所以「第二个 System 跑起来」只可能发生在另一条线程。
/// 判定因此是确定的：Scheduler 若退化成串行，overlapped 必为 false。
TEST(scheduler_runs_batch_systems_on_worker_threads)
{
    std::mutex mtx;
    std::vector<std::thread::id> ids;
    ids.reserve(64);

    std::atomic<int> started{0};
    std::atomic<bool> overlapped{false};

    Scheduler scheduler{4};
    ThreadProbe probes[32];
    for (auto& p : probes)
    {
        p.mtx = &mtx;
        p.ids = &ids;
        p.started = &started;
        p.overlapped = &overlapped;
        scheduler.add(p);
    }

    World world;
    CommandBuffer cmd;
    scheduler.run(world, cmd);

    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
    CHECK_EQ(started.load(), 32);
    CHECK_EQ(ids.size(), std::size_t{32});
    CHECK(overlapped.load());
}

/// in_system_ 是 World 上单个原子 bool，不是计数器。
///
/// 若 Scheduler 把 enter/exit 放进并发 job，先完成的 System 会把标志清成
/// false，仍在执行的兄弟 System 就会读到 parallelize_allowed() 为真。
/// 本条复用 ThreadProbe 的自旋屏障，保证并发真实发生（overlapped 为真），
/// 此时批内每个 System 观察到的都必须是 false —— 它只取决于标志的置位时机，
/// 与 System 内部做了什么无关。
TEST(scheduler_holds_system_flag_for_whole_batch)
{
    std::mutex mtx;
    std::vector<std::thread::id> ids;
    ids.reserve(64);

    // vector<bool> 是位压缩存储，多线程写不同位仍是同一个字的竞争；
    // 观测槽改用 atomic<bool>，每个 System 只写自己那一个。
    std::vector<std::atomic<bool>> observed(32);
    for (auto& slot : observed)
    {
        slot.store(true);
    }

    std::atomic<int> started{0};
    std::atomic<bool> overlapped{false};

    Scheduler scheduler{4};
    ThreadProbe probes[32];
    for (std::size_t i = 0; i < 32; ++i)
    {
        probes[i].mtx = &mtx;
        probes[i].ids = &ids;
        probes[i].saw_parallelize_allowed = &observed[i];
        probes[i].started = &started;
        probes[i].overlapped = &overlapped;
        scheduler.add(probes[i]);
    }

    World world;
    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // 前置条件：并发真的发生了，否则下面的断言测不到任何东西。
    CHECK(overlapped.load());
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
    for (std::size_t i = 0; i < 32; ++i)
    {
        CHECK(!observed[i].load());
    }
}

/// 后批必须读到前批写入的新值：这是「批次之间有 barrier」的唯一可观测后果。
TEST(scheduler_later_batch_sees_earlier_batch_writes)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{0});
    (void)world.add<Health>(e, Health{0});

    Scheduler scheduler{4};
    // 两批各一个 System：第 0 批读，第 1 批写，Position 上读写冲突。
    ReadsPosition reader;
    WritesPosition writer;
    scheduler.add(reader);
    scheduler.add(writer);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // reader 先跑，看到的是写入前的 0。
    CHECK_EQ(reader.observed, 0);
    CHECK_EQ(world.get_if<Position>(e)->x, 1);
}

/// 单 System 批次必须保住内并行（设计文档 7.3 规范表第一行）。
///
/// Scheduler 若对单 System 批次也置位 in_system_，System 内的 each_parallel
/// 会退化成串行，整个线程池闲置 —— 这正是当初选 Archetype 分块存储而非每组件
/// 独立稀疏存储要换来的收益。本条断言「第二个 chunk 跑在别的线程上」。
TEST(scheduler_single_system_batch_keeps_inner_parallelism)
{
    constexpr int kEntities = 20000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{0});
    }

    std::atomic<int> started{0};
    std::atomic<bool> overlapped{false};
    std::atomic<int> visited{0};

    Scheduler scheduler{4};
    InnerParallelProbe probe;
    probe.started = &started;
    probe.overlapped = &overlapped;
    probe.visited = &visited;
    scheduler.add(probe);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // 每个实体恰好被访问一次：并发没有造成漏访或重访。
    CHECK_EQ(visited.load(), kEntities);

    // 全部实体都被 +1，结果正确。
    int incremented = 0;
    world.view<const Position>().each(
        [&incremented](const Position& p) { incremented += (p.x == 1); });
    CHECK_EQ(incremented, kEntities);

    // 内并行确实发生：第二个 chunk 由另一条线程处理。
    //
    // 前置条件：each_parallel 提交到的是 World 自己的池，与本用例的
    // Scheduler{4} 无关。hardware_concurrency<=2 时 default_worker_count()
    // 返回 0，池里没有 worker，each_parallel 必然全串行 —— 此时断言正并行
    // 是确定性假红，不是被测行为出错。池为空时只能跳过，如实标注。
    if (world.parallel_pool().size() > 0)
    {
        CHECK(overlapped.load());
    }
}

/// 同批两个 System 并发构造 View、各自遍历大量实体、写入互不相交的组件列。
///
/// 这是本模块最核心的并发路径：批内并发构造 View + 并发写互不相交的组件列。
/// 两者结果都正确且互不污染，才说明分批与访问集是对的。
///
/// 不覆盖并发写 CommandBuffer：两个 System 的 CommandBuffer 形参是无名参数，
/// 完全不碰它。并发写 buffers[idx] 的安全性靠的是架构不变量（定长构造后不
/// 重分配、合并在单线程完成），不是本用例验证出来的。
TEST(scheduler_same_batch_systems_view_disjoint_components)
{
    constexpr int kEntities = 20000;
    World world;
    for (int i = 0; i < kEntities; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{0});
        (void)world.add<Health>(e, Health{0});
    }

    std::atomic<bool> saw_parallelize_a{true};
    std::atomic<bool> saw_parallelize_b{true};

    ConcurrentViewA a;
    a.saw_parallelize_allowed = &saw_parallelize_a;
    a.delta = 3;
    ConcurrentViewB b;
    b.saw_parallelize_allowed = &saw_parallelize_b;
    b.delta = 7;

    Scheduler scheduler{4};
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    // Position 与 Health 不相交，必须同批。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    int pos_ok = 0;
    int hp_ok = 0;
    world.view<const Position, const Health>().each(
        [&pos_ok, &hp_ok](const Position& p, const Health& h)
        {
            pos_ok += (p.x == 3);
            hp_ok += (h.hp == -7);
        });
    // 两者结果都正确，且互不污染（谁的 delta 都没跑到对方列上）。
    CHECK_EQ(pos_ok, kEntities);
    CHECK_EQ(hp_ok, kEntities);

    // 批内整批置位：两个 System 都应看到 parallelize_allowed() 为假。
    CHECK(!saw_parallelize_a.load());
    CHECK(!saw_parallelize_b.load());
}

namespace {

/// 执行序记录器。批次内是并行派发的，所以只能断言「批的先后」，不能断言
/// 同批内谁先跑——下面的断言都刻意留了同批这一层不确定性。
std::mutex g_log_mutex;
std::vector<int> g_execution_log;

void log_execution(int tag)
{
    const std::lock_guard lock{g_log_mutex};
    g_execution_log.push_back(tag);
}

/// 写 Health，与 Later 同为 Health 的写者，因此必然被分进不同批次。
/// 与 Earlier 没有任何共读写集，本可同批并行——order 是唯一把二者分开的手段。
struct Earlier final : gamev2::ecs::System
{
    static constexpr int order = 0;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;
    int tag = 1;

    void execute(World&, CommandBuffer&) override
    {
        log_execution(tag);
    }
};

struct Later final : gamev2::ecs::System
{
    static constexpr int order = 1;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;
    int tag = 2;

    void execute(World&, CommandBuffer&) override
    {
        log_execution(tag);
    }
};

struct First final : gamev2::ecs::System
{
    static constexpr int order = 0;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;
    int tag = 1;
    void execute(World&, CommandBuffer&) override { log_execution(tag); }
};

struct Second final : gamev2::ecs::System
{
    static constexpr int order = 1;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Velocity>;
    int tag = 2;
    void execute(World&, CommandBuffer&) override { log_execution(tag); }
};

/// 两个 system，各自在 execute 里创建一个实体并把新句柄回写到自己身上。
/// 两者同写 Health 因此必分进不同批，先后由 order 决定。
template<int Order>
struct Creator final : gamev2::ecs::System
{
    static constexpr int order = Order;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;
    gamev2::ecs::EntityId born{};

    void execute(World&, CommandBuffer& cmd) override
    {
        cmd.create(born, Position{});
    }
};

/// 声明结构变更的系统。它真的创建一个实体——标记不是注释——但访问集为空，
/// 因此只有 mutates_structure 能把它与无关 system 分开。
struct StructuralMid final : gamev2::ecs::System
{
    static constexpr int order = 1;
    static constexpr bool mutates_structure = true;
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<>;
    int tag = 2;

    void execute(World& world, CommandBuffer&) override
    {
        (void)world.create();
        log_execution(tag);
    }
};

/// 顺序排在结构变更之后、访问集却与它不相交的读者。冲突矩阵漏掉
/// mutates_structure 时，它会和 First 一起进第一批，把执行序变成 A→C→B。
struct AfterStructural final : gamev2::ecs::System
{
    static constexpr int order = 2;
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<>;
    int tag = 3;

    void execute(World&, CommandBuffer&) override
    {
        log_execution(tag);
    }
};

/// 与 WritesPosition 访问集不相交，因此两者同批；本 system 抛出。
struct ThrowsWhileWritingHealth final : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World&, CommandBuffer&) override
    {
        throw std::runtime_error{"system failed"};
    }
};

}  // namespace

TEST(system_order_overrides_registration_order)
{
    // 访问集只保证「不并行」，批次之间的先后原先纯属 plan() 按注册顺序
    // 贪心的副产品。这里故意先注册 Later：没有 System::order 时它会落在
    // 第一批，Earlier 被挤到第二批，先后正好反了。
    auto world = World{};
    auto cmd = CommandBuffer{};

    Earlier earlier;
    Later later;
    Scheduler scheduler{2};
    scheduler.add(later);
    scheduler.add(earlier);

    g_execution_log.clear();
    scheduler.run(world, cmd);

    CHECK_EQ(g_execution_log.size(), std::size_t{2});
    if (g_execution_log.size() == 2)
    {
        CHECK_EQ(g_execution_log[0], earlier.tag);
        CHECK_EQ(g_execution_log[1], later.tag);
    }
}

/// mutates_structure 必须进冲突矩阵，否则一个 order 更大的无关 system 会
/// 抢在结构变更之前执行：两者访问集不相交，读/读不算冲突，于是同批并行，
/// 而结构变更正踩在那批 system 的无锁遍历上。
///
/// 探针形状：First（order 0、写 Position）→ StructuralMid（order 1、结构
/// 变更）→ AfterStructural（order 2、读 Velocity）。修复前 First 与 After
/// 同批，执行序实测为 A→C→B；修复后三批，执行序 A→B→C。
TEST(scheduler_structure_mutation_blocks_later_readers)
{
    auto world = World{};
    auto cmd = CommandBuffer{};

    First before;
    StructuralMid structural;
    AfterStructural after;

    Scheduler scheduler{2};
    scheduler.add(before);
    scheduler.add(structural);
    scheduler.add(after);
    scheduler.plan();

    CHECK_EQ(scheduler.batch_count(), std::size_t{3});

    g_execution_log.clear();
    scheduler.run(world, cmd);

    CHECK_EQ(g_execution_log.size(), std::size_t{3});
    if (g_execution_log.size() == 3)
    {
        CHECK_EQ(g_execution_log[0], before.tag);
        CHECK_EQ(g_execution_log[1], structural.tag);
        CHECK_EQ(g_execution_log[2], after.tag);
    }
}

/// 批内 system 抛出时，Scheduler::run 必须先恢复 in_system_ 再把异常放走。
/// 标志不是 RAII，漏掉这一恢复，一次异常就让此后所有
/// parallelize_allowed() 永远为假。
TEST(scheduler_restores_system_flag_when_a_batch_throws)
{
    auto world = World{};
    auto cmd = CommandBuffer{};

    WritesPosition writer;         // 写 Position
    ThrowsWhileWritingHealth bad;  // 写 Health：与 writer 不相交 ⇒ 同批

    Scheduler scheduler{0};
    scheduler.add(writer);
    scheduler.add(bad);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
    CHECK(world.parallelize_allowed());

    bool threw = false;
    try
    {
        scheduler.run(world, cmd);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
    CHECK(world.parallelize_allowed());
}

TEST(system_order_does_not_serialize_non_conflicting_systems)
{
    // order 只决定「谁先参与分层」，不制造冲突。Position 的两个互不冲突的
    // 写者即使声明了先后，仍应留在同一批并行派发——否则每个带 order 的
    // system 都会把整条流水线退化成串行。
    auto world = World{};
    auto cmd = CommandBuffer{};

    Second second;
    First first;
    Scheduler scheduler{2};
    scheduler.add(second);
    scheduler.add(first);
    scheduler.plan();

    // 写集不相交 => 同一批 => 一次 plan 就够。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
}

/// 命令序列必须跟着执行次序走，也就是跟着 order 走——不能跟着注册顺序走。
///
/// 观测量是**句柄**：create 在 flush 时才分配槽位，谁的 create 命令先落地，
/// 谁拿到的 EntityId 就小。故意把 Later 排在前面注册，于是「注册序」与
/// 「执行序」恰好相反，两种合并顺序给出的答案也就相反。
///
/// 批内次序取 batch 自身的下标顺序（plan 按 order 稳定排序后依次入列），
/// 因此与并行派发的真实完成次序无关。本例两个批各只有一个 member，不涉及
/// 批内次序。
TEST(command_merge_follows_batch_order_not_registration_order)
{
    auto world = World{};
    auto cmd = CommandBuffer{};

    Creator<1> later;
    Creator<0> earlier;
    Scheduler scheduler{1};
    scheduler.add(later);    // 注册在前
    scheduler.add(earlier);  // 执行在前
    scheduler.run(world, cmd);

    // earlier 排在更早的批，它的 create 先落地 ⇒ 句柄更小。
    CHECK(earlier.born);
    CHECK(later.born);
    CHECK(earlier.born.index() < later.born.index());
    CHECK_EQ(world.entity_count(), std::size_t{2});
}
