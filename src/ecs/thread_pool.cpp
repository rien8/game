#include "gamev2/ecs/thread_pool.hpp"

namespace gamev2::ecs {

ThreadPool::ThreadPool(std::size_t thread_count)
{
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
    {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

void ThreadPool::drain(const std::vector<std::function<void()>>* jobs)
{
    if (jobs == nullptr)
    {
        return;
    }
    for (;;)
    {
        if (failed_.load(std::memory_order_relaxed))
        {
            return;
        }
        const auto i = next_.fetch_add(1, std::memory_order_relaxed);
        if (i >= jobs->size())
        {
            return;
        }
        try
        {
            (*jobs)[i]();
        }
        catch (...)
        {
            {
                std::lock_guard lock{mutex_};
                if (failure_ == nullptr)
                {
                    failure_ = std::current_exception();
                }
            }
            failed_.store(true, std::memory_order_release);
            // 未开工的任务不会再执行，因此把 done_ 直接记满：run() 的
            // 谓词只认 done_ >= size 与 active_ == 0，缺一条就会挂死。
            done_.store(jobs->size(), std::memory_order_release);
            done_cv_.notify_all();
            return;
        }
        // 只有最后一个完成任务的人唤醒等待者：run() 的谓词同时要求
        // done_ 到位与 active_ 归零，任何一方推进都可能让它成立。
        if (done_.fetch_add(1, std::memory_order_acq_rel) + 1 == jobs->size())
        {
            done_cv_.notify_one();
        }
    }
}

void ThreadPool::run(const std::vector<std::function<void()>>& jobs)
{
    if (jobs.empty())
    {
        return;
    }
    if (workers_.empty())
    {
        for (const auto& job : jobs)
        {
            job();
        }
        return;
    }

    {
        std::lock_guard lock{mutex_};
        jobs_ = &jobs;
        next_.store(0, std::memory_order_relaxed);
        done_.store(0, std::memory_order_relaxed);
        failed_.store(false, std::memory_order_relaxed);
        failure_ = nullptr;
        ++generation_;
    }
    cv_.notify_all();

    drain(&jobs);

    std::unique_lock lock{mutex_};
    // active_ == 0 与 done_ 到位缺一不可：前者保证没有 worker 还会碰
    // jobs，后者保证没有任务仍在飞行中。任务抛出时 drain 会把 done_ 记满，
    // 但 worker 仍可能在执行，因此这条等待同样不可跳过。
    done_cv_.wait(lock, [this, &jobs]
                  { return done_.load(std::memory_order_acquire) >=
                               jobs.size() &&
                         active_ == 0; });
    jobs_ = nullptr;
    const std::exception_ptr failure = failure_;
    failure_ = nullptr;
    lock.unlock();
    if (failure != nullptr)
    {
        std::rethrow_exception(failure);
    }
}

void ThreadPool::worker_loop(std::uint64_t seen)
{
    for (;;)
    {
        const std::vector<std::function<void()>>* jobs = nullptr;
        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock,
                     [this, seen] { return generation_ != seen || stop_; });
            if (stop_)
            {
                return;
            }
            seen = generation_;
            // 指针与计数必须同一个临界区内取：run() 判定 active_ == 0 后
            // 才会把 jobs_ 置空，两者由 mutex_ 排出先后。
            jobs = jobs_;
            if (jobs != nullptr)
            {
                ++active_;
            }
        }

        drain(jobs);

        if (jobs != nullptr)
        {
            std::lock_guard lock{mutex_};
            --active_;
            if (active_ == 0)
            {
                done_cv_.notify_all();
            }
        }
    }
}

}  // namespace gamev2::ecs
