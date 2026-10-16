/// @file scheduler.hpp
/// @brief 系统调度器：读取每个 System 自声明的读写集，贪心分层，把互不冲突的
///        System 编进同一批并行派发，有冲突的强制落到不同批次串行执行。
///
/// 分层只表达「不得并行」，不表达「谁先谁后」：批次之间的次序由 plan() 的
/// 贪心算法决定，不是声明式的顺序契约。需要先后关系的 System 只能在各自
/// 文档里写明，并自行规避依赖调度器——目前模块内只有一处这样的约束。
#pragma once

#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/thread_pool.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 依据读写集贪心分层，把无冲突的 System 拆进同一批并行执行。
class Scheduler
{
public:
    explicit Scheduler(std::size_t worker_count);

    template<typename S>
    void add(S& system);

    void plan();

    void run(World& world, CommandBuffer& cmd);

    [[nodiscard]] std::size_t batch_count() const noexcept
    {
        return batches_.size();
    }

private:
    struct Entry
    {
        System* system;
        std::vector<ComponentId> reads;
        std::vector<ComponentId> writes;
        bool mutates_structure = false;
    };

    std::vector<Entry> entries_;
    std::vector<std::vector<std::size_t>> batches_;
    bool planned_ = false;
    ThreadPool pool_;
};

template<typename S>
void Scheduler::add(S& system)
{
    Entry entry;
    entry.system = &system;
    entry.writes = write_set_of<S>();
    entry.reads = read_set_of<S>();
    entry.mutates_structure = S::mutates_structure;
    entries_.push_back(std::move(entry));
    planned_ = false;
}

}  // namespace gamev2::ecs
