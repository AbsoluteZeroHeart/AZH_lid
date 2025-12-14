#ifndef EPOLL_HPP
#define EPOLL_HPP

#include <vector>
#include <sys/epoll.h>

class Channel;

class Epoll {
public:
    Epoll();
    ~Epoll();

    bool  add(Channel* ch);
    bool  mod(Channel* ch);
    bool  del(Channel* ch);

    int poll(int timeout_ms, std::vector<epoll_event>& active);

private:
    int epfd_;
};

#endif
