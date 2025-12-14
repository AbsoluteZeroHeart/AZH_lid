#ifndef CONNECTION_TIMEOUT_MANAGER_HPP
#define CONNECTION_TIMEOUT_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <condition_variable>

#include "TcpConnection.hpp"

/**
 * @brief 基于时间轮算法的连接超时管理器
 * @details 用于管理 TCP 连接的空闲超时，支持高效的连接活动更新和超时检测
 */
class ConnectionTimeoutManager {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using TimeoutCallback = std::function<void(const TcpConnectionPtr&)>;
    
    /**
     * @brief 构造函数
     * @param idle_timeout_ms 空闲超时时间（毫秒）
     * @param wheel_size 时间轮大小（槽数），默认为 60（秒）
     * @param tick_interval_ms 时间轮转动间隔（毫秒），默认为 1000（1秒）
     */
    ConnectionTimeoutManager(int idle_timeout_ms = 300000, // 5分钟
                            int wheel_size = 60,
                            int tick_interval_ms = 1000);
    
    ~ConnectionTimeoutManager();
    
    // 禁用拷贝和移动
    ConnectionTimeoutManager(const ConnectionTimeoutManager&) = delete;
    ConnectionTimeoutManager& operator=(const ConnectionTimeoutManager&) = delete;
    ConnectionTimeoutManager(ConnectionTimeoutManager&&) = delete;
    ConnectionTimeoutManager& operator=(ConnectionTimeoutManager&&) = delete;
    
    /**
     * @brief 启动超时管理器
     */
    void start();
    
    /**
     * @brief 停止超时管理器
     */
    void stop();
    
    /**
     * @brief 添加连接
     * @param conn 连接指针
     */
    void add_connection(const TcpConnectionPtr& conn);
    
    /**
     * @brief 更新连接活动时间（当连接有数据收发时调用）
     * @param conn_id 连接ID
     */
    void update_activity(int conn_id);
    
    /**
     * @brief 移除连接（当连接关闭时调用）
     * @param conn_id 连接ID
     */
    void remove_connection(int conn_id);
    
    /**
     * @brief 设置超时回调
     * @param callback 超时回调函数
     */
    void set_timeout_callback(TimeoutCallback callback);
    
    /**
     * @brief 获取当前管理的连接数
     * @return 连接数量
     */
    size_t connection_count() const;
    
    /**
     * @brief 获取当前空闲连接数
     * @return 空闲连接数量
     */
    size_t idle_connection_count() const;
    
    /**
     * @brief 设置空闲超时时间
     * @param idle_timeout_ms 空闲超时时间（毫秒）
     */
    void set_idle_timeout(int idle_timeout_ms);
    
    /**
     * @brief 重置所有连接的空闲时间
     */
    void reset_all();

private:
    // 时间轮中的连接条目
    struct ConnectionEntry {
        TcpConnectionPtr conn;
        int slot_position;           // 当前在时间轮中的槽位
        int remaining_ticks;         // 剩余tick次数
        std::chrono::steady_clock::time_point last_activity_time;
        
        ConnectionEntry(const TcpConnectionPtr& c, int pos, int ticks)
            : conn(c), slot_position(pos), remaining_ticks(ticks),
              last_activity_time(std::chrono::steady_clock::now()) {}
    };
    
    // 时间轮槽位
    struct TimeWheelSlot {
        std::vector<std::shared_ptr<ConnectionEntry>> entries;
        std::mutex slot_mutex;
    };
    
    /**
     * @brief 时间轮线程主循环
     */
    void time_wheel_loop();
    
    /**
     * @brief 处理超时连接
     */
    void process_timeout_connections();
    
    /**
     * @brief 将连接移动到新的槽位
     * @param entry 连接条目
     */
    void move_to_new_slot(std::shared_ptr<ConnectionEntry> entry);
    
    /**
     * @brief 计算连接应该放置的槽位
     * @param last_activity_time 最后活动时间
     * @return 槽位位置
     */
    int calculate_slot(const std::chrono::steady_clock::time_point& last_activity_time);
    
    /**
     * @brief 清理已关闭的连接
     */
    void cleanup_closed_connections();
    
private:
    // 配置参数
    int idle_timeout_ms_;
    int wheel_size_;
    int tick_interval_ms_;
    
    // 时间轮数据结构
    std::vector<TimeWheelSlot> time_wheel_;
    int current_slot_ = 0;
    
    // 连接管理
    std::unordered_map<int, std::shared_ptr<ConnectionEntry>> connections_;
    mutable std::mutex connections_mutex_;
    
    // 超时回调
    TimeoutCallback timeout_callback_;
    
    // 控制线程
    std::thread time_wheel_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    
    // 统计数据
    std::atomic<size_t> total_connections_{0};
    std::atomic<size_t> idle_connections_{0};
    
    // 清理线程
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    std::condition_variable cleanup_cv_;
    std::mutex cleanup_mutex_;
};

#endif // CONNECTION_TIMEOUT_MANAGER_HPP