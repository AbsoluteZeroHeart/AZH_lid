#include "EventLoopThreadPool.hpp"
#include "pr.hpp"
#include <algorithm>
#include <stdexcept>

// 构造函数：初始化线程池名称和线程数（用户指定>硬件并发数>1）
EventLoopThreadPool::EventLoopThreadPool(const std::string& name, int thread_count)
    : name_(name)
    , thread_count_(thread_count > 0 ? thread_count : std::thread::hardware_concurrency())
{
    // 保证线程数至少为1
    if (thread_count_ < 0) {
        thread_count_ = 1;
    }
    
    LOG_INFO("EventLoopThreadPool[%s] created with %d threads\n", 
             name_.c_str(), thread_count_);
}

// 析构函数：停止线程池，释放资源
EventLoopThreadPool::~EventLoopThreadPool() {
    stop();
}

// 启动线程池：创建并启动所有工作线程+对应的EventLoop
void EventLoopThreadPool::start(const ThreadInitCallback& init_cb) {
    // 原子校验：避免重复启动（线程安全）
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        LOG_WARN("EventLoopThreadPool[%s] already started\n", name_.c_str());
        return;
    }
    
    if (thread_count_ <= 0) {
        LOG_WARN("EventLoopThreadPool[%s] thread_count <= 0\n", name_.c_str());
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);  // 保护线程列表
    
    threads_.reserve(thread_count_);  // 预分配内存
    for (int i = 0; i < thread_count_; ++i) {
        // 创建EventLoop（unique_ptr独占所有权）
        auto loop = std::make_unique<EventLoop>();
        auto* loop_ptr = loop.get();
        
        // 创建线程数据载体，接管EventLoop所有权
        auto thread_data = std::make_unique<ThreadData>(std::move(loop));
        thread_data->running.store(true, std::memory_order_release);
        
        // 启动工作线程：执行run_in_thread
        thread_data->thread = std::thread(
            [this, i, loop_ptr, &init_cb, thread_ptr = thread_data.get()]() {
                this->run_in_thread(i, init_cb);
                thread_ptr->running.store(false, std::memory_order_release);
            }
        );
        
        // Linux下设置线程名称（便于调试）
#ifdef __linux__
        std::string thread_name = name_ + "-" + std::to_string(i);
        pthread_setname_np(thread_data->thread.native_handle(), thread_name.c_str());
#endif
        
        // 将线程数据存入线程池
        threads_.push_back(std::move(thread_data));
        
        LOG_INFO("EventLoopThreadPool[%s] started thread %d, loop=%p\n", 
                 name_.c_str(), i, static_cast<void*>(loop_ptr));
    }
    
    LOG_INFO("EventLoopThreadPool[%s] started with %zu threads\n", 
             name_.c_str(), threads_.size());
}

// 停止线程池：停止所有EventLoop→等待线程退出→清空资源
void EventLoopThreadPool::stop() {
    // 原子校验：未启动/已停止则返回
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }
    
    LOG_INFO("EventLoopThreadPool[%s] stopping...\n", name_.c_str());
    
    // 1. 通知所有EventLoop停止（非阻塞）
    std::vector<EventLoop*> loops;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& td : threads_) {
            if (td && td->loop) {
                loops.push_back(td->loop.get());
            }
        }
    }
    for (auto* loop : loops) {
        loop->stop();
    }
    
    // 2. 收集并等待所有工作线程退出
    std::vector<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& td : threads_) {
            if (td && td->thread.joinable()) {
                threads_to_join.push_back(std::move(td->thread));
            }
        }
        threads_.clear();  // 清空线程数据，触发EventLoop析构
    }
    for (auto& t : threads_to_join) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    LOG_INFO("EventLoopThreadPool[%s] stopped\n", name_.c_str());
}

// 工作线程入口函数：执行初始化回调→运行EventLoop事件循环
void EventLoopThreadPool::run_in_thread(size_t index, const ThreadInitCallback& init_cb) {
    EventLoop* loop = nullptr;
    
    // 获取当前线程对应的EventLoop裸指针（不转移所有权）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= threads_.size() || !threads_[index]) {
            return;
        }
        loop = threads_[index]->loop.get();
    }
    
    if (!loop) return;
    
    // 执行线程初始化回调
    if (init_cb) {
        init_cb(loop);
    }
    
    // 运行EventLoop事件循环（阻塞，直到loop->stop()被调用）
    loop->loop();
    
    // 标记线程停止
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < threads_.size() && threads_[index]) {
            threads_[index]->running.store(false, std::memory_order_release);
        }
    }
}

// 轮询（Round-Robin）获取下一个EventLoop裸指针（不转移所有权）
EventLoop* EventLoopThreadPool::get_next_loop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (threads_.empty()) {
        return nullptr;
    }
    
    // 原子递增索引，取模实现轮询负载均衡
    size_t idx = next_index_.fetch_add(1, std::memory_order_relaxed) % threads_.size();
    return threads_[idx]->loop.get();
}

// 获取指定索引的EventLoop裸指针
EventLoop* EventLoopThreadPool::get_loop(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= threads_.size()) {
        return nullptr;
    }
    return threads_[index]->loop.get();
}

// 获取所有EventLoop的裸指针列表
std::vector<EventLoop*> EventLoopThreadPool::get_all_loops() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<EventLoop*> result;
    result.reserve(threads_.size());
    
    for (const auto& td : threads_) {
        if (td && td->loop) {
            result.push_back(td->loop.get());
        }
    }
    
    return result;
}

// 获取线程池实际启动的线程数（线程安全）
size_t EventLoopThreadPool::thread_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.size();
}