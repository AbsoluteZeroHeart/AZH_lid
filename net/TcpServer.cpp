#include "TcpServer.hpp"
#include "logger.hpp"
#include "pr.hpp"

#include <stdexcept>
#include <algorithm>
#include <utility>

// 构造函数：初始化服务器核心参数，创建线程池，设置默认回调
TcpServer::TcpServer(EventLoop* base_loop, 
                     const std::string& ip, 
                     uint16_t port, 
                     int io_thread_count,
                     const std::string& name)
    : name_(name),
      base_loop_(base_loop),
      ip_(ip),
      port_(port),
      io_thread_count_(std::max(0, io_thread_count)),  // 保证线程数非负
      user_conn_cb_(),
      user_msg_cb_(),
      user_close_cb_(),
      user_data_cb_(),
      started_(false) {
    
    // 校验核心参数：base_loop不能为空
    if (!base_loop_) {
        PR_ERROR("TcpServer[%s] ctor: base_loop is null\n", name_.c_str());
        throw std::invalid_argument("base_loop is null");
    }

    // 校验端口：不能为0
    if (port == 0) {
        PR_ERROR("TcpServer[%s] ctor: port cannot be 0\n", name_.c_str());
        throw std::invalid_argument("port cannot be 0");
    }

    // 创建IO线程池（仅初始化，未启动）
    thread_pool_ = std::make_unique<EventLoopThreadPool>(name_ + "-ThreadPool", 
                                                         io_thread_count_);

    // 设置服务器默认回调（连接/关闭/消息处理）
    setup_default_callbacks();

    LOG_INFO("TcpServer[%s] created: %s:%u, io_threads=%d\n", 
             name_.c_str(), ip_.c_str(), port_, io_thread_count_);
}

// 析构函数：停止服务器，释放资源
TcpServer::~TcpServer() {
    stop();
}

// 设置默认回调：连接/关闭/消息处理的基础逻辑
void TcpServer::setup_default_callbacks() {
    // 默认关闭回调：移除连接 → 清理空闲管理器 → 调用用户关闭回调
    ts_close_cb = [this](const TcpConnectionPtr& conn) {
        this->remove_tcp_conn(conn);

        if (this->idle_timeout_enabled_ && this->idle_manager_) {
            this->idle_manager_->remove_connection(conn->fd());
        }

        if (this->user_close_cb_) {
            try {
                this->user_close_cb_(conn);
            } catch (...) {
                PR_ERROR("TcpServer[%s] user close callback threw exception\n", 
                         this->name_.c_str());
            }
        }
    };

    // 默认连接回调：调用用户自定义连接回调
    ts_connected_cb = [this](const TcpConnectionPtr& conn) {
        if (this->user_conn_cb_) {
            try {
                this->user_conn_cb_(conn);
            } catch (...) {
                PR_ERROR("TcpServer[%s] user connected callback threw exception\n", 
                         this->name_.c_str());
            }
        }
    };

    // 默认消息回调：更新连接活动时间 → 调用用户数据/消息回调
    ts_message_cb = [this](const TcpConnectionPtr& conn, InputBuffer& buf) {
        this->on_connection_active(conn);

        if (this->user_data_cb_) {
            try {
                size_t readable = static_cast<size_t>(buf.length());
                if (readable > 0) {
                    this->user_data_cb_(conn, buf.get_from_buf(), readable);
                }
            } catch (...) {
                PR_ERROR("TcpServer[%s] user data callback threw exception\n", 
                         this->name_.c_str());
            }
        }

        if (this->user_msg_cb_) {
            try {
                this->user_msg_cb_(conn, buf);
            } catch (...) {
                PR_ERROR("TcpServer[%s] user message callback threw exception\n", 
                         this->name_.c_str());
            }
        }
    };
}

// 启动服务器：初始化空闲管理器 → 启动线程池 → 创建Acceptor→开始监听
void TcpServer::start() {
    // 原子校验：避免重复启动（compare_exchange_strong保证线程安全）
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        LOG_WARN("TcpServer[%s]::start called but server already started\n", 
                 name_.c_str());
        return;
    }

    // 1) 初始化空闲连接管理器（若启用超时）
    if (idle_timeout_enabled_) {
        idle_manager_ = std::make_unique<ConnectionTimeoutManager>(
            idle_timeout_ms_, 60, 1000);
        
        idle_manager_->set_timeout_callback([this](const TcpConnectionPtr& conn) {
            this->on_connection_idle_timeout(conn);
        });
        
        idle_manager_->start();
        
        LOG_INFO("TcpServer[%s] idle timeout enabled: %dms\n", 
                 name_.c_str(), idle_timeout_ms_);
    }

    // 2) 启动IO线程池
    thread_pool_->start(thread_init_cb_);
    LOG_INFO("TcpServer[%s] thread pool started with %zu threads\n", 
             name_.c_str(), thread_pool_->thread_count());

    // 3) 创建Acceptor（监听fd的核心组件，运行在base_loop）
    acceptor_ = std::make_unique<Acceptor>(this, base_loop_, ip_, port_);

    // 4) 开始监听端口（注册监听事件到base_loop）
    acceptor_->listen();

    LOG_INFO("TcpServer[%s] started on %s:%u, idle_timeout=%s\n", 
             name_.c_str(), ip_.c_str(), port_,
             idle_timeout_enabled_ ? "enabled" : "disabled");
}

// 停止服务器：停止空闲管理器→销毁Acceptor→关闭所有连接→停止线程池
void TcpServer::stop() {
    // 原子校验：未启动/已停止则直接返回
    LOG_ERROR("TcpServer::stop() CALLED");

    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    LOG_INFO("TcpServer[%s] stopping...\n", name_.c_str());

    // 1) 停止并释放空闲连接管理器
    if (idle_manager_) {
        idle_manager_->stop();
        idle_manager_.reset();
    }

    // 2) 销毁Acceptor（会关闭监听fd）
    acceptor_.reset();

    // 3) 收集待关闭的连接（锁外操作避免死锁）
    std::vector<TcpConnectionPtr> connections_to_close;
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);  // 保护连接表
        for (auto& [fd, conn] : connections_) {
            if (conn) {
                connections_to_close.push_back(std::move(conn));
            }
        }
        connections_.clear();
    }

    // 4) 关闭所有现有连接
    for (auto& conn : connections_to_close) {
        try {
            conn->shutdown();
        } catch (...) {
            LOG_WARN("TcpServer[%s] exception when closing connection fd=%d\n", 
                     name_.c_str(), conn->fd());
        }
    }

    // 5) 停止IO线程池（等待所有线程退出）
    if (thread_pool_) {
        thread_pool_->stop();
    }

    LOG_INFO("TcpServer[%s] stopped\n", name_.c_str());
}

// 获取下一个IO线程的EventLoop（轮询/哈希策略）
EventLoop* TcpServer::get_next_loop() {
    // 无IO线程则返回base_loop
    if (!thread_pool_ || thread_pool_->thread_count() == 0) {
        return base_loop_;
    }
    
    // 从线程池获取下一个EventLoop
    EventLoop* loop = thread_pool_->get_next_loop();
    if (!loop) {
        LOG_WARN("TcpServer[%s] get_next_loop returned null, using base_loop\n", 
                 name_.c_str());
        return base_loop_;
    }
    
    return loop;
}

// 注册新连接：加入连接表→注册到空闲管理器
void TcpServer::add_new_tcp_conn(const TcpConnectionPtr& conn) {
    if (!conn) return;
    
    int fd = conn->fd();
    if (fd <= 0) {
        LOG_WARN("TcpServer[%s] add_new_tcp_conn: invalid fd=%d\n", 
                 name_.c_str(), fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(conn_mutex_);  // 保护连接表
        
        // 避免重复注册
        if (connections_.find(fd) != connections_.end()) {
            LOG_WARN("TcpServer[%s] add_new_tcp_conn: connection fd=%d already exists\n", 
                     name_.c_str(), fd);
            return;
        }
        
        connections_[fd] = conn;
    }

    // 注册到空闲管理器（若启用超时）
    if (idle_timeout_enabled_ && idle_manager_) {
        idle_manager_->add_connection(conn);
    }

    LOG_INFO("TcpServer[%s] added new connection fd=%d total=%zu\n", 
             name_.c_str(), fd, connection_count());
}

// 移除连接：从空闲管理器清理→从连接表删除
void TcpServer::remove_tcp_conn(const TcpConnectionPtr& conn) {
    if (!conn) return;
    
    int fd = conn->fd();
    
    // 从空闲管理器移除
    if (idle_timeout_enabled_ && idle_manager_) {
        idle_manager_->remove_connection(fd);
    }
    
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);  // 保护连接表
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            connections_.erase(it);
            LOG_INFO("TcpServer[%s] removed connection fd=%d total=%zu\n", 
                     name_.c_str(), fd, connection_count());
        } else {
            LOG_WARN("TcpServer[%s] remove_tcp_conn: fd=%d not found\n", 
                     name_.c_str(), fd);
        }
    }
}

// 设置空闲超时时间（最小1秒）
void TcpServer::set_idle_timeout(int timeout_ms) {
    idle_timeout_ms_ = std::max(1000, timeout_ms);
    
    if (idle_manager_) {
        idle_manager_->set_idle_timeout(idle_timeout_ms_);
    }
    
    LOG_INFO("TcpServer[%s] idle timeout set to %dms\n", name_.c_str(), idle_timeout_ms_);
}

// 启用/禁用空闲超时检测
void TcpServer::enable_idle_timeout(bool enable) {
    if (idle_timeout_enabled_ == enable) {
        return;
    }
    
    idle_timeout_enabled_ = enable;
    
    // 启用：创建并启动空闲管理器
    if (enable) {
        if (!idle_manager_) {
            idle_manager_ = std::make_unique<ConnectionTimeoutManager>(
                idle_timeout_ms_, 60, 1000);
            idle_manager_->set_timeout_callback([this](const TcpConnectionPtr& conn) {
                this->on_connection_idle_timeout(conn);
            });
            
            if (started_.load()) {
                idle_manager_->start();
            }
        }
    } 
    // 禁用：停止并释放空闲管理器
    else {
        if (idle_manager_) {
            idle_manager_->stop();
            idle_manager_.reset();
        }
    }
    
    LOG_INFO("TcpServer[%s] idle timeout %s\n", name_.c_str(), 
             enable ? "enabled" : "disabled");
}

// 对外暴露：更新连接活动时间
void TcpServer::update_connection_activity(const TcpConnectionPtr& conn) {
    on_connection_active(conn);
}

// 获取当前连接数（线程安全）
size_t TcpServer::connection_count() const {
    std::lock_guard<std::mutex> lk(conn_mutex_);
    return connections_.size();
}

// 获取空闲连接数
size_t TcpServer::idle_connection_count() const {
    if (idle_manager_) {
        return idle_manager_->idle_connection_count();
    }
    return 0;
}

// 内部：更新连接活动时间（用于空闲超时检测）
void TcpServer::on_connection_active(const TcpConnectionPtr& conn) {
    if (!conn || !idle_timeout_enabled_ || !idle_manager_) {
        return;
    }
    
    idle_manager_->update_activity(conn->fd());
}

// 内部：处理连接空闲超时→关闭连接
void TcpServer::on_connection_idle_timeout(const TcpConnectionPtr& conn) {
    if (!conn) {
        return;
    }
    
    LOG_INFO("TcpServer[%s] closing idle connection fd=%d (idle for %dms)\n", 
             name_.c_str(), conn->fd(), idle_timeout_ms_);
    
    try {
        conn->shutdown();
    } catch (const std::exception& e) {
        LOG_ERROR("TcpServer[%s] error closing idle connection fd=%d: %s\n", 
                  name_.c_str(), conn->fd(), e.what());
    }
}