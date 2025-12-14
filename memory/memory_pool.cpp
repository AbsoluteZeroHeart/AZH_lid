#include "memory_pool.hpp"
#include <iostream>
#include <algorithm>
#include <cassert>

// 内存池构造函数（私有，单例模式）
MemoryPool::MemoryPool()
    : max_capacity_bytes_(128 * 1024 * 1024)  // 默认最大容量128MB
    , current_usage_bytes_(0)                 // 初始使用量为0
    , preallocated_bytes_(0)                  // 初始预分配字节数为0
    , stats_()                                // 初始化统计信息（默认值）
{
    // 预置所有规格的键，值为 nullptr（便于后续逻辑统一处理，避免key不存在）
    for (size_t s : MEM_SIZES) pool_[s] = nullptr;
    initialize_pool();
}

// 内存池析构函数
MemoryPool::~MemoryPool() {
    clear();
}

// 清空内存池所有资源
void MemoryPool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 遍历所有规格的内存块链表，逐个释放
    for (auto& pair : pool_) {
        Chunk* current = pair.second;
        while (current != nullptr) {
            Chunk* next = current->next;  // 先保存下一个节点，避免释放后指针失效
            delete current;               // 释放当前内存块
            current = next;
        }
        pair.second = nullptr;  // 清空链表头指针
    }

    // 重置所有状态
    pool_.clear();                  // 清空内存池映射表
    current_usage_bytes_ = 0;       // 重置当前使用量
    preallocated_bytes_ = 0;        // 重置预分配字节数
    stats_ = PoolStats();           // 重置统计信息
}

// 初始化内存池
void MemoryPool::initialize_pool() {
    preallocate_chunks(MEM_SIZES[0], 200);  
    preallocate_chunks(MEM_SIZES[1], 50);  
    preallocate_chunks(MEM_SIZES[2], 20); 
    preallocate_chunks(MEM_SIZES[3], 10);   
    preallocate_chunks(MEM_SIZES[4], 5);   
    preallocate_chunks(MEM_SIZES[5], 2);   
}

// 预分配指定规格和数量的内存块
// 核心优化：先在锁外分配内存块，再加锁合并到内存池，减少锁持有时间
void MemoryPool::preallocate_chunks(size_t chunk_size, size_t count) {
    if (chunk_size == 0 || count == 0) return;

    // 计算本次预分配的总字节数
    size_t total_size = chunk_size * count;
    // 本地缓存待添加的内存块，减少锁内操作
    std::vector<Chunk*> new_nodes;
    new_nodes.reserve(count);  // 预分配vector容量，避免多次扩容

    try {
        // 批量创建内存块（锁外操作，减少锁持有时间）
        for (size_t i = 0; i < count; ++i) {
            new_nodes.push_back(new Chunk(chunk_size));
        }
    } catch (const std::bad_alloc&) {
        // 分配失败时，释放已创建的内存块，避免内存泄漏
        for (Chunk* c : new_nodes) delete c;
        throw MemoryAllocationError("Failed to preallocate chunk of size: " + std::to_string(chunk_size));
    }

    // 加锁操作：将预分配的内存块加入内存池
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 检查预分配后是否超出最大容量
        if (preallocated_bytes_ + total_size > max_capacity_bytes_) {
            // 超出容量，释放已创建的内存块
            for (Chunk* c : new_nodes) delete c;
            throw MemoryPoolExhaustedError("Preallocation exceeds maximum pool capacity: " + std::to_string(max_capacity_bytes_));
        }

        // 组装本地内存块为链表（头插法准备）
        Chunk* head = nullptr;
        Chunk* tail = nullptr;
        for (Chunk* c : new_nodes) {
            c->next = nullptr;  // 清空next指针，避免野指针
            if (!head) {        // 第一个节点
                head = tail = c;
            } else {            // 后续节点，尾插法组装
                tail->next = c;
                tail = c;
            }
        }

        // 将新链表接入内存池对应规格的链表头部
        auto it = pool_.find(chunk_size);
        if (it != pool_.end() && it->second != nullptr) {
            // 已有链表，将新链表尾部指向原有链表头
            tail->next = it->second;
            it->second = head;  // 更新链表头为新链表头
        } else {
            // 无原有链表，直接设置新链表为头
            pool_[chunk_size] = head;
        }

        // 更新预分配字节数
        preallocated_bytes_ += total_size;
    }
}

// 查找最匹配的内存块规格（向上取整）
size_t MemoryPool::find_suitable_size(size_t requested_size) const {
    for (size_t size : MEM_SIZES) {
        if (requested_size <= size) return size;
    }
    return 0;  
}

// 检查指定大小是否为内存池支持的规格
bool MemoryPool::is_supported_size(size_t s) const {
    // 遍历预定义规格，匹配则返回true
    for (size_t size : MEM_SIZES) {
        if (s == size) return true;
    }
    return false;
}

// 分配指定大小的内存块
// 核心逻辑：优先从预分配链表取（快路径），无则新建，全程保证线程安全
Chunk* MemoryPool::alloc_chunk(size_t n) {
    // 无效请求直接返回nullptr
    if (n == 0) return nullptr;

    // 找到匹配的内存块规格
    size_t chunk_size = find_suitable_size(n);
    if (chunk_size == 0) {
        // 无匹配规格，更新失败统计并返回nullptr
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.allocation_failures++;
        return nullptr;
    }

    // 快路径：加锁从预分配链表取现成的内存块（最常用场景）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pool_.find(chunk_size);
        if (it != pool_.end() && it->second != nullptr) {
            // 取出链表头节点
            Chunk* chunk = it->second;
            it->second = chunk->next;  // 更新链表头为下一个节点
            chunk->next = nullptr;     // 清空取出节点的next指针

            // 更新使用量和统计信息
            current_usage_bytes_ += chunk_size;
            stats_.total_allocations++;
            stats_.current_usage_bytes = current_usage_bytes_;
            // 更新峰值使用量
            if (current_usage_bytes_ > stats_.peak_usage_bytes) {
                stats_.peak_usage_bytes = current_usage_bytes_;
            }
            return chunk;  // 成功分配，返回内存块
        }

        // 预分配链表无可用节点，先检查是否超出最大容量
        if (current_usage_bytes_ + chunk_size > max_capacity_bytes_) {
            stats_.allocation_failures++;
            throw MemoryPoolExhaustedError("Allocation would exceed maximum pool capacity");
        }
        // 释放锁，后续在锁外创建新内存块（减少锁持有时间）
    }

    // 慢路径：预分配无可用节点，锁外创建新内存块
    Chunk* new_chunk = nullptr;
    try {
        new_chunk = new Chunk(chunk_size);
    } catch (const std::bad_alloc&) {
        // 系统内存不足，更新失败统计并抛出异常
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.allocation_failures++;
        throw MemoryAllocationError("Failed to allocate chunk of size: " + std::to_string(chunk_size));
    }

    // 再次加锁，二次校验容量（防止多线程竞态导致超出容量）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (current_usage_bytes_ + chunk_size > max_capacity_bytes_) {
            // 超出容量，回滚：删除刚创建的内存块
            delete new_chunk;
            stats_.allocation_failures++;
            throw MemoryPoolExhaustedError("Allocation would exceed maximum pool capacity (after recheck)");
        }

        // 成功：更新使用量和统计信息
        current_usage_bytes_ += chunk_size;
        stats_.total_allocations++;
        stats_.current_usage_bytes = current_usage_bytes_;
        if (current_usage_bytes_ > stats_.peak_usage_bytes) {
            stats_.peak_usage_bytes = current_usage_bytes_;
        }
    }

    return new_chunk;
}

// 归还内存块到内存池
void MemoryPool::retrieve(Chunk* chunk) {
    // 空指针直接返回
    if (!chunk) return;

    size_t chunk_size = chunk->capacity;
    // 容量为0的无效内存块直接释放
    if (chunk_size == 0) {
        delete chunk;
        return;
    }

    // 非内存池支持的规格，直接释放（防止混入非法规格内存块）
    if (!is_supported_size(chunk_size)) {
        delete chunk;
        return;
    }

    // 清空内存块数据（根据Chunk::clear()实现，如重置数据指针、长度等）
    chunk->clear();

    // 加锁保证线程安全，将内存块归还到对应规格的链表头部
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pool_.find(chunk_size);
    if (it != pool_.end()) {
        // 头插法：将归还的内存块插入链表头部
        chunk->next = it->second;
        it->second = chunk;
    } else {
        // 理论上不会走到这里（构造时已预置所有规格的key），做兜底处理
        chunk->next = nullptr;
        pool_[chunk_size] = chunk;
    }

    // 更新当前使用量（防止下溢，最小为0）
    if (current_usage_bytes_ >= chunk_size) {
        current_usage_bytes_ -= chunk_size;
    } else {
        current_usage_bytes_ = 0;
    }

    // 更新统计信息
    stats_.current_usage_bytes = current_usage_bytes_;
    stats_.total_deallocations++;
}

// 获取内存池统计信息（按值返回，保证线程安全）
PoolStats MemoryPool::get_stats() const {
    // 加锁保证读取的统计信息是完整、一致的
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}