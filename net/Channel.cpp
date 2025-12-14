#include "Channel.hpp"
#include "EventLoop.hpp"
#include <sys/epoll.h>

/**
 * @brief 构造函数实现
 * @param loop 关联的EventLoop
 * @param fd 管理的文件描述符
 */
Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd) {}

/**
 * @brief 析构函数实现
 * @note 空实现：fd的epoll注销由所有者（如TcpConnection）在销毁前显式调用disable_all()，
 *       确保在EventLoop线程操作，避免跨线程析构导致的未定义行为
 */
Channel::~Channel() {}

/**
 * @brief 启用读事件实现
 */
void Channel::enable_read() {
    // 位运算添加读事件：EPOLLIN（可读） + EPOLLRDHUP（对端关闭）
    events_ |= EPOLLIN | EPOLLRDHUP;
    // 同步事件到epoll
    update();
}

/**
 * @brief 启用写事件实现
 */
void Channel::enable_write() {
    // 位运算添加写事件：EPOLLOUT（可写）
    events_ |= EPOLLOUT;
    update();
}

/**
 * @brief 禁用写事件实现
 */
void Channel::disable_write() {
    // 位运算清除写事件（&= ~ 是清除特定位的标准写法）
    events_ &= ~EPOLLOUT;
    update();
}

/**
 * @brief 禁用所有事件实现
 */
void Channel::disable_all() {
    // 清空所有事件掩码
    events_ = 0;
    update();
}

/**
 * @brief 处理触发的事件实现
 * @param revents epoll返回的实际触发事件
 * @note 核心逻辑：先验证绑定的外部对象是否存活，再执行回调
 */
void Channel::handle_event(uint32_t revents) {
    if (tied_) {
        // 尝试将weak_ptr升级为shared_ptr：验证外部对象是否存活
        std::shared_ptr<void> guard = tie_.lock();
        if (guard) {
            // 外部对象存活，执行回调
            if (cb_) {
                cb_(revents);
            }
        } else {
            // 外部对象已销毁，忽略事件（避免野指针访问）
            return;
        }
    } else {
        // 未绑定外部对象，直接执行回调
        if (cb_) {
            cb_(revents);
        }
    }
}

/**
 * @brief 同步事件到epoll的核心实现
 * @note 1. 必须在EventLoop线程执行，保证线程安全
 *       2. shared_from_this()获取自身shared_ptr，避免回调时Channel被销毁
 */
void Channel::update() {
    // 获取自身的shared_ptr（依赖enable_shared_from_this，需保证Channel由shared_ptr管理）
    std::shared_ptr<Channel> self = shared_from_this();

    // 检查当前线程是否是EventLoop的线程
    if (loop_->is_in_loop_thread()) {
        // 本线程：直接调用EventLoop更新Channel
        loop_->update_channel(self);
    } else {
        // 非EventLoop线程：将更新操作投递到EventLoop的任务队列，由loop线程执行
        loop_->runInLoop([self]() {
            self->loop_->update_channel(self);
        });
    }
}