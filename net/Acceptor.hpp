#ifndef ACCEPTOR_HPP
#define ACCEPTOR_HPP

#include <cstdint>
#include <string>
#include <memory>
#include <netinet/in.h>

class EventLoop;
class TcpServer;
class Channel;

class Acceptor {
public:
    // 构造函数：初始化监听组件，关联TcpServer和EventLoop，指定监听IP/端口
    Acceptor(TcpServer* server,
             EventLoop* loop,
             const std::string& ip, 
             uint16_t port);

    // 析构函数：释放监听fd/Channel等资源（noexcept保证不抛异常）
    ~Acceptor() noexcept;

    // 禁用拷贝/赋值：监听fd唯一，避免资源冲突
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    // 启动端口监听：创建监听fd、绑定端口、注册读事件到EventLoop
    void listen();
    // 检查是否正在监听（内联函数，无异常）
    bool is_listening() const noexcept { return listening_; }

private:
    // 私有：处理新连接事件（核心逻辑：调用accept获取新连接fd，回调TcpServer）
    void do_accept();

private:
    TcpServer* server_;          // 关联的TcpServer（裸指针，仅使用权，无所有权）
    EventLoop* loop_;            // 运行监听事件的EventLoop（裸指针，仅使用权）

    int listen_fd_{-1};          // 监听fd（-1表示未创建）
    int idle_fd_{-1};            // 空闲fd（用于处理fd耗尽场景）

    std::shared_ptr<Channel> channel_;  // 管理listen_fd_的Channel（Acceptor持有所有权）

    sockaddr_in server_addr_{};  // 服务器地址结构体（存储IP/端口的网络序格式）
    std::string ip_;             // 监听的IP地址（字符串格式）
    uint16_t port_{0};           // 监听的端口号

    bool listening_{false};      // 标记是否正在监听

    static constexpr int kBacklog = 1024;  // listen系统调用的backlog参数（未完成连接队列长度）
};

#endif