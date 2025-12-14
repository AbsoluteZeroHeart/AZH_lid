#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdint>

#include "EventLoop.hpp"
#include "EventLoopThreadPool.hpp" 
#include "Acceptor.hpp"
#include "TcpConnection.hpp"
#include "data_buf.hpp"
#include "ConnectionTimeoutManager.hpp"

class TcpServer {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback    = std::function<void(const TcpConnectionPtr&, InputBuffer&)>;
    using CloseCallback      = std::function<void(const TcpConnectionPtr&)>;
    using DataCallback       = std::function<void(const TcpConnectionPtr&, const char*, size_t)>;
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /**
     * @param base_loop 主 EventLoop（通常在主线程，用来 accept）
     * @param ip        监听 IP（如 "0.0.0.0"）
     * @param port      监听端口
     * @param io_thread_count 子 IO 线程数量（>=0）。0 表示仅用 base_loop 作为唯一 IO loop。
     * @param name      服务器名称（用于日志）
     */
    TcpServer(EventLoop* base_loop, 
              const std::string& ip, 
              uint16_t port, 
              int io_thread_count = 4,
              const std::string& name = "TcpServer");
    ~TcpServer();

    // 启动服务器（创建 acceptor、启动 io 线程并 listen）
    void start();

    void stop();
    void remove_connection(const TcpConnection::Ptr& conn);

    // Round-robin 获取下一个 IO loop（供 Acceptor 使用）
    EventLoop* get_next_loop();

    // 添加/移除连接（Acceptor 会调用 add_new_tcp_conn）
    void add_new_tcp_conn(const TcpConnectionPtr& conn);
    void remove_tcp_conn(const TcpConnectionPtr& conn);

    // 空闲连接超时管理
    void set_idle_timeout(int timeout_ms);
    void enable_idle_timeout(bool enable);
    void update_connection_activity(const TcpConnectionPtr& conn);

    // 设置线程初始化回调（必须在start之前调用）
    void set_thread_init_callback(const ThreadInitCallback& cb) {
        thread_init_cb_ = cb;
    }

    // Setters for user callbacks
    void set_connection_callback(ConnectionCallback cb) { user_conn_cb_ = std::move(cb); }
    void set_message_callback(MessageCallback cb)       { user_msg_cb_ = std::move(cb); }
    void set_close_callback(CloseCallback cb)           { user_close_cb_ = std::move(cb); }
    void set_data_callback(DataCallback cb)             { user_data_cb_ = std::move(cb); }

    // 统计信息
    size_t connection_count() const;
    size_t idle_connection_count() const;
    
    // 获取服务器名称
    const std::string& name() const { return name_; }
    
    // 获取EventLoop线程池
    EventLoopThreadPool* thread_pool() { return thread_pool_.get(); }

    // ---------------------------------------------------------
    // 友元类声明
    // ---------------------------------------------------------
    friend class Acceptor;

private:
    // 设置默认回调
    void setup_default_callbacks();

    // 连接活动处理
    void on_connection_active(const TcpConnectionPtr& conn);

    // 空闲连接超时回调
    void on_connection_idle_timeout(const TcpConnectionPtr& conn);

private:
    std::string name_;           // 服务器名称
    EventLoop* base_loop_;       // 不所有权（由外部创建/销毁）
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<EventLoopThreadPool> thread_pool_;  

    std::string ip_;
    uint16_t port_;
    int io_thread_count_;

    // 管理连接：fd -> TcpConnectionPtr
    std::unordered_map<int, TcpConnectionPtr> connections_;
    mutable std::mutex conn_mutex_; // 保护 connections_

    // 用户回调保存（被 ts_* 包装调用）
    ConnectionCallback user_conn_cb_;
    MessageCallback    user_msg_cb_;
    CloseCallback      user_close_cb_;
    DataCallback       user_data_cb_;
    ThreadInitCallback thread_init_cb_;
    // ---------------------------------------------------------
    // 供 Acceptor 直接访问的回调（通过友元关系）
    // ---------------------------------------------------------
    ConnectionCallback ts_connected_cb;
    MessageCallback    ts_message_cb;
    CloseCallback      ts_close_cb;
    // ---------------------------------------------------------

    // 空闲连接管理器
    std::unique_ptr<ConnectionTimeoutManager> idle_manager_;
    int idle_timeout_ms_ = 300000; // 默认5分钟
    bool idle_timeout_enabled_ = false;

    // 服务器状态
    std::atomic<bool> started_{false};
};

#endif // TCPSERVER_HPP