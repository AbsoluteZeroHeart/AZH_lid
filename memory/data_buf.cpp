#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <algorithm>
#include "data_buf.hpp"
#include <limits>


namespace {
    bool safe_int_to_size(int value, size_t& result) {
        if (value < 0) {
            return false;
        }
        result = static_cast<size_t>(value);
        return true;
    }
    
    bool safe_add(size_t a, size_t b, size_t& result) {
        if (a > std::numeric_limits<size_t>::max() - b) {
            return false;
        }
        result = a + b;
        return true;
    }
}


BufferBase::~BufferBase() {
    try {
        clear();
    } catch (const std::exception& e) {
        PR_ERROR("Exception in BufferBase destructor: %s", e.what());
    }
}

int BufferBase::length() const {
    return data_buf != nullptr ? static_cast<int>(data_buf->length) : 0;
}

void BufferBase::pop(int len) {
    if (data_buf == nullptr) {
        PR_WARN("Attempt to pop from nullptr buffer");
        return;
    }
    
    if (len <= 0) {
        PR_WARN("Invalid pop length: %d", len);
        return;
    }
    
    if (static_cast<size_t>(len) > data_buf->length) {
        PR_ERROR("Pop length %d exceeds buffer length %zu", 
                len, data_buf->length);
        throw std::runtime_error("Pop length exceeds buffer length");
    }
    
    try {
        data_buf->pop(static_cast<size_t>(len));
        
        if (data_buf->length == 0) {
            MemoryPool::get_instance().retrieve(data_buf);
            data_buf = nullptr;
            PR_DEBUG("Buffer emptied and returned to pool");
        }
    } catch (const std::exception& e) {
        PR_ERROR("Failed to pop buffer: %s", e.what());
        throw;
    }
}

void BufferBase::clear() {
    if (data_buf != nullptr) {
        try {
            MemoryPool::get_instance().retrieve(data_buf);
            data_buf = nullptr;
            PR_DEBUG("Buffer cleared and returned to pool");
        } catch (const std::exception& e) {
            PR_ERROR("Failed to clear buffer: %s", e.what());
            // 尝试继续执行，避免内存泄漏
            data_buf = nullptr;
        }
    }
}

// InputBuffer 实现
bool InputBuffer::ensure_space_available(int additional_size) {
    // 参数验证
    if (additional_size <= 0) {
        PR_ERROR("ensure_space_available: invalid size %d", additional_size);
        return false;
    }
    
    // 大小限制
    static constexpr int MAX_ALLOWED_SIZE = 1024 * 1024;  // 1MB
    if (additional_size > MAX_ALLOWED_SIZE) {
        PR_ERROR("ensure_space_available: size %d > max %d",
                additional_size, MAX_ALLOWED_SIZE);
        return false;
    }
    
    // 安全转换
    size_t additional;
    if (!safe_int_to_size(additional_size, additional)) {
        PR_ERROR("ensure_space_available: negative size %d", additional_size);
        return false;
    }
    
    if (data_buf == nullptr) {
        // 初始分配
        int alloc_size = std::max(additional_size, DEFAULT_BUFFER_SIZE);
        size_t safe_size;
        if (!safe_int_to_size(alloc_size, safe_size)) {
            PR_ERROR("ensure_space_available: alloc_size invalid %d", alloc_size);
            return false;
        }
        
        try {
            data_buf = MemoryPool::get_instance().alloc_chunk(safe_size);
        } catch (...) {
            PR_ERROR("Failed to allocate buffer");
            return false;
        }
        
        if (!data_buf) {
            PR_ERROR("Allocation returned nullptr");
            return false;
        }
        return true;
    }
    
    // 确保数据已调整
    if (data_buf->head != 0) {
        adjust();
    }
    
    // 检查是否需要扩容
    size_t available = data_buf->capacity - data_buf->length;
    if (available >= additional) {
        return true;
    }
    
    // 安全计算需要的大小
    size_t needed = additional - available;
    int needed_int;
    if (needed > static_cast<size_t>(std::numeric_limits<int>::max())) {
        PR_ERROR("needed size too large: %zu", needed);
        return false;
    }
    needed_int = static_cast<int>(needed);
    
    return expand_buffer(needed_int);
}

bool InputBuffer::expand_buffer(int needed_size) {
    assert(data_buf != nullptr);
    
    size_t new_size = data_buf->length + static_cast<size_t>(needed_size);
    Chunk* new_buf = nullptr;
    
    try {
        new_buf = MemoryPool::get_instance().alloc_chunk(new_size);
    } catch (const MemoryPoolExhaustedError& e) {
        PR_ERROR("Memory pool exhausted while expanding: %s", e.what());
        return false;
    } catch (const MemoryAllocationError& e) {
        PR_ERROR("Memory allocation failed: %s", e.what());
        return false;
    }
    
    if (new_buf == nullptr) {
        PR_ERROR("Failed to expand buffer to size %zu", new_size);
        return false;
    }
    
    // 复制数据
    try {
        new_buf->copy(data_buf);
    } catch (const std::exception& e) {
        PR_ERROR("Failed to copy buffer data: %s", e.what());
        MemoryPool::get_instance().retrieve(new_buf);
        return false;
    }
    
    // 交换缓冲区
    MemoryPool::get_instance().retrieve(data_buf);
    data_buf = new_buf;
    
    PR_DEBUG("Buffer expanded from %zu to %zu bytes", 
            data_buf->capacity, new_buf->capacity);
    
    return true;
}

int InputBuffer::read_from_fd(int fd) {
    if (fd < 0) {
        PR_ERROR("Invalid fd: %d", fd);
        return -1;
    }
    
    // 确保有默认空间
    if (!ensure_space_available(DEFAULT_BUFFER_SIZE)) {
        PR_ERROR("Failed to ensure space");
        return -1;
    }
    
    if (!data_buf) {
        PR_ERROR("Buffer not allocated");
        return -1;
    }
    
    // 读取数据
    size_t available = data_buf->capacity - data_buf->length;
    if (available == 0) {
        PR_ERROR("No space in buffer");
        return -1;
    }
    
    // 限制读取大小，防止一次读取过多
    size_t to_read = std::min(available, static_cast<size_t>(65536));  // 64KB max
    
    ssize_t bytes_read;
    do {
        bytes_read = read(fd, data_buf->data + data_buf->length, to_read);
    } while (bytes_read == -1 && errno == EINTR);
    
    if (bytes_read > 0) {
        data_buf->length += static_cast<size_t>(bytes_read);
        PR_DEBUG("Read %zd bytes", bytes_read);
        return static_cast<int>(bytes_read);
    } else if (bytes_read == 0) {
        PR_DEBUG("EOF on fd %d", fd);
        return 0;
    } else {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0;
        }
        PR_ERROR("Read failed on fd %d: %s", fd, strerror(err));
        return -1;
    }
}

// 读取缓冲区数据
const char* InputBuffer::get_from_buf() const {
    if (data_buf == nullptr) {
        return nullptr;
    }
    return data_buf->data + data_buf->head;
}

void InputBuffer::adjust() {
    if (data_buf != nullptr && data_buf->head > 0) {
        size_t old_head = data_buf->head;
        data_buf->adjust();
        PR_DEBUG("Buffer adjusted, head moved from %zu to 0", old_head);
    }
}

// OutputBuffer 实现
bool OutputBuffer::ensure_capacity(int additional_size) {
    // 参数验证
    if (additional_size <= 0) {
        PR_ERROR("ensure_capacity: invalid size %d", additional_size);
        return false;
    }
    
    static constexpr int MAX_ALLOWED_SIZE = 1024 * 1024;  // 1MB
    if (additional_size > MAX_ALLOWED_SIZE) {
        PR_ERROR("ensure_capacity: size %d > max %d",
                additional_size, MAX_ALLOWED_SIZE);
        return false;
    }
    
    // 安全转换
    size_t additional;
    if (!safe_int_to_size(additional_size, additional)) {
        PR_ERROR("ensure_capacity: negative size %d", additional_size);
        return false;
    }
    
    if (data_buf == nullptr) {
        int alloc_size = std::max(additional_size, DEFAULT_BUFFER_SIZE);
        size_t safe_size;
        if (!safe_int_to_size(alloc_size, safe_size)) {
            PR_ERROR("ensure_capacity: alloc_size invalid %d", alloc_size);
            return false;
        }
        
        try {
            data_buf = MemoryPool::get_instance().alloc_chunk(safe_size);
        } catch (...) {
            PR_ERROR("Failed to allocate buffer");
            return false;
        }
        
        if (!data_buf) {
            PR_ERROR("Allocation returned nullptr");
            return false;
        }
        return true;
    }
    
    // 输出缓冲区应该总是有head为0
    if (data_buf->head != 0) {
        data_buf->adjust();
    }
    
    // 检查空间
    size_t available = data_buf->capacity - data_buf->length;
    if (available >= additional) {
        return true;
    }
    
    // 需要扩容
    size_t current_len = data_buf->length;
    size_t new_size;
    if (!safe_add(current_len, additional, new_size)) {
        PR_ERROR("ensure_capacity: overflow %zu + %zu", current_len, additional);
        return false;
    }
    
    // 检查最大大小
    static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024;  // 1MB
    if (new_size > MAX_BUFFER_SIZE) {
        PR_ERROR("ensure_capacity: new size %zu > max %zu", 
                new_size, MAX_BUFFER_SIZE);
        return false;
    }
    
    Chunk* new_buf = nullptr;
    try {
        new_buf = MemoryPool::get_instance().alloc_chunk(new_size);
    } catch (...) {
        PR_ERROR("Failed to allocate expanded buffer");
        return false;
    }
    
    if (!new_buf) {
        PR_ERROR("Expansion allocation returned nullptr");
        return false;
    }
    
    // 复制数据
    try {
        new_buf->copy(data_buf);
    } catch (...) {
        MemoryPool::get_instance().retrieve(new_buf);
        PR_ERROR("Failed to copy data");
        return false;
    }
    
    // 交换缓冲区
    MemoryPool::get_instance().retrieve(data_buf);
    data_buf = new_buf;
    
    return true;
}

int OutputBuffer::write_to_buf(const char* data, int len) {
    if (!data) {
        PR_ERROR("Null data pointer");
        return -1;
    }
    
    if (len <= 0) {
        PR_WARN("Zero or negative length: %d", len);
        return 0;
    }
    
    // 验证长度
    if (len > 1024 * 1024) {  // 1MB
        PR_ERROR("Data too large: %d bytes", len);
        return -1;
    }
    
    if (!ensure_capacity(len)) {
        PR_ERROR("Failed to ensure capacity for %d bytes", len);
        return -1;
    }
    
    if (!data_buf) {
        PR_ERROR("Buffer not allocated after ensure_capacity");
        return -1;
    }
    
    // 写入数据
    std::memcpy(data_buf->data + data_buf->length, data, 
               static_cast<size_t>(len));
    data_buf->length += static_cast<size_t>(len);
    
    return 0;
}

int OutputBuffer::write_to_fd(int fd) {
    if (fd < 0) {
        PR_ERROR("Invalid file descriptor: %d", fd);
        return -1;
    }
    
    if (data_buf == nullptr || data_buf->length == 0) {
        PR_DEBUG("No data to write to fd %d", fd);
        return 0;
    }
    
    assert(data_buf->head == 0 && "Output buffer should always have head at 0");
    
    ssize_t bytes_written = 0;
    do {
        bytes_written = write(fd, 
                             data_buf->data + data_buf->head,
                             data_buf->length);
    } while (bytes_written == -1 && errno == EINTR);
    
    if (bytes_written > 0) {
        PR_DEBUG("Wrote %zd bytes to fd %d, buffer had %zu bytes",
                bytes_written, fd, data_buf->length);
        
        try {
            pop(static_cast<int>(bytes_written));
        } catch (const std::exception& e) {
            PR_ERROR("Failed to pop written bytes: %s", e.what());
            return -1;
        }
    } else if (bytes_written == 0) {
        PR_DEBUG("Write returned 0 on fd %d", fd);
    } else if (bytes_written == -1) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            PR_DEBUG("Write would block on fd %d", fd);
            return 0;  // 非阻塞模式，稍后重试
        } else {
            PR_ERROR("Write failed on fd %d: %s", fd, strerror(err));
            return -1;
        }
    }
    
    return static_cast<int>(bytes_written);
}

int OutputBuffer::available_space() const {
    if (data_buf == nullptr) {
        return DEFAULT_BUFFER_SIZE;
    }
    return static_cast<int>(data_buf->capacity - data_buf->length);
}
