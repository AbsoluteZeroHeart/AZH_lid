#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP

#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <memory>

#include "Epoll.hpp"

class Channel;

class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void stop();

    bool is_in_loop_thread() const;

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void update_channel(const std::shared_ptr<Channel>& ch);
    void remove_channel(const std::shared_ptr<Channel>& ch);

private:
    void wakeup();
    void handle_wakeup();
    void do_pending_functors();

private:
    std::atomic<bool> running_{false};
    std::thread::id thread_id_;

    Epoll epoller_;
    std::vector<epoll_event> active_events_{1024};

    int wakeup_fd_;
    std::shared_ptr<Channel> wakeup_channel_;

    std::mutex mutex_;
    std::vector<Functor> pending_functors_;

    std::unordered_map<int, std::weak_ptr<Channel>> channels_;
};

#endif // EVENT_LOOP_HPP
