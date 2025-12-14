#ifndef TIMER_HPP
#define TIMER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

#include "ThreadPool.hpp"

/**
 * @brief 定时器类，支持单次、周期性和重复任务
 */
class Timer {
public:
    /**
     * @brief 定时器任务结构
     */
    struct TimerTask {
        std::chrono::steady_clock::time_point expire_time;  ///< 过期时间
        std::function<void()> callback;                     ///< 回调函数
        int task_id;                                        ///< 任务ID
        int repeat_count;                                   ///< 剩余重复次数
        std::chrono::milliseconds interval;                 ///< 执行间隔
        bool is_periodic;                                   ///< 是否为周期性任务
        bool is_repeat;                                     ///< 是否为重复任务
        
        bool operator<(const TimerTask& other) const {
            // 最小堆：过期时间早的优先级高
            return expire_time > other.expire_time;
        }
    };
    
    /**
     * @brief 构造函数
     * @param pool_size 定时器使用的线程池大小（默认2）
     */
    explicit Timer(std::size_t pool_size = 2);
    
    /**
     * @brief 析构函数
     */
    ~Timer();
    
    // 禁用拷贝和移动
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
    
    /**
     * @brief 启动定时器
     * @return 启动是否成功
     */
    bool start();
    
    /**
     * @brief 停止定时器
     */
    void stop();
    
    /**
     * @brief 检查定时器是否正在运行
     * @return 是否运行
     */
    bool is_running() const { return is_running_.load(); }
    
    /**
     * @brief 获取待执行任务数量
     * @return 任务数量
     */
    std::size_t pending_tasks() const;
    
    /**
     * @brief 延迟执行任务（单次）
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param delay_ms 延迟时间（毫秒）
     * @param func 可调用对象
     * @param args 参数
     * @return 任务ID，失败返回-1
     */
    template <typename F, typename... Args>
    int schedule_once(int delay_ms, F&& func, Args&&... args);
    
    /**
     * @brief 周期性执行任务
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param interval_ms 间隔时间（毫秒）
     * @param func 可调用对象
     * @param args 参数
     * @return 任务ID，失败返回-1
     */
    template <typename F, typename... Args>
    int schedule_periodic(int interval_ms, F&& func, Args&&... args);
    
    /**
     * @brief 重复执行指定次数的任务
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param interval_ms 间隔时间（毫秒）
     * @param repeat_count 重复次数
     * @param func 可调用对象
     * @param args 参数
     * @return 任务ID，失败返回-1
     */
    template <typename F, typename... Args>
    int schedule_repeat(int interval_ms, int repeat_count, F&& func, Args&&... args);
    
    /**
     * @brief 取消任务
     * @param task_id 任务ID
     * @return 是否成功取消
     */
    bool cancel(int task_id);

private:
    /**
     * @brief 定时器主循环
     */
    void timer_loop();
    
    /**
     * @brief 添加任务到队列
     * @param task 任务
     */
    void add_task(TimerTask task);
    
    /**
     * @brief 生成唯一任务ID
     * @return 任务ID
     */
    int generate_task_id();
    
private:
    std::unique_ptr<ThreadPool> thread_pool_;
    std::priority_queue<TimerTask> task_queue_;
    std::unordered_set<int> cancelled_tasks_;  // 已取消的任务ID集合
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread timer_thread_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<int> next_task_id_{0};
};

// 模板方法实现
template <typename F, typename... Args>
int Timer::schedule_once(int delay_ms, F&& func, Args&&... args) {
    if (delay_ms <= 0 || !is_running_.load()) {
        return -1;
    }
    
    TimerTask task;
    task.task_id = generate_task_id();
    task.expire_time = std::chrono::steady_clock::now() + 
                       std::chrono::milliseconds(delay_ms);
    task.callback = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
    task.is_periodic = false;
    task.is_repeat = false;
    task.interval = std::chrono::milliseconds(delay_ms);
    task.repeat_count = 0;
    
    add_task(std::move(task));
    return task.task_id;
}

template <typename F, typename... Args>
int Timer::schedule_periodic(int interval_ms, F&& func, Args&&... args) {
    if (interval_ms <= 0 || !is_running_.load()) {
        return -1;
    }
    
    TimerTask task;
    task.task_id = generate_task_id();
    task.expire_time = std::chrono::steady_clock::now() + 
                       std::chrono::milliseconds(interval_ms);
    task.callback = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
    task.is_periodic = true;
    task.is_repeat = false;
    task.interval = std::chrono::milliseconds(interval_ms);
    task.repeat_count = 0;
    
    add_task(std::move(task));
    return task.task_id;
}

template <typename F, typename... Args>
int Timer::schedule_repeat(int interval_ms, int repeat_count, F&& func, Args&&... args) {
    if (interval_ms <= 0 || repeat_count <= 0 || !is_running_.load()) {
        return -1;
    }
    
    TimerTask task;
    task.task_id = generate_task_id();
    task.expire_time = std::chrono::steady_clock::now() + 
                       std::chrono::milliseconds(interval_ms);
    task.callback = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
    task.is_periodic = false;
    task.is_repeat = true;
    task.interval = std::chrono::milliseconds(interval_ms);
    task.repeat_count = repeat_count;
    
    add_task(std::move(task));
    return task.task_id;
}

#endif // TIMER_HPP