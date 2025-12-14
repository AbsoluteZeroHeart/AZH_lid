#include "TcpConnection.hpp"

#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sstream>

// 构造函数：初始化连接核心参数，关联TcpServer、IO线程EventLoop，记录连接fd和对端地址
TcpConnection::TcpConnection(TcpServer* server,
                             EventLoop* loop,
                             int connfd,
                             const sockaddr_in& peer,
                             socklen_t peer_len)
    : server_(server),
      loop_(loop),
      connfd_(connfd),
      peer_addr_(peer),
      peer_len_(peer_len) {
}

// 析构函数：空实现（连接资源在handle_close中释放，避免double free）
TcpConnection::~TcpConnection() {
}

// IO线程内完成连接初始化：创建Channel、注册读事件、更新状态、触发连接回调
void TcpConnection::connect_established() {
    auto self = shared_from_this();
    // 创建Channel管理连接fd，绑定事件回调
    channel_ = std::make_shared<Channel>(loop_, connfd_);
    channel_->set_callback([self](uint32_t events){ self->handle_event(events); });
    channel_->enable_read();  // 启用读事件（监听数据到达）
    
    channel_->tie(self);  // 绑定self，避免Channel回调时TcpConnection已销毁
    state_.store(State::kConnected);  // 原子更新连接状态为已连接

    // 触发连接建立回调
    if (connected_cb_) {
        connected_cb_(shared_from_this());
    }
}

// 分发epoll事件到对应处理函数（错误/挂断/读/写）
void TcpConnection::handle_event(uint32_t events) {
    // 优先处理错误/挂断事件
    if (events & (EPOLLERR | EPOLLHUP)) {
        handle_error();
        return;
    }
    // 对端关闭连接（RDHUP）
    if (events & EPOLLRDHUP) {
        handle_close();
        return;
    }
    // 读事件（数据到达）
    if (events & EPOLLIN) {
        handle_read();
    }
    // 写事件（fd可写）
    if (events & EPOLLOUT) {
        handle_write();
    }
}

// 处理读事件：从fd读取数据到输入缓冲区，触发消息回调
void TcpConnection::handle_read() {
    // 从fd读取数据到input_buf_
    int n = input_buf_.read_from_fd(connfd_);
    if (n > 0) {
        // 有数据，触发消息回调（交给上层处理）
        if (message_cb_) {
            message_cb_(shared_from_this(), input_buf_);
        }
    } else if (n == 0) {
        // 对端关闭（EOF），处理连接关闭
        handle_close();
    } else {
        // 读错误，处理错误
        handle_error();
    }
}

// 处理写事件：将输出缓冲区数据写入fd，写完禁用写事件
void TcpConnection::handle_write() {
    // 写缓冲区数据到fd
    int n = output_buf_.write_to_fd(connfd_);
    if (n < 0) {
        handle_error();
        return;
    }

    // 缓冲区已空，禁用写事件（避免epoll频繁触发）
    if (output_buf_.length() == 0) {
        channel_->disable_write();
        // 若处于断开中状态，关闭写端（半关闭）
        if (state_.load() == State::kDisconnecting) {
            ::shutdown(connfd_, SHUT_WR);
        }
    }
}

// 处理连接关闭：更新状态、清理Channel、触发关闭回调、关闭fd
void TcpConnection::handle_close() {
    // 原子更新状态（仅当当前是已连接时才处理，避免重复关闭）
    State expected = State::kConnected;
    if (!state_.compare_exchange_strong(expected, State::kDisconnected)) {
        return;
    }

    // 禁用Channel所有事件并释放
    if(channel_){
        channel_->disable_all();
        channel_.reset();
    }

    // 触发关闭回调（通知TcpServer移除连接）
    if (close_cb_) {
        close_cb_(shared_from_this());
    }

    // 关闭连接fd，标记为无效
    ::close(connfd_);
    connfd_ = -1;
}

// 错误处理：简化为直接关闭连接
void TcpConnection::handle_error() {
    handle_close();
}

// 对外发送数据接口：判断是否在IO线程，直接发送或投递到IO线程
bool TcpConnection::send(const char* data, size_t len) {
    // 非已连接状态，直接返回失败
    if (state_.load() != State::kConnected) return false;

    auto self = shared_from_this();
    // 已在IO线程，直接调用sendInLoop
    if (loop_->is_in_loop_thread()) {
        sendInLoop(data, len);
    } else {
        // 跨线程，拷贝数据后投递任务到IO线程
        std::string msg(data, len);
        loop_->queueInLoop([self, msg] {
            self->sendInLoop(msg.data(), msg.size());
        });
    }
    return true;
}

// IO线程内实际发送逻辑：先尝试直接写，剩余数据入写缓冲区并启用写事件
void TcpConnection::sendInLoop(const char* data, size_t len) {
    if (state_.load() != State::kConnected) return;

    ssize_t n = 0;
    // 写缓冲区为空，尝试直接写入fd
    if (output_buf_.length() == 0) {
        n = ::write(connfd_, data, len);
        if (n < 0) {
            // 非EAGAIN（fd不可写）则处理错误
            if (errno != EAGAIN) {
                handle_error();
                return;
            }
            n = 0;  // EAGAIN则标记已写0字节
        }
    }

    // 未写完的数据存入输出缓冲区，启用写事件（等待fd可写）
    if (static_cast<size_t>(n) < len) {
        output_buf_.write_to_buf(data + n, len - n);
        channel_->enable_write();
    }
}

// 对外断开连接接口：投递到IO线程执行
void TcpConnection::shutdown() {
    if (state_.load() == State::kConnected) {
        auto self = shared_from_this();
        loop_->runInLoop([self] {
            self->shutdownInLoop();
        });
    }
}

// IO线程内断开连接逻辑：标记状态，无待发数据则关闭写端（半关闭）
void TcpConnection::shutdownInLoop() {
    if (state_.load() != State::kConnected) return;

    state_.store(State::kDisconnecting);  // 标记为正在断开
    // 输出缓冲区为空，直接关闭写端（避免数据丢失）
    if (output_buf_.length() == 0) {
        ::shutdown(connfd_, SHUT_WR);
    }
}

// 转换对端地址为"IP:端口"字符串（如127.0.0.1:8080）
std::string TcpConnection::peer_ipport() const {
    char ipbuf[64];
    ::inet_ntop(AF_INET, &peer_addr_.sin_addr, ipbuf, sizeof(ipbuf));
    uint16_t port = ntohs(peer_addr_.sin_port);  // 网络序转主机序

    std::ostringstream ss;
    ss << ipbuf << ":" << port;
    return ss.str();
}