#ifndef EVENTLOOPTHREADPOOL_HPP
#define EVENTLOOPTHREADPOOL_HPP

#include "EventLoop.hpp"
#include "logger.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <string>
#include <future>

class EventLoopThreadPool {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    
    /**
     * @param name 线程池名称（用于日志）
     * @param thread_count 线程数量，0表示使用CPU核心数
     */
    explicit EventLoopThreadPool(const std::string& name = "EventLoopThreadPool", 
                                 int thread_count = 0);
    ~EventLoopThreadPool();
    
    // 禁止拷贝和移动
    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool(EventLoopThreadPool&&) = delete;
    EventLoopThreadPool& operator=(EventLoopThreadPool&&) = delete;
    
    /**
     * @brief 启动线程池
     * @param init_cb 每个线程启动后的初始化回调
     */
    void start(const ThreadInitCallback& init_cb = nullptr);
    
    /**
     * @brief 停止线程池（等待所有线程退出）
     */
    void stop();
    
    /**
     * @brief 获取下一个EventLoop（Round-Robin）
     */
    EventLoop* get_next_loop();
    
    /**
     * @brief 获取指定索引的EventLoop
     */
    EventLoop* get_loop(size_t index);
    
    /**
     * @brief 获取所有EventLoop
     */
    std::vector<EventLoop*> get_all_loops() const;
    
    /**
     * @brief 获取线程数量
     */
    size_t thread_count() const;
    
    /**
     * @brief 获取线程池名称
     */
    const std::string& name() const { return name_; }
    
    /**
     * @brief 是否已启动
     */
    bool started() const { return started_; }
    
private:
    // 线程数据
    struct ThreadData {
        std::thread thread;
        std::unique_ptr<EventLoop> loop;
        std::atomic<bool> running{false};
        
        ThreadData(std::unique_ptr<EventLoop> lp) 
            : loop(std::move(lp)) {}
    };
    
    // 线程工作函数
    void run_in_thread(size_t index, const ThreadInitCallback& init_cb);
    
private:
    std::string name_;
    std::vector<std::unique_ptr<ThreadData>> threads_;
    std::atomic<size_t> next_index_{0};
    mutable std::mutex mutex_;
    std::atomic<bool> started_{false};
    int thread_count_;
};

#endif // EVENTLOOPTHREADPOOL_HPP