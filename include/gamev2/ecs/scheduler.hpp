/// @file scheduler.hpp
/// @brief 系统调度器：读取每个 System 自声明的读写集，贪心分层，把互不冲突的
///        System 编进同一批并行派发，有冲突的强制落到不同批次串行执行。
///
/// 分层表达「不得并行」；批次之间的先后由 System::order 表达——plan() 先按
/// order 稳定排序再贪心分层，先序的 system 优先占住靠前的批次，因此先后不再
/// 取决于注册顺序。
///
/// order 只是排序键，不制造冲突：它能兑现「B 晚于 A」的前提是 A 与 B 本来
/// 就冲突（访问集相交，或一方 mutates_structure）。互不冲突的两个 system 仍
/// 会被放进同一批并行派发，order 管不到那里。详见 system.hpp 里 order 的说明。
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
        int order = 0;
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
    entry.order = S::order;
    entries_.push_back(std::move(entry));
    planned_ = false;
}

}  // namespace gamev2::ecs
