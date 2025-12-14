#ifndef LOGGER_LOG_QUEUE_H
#define LOGGER_LOG_QUEUE_H

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>  

namespace logger {

template<typename T>
class LogQueue {
public:
    explicit LogQueue(size_t capacity, bool debug = false)
        : capacity_(capacity)
        , debug_(debug)
        , buffer_(std::make_unique<T[]>(capacity))
        , size_(0)
        , front_(0)
        , rear_(0)
        , read_count_(0)
        , write_count_(0) {
        if (capacity == 0) {
            throw std::invalid_argument("Capacity must be greater than 0");
        }
    }

    ~LogQueue() = default;

    // 禁止拷贝
    LogQueue(const LogQueue&) = delete;
    LogQueue& operator=(const LogQueue&) = delete;

    // 增加移动语义的push，减少拷贝
    bool push(const T& item, int timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeout_ms > 0) {
            if (!not_full_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                       [this] { return size_ < capacity_; })) {
                return false;
            }
        } else {
            not_full_cv_.wait(lock, [this] { return size_ < capacity_; });
        }

        buffer_[rear_] = item; // 拷贝赋值
        rear_ = (rear_ + 1) % capacity_;
        ++size_;
        
        if (debug_) {
            ++read_count_; // 原子变量，无需锁
        }
        
        not_empty_cv_.notify_one();
        return true;
    }

    // 移动版本push
    bool push(T&& item, int timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeout_ms > 0) {
            if (!not_full_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                       [this] { return size_ < capacity_; })) {
                return false;
            }
        } else {
            not_full_cv_.wait(lock, [this] { return size_ < capacity_; });
        }

        buffer_[rear_] = std::move(item); // 移动赋值，减少拷贝
        rear_ = (rear_ + 1) % capacity_;
        ++size_;
        
        if (debug_) {
            ++write_count_;
        }
        
        not_empty_cv_.notify_one();
        return true;
    }

    bool pop(T& item, int timeout_ms = 0) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (timeout_ms > 0) {
        // 使用wait_for，正确处理超时
        if (!not_empty_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [this] { return size_ > 0; })) {
            return false;  // 超时返回false
        }
    } else if (timeout_ms == 0) {
        // 非阻塞模式
        if (size_ == 0) {
            return false;
        }
    } else {
        // 阻塞模式，无限等待
        not_empty_cv_.wait(lock, [this] { return size_ > 0; });
    }
    
    // 取出元素
    item = std::move(buffer_[front_]);
    front_ = (front_ + 1) % capacity_;
    --size_;
    
    if (debug_) {
        ++read_count_;
    }
    
    not_full_cv_.notify_one();
    return true;
}

    // 批量pop（适配日志批量写入，减少锁竞争）
    size_t pop_batch(std::vector<T>& items, size_t max_count, int timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeout_ms > 0) {
            if (!not_empty_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [this] { return size_ > 0; })) {
                return 0;
            }
        } else {
            not_empty_cv_.wait(lock, [this] { return size_ > 0; });
        }

        // 最多取max_count条，或队列剩余所有
        size_t count = 0;
        while (count < max_count && size_ > 0) {
            items.push_back(std::move(buffer_[front_]));
            front_ = (front_ + 1) % capacity_;
            --size_;
            ++count;
            
            if (debug_) {
                ++read_count_;
            }
        }

        not_full_cv_.notify_one();
        return count;
    }

    // 原clear逻辑保留
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_ = 0;
        front_ = 0;
        rear_ = 0;
        if (debug_) {
            read_count_ = 0;
            write_count_ = 0;
        }
    }

    // 状态查询接口保留（无问题）
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == capacity_;
    }

    size_t capacity() const { return capacity_; }
    
    // debug计数改为原子变量，无需锁
    uint64_t read_count() const {
        return debug_ ? read_count_.load() : 0;
    }
    
    uint64_t write_count() const {
        return debug_ ? write_count_.load() : 0;
    }

    // notify_all加锁，保证内存可见性
    void notify_all() {
        std::lock_guard<std::mutex> lock(mutex_); // 加锁保证状态可见性
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

private:
    const size_t capacity_;
    const bool debug_;
    
    std::unique_ptr<T[]> buffer_;
    size_t size_;          // 受mutex保护，无需原子
    size_t front_;         // 队首指针
    size_t rear_;          // 队尾指针
    
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    
    // 改为原子变量，debug模式下无锁开销
    std::atomic<uint64_t> read_count_{0};
    std::atomic<uint64_t> write_count_{0};
};

} // namespace logger

#endif // LOGGER_LOG_QUEUE_H