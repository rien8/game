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
    entries_.push_back(std::move(entry));
    planned_ = false;
}

}  // namespace gamev2::ecs
