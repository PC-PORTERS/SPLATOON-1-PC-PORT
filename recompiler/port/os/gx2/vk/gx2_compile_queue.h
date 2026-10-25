#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gx2vk {
// Owns compiler work only: never guest memory, command buffers or renderer maps.
// Demand work can overtake queued warm-up work. Destruction drains and joins
// workers before their Vulkan dependencies may be destroyed.
class CompileQueue {
    struct Work { std::function<void()> run; };
public:
    template<class T> struct Job {
        std::shared_future<T> result;
        std::shared_ptr<Work> work;
        explicit operator bool() const { return result.valid(); }
        bool ready() const { return result.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    };
    explicit CompileQueue(unsigned workers) {
        try {
            for (unsigned i = 0; i < std::max(1u, workers); ++i)
                threads_.emplace_back([this, i] { loop(i); });
        } catch (...) { drain(); throw; }
    }
    ~CompileQueue() { drain(); }
    CompileQueue(const CompileQueue&) = delete;
    CompileQueue& operator=(const CompileQueue&) = delete;
    template<class F> auto post(F&& fn, bool background = false) {
        using T = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<T()>>(std::forward<F>(fn));
        Job<T> job{task->get_future().share(), std::make_shared<Work>()};
        job.work->run = [task] { (*task)(); };
        {
            std::lock_guard lock(mutex_);
            if (stopping_) throw std::runtime_error("shader compiler is stopping");
            if (background) queue_.push_back(job.work); else queue_.push_front(job.work);
        }
        ready_.notify_one();
        return job;
    }
    template<class T> T get(const Job<T>& job) {
        promote(job);
        return job.result.get();
    }
    // Promote an existing warm-up job rather than compiling it twice.
    template<class T> void promote(const Job<T>& job) {
        {
            std::lock_guard lock(mutex_);
            auto it = std::find(queue_.begin(), queue_.end(), job.work);
            if (it == queue_.end() || it == queue_.begin()) return;
            auto work = *it; queue_.erase(it); queue_.push_front(std::move(work));
        }
        ready_.notify_one();
    }
    void drain() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_all();
        for (auto& thread : threads_) if (thread.joinable()) thread.join();
        threads_.clear();
    }
private:
    void loop(unsigned worker_index) {
#ifdef _WIN32
        // Background warm-up must yield CPU time to simulation and rendering.
        // Retain one normal-priority compiler so demand builds can progress.
        SetThreadPriority(GetCurrentThread(), worker_index == 0 ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_BELOW_NORMAL);
#else
        (void)worker_index;
#endif
        for (;;) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;
                work = std::move(queue_.front()); queue_.pop_front();
            }
            work->run(); // packaged_task carries failures to the consumer
        }
    }
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Work>> queue_;
    std::vector<std::thread> threads_;
    bool stopping_ = false;
};
// The render owner resolves a completed compilation once. Keep the promise and
// work item for warm-up promotion/lifetime, but do not poll a future every draw.
// Only the owner accesses this wrapper; compiler threads access pending's state.
template<class T> class ResolvedCompileJob {
public:
    ResolvedCompileJob()=default;
    ResolvedCompileJob(CompileQueue::Job<T> job):pending(std::move(job)){}
    bool ready() const{return resolved_ || pending.ready();}
    template<class Resolve> T get(Resolve&& resolve) {
        if(!resolved_){value_=resolve(pending);resolved_=true;}
        return value_;
    }
    CompileQueue::Job<T> pending;
private:
    T value_{};
    bool resolved_=false;
};
} // namespace gx2vk
