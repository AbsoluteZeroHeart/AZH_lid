#ifndef CHUNK_HPP
#define CHUNK_HPP

#include <cstddef>

struct Chunk {
    size_t capacity;
    size_t length;
    size_t head;
    char* data;
    Chunk* next;

    explicit Chunk(size_t cap);
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;

    // 禁止拷贝
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    ~Chunk();

    void clear();
    void adjust();
    void copy(const Chunk* other);
    void pop(size_t len);
    bool ensure_capacity(size_t required_capacity);
    bool expand_capacity(size_t new_capacity);
};

#endif // CHUNK_HPP
