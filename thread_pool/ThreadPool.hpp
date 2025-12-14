#pragma once

#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>
#include <mutex>
#include <memory>
#include <type_traits>

class ThreadPool {
public:
    using Task = std::function<void()>;
    static constexpr std::size_t kMaxThreads = 64;

    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency()) {
        if (thread_count == 0) thread_count = 1;
        if (thread_count > kMaxThreads) 
            throw std::invalid_argument("thread_count exceeds maximum allowed threads");
        
        tp_run_.store(true, std::memory_order_release);
        add_threads(thread_count);
    }

    ~ThreadPool() noexcept {
        stop();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<class F, class... Args>
    auto post_task(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;
        
        // 使用memory_order_relaxed足够，因为stop()会进行同步
        if (!tp_run_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("post_task on stopped ThreadPool");
        }

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(tp_mutex_);
            if (!tp_run_.load(std::memory_order_relaxed)) {
                throw std::runtime_error("post_task on stopped ThreadPool");
            }
            tp_tasks_.emplace([task]() { (*task)(); });
        }

        tp_task_cv_.notify_one();
        return res;
    }

    int idle_thread_count() const noexcept { 
        return static_cast<int>(tp_idle_count_.load(std::memory_order_acquire)); 
    }
    
    std::size_t thread_count() const noexcept {
        std::lock_guard<std::mutex> lock(tp_mutex_);
        return tp_pool_.size();
    }

    void stop() noexcept {
        bool expected = true;
        if (tp_run_.compare_exchange_strong(expected, false, 
                                           std::memory_order_acq_rel)) {
            tp_task_cv_.notify_all();
            
            std::vector<std::thread> to_join;
            {
                std::lock_guard<std::mutex> lock(tp_mutex_);
                to_join.swap(tp_pool_);
            }
            
            for (auto &t : to_join) {
                if (t.joinable()) t.join();
            }
        }
    }

private:
    void add_threads(std::size_t count) {
        std::lock_guard<std::mutex> lock(tp_mutex_);
        std::size_t threads_to_create = std::min(count, kMaxThreads - tp_pool_.size());
        
        for (std::size_t i = 0; i < threads_to_create; ++i) {
            tp_pool_.emplace_back([this] {
                thread_worker();
            });
            tp_idle_count_.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void thread_worker() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(tp_mutex_);
                tp_task_cv_.wait(lock, [this] {
                    return !tp_run_.load(std::memory_order_acquire) || !tp_tasks_.empty();
                });
                
                if (!tp_run_.load(std::memory_order_acquire) && tp_tasks_.empty()) {
                    return;
                }
                
                task = std::move(tp_tasks_.front());
                tp_tasks_.pop();
                tp_idle_count_.fetch_sub(1, std::memory_order_acq_rel);
            }

            // 直接执行任务，异常会自动存储到future
            task();

            tp_idle_count_.fetch_add(1, std::memory_order_acq_rel);
        }
    }

private:
    mutable std::mutex tp_mutex_;
    std::vector<std::thread> tp_pool_;
    std::queue<Task> tp_tasks_;
    std::condition_variable tp_task_cv_;
    std::atomic<bool> tp_run_{false};
    std::atomic<int> tp_idle_count_{0};
};