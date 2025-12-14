#include "Timer.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <cassert>
#include <cmath>

void test_basic_timer() {
    std::cout << "测试1: 基础定时器功能..." << std::endl;
    
    Timer timer(2);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    
    // 延迟执行任务
    int task_id = timer.schedule_once(50, [&counter]() {
        counter.fetch_add(1);
        std::cout << "单次任务执行" << std::endl;
    });
    
    assert(task_id >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(counter.load() == 1);
    
    timer.stop();
    std::cout << "基础定时器功能测试通过" << std::endl;
}

void test_periodic_timer() {
    std::cout << "\n测试2: 周期性定时器..." << std::endl;
    
    Timer timer(2);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    auto start_time = std::chrono::steady_clock::now();
    
    // 周期性任务，每50ms执行一次
    int task_id = timer.schedule_periodic(50, [&counter, start_time]() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        int value = counter.fetch_add(1) + 1;
        std::cout << "周期性任务执行第 " << value << " 次，经过 " << elapsed << "ms" << std::endl;
    });
    
    assert(task_id >= 0);
    
    // 等待220ms，应该执行大约4-5次
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    
    int count = counter.load();
    std::cout << "实际执行次数: " << count << std::endl;
    
    timer.stop();
    
    // 允许一定的误差范围，因为定时器不是绝对精确的
    // 220ms / 50ms = 4.4，所以4-5次都是合理的
    assert(count >= 4 && count <= 5);
    std::cout << "周期性定时器测试通过，执行次数: " << count << std::endl;
}

void test_repeat_timer() {
    std::cout << "\n测试3: 重复定时器..." << std::endl;
    
    Timer timer(2);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    
    // 重复3次，每次间隔30ms
    int task_id = timer.schedule_repeat(30, 3, [&counter]() {
        int value = counter.fetch_add(1) + 1;
        std::cout << "重复任务第 " << value << " 次执行" << std::endl;
    });
    
    assert(task_id >= 0);
    
    // 等待足够长的时间确保所有任务都执行完
    // 3次 * 30ms = 90ms，加上一些缓冲时间
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    timer.stop();
    
    // 应该正好执行3次
    assert(counter.load() == 3);
    std::cout << "重复定时器测试通过，执行次数: " << counter.load() << std::endl;
}

void test_cancel_timer() {
    std::cout << "\n测试4: 取消定时器任务..." << std::endl;
    
    Timer timer(2);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    
    // 计划一个延迟任务
    int task_id = timer.schedule_once(100, [&counter]() {
        counter.fetch_add(1);
        std::cout << "这个任务不应该执行" << std::endl;
    });
    
    assert(task_id >= 0);
    
    // 立即取消
    assert(timer.cancel(task_id));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // 任务应该被取消，没有执行
    assert(counter.load() == 0);
    
    timer.stop();
    std::cout << "取消定时器任务测试通过" << std::endl;
}

void test_concurrent_timers() {
    std::cout << "\n测试5: 并发定时器任务..." << std::endl;
    
    Timer timer(4);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    const int task_count = 20;
    std::vector<int> task_ids;
    
    // 创建多个定时任务
    for (int i = 0; i < task_count; ++i) {
        int delay = 10 + i * 5; // 不同延迟
        int task_id = timer.schedule_once(delay, [&counter, i]() {
            counter.fetch_add(1);
        });
        task_ids.push_back(task_id);
    }
    
    // 等待所有任务执行
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    timer.stop();
    
    // 所有任务都应该执行
    assert(counter.load() == task_count);
    std::cout << "并发定时器任务测试通过，执行任务数: " << counter.load() << std::endl;
}

void test_timer_resilience() {
    std::cout << "\n测试6: 定时器健壮性..." << std::endl;
    
    Timer timer(2);
    assert(timer.start());
    
    std::atomic<int> counter{0};
    
    // 测试异常任务
    timer.schedule_once(50, [&counter]() {
        counter.fetch_add(1);
        throw std::runtime_error("测试异常");
    });
    
    // 正常任务
    timer.schedule_once(100, [&counter]() {
        counter.fetch_add(1);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    timer.stop();
    
    // 两个任务都应该被调度（虽然第一个抛出异常）
    assert(counter.load() == 2);
    std::cout << "定时器健壮性测试通过" << std::endl;
}

int main() {
    try {
        std::cout << "=== 定时器测试开始 ===" << std::endl;
        
        test_basic_timer();
        test_periodic_timer();
        test_repeat_timer();
        test_cancel_timer();
        test_concurrent_timers();
        test_timer_resilience();
        
        std::cout << "\n=== 所有测试通过 ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}