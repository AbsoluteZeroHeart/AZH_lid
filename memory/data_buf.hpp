#ifndef DATA_BUF_H
#define DATA_BUF_H

#include "chunk.hpp"
#include "memory_pool.hpp"
#include "pr.hpp"

class BufferBase {
public:
    BufferBase() = default;
    virtual ~BufferBase();
    
    BufferBase(const BufferBase&) = delete;
    BufferBase& operator=(const BufferBase&) = delete;
    
    int length() const;
    void pop(int len);
    void clear();

protected:
    static constexpr int DEFAULT_BUFFER_SIZE = 4096;
    Chunk* data_buf{nullptr};
};

class InputBuffer : public BufferBase {
public:
    int read_from_fd(int fd);
    const char* get_from_buf() const;
    void adjust();
    
private:
    bool expand_buffer(int needed_size);
    bool ensure_space_available(int additional_size);
};

class OutputBuffer : public BufferBase {
public:
    int write_to_buf(const char* data, int len);
    int write_to_fd(int fd);
    int available_space() const;
    
private:
    bool ensure_capacity(int additional_size);
};

#endif // DATA_BUF_H