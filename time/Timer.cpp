#include "Timer.hpp"
#include <iostream>

Timer::Timer(std::size_t pool_size)
    : thread_pool_(std::make_unique<ThreadPool>(pool_size)) {
}

Timer::~Timer() {
    stop();
}

bool Timer::start() {
    if (is_running_.exchange(true)) {
        return false; // 已经在运行
    }
    
    should_stop_.store(false);
    
    try {
        timer_thread_ = std::thread([this]() { timer_loop(); });
        return true;
    } catch (...) {
        is_running_.store(false);
        return false;
    }
}

void Timer::stop() {
    if (!is_running_.load()) {
        return;
    }
    
    should_stop_.store(true);
    condition_.notify_all();
    
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
    
    // 停止线程池
    thread_pool_->stop();
    
    // 清理资源
    std::lock_guard<std::mutex> lock(mutex_);
    task_queue_ = std::priority_queue<TimerTask>(); // 清空队列
    cancelled_tasks_.clear();
    
    is_running_.store(false);
}

std::size_t Timer::pending_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_queue_.size();
}

int Timer::generate_task_id() {
    return next_task_id_.fetch_add(1, std::memory_order_relaxed);
}

void Timer::add_task(TimerTask task) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_queue_.push(std::move(task));
    condition_.notify_one();
}

bool Timer::cancel(int task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 如果任务已经在取消集合中，返回false
    if (cancelled_tasks_.find(task_id) != cancelled_tasks_.end()) {
        return false;
    }
    
    cancelled_tasks_.insert(task_id);
    return true;
}

void Timer::timer_loop() {
    while (!should_stop_.load()) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到有任务可执行或收到停止信号
        if (task_queue_.empty()) {
            condition_.wait(lock, [this]() {
                return should_stop_.load() || !task_queue_.empty();
            });
            
            if (should_stop_.load()) {
                break;
            }
        }
        
        // 获取当前时间
        auto now = std::chrono::steady_clock::now();
        const TimerTask& top_task = task_queue_.top();
        
        // 如果任务还没到期，等待到到期时间
        if (top_task.expire_time > now) {
            auto wait_time = top_task.expire_time - now;
            condition_.wait_for(lock, wait_time);
            continue;
        }
        
        // 取出到期的任务
        TimerTask task = std::move(const_cast<TimerTask&>(task_queue_.top()));
        task_queue_.pop();
        
        // 检查任务是否被取消
        bool is_cancelled = cancelled_tasks_.erase(task.task_id) > 0;
        
        // 如果不是周期性/重复任务或任务被取消，直接执行（或不执行）
        if (is_cancelled) {
            // 任务已取消，跳过执行
            lock.unlock();
            continue;
        }
        
        // 如果需要重新调度（周期性任务或重复任务还有剩余次数）
        if (task.is_periodic || (task.is_repeat && task.repeat_count > 1)) {
            // 创建下一次执行的任务
            TimerTask next_task = task;
            next_task.expire_time = now + task.interval;
            
            if (task.is_repeat) {
                next_task.repeat_count--;
            }
            
            // 重新加入队列
            task_queue_.push(std::move(next_task));
            condition_.notify_one();
        }
        
        // 释放锁，避免执行任务时阻塞定时器
        lock.unlock();
        
        // 在线程池中执行任务
        try {
            thread_pool_->post_task(std::move(task.callback));
        } catch (const std::exception& e) {
            std::cerr << "Failed to post timer task: " << e.what() << std::endl;
        }
    }
}