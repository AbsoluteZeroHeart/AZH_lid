#include "EventLoop.hpp"
#include "Channel.hpp"
#include "logger.hpp"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <errno.h>

EventLoop::EventLoop() {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(wakeup_fd_ >= 0);

    wakeup_channel_ = std::make_shared<Channel>(this, wakeup_fd_);
    wakeup_channel_->set_callback(
        [this](uint32_t) { handle_wakeup(); }
    );
    wakeup_channel_->enable_read();
}

EventLoop::~EventLoop() {
    wakeup_channel_->disable_all();
    wakeup_channel_.reset();
    ::close(wakeup_fd_);
}

bool EventLoop::is_in_loop_thread() const {
    return std::this_thread::get_id() == thread_id_;
}

// 启动事件循环 epoll_wait（假如没有事件发生，阻塞在这里） -> handle_event -> do_pending_functors
void EventLoop::loop() {
    running_.store(true);
    thread_id_ = std::this_thread::get_id();

    while (running_) {
        do_pending_functors();

        int n = epoller_.poll(10000, active_events_);
        if (n == static_cast<int>(active_events_.size())) {
            active_events_.resize(active_events_.size() * 2);
        }

        for (int i = 0; i < n; ++i) {
            auto* raw_ch = static_cast<Channel*>(active_events_[i].data.ptr);
            if (!raw_ch) continue;
            int fd = raw_ch->fd(); 

            auto it = channels_.find(fd);
            if (it != channels_.end()) {
                auto sp = it->second.lock();
                if (sp) {
                    sp->handle_event(active_events_[i].events);
                } else {
                    // weak_ptr expired：channel 已被销毁或已从 map 中移除，跳过
                    LOG_DEBUG("EventLoop: channel expired for fd=%d, skipping event", fd);
                }
            } else {
                // 未找到 weak_ptr（可能是快速 remove），尽量安全地跳过
                LOG_DEBUG("EventLoop: channel not in map for fd=%d, skipping", fd);
            }
        }
        do_pending_functors();
    }
}

// 停止事件循环
void EventLoop::stop() {
    running_.store(false);
    wakeup();
}

// 运行在 loop 线程中
void EventLoop::runInLoop(Functor cb) {
    if (is_in_loop_thread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

// 向待执行的函数队列中添加函数
void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pending_functors_.push_back(std::move(cb));
    }
    wakeup();
}

// 主动触发epoll事件，使跳出epoll_wait阻塞
void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        if (n == -1 && errno != EAGAIN) {
            LOG_ERROR("EventLoop::wakeup write failed: %s", strerror(errno));
        }
    }
}

// 处理 wakeup 事件
void EventLoop::handle_wakeup() {
    uint64_t one;
    while (true) {
        ssize_t n = ::read(wakeup_fd_, &one, sizeof(one));
        if (n < 0) {
            if (errno == EINTR) 
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
                break;
            LOG_ERROR("EventLoop::handle_wakeup read failed: %s", strerror(errno));
            break;
        }
        if (n == 0) break; // 不太可能
        // 成功读到数据，继续循环直到 EAGAIN
    }
}

// 执行待处理的函数
void EventLoop::do_pending_functors() {
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        functors.swap(pending_functors_);
    }
    for (auto& fn : functors) {
        fn();
    }
}

// 更新或添加 Channel 到 epoll 事件循环中
void EventLoop::update_channel(const std::shared_ptr<Channel>& ch) {
    int fd = ch->fd();
    int evs = ch->events();

    if (evs == 0) {
        epoller_.del(ch.get());
        channels_.erase(fd);
        return;
    }

    auto it = channels_.find(fd);
    if (it == channels_.end()) {
        if (epoller_.add(ch.get())) {
            channels_[fd] = ch;
        } else {
            LOG_ERROR("EventLoop::update_channel add failed fd=%d", fd);
        }
    } else {
        if (!epoller_.mod(ch.get())) {
            LOG_ERROR("EventLoop::update_channel mod failed fd=%d", fd);
        }
    }
}

// 从 epoll 事件循环中移除 Channel
void EventLoop::remove_channel(const std::shared_ptr<Channel>& ch) {
    int fd = ch->fd();
    channels_.erase(fd);
    epoller_.del(ch.get());
}
