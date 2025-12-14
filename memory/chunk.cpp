#include "chunk.hpp"
#include <cstring>  
#include <cassert>  
#include <algorithm> 
#include <iostream> 
#include <utility>   

// Chunk构造函数 - 创建指定容量的内存块
Chunk::Chunk(size_t cap) 
    : capacity(cap)      
    , length(0)           
    , head(0)             
    , data(new char[cap]())  // 分配cap字节内存并初始化为0（ ()保证零初始化）
    , next(nullptr) {    
    assert(cap > 0);      // 调试期断言：容量必须大于0，防止创建空内存块
}

// Chunk移动构造函数（ noexcept 保证不抛出异常）
Chunk::Chunk(Chunk&& other) noexcept
    // 直接接管other的所有资源
    : capacity(other.capacity)
    , length(other.length)
    , head(other.head)
    , data(other.data)
    , next(other.next) {
    // 重置源对象，防止其析构时释放已转移的资源
    other.capacity = 0;
    other.length = 0;
    other.head = 0;
    other.data = nullptr; // 源对象不再持有内存指针
    other.next = nullptr;
}

// Chunk移动赋值运算符（ noexcept 保证不抛出异常）
Chunk& Chunk::operator=(Chunk&& other) noexcept {
    // 防止自赋值（移动自身无意义，且会导致资源释放）
    if (this != &other) {
        // 第一步：释放当前对象持有的内存资源，避免内存泄漏
        delete[] data;
        
        // 第二步：接管other的所有资源
        capacity = other.capacity;
        length = other.length;
        head = other.head;
        data = other.data;
        next = other.next;
        
        // 第三步：重置源对象，防止其析构时释放已转移的资源
        other.capacity = 0;
        other.length = 0;
        other.head = 0;
        other.data = nullptr;
        other.next = nullptr;
    }
    return *this;
}

// Chunk析构函数
Chunk::~Chunk() {
    delete[] data;  // 释放char数组（匹配构造函数的new char[]）
    data = nullptr; // 置空指针，避免野指针（防御性编程）
}

// 清空Chunk中的有效数据（不释放内存，仅重置状态）
void Chunk::clear() {
    length = 0; // 有效数据长度置0
    head = 0;   // 数据起始偏移置0
}

// 调整Chunk中的数据位置，消除头部偏移
void Chunk::adjust() {
    if (head != 0) {       // 仅当头部有偏移时才调整
        if (length != 0) { // 有有效数据时，移动数据
            // 将data+head处的length字节数据移动到data起始位置
            std::memmove(data, data + head, length);
        }
        head = 0; // 重置头部偏移为0
    }
}

// 从另一个Chunk拷贝数据到当前Chunk
void Chunk::copy(const Chunk* other) {
    // 源对象为空或无有效数据，直接清空当前数据
    if (other == nullptr || other->length == 0) {
        length = 0;
        return;
    }
    
    // 源数据长度超过当前容量，尝试扩展容量
    if (other->length > capacity) {
        // 扩展失败则直接返回，不执行拷贝
        if (!ensure_capacity(other->length)) {
            return;
        }
    }
    
    // 拷贝源Chunk的有效数据（从other->data+other->head开始）到当前Chunk起始位置
    std::memcpy(data, other->data + other->head, other->length);
    head = 0;               // 重置头部偏移为0
    length = other->length; // 更新当前有效数据长度
}

// 从Chunk头部移除指定长度的数据
void Chunk::pop(size_t len) {
    if (len >= length) {
        // 移除长度≥有效数据长度，直接清空所有数据
        length = 0;
        head = 0;
    } else {
        // 移除部分数据：头部偏移后移，有效长度减少
        head += len;
        length -= len;
    }
}

// 确保Chunk容量至少满足指定需求，不足则扩展
bool Chunk::ensure_capacity(size_t required_capacity) {
    // 容量已满足需求，直接返回成功
    if (required_capacity <= capacity) {
        return true;
    }
    
    // 计算新容量：至少翻倍（减少频繁扩展），且不小于所需容量
    size_t new_capacity = std::max(capacity * 2, required_capacity);
    // 调用扩展容量方法
    return expand_capacity(new_capacity);
}

// 扩展Chunk容量到指定大小
bool Chunk::expand_capacity(size_t new_capacity) {
    // 新容量≤当前容量，扩展无意义，返回失败
    if (new_capacity <= capacity) {
        return false;
    }
    
    try {
        // 分配新的内存块并初始化为0
        char* new_data = new char[new_capacity]();
        
        // 复制现有有效数据到新内存块起始位置
        if (length > 0) {
            std::memcpy(new_data, data + head, length);
        }
        
        // 释放旧内存块，避免内存泄漏
        delete[] data;
        
        // 更新指针和状态：新内存块、重置头部偏移、更新容量
        data = new_data;
        head = 0;          // 扩展后数据移到起始位置，偏移置0
        capacity = new_capacity;
        
        return true; // 扩展成功
    } catch (const std::bad_alloc&) {
        // 内存分配失败，返回false
        return false;
    }
}