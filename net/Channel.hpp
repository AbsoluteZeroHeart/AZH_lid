#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <functional>
#include <cstdint>
#include <memory>

class EventLoop;

/**
 * @brief Reactor模式核心组件：IO事件通道
 * @details 单个Channel对应一个文件描述符(fd)，管理其IO事件的注册/更新/处理，
 *          不直接处理IO，仅将事件转发给回调函数，与EventLoop强关联
 */
class Channel : public std::enable_shared_from_this<Channel> {
public:
    /**
     * @brief 事件回调函数类型
     * @param revents 触发的事件（如EPOLLIN/EPOLLOUT/EPOLLRDHUP等，对应epoll的events）
     */
    using EventCallback = std::function<void(uint32_t)>;

    /**
     * @brief 构造函数
     * @param loop 关联的事件循环（Channel属于某个EventLoop，生命周期由外部管理）
     * @param fd 要管理的文件描述符（如socket fd）
     */
    Channel(EventLoop* loop, int fd);

    /**
     * @brief 析构函数
     * @note 空实现：epoll中fd的移除需由所有者在loop线程显式调用disable_all()完成，
     *       避免析构时跨线程操作epoll导致的竞态
     */
    ~Channel();

    // 禁用拷贝构造和赋值：保证一个fd仅被一个Channel管理，避免资源冲突
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /**
     * @brief 获取管理的文件描述符
     * @return 对应的fd
     */
    int fd() const { return fd_; }

    /**
     * @brief 获取当前注册到epoll的事件集合
     * @return 事件掩码（如EPOLLIN | EPOLLOUT）
     */
    uint32_t events() const { return events_; }

    /**
     * @brief 设置事件触发时的回调函数
     * @param cb 回调函数（移动语义减少拷贝）
     */
    void set_callback(EventCallback cb) { cb_ = std::move(cb); }

    /**
     * @brief 启用读事件（EPOLLIN + EPOLLRDHUP）
     * @note EPOLLRDHUP：检测对端关闭连接/半关闭（仅epoll支持）
     */
    void enable_read();

    /**
     * @brief 启用写事件（EPOLLOUT）
     */
    void enable_write();

    /**
     * @brief 禁用写事件
     */
    void disable_write();

    /**
     * @brief 禁用所有事件（将events置0）
     */
    void disable_all();

    /**
     * @brief 处理epoll触发的事件（由EventLoop调用）
     * @param revents 实际触发的事件掩码
     */
    void handle_event(uint32_t revents);

    /**
     * @brief 绑定外部对象，保证事件处理时外部对象未销毁
     * @param obj 要绑定的外部对象（如TcpConnection）
     * @note 通过weak_ptr监听外部对象生命周期，避免回调时访问已销毁对象
     */
    void tie(const std::shared_ptr<void>& obj) { tie_ = obj; tied_ = true; }

private:
    /**
     * @brief 同步当前events到epoll（核心逻辑，仅内部调用）
     * @note 保证操作在EventLoop线程执行，非线程则投递任务到loop队列
     */
    void update();

private:
    EventLoop* loop_;          // 关联的事件循环（不持有所有权，仅引用）
    const int fd_;             // 管理的文件描述符（const：一个Channel仅对应一个fd）
    uint32_t events_{0};       // 要注册到epoll的事件掩码（初始为0）
    EventCallback cb_;         // 事件回调函数（由外部设置）

    std::weak_ptr<void> tie_;  // 绑定的外部对象（弱引用，不影响其生命周期）
    bool tied_{false};         // 是否已绑定外部对象
};

#endif // CHANNEL_HPP