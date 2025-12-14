#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <cassert>
#include <exception>
#include <cstring>  
#include <atomic>   
#include <cstdlib>   

#include "memory_pool.hpp"
#include "chunk.hpp"

using namespace std::chrono;

void print_stats(const PoolStats& s) {
    std::cout << "PoolStats:\n"
              << "  total_allocations:    " << s.total_allocations << "\n"
              << "  total_deallocations:  " << s.total_deallocations << "\n"
              << "  peak_usage_bytes:     " << s.peak_usage_bytes << "\n"
              << "  current_usage_bytes:  " << s.current_usage_bytes << "\n"
              << "  allocation_failures:  " << s.allocation_failures << "\n";
}

// 简单断言工具：如果条件不满足则打印并退出非0
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << std::endl;
        std::exit(2);
    }
}

void single_thread_basic_test() {
    std::cout << "== 单线程基本测试 ==\n";
    MemoryPool& pool = MemoryPool::get_instance();

    // 记录初始统计
    PoolStats before = pool.get_stats();

    // 分配默认 chunk
    Chunk* c = pool.alloc_chunk();
    require(c != nullptr, "alloc_chunk() returned nullptr");

    // 写入数据以验证可用性
    const char* msg = "hello-pool";
    size_t msglen = strlen(msg);
    require(c->capacity >= msglen, "chunk capacity too small for test message");
    std::memcpy(c->data, msg, msglen);
    c->length = msglen;
    c->head = 0;

    // 归还
    pool.retrieve(c);

    PoolStats after = pool.get_stats();
    print_stats(after);

    // 基本断言：deallocations 增加
    require(after.total_deallocations >= before.total_deallocations + 1, "total_deallocations did not increase");
    std::cout << "单线程基本测试通过\n\n";
}

void each_size_once_test() {
    std::cout << "== 各规格一次分配/归还测试 ==\n";
    MemoryPool& pool = MemoryPool::get_instance();

    for (size_t s : MEM_SIZES) {
        std::cout << "Testing size: " << s << " bytes ... ";
        Chunk* c = nullptr;
        try {
            c = pool.alloc_chunk(s);
        } catch (const std::exception& e) {
            std::cout << "exception during alloc_chunk(" << s << "): " << e.what() << "\n";
            require(false, "alloc_chunk threw exception for supported size");
        }
        require(c != nullptr, "alloc_chunk returned nullptr for supported size");
        require(c->capacity >= s, "returned chunk smaller than requested size");

        // 写少量数据
        if (c->capacity > 0) {
            c->length = 1;
            c->head = 0;
            c->data[0] = 42;
        }

        pool.retrieve(c);
        std::cout << "OK\n";
    }
    std::cout << "各规格测试通过\n\n";
}

void concurrent_stress_test(size_t thread_count, size_t ops_per_thread) {
    std::cout << "== 并发压力测试 ==\n";
    MemoryPool& pool = MemoryPool::get_instance();

    std::atomic<size_t> allocations_failed{0};
    std::atomic<size_t> allocations_succeeded{0};
    std::atomic<size_t> retrievals_succeeded{0};
    std::atomic<size_t> exceptions_caught{0};

    auto worker = [&](unsigned seed) {
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int> idx_dist(0, static_cast<int>(MEM_SIZES.size()) - 1);

        for (size_t i = 0; i < ops_per_thread; ++i) {
            size_t idx = idx_dist(rng);
            size_t req = MEM_SIZES[idx];

            try {
                Chunk* c = pool.alloc_chunk(req);
                if (!c) {
                    allocations_failed.fetch_add(1);
                    continue;
                }
                allocations_succeeded.fetch_add(1);

                // 模拟工作：写入前几个字节
                size_t write = std::min<size_t>(std::max<size_t>(1, req / 1024), c->capacity);
                c->length = write;
                c->head = 0;
                for (size_t k = 0; k < write; ++k) {
                    c->data[k] = static_cast<char>((k + i) & 0xFF);
                }

                // 随机短暂睡眠，增加并发交错
                if ((i & 7) == 0) std::this_thread::sleep_for(std::chrono::microseconds(10));

                pool.retrieve(c);
                retrievals_succeeded.fetch_add(1);
            } catch (const MemoryPoolExhaustedError& e) {
                allocations_failed.fetch_add(1);
            } catch (const MemoryAllocationError& e) {
                allocations_failed.fetch_add(1);
            } catch (const std::exception& e) {
                exceptions_caught.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    auto start = high_resolution_clock::now();
    for (size_t t = 0; t < thread_count; ++t) {
        threads.emplace_back(worker, static_cast<unsigned>(std::random_device{}() + t));
    }

    for (auto& th : threads) th.join();
    auto end = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(end - start).count();

    std::cout << "threads: " << thread_count << ", ops/thread: " << ops_per_thread << ", elapsed(ms): " << ms << "\n";
    std::cout << "allocations_succeeded: " << allocations_succeeded.load() << "\n";
    std::cout << "allocations_failed:    " << allocations_failed.load() << "\n";
    std::cout << "retrievals_succeeded:  " << retrievals_succeeded.load() << "\n";
    std::cout << "exceptions_caught:     " << exceptions_caught.load() << "\n";

    PoolStats stats = pool.get_stats();
    print_stats(stats);

    // 验证：所有分配都被归还（在没有异常/退出路径的情况下）
    // 注意：allocations_succeeded 应该等于 retrievals_succeeded
    require(allocations_succeeded.load() == retrievals_succeeded.load(),
            "allocations_succeeded != retrievals_succeeded (some chunks not returned?)");

    // current usage 应为 0（所有 chunk 都已归还）
    require(stats.current_usage_bytes == 0, "current_usage_bytes != 0 after all returns");

    std::cout << "并发压力测试通过\n\n";
}

int main() {
    try {
        single_thread_basic_test();
        each_size_once_test();

        // 并发测试参数：线程数与每线程操作次数
        const size_t threads = 8;
        const size_t ops_per_thread = 2000;
        concurrent_stress_test(threads, ops_per_thread);

        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Uncaught exception in tests: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown uncaught exception\n";
        return 1;
    }
}
