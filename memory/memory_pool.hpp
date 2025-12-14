#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <unordered_map>  
#include <mutex>           
#include <array>         
#include <vector>         
#include <memory>         
#include <stdexcept>       
#include <cstddef>        
#include <cstdint>         
#include <algorithm>       
#include "chunk.hpp"       

// 内存分配错误异常类 - 继承自标准运行时异常
// 用于表示内存分配过程中出现的错误（如参数非法等）
class MemoryAllocationError : public std::runtime_error {
public:
    // 显式构造函数，接收错误信息并传递给父类
    explicit MemoryAllocationError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// 内存池耗尽错误异常类 - 继承自标准运行时异常
// 用于表示内存池达到最大容量，无法分配新内存
class MemoryPoolExhaustedError : public std::runtime_error {
public:
    // 显式构造函数，接收错误信息并传递给父类
    explicit MemoryPoolExhaustedError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// 预定义的内存块规格数组（单位：字节）
constexpr std::array<size_t, 6> MEM_SIZES = {
    4096,                // 4K - 基础规格
    4096 * 4,            // 16K
    4096 * 16,           // 64K
    4096 * 64,           // 256K
    4096 * 256,          // 1M
    4096 * 1024          // 4M - 最大预设规格
};

// 内存池统计信息结构体
// 用于记录内存池的使用状态和性能指标
struct PoolStats {
    size_t total_allocations = 0;      // 累计分配次数
    size_t total_deallocations = 0;    // 累计释放次数
    size_t peak_usage_bytes = 0;       // 峰值使用字节数
    size_t current_usage_bytes = 0;    // 当前使用字节数
    size_t allocation_failures = 0;    // 分配失败次数
};

// 内存池核心类 - 采用单例模式实现
// 管理不同规格的预分配内存块，提供高效的内存分配/释放功能
class MemoryPool {
public:
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    static MemoryPool& get_instance() {
        static MemoryPool instance;  // 静态局部变量，保证只初始化一次
        return instance;
    }

    // 核心方法：分配指定字节数的内存块
    Chunk* alloc_chunk(size_t n);
    Chunk* alloc_chunk() { return alloc_chunk(MEM_SIZES[0]); }

    // 核心方法：将内存块归还到内存池
    void retrieve(Chunk* chunk);

    ~MemoryPool();

    // 获取内存池统计信息
    PoolStats get_stats() const;

    // 设置内存池最大容量（字节数）
    void set_max_capacity(size_t max_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_capacity_bytes_ = max_bytes;
    }

    // 获取当前内存使用量（字节数）
    size_t get_current_usage() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_usage_bytes_;
    }

    // 获取内存池最大容量（字节数）
    size_t get_max_capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_capacity_bytes_;
    }

    // 清空内存池 - 释放所有预分配的内存块
    void clear();

private:
    // 私有构造函数 
    MemoryPool();

    void initialize_pool();                                    // 初始化内存池 - 内部初始化逻辑
    void preallocate_chunks(size_t chunk_size, size_t count);  // 预分配指定规格和数量的内存块
    size_t find_suitable_size(size_t requested_size) const;    // 根据请求的大小，找到最匹配的预定义规格（向上取整）
    bool is_supported_size(size_t s) const;                    // 检查指定大小是否是内存池支持的规格

private:
    // 内存池映射类型定义：键=内存块大小，值=该规格的内存块链表头指针
    using PoolMap = std::unordered_map<size_t, Chunk*>;
    PoolMap pool_;                  // 存储不同规格内存块的核心容器
    mutable std::mutex mutex_;      // 互斥锁，保证线程安全（mutable允许const方法修改）
    size_t max_capacity_bytes_;     // 内存池最大容量（字节）
    size_t current_usage_bytes_;    // 当前已使用字节数
    size_t preallocated_bytes_;     // 预分配的总字节数
    PoolStats stats_;               // 内存池运行时统计信息
};

#endif // MEMORY_POOL_HPP