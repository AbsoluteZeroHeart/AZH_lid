#include "Epoll.hpp"
#include "Channel.hpp"
#include <unistd.h>
#include <cassert>
#include <logger.hpp>
#include <cstring> 

Epoll::Epoll() {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    assert(epfd_ >= 0);
}

Epoll::~Epoll() {
    ::close(epfd_);
}

bool Epoll::add(Channel* ch) {
    epoll_event ev{};
    ev.events = ch->events();
    ev.data.ptr = ch;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, ch->fd(), &ev) == 0) return true;
    int e = errno;
    LOG_ERROR("epoll_ctl ADD fd=%d failed: %s", ch->fd(), strerror(e));
    return false;
}

bool Epoll::mod(Channel* ch) {
    epoll_event ev{};
    ev.events = ch->events();
    ev.data.ptr = ch;
    if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, ch->fd(), &ev) == 0) return true;
    int e = errno;
    LOG_ERROR("epoll_ctl MOD fd=%d failed: %s", ch->fd(), strerror(e));
    return false;
}

bool Epoll::del(Channel* ch) {
    if(::epoll_ctl(epfd_, EPOLL_CTL_DEL, ch->fd(), nullptr) == 0) return true;
    int e = errno;
    LOG_ERROR("epoll_ctl DEL fd=%d failed: %s", ch->fd(), strerror(e));
    return false;  
}


int Epoll::poll(int timeout_ms, std::vector<epoll_event>& active) {
    while (true) {
        int n = ::epoll_wait(epfd_, active.data(), static_cast<int>(active.size()), timeout_ms);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        int e = errno;
        LOG_ERROR("epoll_wait failed: %s", strerror(e));
        return -1;
    }
}
