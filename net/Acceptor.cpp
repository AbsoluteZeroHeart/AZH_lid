#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "logger.hpp"
#include "pr.hpp"

#include "EventLoop.hpp"
#include "Channel.hpp"
#include "TcpServer.hpp"
#include "TcpConnection.hpp"
#include "Acceptor.hpp"

// 创建监听socket：设置非阻塞+CLOEXEC，Linux优先用socket flag，其他系统用fcntl补全
static int create_listen_socket() {
#if defined(__linux__)
    // Linux下直接通过socket flag设置非阻塞+CLOEXEC，减少系统调用
    int fd = ::socket(AF_INET,
                      SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                      IPPROTO_TCP);
    if (fd >= 0) return fd;
#endif
    // 非Linux系统先创建socket，再通过fcntl设置非阻塞
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) return -1;

    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
}

// 设置socket复用：开启SO_REUSEADDR（地址复用）、SO_REUSEPORT（端口复用）
static void set_socket_reuse(int fd) {
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
}

// 构造函数：初始化监听fd、绑定IP/端口、创建Channel并设置新连接回调
Acceptor::Acceptor(TcpServer* server,
                   EventLoop* loop,
                   const std::string& ip,
                   uint16_t port)
    : server_(server),
      loop_(loop),
      ip_(ip),
      port_(port) {

    // 校验核心依赖：server/loop不能为空
    if (!server_ || !loop_) {
        throw std::invalid_argument("Acceptor: null server or loop");
    }

    // 创建监听fd
    listen_fd_ = create_listen_socket();
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    // 设置socket复用
    set_socket_reuse(listen_fd_);

    // 创建空闲fd：用于处理fd耗尽场景（EMFILE/ENFILE）
    idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);

    // 初始化服务器地址结构体，绑定IP+端口（网络序）
    std::memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = ::htons(port_);

    // 转换IP字符串到网络序
    if (::inet_pton(AF_INET, ip_.c_str(), &server_addr_.sin_addr) != 1) {
        ::close(listen_fd_);
        throw std::invalid_argument("invalid ip");
    }

    // 绑定监听fd到指定IP+端口
    if (::bind(listen_fd_,
               reinterpret_cast<sockaddr*>(&server_addr_),
               sizeof(server_addr_)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("bind failed");
    }

    // 创建Channel管理监听fd，设置事件回调（读/错误/挂断时处理新连接）
    channel_ = std::make_shared<Channel>(loop_, listen_fd_);
    channel_ -> set_callback([this](uint32_t events) {
        if (events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
            do_accept();
        }
    });


    LOG_INFO("Acceptor created fd=%d %s:%u\n",
             listen_fd_, ip_.c_str(), port_);
}

// 析构函数：清理Channel、关闭监听fd/空闲fd（保证不抛异常）
Acceptor::~Acceptor() noexcept {
    LOG_ERROR("~Acceptor() called, closing listen fd=%d", listen_fd_);

    if (channel_) {
        // 若在EventLoop线程，直接禁用所有事件；否则跨线程调用
        if (loop_->is_in_loop_thread()) {
            channel_->disable_all();
        } else {
            auto ch = std::move(channel_);
            channel_.reset();
            loop_->runInLoop([ch]() {
                ch->disable_all();
            });
        }
    }

    // 关闭文件描述符
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (idle_fd_ >= 0) ::close(idle_fd_);
}

// 启动监听：调用listen系统调用，注册读事件到EventLoop
void Acceptor::listen() {
    if (listening_) return;

    // 启动监听（backlog为未完成连接队列长度）
    if (::listen(listen_fd_, kBacklog) < 0) {
        throw std::runtime_error("listen failed");
    }

    listening_ = true;

    // 注册监听fd的读事件（必须在EventLoop线程执行）
    loop_->runInLoop([this]() {
        channel_->enable_read();
    });

    LOG_INFO("Acceptor listening on %s:%u\n",
             ip_.c_str(), port_);
}

// 处理新连接：循环accept获取连接fd，分配IO线程，创建TcpConnection
void Acceptor::do_accept() {
    while (true) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);

#if defined(__linux__)
        int connfd = ::accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&peer),
            &len,
            SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        int connfd = ::accept(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&peer),
            &len);
#endif

        if (connfd < 0) {
            int err = errno;

            if (err == EINTR) {
                continue;
            }

            if (err == EAGAIN || err == EWOULDBLOCK) {
                // ET 模式：必须读到这里才能退出
                break;
            }

            if (err == EMFILE || err == ENFILE) {
                LOG_ERROR("accept EMFILE, fd limit reached");

                // 正确的 idle_fd 技巧
                ::close(idle_fd_);
                int tmp = ::accept(listen_fd_, nullptr, nullptr);
                if (tmp >= 0) {
                    ::close(tmp);
                }
                idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);

                continue;  // 不能 break 继续循环，直到 EAGAIN 
            }

            LOG_ERROR("accept error: errno=%d (%s)", err, strerror(err));
            break;
        }

        EventLoop* io_loop = server_->get_next_loop();
        if (!io_loop) {
            ::close(connfd);
            continue;
        }

        auto conn = std::make_shared<TcpConnection>(
            server_, io_loop, connfd, peer, len);

        conn->set_connected_cb(server_->ts_connected_cb);
        conn->set_message_cb(server_->ts_message_cb);
        conn->set_close_cb(server_->ts_close_cb);

        io_loop->runInLoop([conn]() {
            conn->connect_established();
        });

        server_->add_new_tcp_conn(conn);
    }
}

