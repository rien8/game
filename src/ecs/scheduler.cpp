#include "gamev2/ecs/scheduler.hpp"

#include <algorithm>
#include <functional>
#include <numeric>

namespace gamev2::ecs {

namespace {

[[nodiscard]] bool intersects(const std::vector<ComponentId>& a,
                              const std::vector<ComponentId>& b)
{
    return std::ranges::any_of(a, [&b](ComponentId id)
                               { return std::ranges::find(b, id) != b.end(); });
}

}  // namespace

Scheduler::Scheduler(std::size_t worker_count)
    : pool_{worker_count}
{
}

void Scheduler::plan()
{
    batches_.clear();

    const auto count = entries_.size();
    std::vector<bool> taken(count, false);
    std::size_t remaining = count;

    // 按 System::order 稳定排序后再分层：先序的 system 优先占住靠前的批次，
    // 「谁先谁后」因此由 order 决定，而不是碰巧等于注册顺序。排序的是下标
    // 视图而非 entries_ 本身，Entry 的排列保持注册序，批次下标的含义不变。
    // 稳定排序保证 order 相同的 system 仍按注册顺序定序。
    std::vector<std::size_t> visiting(count);
    std::iota(visiting.begin(), visiting.end(), std::size_t{0});
    std::ranges::stable_sort(visiting, {}, [this](std::size_t i)
                             { return entries_[i].order; });

    // 成对冲突矩阵：分层要判两件事——与批内成员冲不冲突，以及有没有更早的
    // 未排 system 与自己冲突。两个判据因此都退化成查表。该关系对称，故只算
    // 上三角。
    //
    // read_set_of 把写集并入读集，故每个 entry 恒有 reads ⊇ writes，第一个
    // disjunct 因此恒被后两条蕴含、当前是冗余的。保留它作为写-写方向的
    // 独立守卫：若哪天 read_set_of 改成只含显式 ReadSet，它就是唯一还能
    // 拦住写-写冲突的一支。
    std::vector<std::vector<bool>> conflict(count,
                                            std::vector<bool>(count, false));
    for (std::size_t a = 0; a < count; ++a)
    {
        for (std::size_t b = a + 1; b < count; ++b)
        {
            conflict[a][b] = conflict[b][a]
                = intersects(entries_[a].writes, entries_[b].writes)
                  || intersects(entries_[a].writes, entries_[b].reads)
                  || intersects(entries_[a].reads, entries_[b].writes);
        }
    }

    while (remaining > 0)
    {
        std::vector<std::size_t> batch;
        // 批里已有结构变更方时，它已经独占了一批（下面的判据保证它只能
        // 进空批），因此这里等价于「本批非空且已占用」。
        bool batch_mutates_structure = false;

        for (const auto i : visiting)
        {
            if (taken[i])
            {
                continue;
            }
            const auto& e = entries_[i];
            // 声明 mutates_structure 的 system 与批内任何成员都冲突，因此
            // 独占一批。access set 挡不住它：World 的槽位表与列存储在结构
            // 变更时可能重分配，而另一 system 正在同一张表上做无锁遍历，
            // 读/读不算冲突的规则正会把两个不相交的只读 system 放进同一批。
            if (!batch.empty()
                && (e.mutates_structure || batch_mutates_structure))
            {
                continue;
            }
            if (std::ranges::any_of(batch, [&conflict, i](const std::size_t j)
                                    { return conflict[i][j]; }))
            {
                continue;
            }
            // order 只在冲突时表达先后，而单靠排序兑现不了它：贪心 first-fit
            // 会先放行一个 order 大的 system，把它挡在当前批的 order 小的那个
            // 反而推到下一批，「B 晚于 A」就此反转。补上这条之后，冲突且
            // order 递增的两个 system 必然落在严格递增的批次上。
            //
            // 不会把剩余的 system 饿死：visiting 里 order 最小的未排 system
            // 没有更早的未排伙伴，而空批的批内判据也一律放行，因此每轮必有
            // 收获。
            if (std::ranges::any_of(
                    visiting, [this, &taken, &conflict, &e, i](
                                  const std::size_t j)
                    {
                        return !taken[j] && entries_[j].order < e.order
                               && conflict[i][j];
                    }))
            {
                continue;
            }
            batch.push_back(i);
            taken[i] = true;
            --remaining;
            batch_mutates_structure = batch_mutates_structure
                                      || e.mutates_structure;
        }

        batches_.push_back(std::move(batch));
    }

    planned_ = true;
}

void Scheduler::run(World& world, CommandBuffer& cmd)
{
    if (!planned_)
    {
        plan();
    }

    // 每个 System 一个 CommandBuffer，合并顺序见末尾那个循环。
    std::vector<CommandBuffer> buffers(entries_.size());

    for (const auto& batch : batches_)
    {
        if (batch.size() == 1)
        {
            // 单 System 批次不置位：设计 7.3 要求它仍能内部按 chunk 并行，
            // 这正是选 Archetype 分块存储的收益所在。
            entries_[batch[0]].system->execute(world, buffers[batch[0]]);
        }
        else
        {
            // 进入/退出必须成对发生在调用线程上。in_system_ 是单个原子 bool
            // 而非计数器：放进并发 job 里会被先完成的 System 提前清成 false，
            // 批内其余 System 随后读到 parallelize_allowed() 为真。
            //
            // 整批置位是设计 7.3 的要求：禁止批内并行再嵌套内层并行。要挡的
            // 不是死锁——批内 System 由本 Scheduler 的 pool_ 派发，内层
            // each_parallel 提交到的是 world 的另一个池，两个 ThreadPool 之间
            // 不存在环形等待。真正要挡的是同池并发：批内若两个 System 各自调
            // each_parallel，两个 run() 就会并发落在 World 的同一个池上，内层
            // 那个覆盖外层的 jobs_ 与 next_ / done_ / generation_。
            world.enter_system_execution();

            std::vector<std::function<void()>> jobs;
            jobs.reserve(batch.size());
            for (const auto idx : batch)
            {
                jobs.emplace_back(
                    [&world, this, idx, &buffers]
                    {
                        entries_[idx].system->execute(world, buffers[idx]);
                    });
            }
            pool_.run(jobs);

            world.exit_system_execution();
        }
    }

    // 命令按**批次**顺序合并，与执行顺序一致：batches_ 是 plan() 按 order
    // 分层的结果，按注册序合并会让命令序列与实际执行次序脱钩——而 order 的
    // 全部意义正是让执行次序不再取决于注册顺序。
    //
    // 批内的次序取 batch 自身（下标数组）的顺序，即 plan() 按 order 稳定
    // 排序后依次入列的顺序，因此它是确定的，与批内并行派发的真实完成次序
    // 无关。冲突的两个 system 必在不同批，所以批内成员之间本来就不该有
    // 命令上的先后依赖。
    for (const auto& batch : batches_)
    {
        for (const auto idx : batch)
        {
            cmd.append(std::move(buffers[idx]));
        }
    }
    cmd.flush(world);
}

}  // namespace gamev2::ecs
