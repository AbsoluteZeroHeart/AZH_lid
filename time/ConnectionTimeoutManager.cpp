#include "ConnectionTimeoutManager.hpp"
#include "logger.hpp"
#include <algorithm>
#include <random>

ConnectionTimeoutManager::ConnectionTimeoutManager(int idle_timeout_ms, 
                                                 int wheel_size,
                                                 int tick_interval_ms)
    : idle_timeout_ms_(idle_timeout_ms),
      wheel_size_(wheel_size),
      tick_interval_ms_(tick_interval_ms),
      time_wheel_(wheel_size) {
    
    if (idle_timeout_ms_ <= 0) {
        idle_timeout_ms_ = 300000; // 默认5分钟
    }
    
    if (wheel_size_ <= 0) {
        wheel_size_ = 60; // 默认60个槽
    }
    
    if (tick_interval_ms_ <= 0) {
        tick_interval_ms_ = 1000; // 默认1秒
    }
    
    LOG_INFO("ConnectionTimeoutManager created: timeout=%dms, wheel_size=%d, tick_interval=%dms\n",
            idle_timeout_ms_, wheel_size_, tick_interval_ms_);
}

ConnectionTimeoutManager::~ConnectionTimeoutManager() {
    stop();
}

void ConnectionTimeoutManager::start() {
    if (running_.exchange(true)) {
        return; // 已经在运行
    }
    
    should_stop_.store(false);
    
    // 启动时间轮线程
    time_wheel_thread_ = std::thread([this]() {
        time_wheel_loop();
    });
    
    // 启动清理线程
    cleanup_running_.store(true);
    cleanup_thread_ = std::thread([this]() {
        while (cleanup_running_.load()) {
            std::unique_lock<std::mutex> lock(cleanup_mutex_);
            cleanup_cv_.wait_for(lock, std::chrono::seconds(30), 
                                [this]() { return !cleanup_running_.load(); });
            
            if (!cleanup_running_.load()) {
                break;
            }
            
            cleanup_closed_connections();
        }
    });
    
    LOG_INFO("ConnectionTimeoutManager started\n");
}

void ConnectionTimeoutManager::stop() {
    if (!running_.exchange(false)) {
        return; // 没有运行
    }
    
    should_stop_.store(true);
    
    // 停止时间轮线程
    if (time_wheel_thread_.joinable()) {
        time_wheel_thread_.join();
    }
    
    // 停止清理线程
    cleanup_running_.store(false);
    cleanup_cv_.notify_all();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // 清空所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.clear();
        total_connections_.store(0);
        idle_connections_.store(0);
    }
    
    LOG_INFO("ConnectionTimeoutManager stopped\n");
}

void ConnectionTimeoutManager::add_connection(const TcpConnectionPtr& conn) {
    if (!conn) {
        LOG_WARN("Attempt to add null connection\n");
        return;
    }
    
    int conn_id = conn->fd();
    if (conn_id <= 0) {
        LOG_WARN("Invalid connection ID: %d\n", conn_id);
        return;
    }
    
    // 计算连接应该放置的槽位
    auto now = std::chrono::steady_clock::now();
    int slot_pos = calculate_slot(now);
    
    // 计算需要的tick次数
    int ticks_needed = idle_timeout_ms_ / tick_interval_ms_;
    
    auto entry = std::make_shared<ConnectionEntry>(conn, slot_pos, ticks_needed);
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        // 检查连接是否已经存在
        if (connections_.find(conn_id) != connections_.end()) {
            LOG_WARN("Connection %d already exists in timeout manager\n", conn_id);
            return;
        }
        
        // 添加到连接表
        connections_[conn_id] = entry;
        total_connections_.fetch_add(1);
        
        // 添加到时间轮
        std::lock_guard<std::mutex> slot_lock(time_wheel_[slot_pos].slot_mutex);
        time_wheel_[slot_pos].entries.push_back(entry);
    }
    
    LOG_DEBUG("Connection %d added to timeout manager (slot=%d, ticks=%d)\n",
             conn_id, slot_pos, ticks_needed);
}

void ConnectionTimeoutManager::update_activity(int conn_id) {
    std::shared_ptr<ConnectionEntry> entry;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        if (it == connections_.end()) {
            return; // 连接不存在
        }
        entry = it->second;
    }
    
    if (!entry) {
        return;
    }
    
    // 更新最后活动时间
    entry->last_activity_time = std::chrono::steady_clock::now();
    
    // 移动连接到新的槽位
    move_to_new_slot(entry);
    
    LOG_DEBUG("Connection %d activity updated\n", conn_id);
}

void ConnectionTimeoutManager::remove_connection(int conn_id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return;
    }
    
    // 注意：不从时间轮槽中立即移除，由清理线程处理
    connections_.erase(it);
    total_connections_.fetch_sub(1);
    
    LOG_DEBUG("Connection %d removed from timeout manager\n", conn_id);
}

void ConnectionTimeoutManager::set_timeout_callback(TimeoutCallback callback) {
    timeout_callback_ = std::move(callback);
}

size_t ConnectionTimeoutManager::connection_count() const {
    return total_connections_.load();
}

size_t ConnectionTimeoutManager::idle_connection_count() const {
    return idle_connections_.load();
}

void ConnectionTimeoutManager::set_idle_timeout(int idle_timeout_ms) {
    if (idle_timeout_ms <= 0) {
        LOG_WARN("Invalid idle timeout: %dms\n", idle_timeout_ms);
        return;
    }
    
    idle_timeout_ms_ = idle_timeout_ms;
    LOG_INFO("Idle timeout changed to %dms\n", idle_timeout_ms_);
}

void ConnectionTimeoutManager::reset_all() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [conn_id, entry] : connections_) {
        if (entry) {
            entry->last_activity_time = now;
            move_to_new_slot(entry);
        }
    }
    
    LOG_INFO("All connections reset in timeout manager\n");
}

void ConnectionTimeoutManager::time_wheel_loop() {
    LOG_INFO("Time wheel loop started\n");
    
    while (!should_stop_.load()) {
        auto start_time = std::chrono::steady_clock::now();
        
        // 处理当前槽位的连接
        process_timeout_connections();
        
        // 移动到下一个槽位
        current_slot_ = (current_slot_ + 1) % wheel_size_;
        
        // 计算需要等待的时间
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        if (elapsed.count() < tick_interval_ms_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(tick_interval_ms_ - elapsed.count()));
        }
    }
    
    LOG_INFO("Time wheel loop stopped\n");
}

void ConnectionTimeoutManager::process_timeout_connections() {
    TimeWheelSlot& slot = time_wheel_[current_slot_];
    std::vector<std::shared_ptr<ConnectionEntry>> expired_entries;
    
    {
        std::lock_guard<std::mutex> slot_lock(slot.slot_mutex);
        
        // 收集过期的连接
        auto now = std::chrono::steady_clock::now();
        auto it = slot.entries.begin();
        
        while (it != slot.entries.end()) {
            auto entry = *it;
            
            if (!entry) {
                it = slot.entries.erase(it);
                continue;
            }
            
            // 检查连接是否还有剩余tick
            if (entry->remaining_ticks > 0) {
                entry->remaining_ticks--;
                ++it;
                continue;
            }
            
            // 计算空闲时间
            auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - entry->last_activity_time);
            
            if (idle_time.count() >= idle_timeout_ms_) {
                // 连接超时
                expired_entries.push_back(entry);
                it = slot.entries.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 处理超时连接
    for (auto& entry : expired_entries) {
        if (!entry || !entry->conn) {
            continue;
        }
        
        // 从连接表中移除
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.erase(entry->conn->fd());
        }
        
        total_connections_.fetch_sub(1);
        
        // 调用超时回调
        if (timeout_callback_) {
            try {
                timeout_callback_(entry->conn);
                LOG_INFO("Connection %d timed out (idle for %dms)\n", 
                        entry->conn->fd(), idle_timeout_ms_);
            } catch (const std::exception& e) {
                LOG_ERROR("Timeout callback exception for connection %d: %s\n",
                         entry->conn->fd(), e.what());
            }
        }
    }
}

void ConnectionTimeoutManager::move_to_new_slot(std::shared_ptr<ConnectionEntry> entry) {
    if (!entry) {
        return;
    }
    
    // 计算新的槽位
    int new_slot_pos = calculate_slot(entry->last_activity_time);
    
    // 如果槽位没变，只需要重置剩余tick
    if (new_slot_pos == entry->slot_position) {
        entry->remaining_ticks = idle_timeout_ms_ / tick_interval_ms_;
        return;
    }
    
    // 从旧槽位移除
    {
        std::lock_guard<std::mutex> old_lock(time_wheel_[entry->slot_position].slot_mutex);
        auto& old_entries = time_wheel_[entry->slot_position].entries;
        old_entries.erase(
            std::remove_if(old_entries.begin(), old_entries.end(),
                [entry](const std::shared_ptr<ConnectionEntry>& e) {
                    return e == entry;
                }),
            old_entries.end());
    }
    
    // 添加到新槽位
    entry->slot_position = new_slot_pos;
    entry->remaining_ticks = idle_timeout_ms_ / tick_interval_ms_;
    
    {
        std::lock_guard<std::mutex> new_lock(time_wheel_[new_slot_pos].slot_mutex);
        time_wheel_[new_slot_pos].entries.push_back(entry);
    }
}

int ConnectionTimeoutManager::calculate_slot(
    const std::chrono::steady_clock::time_point& last_activity_time) {
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_activity_time).count();
    
    // 计算距离超时还有多少tick
    int ticks_remaining = (idle_timeout_ms_ - elapsed_ms) / tick_interval_ms_;
    ticks_remaining = std::max(0, ticks_remaining);
    
    // 计算槽位：(当前槽位 + 剩余tick) % 轮子大小
    int slot = (current_slot_ + ticks_remaining) % wheel_size_;
    
    return slot;
}

void ConnectionTimeoutManager::cleanup_closed_connections() {
    std::vector<int> closed_conns;
    
    // 收集已关闭的连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& [conn_id, entry] : connections_) {
            if (!entry || !entry->conn || !entry->conn->is_connected()) {
                closed_conns.push_back(conn_id);
            }
        }
    }
    
    // 移除已关闭的连接
    for (int conn_id : closed_conns) {
        remove_connection(conn_id);
        
        // 从时间轮中清理
        for (auto& slot : time_wheel_) {
            std::lock_guard<std::mutex> slot_lock(slot.slot_mutex);
            slot.entries.erase(
                std::remove_if(slot.entries.begin(), slot.entries.end(),
                    [conn_id](const std::shared_ptr<ConnectionEntry>& e) {
                        return e && e->conn && e->conn->fd() == conn_id;
                    }),
                slot.entries.end());
        }
    }
    
    if (!closed_conns.empty()) {
        LOG_DEBUG("Cleaned up %zu closed connections\n", closed_conns.size());
    }
}