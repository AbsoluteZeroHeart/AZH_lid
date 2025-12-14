#ifndef THREAD_SAFE_HASH_MAP_H
#define THREAD_SAFE_HASH_MAP_H

#include <unordered_map>
#include <mutex>
#include <optional>
#include <shared_mutex>

/**
 * @brief 线程安全的哈希映射容器
 * @tparam Key 键类型
 * @tparam Value 值类型
 */
template <typename Key, typename Value>
class ThreadSafeHashMap {
public:
    using value_type = typename std::unordered_map<Key, Value>::value_type;
    
    /**
     * @brief 插入键值对（复制）
     */
    void insert(const Key& key, const Value& value) {
        std::unique_lock lock(mutex_);
        map_[key] = value;
    }
    
    /**
     * @brief 插入键值对（移动）
     */
    void insert(const Key& key, Value&& value) {
        std::unique_lock lock(mutex_);
        map_[key] = std::move(value);
    }
    
    /**
     * @brief 插入或赋值
     */
    template <typename... Args>
    void emplace(Args&&... args) {
        std::unique_lock lock(mutex_);
        map_.emplace(std::forward<Args>(args)...);
    }
    
    /**
     * @brief 删除指定键
     */
    bool erase(const Key& key) {
        std::unique_lock lock(mutex_);
        return map_.erase(key) > 0;
    }
    
    /**
     * @brief 获取值（如果存在）
     */
    std::optional<Value> get(const Key& key) const {
        std::shared_lock lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief 检查键是否存在
     */
    bool contains(const Key& key) const {
        std::shared_lock lock(mutex_);
        return map_.find(key) != map_.end();
    }
    
    /**
     * @brief 获取映射大小
     */
    size_t size() const {
        std::shared_lock lock(mutex_);
        return map_.size();
    }
    
    /**
     * @brief 清空映射
     */
    void clear() {
        std::unique_lock lock(mutex_);
        map_.clear();
    }
    
    /**
     * @brief 访问所有元素（线程安全副本）
     */
    std::unordered_map<Key, Value> snapshot() const {
        std::shared_lock lock(mutex_);
        return map_;
    }
    
    /**
     * @brief 原子更新操作
     */
    template <typename Updater>
    void update(const Key& key, Updater&& updater) {
        std::unique_lock lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            updater(it->second);
        }
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, Value> map_;
};

#endif // THREAD_SAFE_HASH_MAP_H