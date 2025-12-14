#ifndef TCP_CONNECTION_HPP
#define TCP_CONNECTION_HPP

#include <memory>
#include <functional>
#include <string>
#include <atomic>

#include <netinet/in.h>

#include "data_buf.hpp"
#include "EventLoop.hpp"
#include "Channel.hpp"

// 前向声明：避免循环包含
class TcpServer;

// TcpConnection：管理单个TCP连接的核心类，封装连接fd、读写缓冲区、事件回调
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using ConnectedCallback = std::function<void(Ptr)>;    // 连接建立回调：连接成功后触发
    using MessageCallback   = std::function<void(Ptr, InputBuffer&)>;    // 消息回调：收到数据后触发（携带输入缓冲区）
    using CloseCallback     = std::function<void(Ptr)>;    // 关闭回调：连接关闭后触发


    // 连接状态枚举：生命周期状态机
    enum class State {
        kConnecting,    // 连接中
        kConnected,     // 已连接（正常通信）
        kDisconnecting, // 正在断开
        kDisconnected   // 已断开
    };

    // 构造函数：初始化连接fd、关联EventLoop/服务器、记录对端地址
    TcpConnection(TcpServer* server,
                  EventLoop* loop,
                  int connfd,
                  const sockaddr_in& peer,
                  socklen_t peer_len);

    ~TcpConnection();

    // 禁用拷贝/赋值：连接fd唯一，避免资源冲突
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // 设置各类回调函数（移动语义减少拷贝）
    void set_connected_cb(ConnectedCallback cb) { connected_cb_ = std::move(cb); }
    void set_message_cb(MessageCallback cb)     { message_cb_   = std::move(cb); }
    void set_close_cb(CloseCallback cb)         { close_cb_     = std::move(cb); }

    // 发送数据（对外接口）
    bool send(const char* data, size_t len);
    bool send(const std::string& data) { return send(data.data(), data.size()); }

    // 关闭连接（触发断开流程）
    void shutdown();

    // 获取连接fd（对外只读）
    int fd() const { return connfd_; }
    // 检查连接是否处于已连接状态（原子操作，线程安全）
    bool is_connected() const {
        return state_.load() == State::kConnected;
    }

    // 获取对端IP+端口（字符串格式，如127.0.0.1:8080）
    std::string peer_ipport() const;

    // 连接建立完成：更新状态+触发连接回调
    void connect_established();

private:

    // 处理Channel事件（EPOLLIN/EPOLLOUT/EPOLLERR等）
    void handle_event(uint32_t events);

    void handle_read();    // 读事件处理：从fd读取数据到输入缓冲区，触发消息回调

    void handle_write();    // 写事件处理：将输出缓冲区数据写入fd

    void handle_close();    // 关闭事件处理：更新状态+触发关闭回调

    void handle_error();    // 错误事件处理：记录错误日志


    // IO线程内发送数据（实际发送逻辑，避免跨线程操作）
    void sendInLoop(const char* data, size_t len);
    // IO线程内关闭连接（实际断开逻辑）
    void shutdownInLoop();

private:
    TcpServer* server_;          // 关联的TcpServer（裸指针，仅使用权）
    EventLoop* loop_;            // 连接所属的IO线程EventLoop（裸指针，仅使用权）

    int connfd_;                 // 连接fd
    sockaddr_in peer_addr_;      // 对端地址结构体
    socklen_t peer_len_;         // 对端地址长度

    std::shared_ptr<Channel> channel_;  // 管理connfd_的Channel（TcpConnection持有所有权）
    InputBuffer  input_buf_;     // 读缓冲区：存储从fd读取的未处理数据
    OutputBuffer output_buf_;    // 写缓冲区：存储待写入fd的数据

    ConnectedCallback connected_cb_;    // 连接建立回调
    MessageCallback   message_cb_;      // 消息回调
    CloseCallback     close_cb_;        // 关闭回调

    std::atomic<State> state_{State::kConnecting};  // 连接状态（原子变量，线程安全）
};

#endif