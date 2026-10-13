#include "gamev2/ecs/scheduler.hpp"

#include <algorithm>
#include <functional>

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

    while (remaining > 0)
    {
        std::vector<std::size_t> batch;
        std::vector<ComponentId> written;
        std::vector<ComponentId> read;

        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            if (taken[i])
            {
                continue;
            }
            const auto& e = entries_[i];
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
