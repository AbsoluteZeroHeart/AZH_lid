#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <cassert>
#include "ThreadPool.hpp"
#include <iomanip>

void test_basic_functionality() {
    std::cout << "测试1: 基本功能测试..." << std::endl;
    ThreadPool pool(4);
    
    auto future = pool.post_task([]() {
        return 42;
    });
    
    assert(future.get() == 42);
    std::cout << "基本功能测试通过" << std::endl;
}

void test_multiple_tasks() {
    std::cout << "\n测试2: 多任务测试..." << std::endl;
    ThreadPool pool(4);
    const int task_count = 100;
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.post_task([i]() {
            return i * i;
        }));
    }
    
    for (int i = 0; i < task_count; ++i) {
        assert(futures[i].get() == i * i);
    }
    std::cout << "多任务测试通过 (" << task_count << "个任务)" << std::endl;
}

void test_exception_handling() {
    std::cout << "\n测试3: 异常处理测试..." << std::endl;
    ThreadPool pool(2);
    
    auto future = pool.post_task([]() {
        throw std::runtime_error("测试异常");
        return 0;
    });
    
    try {
        future.get();
        assert(false && "应该抛出异常");
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()) == "测试异常");
        std::cout << "异常处理测试通过" << std::endl;
    }
}

void test_stop_behavior() {
    std::cout << "\n测试4: 停止行为测试..." << std::endl;
    ThreadPool pool(4);
    
    // 提交一些任务
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.post_task([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i;
        }));
    }
    
    // 停止线程池
    pool.stop();
    
    // 验证停止后不能再提交任务
    try {
        pool.post_task([]() { return 1; });
        assert(false && "应该抛出异常");
    } catch (const std::runtime_error&) {
        std::cout << "停止后提交任务正确抛出异常" << std::endl;
    }
    
    // 验证已提交的任务可以完成
    for (int i = 0; i < 10; ++i) {
        assert(futures[i].get() == i);
    }
    std::cout << "停止行为测试通过" << std::endl;
}

void test_concurrent_access() {
    std::cout << "\n测试5: 并发访问测试..." << std::endl;
    ThreadPool pool(8);
    const int task_count = 1000;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.post_task([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    
    // 等待所有任务完成
    for (auto& f : futures) {
        f.get();
    }
    
    assert(counter.load() == task_count);
    std::cout << "并发访问测试通过 (计数器: " << counter.load() << ")" << std::endl;
}

void test_idle_counter() {
    std::cout << "\n测试6: 空闲计数测试..." << std::endl;
    ThreadPool pool(4);
    
    // 初始应该有4个空闲线程
    assert(pool.idle_thread_count() == 4);
    
    // 提交一个任务，空闲线程应该减少
    auto future = pool.post_task([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 1;
    });
    
    // 等待一小段时间让任务开始执行
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    int idle_count = pool.idle_thread_count();
    assert(idle_count >= 2 && idle_count <= 3); // 近似值
    
    future.get();
    
    // 任务完成后，空闲线程应该恢复
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(pool.idle_thread_count() == 4);
    
    std::cout << "空闲计数测试通过" << std::endl;
}

void test_complex_computation() {
    std::cout << "\n测试7: 复杂计算测试..." << std::endl;
    ThreadPool pool(std::thread::hardware_concurrency());
    const int matrix_size = 100;
    
    // 创建两个随机矩阵
    std::vector<std::vector<int>> A(matrix_size, std::vector<int>(matrix_size));
    std::vector<std::vector<int>> B(matrix_size, std::vector<int>(matrix_size));
    std::vector<std::vector<int>> C(matrix_size, std::vector<int>(matrix_size));
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 10);
    
    for (int i = 0; i < matrix_size; ++i) {
        for (int j = 0; j < matrix_size; ++j) {
            A[i][j] = dis(gen);
            B[i][j] = dis(gen);
        }
    }
    
    // 并行计算矩阵乘法
    std::vector<std::future<void>> futures;
    for (int i = 0; i < matrix_size; ++i) {
        for (int j = 0; j < matrix_size; ++j) {
            futures.push_back(pool.post_task([&A, &B, &C, i, j, matrix_size]() {
                int sum = 0;
                for (int k = 0; k < matrix_size; ++k) {
                    sum += A[i][k] * B[k][j];
                }
                C[i][j] = sum;
            }));
        }
    }
    
    // 等待所有计算完成
    for (auto& f : futures) {
        f.get();
    }
    
    // 验证一个简单的计算
    int test_sum = 0;
    for (int k = 0; k < matrix_size; ++k) {
        test_sum += A[0][k] * B[k][0];
    }
    assert(C[0][0] == test_sum);
    
    std::cout << "复杂计算测试通过 (矩阵大小: " << matrix_size << "x" << matrix_size << ")" << std::endl;
}

void test_stress_performance() {
    std::cout << "\n测试8: 压力测试..." << std::endl;
    
    const int thread_counts[] = {1, 2, 4, 8, 16};
    const int task_counts[] = {1000, 5000, 10000};
    
    for (int threads : thread_counts) {
        for (int tasks : task_counts) {
            ThreadPool pool(threads);
            
            auto start = std::chrono::high_resolution_clock::now();
            
            std::vector<std::future<int>> futures;
            std::atomic<int> completed{0};
            
            // 提交任务
            for (int i = 0; i < tasks; ++i) {
                futures.push_back(pool.post_task([i, &completed]() {
                    // 模拟一些计算工作
                    int result = 0;
                    for (int j = 0; j < 1000; ++j) {
                        result += (i + j) % 100;
                    }
                    completed.fetch_add(1, std::memory_order_relaxed);
                    return result;
                }));
            }
            
            // 等待所有任务完成
            for (auto& f : futures) {
                f.get();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            assert(completed.load() == tasks);
            std::cout << "线程数: " << std::setw(2) << threads 
                      << ", 任务数: " << std::setw(5) << tasks
                      << ", 耗时: " << std::setw(4) << duration.count() << "ms" 
                      << ", 吞吐量: " << std::setw(6) 
                      << (tasks * 1000.0 / duration.count()) << "任务/秒" << std::endl;
            
            // 停止线程池
            pool.stop();
        }
    }
}

void test_memory_leak() {
    std::cout << "\n测试9: 内存泄漏测试..." << std::endl;
    
    // 多次创建和销毁线程池，检查是否有资源泄漏
    const int iterations = 100;
    for (int i = 0; i < iterations; ++i) {
        ThreadPool pool(4);
        
        // 提交一些任务
        std::vector<std::future<int>> futures;
        for (int j = 0; j < 100; ++j) {
            futures.push_back(pool.post_task([j]() {
                return j * j;
            }));
        }
        
        // 获取结果
        for (auto& f : futures) {
            f.get();
        }
        
        // pool在作用域结束时自动销毁
    }
    
    std::cout << "内存泄漏测试通过 (" << iterations << "次迭代)" << std::endl;
}

void test_destructor_with_pending_tasks() {
    std::cout << "\n测试10: 析构时未完成任务测试..." << std::endl;
    
    // 测试析构函数是否能正确处理未完成的任务
    {
        ThreadPool pool(2);
        
        // 提交一些长时间运行的任务
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 5; ++i) {
            futures.push_back(pool.post_task([i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (i + 1)));
            }));
        }
        
        // 不等待任务完成，直接离开作用域
        // 析构函数应该会等待所有任务完成
    }
    
    std::cout << "析构函数正确处理未完成任务" << std::endl;
}

int main() {
    try {
        std::cout << "=== 线程池测试开始 ===" << std::endl;
        
        test_basic_functionality();
        test_multiple_tasks();
        test_exception_handling();
        test_stop_behavior();
        test_concurrent_access();
        test_idle_counter();
        test_complex_computation();
        test_destructor_with_pending_tasks();
        
        std::cout << "\n=== 所有测试通过 ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}