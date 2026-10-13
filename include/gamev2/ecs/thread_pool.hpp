#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gamev2::ecs {

/// 固定大小的 worker 池。用原子计数器分发任务，无需每任务加锁。
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return workers_.size();
    }

    /// 提交一批互相独立的任务并等待全部完成。
    /// 调用线程也参与执行，因此 worker 数为 0 时行为退化为串行。
    ///
    /// 前置条件：同一个池不得被并发调用，也不得从它自己派发的 job 里重入调用。
    /// jobs_ / next_ / done_ / generation_ 是全池共享的「一代」状态：第二个
    /// run() 会覆盖 jobs_ 与计数，前一个 run() 随即在别人的计数上等待，任务
    /// 成批丢失。模块自有路径靠 Scheduler 的整批置位（设计 7.3）已经绕开，
    /// 但池是公开的（World::parallel_pool()），这条约束得由调用方保证。
    void run(const std::vector<std::function<void()>>& jobs);

private:
    void worker_loop(std::uint64_t seen);
    void drain(const std::vector<std::function<void()>>* jobs);

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;

    const std::vector<std::function<void()>>* jobs_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::atomic<std::size_t> done_{0};
    std::uint64_t generation_ = 0;
    /// 已醒来且正在 drain 本代任务的 worker 数，只在 mutex_ 下修改。
    /// run() 必须等它归零才能让 jobs_ 失效：调用方的 jobs 向量随 run()
    /// 返回即销毁，仍在 drain 的 worker 解引用它就是悬垂。
    std::size_t active_ = 0;
    bool stop_ = false;
};

}  // namespace gamev2::ecs
