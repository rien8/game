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

    std::vector<bool> taken(entries_.size(), false);
    std::size_t remaining = entries_.size();

    // 按 System::order 稳定排序后再分层：先序的 system 优先占住靠前的批次，
    // 「谁先谁后」因此由 order 决定，而不是碰巧等于注册顺序。排序的是下标
    // 视图而非 entries_ 本身，Entry 的排列保持注册序，批次下标的含义不变。
    // 稳定排序保证 order 相同的 system 仍按注册顺序定序。
    std::vector<std::size_t> visiting(entries_.size());
    std::iota(visiting.begin(), visiting.end(), std::size_t{0});
    std::ranges::stable_sort(visiting, {}, [this](std::size_t i)
                             { return entries_[i].order; });

    while (remaining > 0)
    {
        std::vector<std::size_t> batch;
        std::vector<ComponentId> written;
        std::vector<ComponentId> read;
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
            // read_set_of 把写集并入读集，故每个 entry 恒有 reads ⊇ writes，
            // 第一个 disjunct 因此恒被第二个蕴含、当前是冗余的。保留它作为
            // 写-写方向的独立守卫：若哪天 read_set_of 改成只含显式 ReadSet，
            // 它就是唯一还能拦住写-写冲突的一支。
            const bool clashes = intersects(e.writes, written) ||
                                 intersects(e.writes, read) ||
                                 intersects(e.reads, written);
            if (clashes)
            {
                continue;
            }
            batch.push_back(i);
            taken[i] = true;
            --remaining;
            batch_mutates_structure = batch_mutates_structure
                                      || e.mutates_structure;
            written.insert(written.end(), e.writes.begin(), e.writes.end());
            read.insert(read.end(), e.reads.begin(), e.reads.end());
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

    // 每个 System 一个 CommandBuffer，按注册顺序合并，保证命令顺序确定。
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

    for (auto& buffer : buffers)
    {
        cmd.append(std::move(buffer));
    }
    cmd.flush(world);
}

}  // namespace gamev2::ecs
