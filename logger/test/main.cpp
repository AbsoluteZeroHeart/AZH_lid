#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>

#include "logger.hpp"

namespace fs = std::filesystem;

// 测试工具类
class TestUtil {
public:
    // 清理测试文件
    static void clear_test_files(const std::string& pattern = "") {
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string filename = entry.path().filename().string();
                if (filename.find(".log") != std::string::npos) {
                    if (pattern.empty() || filename.find(pattern) != std::string::npos) {
                        fs::remove(entry.path());
                    }
                }
            }
        } catch (...) {
            // 忽略错误
        }
    }

    // 统计文件行数
    static size_t count_lines_in_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return 0;
        
        size_t count = 0;
        std::string line;
        while (std::getline(file, line)) {
            count++;
        }
        return count;
    }

    // 检查文件是否包含特定字符串
    static bool file_contains_string(const std::string& filename, const std::string& search_str) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.find(search_str) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // 获取所有日志文件
    static std::vector<std::string> get_log_files(const std::string& pattern = "") {
        std::vector<std::string> files;
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string filename = entry.path().filename().string();
                if (filename.find(".log") != std::string::npos) {
                    if (pattern.empty() || filename.find(pattern) != std::string::npos) {
                        files.push_back(filename);
                    }
                }
            }
        } catch (...) {
            // 忽略错误
        }
        
        std::sort(files.begin(), files.end());
        return files;
    }

    // 统计所有日志文件的总行数
    static size_t count_total_log_lines(const std::string& pattern = "") {
        auto files = get_log_files(pattern);
        size_t total = 0;
        for (const auto& file : files) {
            total += count_lines_in_file(file);
        }
        return total;
    }

    // 验证日志文件格式
    static bool validate_log_format(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;
        
        std::string line;
        int line_num = 0;
        
        // 检查前10行
        while (std::getline(file, line) && line_num < 10) {
            line_num++;
            
            // 检查基本格式：时间戳 [级别] [文件:函数:行号] 消息
            if (line.length() < 30) return false;
            
            // 检查时间戳格式：YYYY-MM-DD HH:MM:SS.ms
            if (line[4] != '-' || line[7] != '-' || line[10] != ' ' ||
                line[13] != ':' || line[16] != ':' || line[19] != '.') {
                return false;
            }
            
            // 检查日志级别
            size_t level_start = line.find('[');
            size_t level_end = line.find(']');
            if (level_start == std::string::npos || level_end == std::string::npos) {
                return false;
            }
            
            std::string level = line.substr(level_start + 1, level_end - level_start - 1);
            if (level != "DEBUG" && level != "INFO" && level != "WARN" && level != "ERROR") {
                return false;
            }
        }
        
        return line_num > 0;
    }
};

// 测试结果管理器
class TestManager {
private:
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    std::vector<std::string> failed_messages;
    
public:
    void start_test(const std::string& test_name) {
        std::cout << "\n================================================================" << std::endl;
        std::cout << "测试: " << test_name << std::endl;
        std::cout << "================================================================" << std::endl;
    }
    
    void end_test(bool success, const std::string& message = "") {
        total_tests++;
        if (success) {
            passed_tests++;
            std::cout << "✓ " << message << std::endl;
        } else {
            failed_tests++;
            std::cout << "✗ " << message << std::endl;
            if (!message.empty()) {
                failed_messages.push_back(message);
            }
        }
    }
    
    // 使用verify而不是assert，避免与标准库宏冲突
    void verify(bool condition, const std::string& message) {
        if (condition) {
            std::cout << "  ✓ " << message << std::endl;
        } else {
            std::cout << "  ✗ " << message << std::endl;
            throw std::runtime_error(message);
        }
    }
    
    void print_summary() {
        std::cout << "\n\n================================================================" << std::endl;
        std::cout << "测试结果摘要" << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << "总测试数: " << total_tests << std::endl;
        std::cout << "通过: " << passed_tests << std::endl;
        std::cout << "失败: " << failed_tests << std::endl;
        
        if (failed_tests > 0) {
            std::cout << "\n失败详情:" << std::endl;
            for (const auto& msg : failed_messages) {
                std::cout << "  - " << msg << std::endl;
            }
        }
        
        if (failed_tests == 0) {
            std::cout << "\n🎉 所有测试通过！" << std::endl;
        } else {
            std::cout << "\n❌ 有测试失败，请检查" << std::endl;
        }
    }
};

// 测试用例1: 基本功能测试
bool test_basic_functionality(TestManager& tm) {
    tm.start_test("基本功能测试");
    
    try {
        TestUtil::clear_test_files("test_basic");
        
        // 配置
        logger::Logger::Config config;
        config.filename = "test_basic.log";
        config.level = logger::Logger::Level::DEBUG;
        config.async = false;
        config.max_lines = 100;
        
        auto& logger = logger::Logger::instance();
        
        // 初始化
        bool init_result = logger.initialize(config);
        tm.verify(init_result, "日志系统初始化成功");
        
        // 测试各种日志级别
        LOG_DEBUG("调试日志: 数字=%d, 字符串=%s", 42, "test");
        LOG_INFO("信息日志: 浮点数=%.2f", 3.14159);
        LOG_WARN("警告日志");
        LOG_ERROR("错误日志");
        
        // 测试日志级别过滤
        logger.set_level(logger::Logger::Level::WARN);
        LOG_DEBUG("这条调试日志不应该出现");
        LOG_INFO("这条信息日志也不应该出现");
        LOG_WARN("这条警告日志应该出现");
        LOG_ERROR("这条错误日志也应该出现");
        
        // 刷新并关闭
        logger.flush();
        logger.shutdown();
        
        // 验证文件
        auto files = TestUtil::get_log_files("test_basic");
        tm.verify(!files.empty(), "日志文件已创建");
        
        // 验证日志内容
        bool has_debug = false;
        bool has_info = false;
        bool has_warn = false;
        bool has_error = false;
        
        for (const auto& file : files) {
            if (TestUtil::file_contains_string(file, "[DEBUG]")) has_debug = true;
            if (TestUtil::file_contains_string(file, "[INFO]")) has_info = true;
            if (TestUtil::file_contains_string(file, "[WARN]")) has_warn = true;
            if (TestUtil::file_contains_string(file, "[ERROR]")) has_error = true;
        }
        
        tm.verify(has_debug, "包含DEBUG级别日志");
        tm.verify(has_info, "包含INFO级别日志");
        tm.verify(has_warn, "包含WARN级别日志");
        tm.verify(has_error, "包含ERROR级别日志");
        
        // 验证日志格式
        for (const auto& file : files) {
            tm.verify(TestUtil::validate_log_format(file), "日志格式正确: " + file);
        }
        
        // 验证过滤功能
        for (const auto& file : files) {
            bool has_filtered = TestUtil::file_contains_string(file, "不应该出现");
            tm.verify(!has_filtered, "日志级别过滤生效");
        }
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例2: 异步模式测试
bool test_async_functionality(TestManager& tm) {
    tm.start_test("异步模式测试");
    
    try {
        TestUtil::clear_test_files("test_async");
        
        // 配置
        logger::Logger::Config config;
        config.filename = "test_async.log";
        config.level = logger::Logger::Level::INFO;
        config.async = true;
        config.queue_capacity = 1000;
        config.max_lines = 100;
        
        auto& logger = logger::Logger::instance();
        
        // 初始化
        bool init_result = logger.initialize(config);
        tm.verify(init_result, "异步日志初始化成功");
        
        // 发送大量日志
        const int LOG_COUNT = 100;
        for (int i = 0; i < LOG_COUNT; i++) {
            LOG_INFO("异步日志测试 %d/%d", i + 1, LOG_COUNT);
        }
        
        // 等待异步处理
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 关闭系统
        logger.shutdown();
        
        // 验证日志数量
        size_t total_lines = TestUtil::count_total_log_lines("test_async");
        tm.verify(total_lines >= LOG_COUNT, 
                 "异步模式正确写入日志，期望至少" + std::to_string(LOG_COUNT) + 
                 "行，实际" + std::to_string(total_lines) + "行");
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例3: 多线程并发测试
bool test_multithreading(TestManager& tm) {
    tm.start_test("多线程并发测试");
    
    try {
        TestUtil::clear_test_files("test_mt");
        
        // 配置
        logger::Logger::Config config;
        config.filename = "test_mt.log";
        config.level = logger::Logger::Level::INFO;
        config.async = true;
        config.queue_capacity = 10000;
        config.max_lines = 1000;
        
        auto& logger = logger::Logger::instance();
        
        // 初始化
        bool init_result = logger.initialize(config);
        tm.verify(init_result, "多线程日志初始化成功");
        
        // 创建多个线程
        std::atomic<int> completed_threads{0};
        const int THREAD_COUNT = 10;
        const int LOGS_PER_THREAD = 100;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < THREAD_COUNT; t++) {
            threads.emplace_back([&logger, t, &completed_threads]() {
                for (int i = 0; i < LOGS_PER_THREAD; i++) {
                    LOG_INFO("线程 %d - 日志 %d", t, i);
                }
                completed_threads++;
            });
        }
        
        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }
        
        tm.verify(completed_threads == THREAD_COUNT, "所有线程完成写入");
        
        // 等待异步处理
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        logger.shutdown();
        
        // 验证日志数量
        size_t total_lines = TestUtil::count_total_log_lines("test_mt");
        int expected_lines = THREAD_COUNT * LOGS_PER_THREAD;
        tm.verify(total_lines >= expected_lines, 
                 "多线程并发写入正确，期望至少" + std::to_string(expected_lines) + 
                 "行，实际" + std::to_string(total_lines) + "行");
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例4: 文件切割测试（按行数）
bool test_file_rotation_by_lines(TestManager& tm) {
    tm.start_test("文件切割测试（按行数）");
    
    try {
        TestUtil::clear_test_files("test_rotation");
        
        // 配置 - 设置很小的max_lines以触发切割
        logger::Logger::Config config;
        config.filename = "test_rotation.log";
        config.level = logger::Logger::Level::INFO;
        config.async = false;
        config.max_lines = 10;  // 每10行切割一次
        
        auto& logger = logger::Logger::instance();
        
        // 初始化
        bool init_result = logger.initialize(config);
        tm.verify(init_result, "文件切割日志初始化成功");
        
        // 写入超过max_lines的日志
        const int TOTAL_LOGS = 25;
        for (int i = 0; i < TOTAL_LOGS; i++) {
            LOG_INFO("测试文件切割，日志行: %d", i + 1);
        }
        
        logger.shutdown();
        
        // 检查是否有多个文件
        auto files = TestUtil::get_log_files("test_rotation");
        
        std::cout << "生成的日志文件:" << std::endl;
        for (const auto& file : files) {
            std::cout << "  - " << file << std::endl;
        }
        
        tm.verify(files.size() >= 2, 
                 "文件切割生效，生成多个文件，期望至少2个，实际" + std::to_string(files.size()) + "个");
        
        // 检查每个文件的行数
        size_t total_lines = 0;
        for (const auto& file : files) {
            size_t lines = TestUtil::count_lines_in_file(file);
            total_lines += lines;
            std::cout << "  文件 " << file << " 包含 " << lines << " 行日志" << std::endl;
            
            // 每个文件不应超过max_lines（除了最后一个可能不满）
            if (file != files.back()) {
                tm.verify(lines == config.max_lines, 
                         "文件 " + file + " 行数正确: " + std::to_string(lines) + " 行");
            }
        }
        
        tm.verify(total_lines >= TOTAL_LOGS, 
                 "所有日志都已保存，期望" + std::to_string(TOTAL_LOGS) + 
                 "行，实际" + std::to_string(total_lines) + "行");
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例5: 错误处理测试
bool test_error_handling(TestManager& tm) {
    tm.start_test("错误处理测试");
    
    try {
        TestUtil::clear_test_files("test_error");
        
        auto& logger = logger::Logger::instance();
        
        // 测试1: 重复初始化
        logger::Logger::Config config;
        config.filename = "test_error.log";
        config.level = logger::Logger::Level::INFO;
        
        bool first_init = logger.initialize(config);
        tm.verify(first_init, "第一次初始化成功");
        
        bool second_init = logger.initialize(config);
        tm.verify(!second_init, "重复初始化失败（符合预期）");
        
        logger.shutdown();
        
        // 测试2: 无效文件名（空字符串）
        logger::Logger::Config invalid_config;
        invalid_config.filename = "";
        invalid_config.stdout_fallback = true;
        
        bool empty_init = logger.initialize(invalid_config);
        // 空文件名可能初始化失败或回退到标准输出，两种结果都可接受
        if (!empty_init) {
            tm.verify(!empty_init, "空文件名初始化失败（符合预期）");
        } else {
            LOG_INFO("空文件名测试日志");
            logger.shutdown();
            tm.verify(true, "空文件名回退到标准输出");
        }
        
        // 测试3: 日志写入前未初始化
        // 应该安全地返回，不崩溃
        LOG_INFO("这条日志不应该被写入（未初始化状态）");
        tm.verify(true, "未初始化时写日志安全返回");
        
        // 测试4: 队列满的情况
        {
            logger::Logger::Config small_queue_config;
            small_queue_config.filename = "test_queue_full.log";
            small_queue_config.level = logger::Logger::Level::INFO;
            small_queue_config.async = true;
            small_queue_config.queue_capacity = 2;  // 很小的队列
            small_queue_config.max_lines = 100;
            
            bool queue_init = logger.initialize(small_queue_config);
            tm.verify(queue_init, "小队列日志初始化成功");
            
            // 快速写入大量日志，可能触发队列满
            for (int i = 0; i < 10; i++) {
                LOG_INFO("队列满测试日志 %d", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            logger.shutdown();
            
            auto queue_files = TestUtil::get_log_files("test_queue_full");
            tm.verify(!queue_files.empty(), "队列满测试产生日志文件");
        }
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例6: 性能测试
bool test_performance(TestManager& tm) {
    tm.start_test("性能测试");
    
    try {
        TestUtil::clear_test_files("test_perf");
        
        // 测试同步模式性能
        {
            logger::Logger::Config sync_config;
            sync_config.filename = "test_perf_sync.log";
            sync_config.level = logger::Logger::Level::INFO;
            sync_config.async = false;
            sync_config.max_lines = 10000;
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(sync_config);
            tm.verify(init_result, "同步性能测试初始化成功");
            
            const int SYNC_LOGS = 1000;
            auto start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < SYNC_LOGS; i++) {
                LOG_INFO("同步性能测试日志 %d", i);
            }
            
            logger.flush();
            auto end = std::chrono::high_resolution_clock::now();
            logger.shutdown();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            double logs_per_sec = SYNC_LOGS / (duration.count() / 1000.0);
            
            std::cout << "  同步模式: " << SYNC_LOGS << " 条日志耗时 " 
                      << duration.count() << "ms, " 
                      << static_cast<int>(logs_per_sec) << " 条/秒" << std::endl;
            
            tm.verify(duration.count() < 5000, "同步模式性能可接受");
        }
        
        // 测试异步模式性能
        {
            logger::Logger::Config async_config;
            async_config.filename = "test_perf_async.log";
            async_config.level = logger::Logger::Level::INFO;
            async_config.async = true;
            async_config.queue_capacity = 10000;
            async_config.max_lines = 10000;
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(async_config);
            tm.verify(init_result, "异步性能测试初始化成功");
            
            const int ASYNC_LOGS = 10000;
            auto start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < ASYNC_LOGS; i++) {
                LOG_INFO("异步性能测试日志 %d", i);
            }
            
            // 等待异步队列处理
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            auto end = std::chrono::high_resolution_clock::now();
            logger.shutdown();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            double logs_per_sec = ASYNC_LOGS / (duration.count() / 1000.0);
            
            std::cout << "  异步模式: " << ASYNC_LOGS << " 条日志耗时 " 
                      << duration.count() << "ms, " 
                      << static_cast<int>(logs_per_sec) << " 条/秒" << std::endl;
            
            tm.verify(duration.count() < 5000, "异步模式性能可接受");
        }
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例7: LogQueue单元测试
bool test_log_queue(TestManager& tm) {
    tm.start_test("LogQueue单元测试");
    
    try {
        // 测试基本功能
        {
            logger::LogQueue<int> queue(10);
            tm.verify(queue.empty(), "新队列为空");
            tm.verify(!queue.full(), "新队列未满");
            tm.verify(queue.size() == 0, "队列大小为0");
            
            // 测试push/pop
            for (int i = 0; i < 5; i++) {
                tm.verify(queue.push(i), "push成功: " + std::to_string(i));
            }
            
            tm.verify(queue.size() == 5, "队列大小正确: 5");
            
            int value;
            for (int i = 0; i < 5; i++) {
                tm.verify(queue.pop(value), "pop成功");
                tm.verify(value == i, "值正确: " + std::to_string(i));
            }
            
            tm.verify(queue.empty(), "队列再次为空");
        }
        
        // 测试超时功能
        {
            logger::LogQueue<int> queue(2);
            tm.verify(queue.push(1), "push 1 成功");
            tm.verify(queue.push(2), "push 2 成功");
            tm.verify(queue.full(), "队列已满");
            
            // 队列已满，push应该超时
            auto start = std::chrono::steady_clock::now();
            bool result = queue.push(3, 50);  // 50ms超时
            auto end = std::chrono::steady_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            tm.verify(!result, "push超时失败（符合预期）");
            tm.verify(duration.count() >= 45, "超时时间基本正确: " + std::to_string(duration.count()) + "ms");
        }
        
        // 测试移动语义
        {
            logger::LogQueue<std::string> queue(5);
            
            std::string str1 = "test1";
            tm.verify(queue.push(std::move(str1)), "移动push成功");
            tm.verify(str1.empty(), "原字符串已被移动");
            
            std::string str2;
            tm.verify(queue.pop(str2), "pop成功");
            tm.verify(str2 == "test1", "字符串值正确: " + str2);
        }
        
        // 测试批量pop
        {
            logger::LogQueue<int> queue(100);
            
            for (int i = 0; i < 50; i++) {
                queue.push(i);
            }
            
            std::vector<int> items;
            size_t count = queue.pop_batch(items, 20, 100);
            
            tm.verify(count == 20, "批量pop数量正确: " + std::to_string(count));
            tm.verify(items.size() == 20, "向量大小正确: " + std::to_string(items.size()));
            
            for (size_t i = 0; i < items.size(); i++) {
                tm.verify(items[i] == static_cast<int>(i), 
                         "批量pop值正确: 期望" + std::to_string(i) + 
                         "，实际" + std::to_string(items[i]));
            }
        }
        
        // 测试线程安全（简单版本）
        {
            logger::LogQueue<int> queue(100);
            std::atomic<int> push_count{0};
            std::atomic<int> pop_count{0};
            
            std::thread writer([&queue, &push_count]() {
                for (int i = 0; i < 100; i++) {
                    if (queue.push(i, 10)) {
                        push_count++;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            });
            
            std::thread reader([&queue, &pop_count]() {
                int value;
                for (int i = 0; i < 100; i++) {
                    if (queue.pop(value, 10)) {
                        pop_count++;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            });
            
            writer.join();
            reader.join();
            
            std::cout << "  线程安全测试: push=" << push_count << ", pop=" << pop_count << std::endl;
            tm.verify(push_count > 0 && pop_count > 0, "多线程操作成功");
        }
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例8: 边界条件测试
bool test_edge_cases(TestManager& tm) {
    tm.start_test("边界条件测试");
    
    try {
        TestUtil::clear_test_files("test_edge");
        
        // 测试1: max_lines = 0（应该不限制行数）
        {
            logger::Logger::Config config;
            config.filename = "test_edge_zero.log";
            config.level = logger::Logger::Level::INFO;
            config.async = false;
            config.max_lines = 0;  // 0表示不限制
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "max_lines=0 初始化成功");
            
            // 写入大量日志，不应该触发文件切割
            for (int i = 0; i < 100; i++) {
                LOG_INFO("max_lines=0 测试日志 %d", i);
            }
            
            logger.shutdown();
            
            auto files = TestUtil::get_log_files("test_edge_zero");
            tm.verify(files.size() == 1, "max_lines=0 不触发文件切割，文件数: " + std::to_string(files.size()));
        }
        
        // 测试2: max_lines = 1（最小有效值）
        {
            logger::Logger::Config config;
            config.filename = "test_edge_one.log";
            config.level = logger::Logger::Level::INFO;
            config.async = false;
            config.max_lines = 1;  // 每1行切割一次
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "max_lines=1 初始化成功");
            
            // 写入3条日志，应该生成3个文件
            for (int i = 0; i < 3; i++) {
                LOG_INFO("max_lines=1 测试日志 %d", i);
            }
            
            logger.shutdown();
            
            auto files = TestUtil::get_log_files("test_edge_one");
            tm.verify(files.size() >= 3, 
                     "max_lines=1 每行切割，期望至少3个文件，实际" + std::to_string(files.size()));
        }
        
        // 测试3: 长日志消息
        {
            logger::Logger::Config config;
            config.filename = "test_edge_long.log";
            config.level = logger::Logger::Level::INFO;
            config.async = false;
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "长日志测试初始化成功");
            
            // 生成超长消息
            std::string long_msg(5000, 'X');  // 5000个字符
            LOG_INFO("超长日志消息: %s", long_msg.c_str());
            
            // 多行消息
            std::string multiline_msg = "第一行\n第二行\n第三行";
            LOG_INFO("多行消息: %s", multiline_msg.c_str());
            
            logger.shutdown();
            
            auto files = TestUtil::get_log_files("test_edge_long");
            tm.verify(!files.empty(), "长日志测试产生文件");
        }
        
        // 测试4: 特殊字符
        {
            logger::Logger::Config config;
            config.filename = "test_edge_special.log";
            config.level = logger::Logger::Level::INFO;
            config.async = false;
            
            auto& logger = logger::Logger::instance();
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "特殊字符测试初始化成功");
            
            LOG_INFO("特殊字符测试: 引号\" 单引号' 反斜杠\\ 制表符\t 换行符\n结束");
            LOG_INFO("Unicode测试: 中文测试 ☀ ★ ♫");
            LOG_INFO("空字符串: %s", "");
            LOG_INFO("NULL指针: %s", static_cast<const char*>(nullptr));
            
            logger.shutdown();
            
            auto files = TestUtil::get_log_files("test_edge_special");
            tm.verify(!files.empty(), "特殊字符测试产生文件");
        }
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

// 测试用例9: 重启测试
bool test_restart(TestManager& tm) {
    tm.start_test("重启测试");
    
    try {
        TestUtil::clear_test_files("test_restart");
        
        auto& logger = logger::Logger::instance();
        
        // 第一次启动
        {
            logger::Logger::Config config;
            config.filename = "test_restart.log";
            config.level = logger::Logger::Level::INFO;
            config.async = false;
            
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "第一次启动成功");
            
            LOG_INFO("第一次启动的日志");
            logger.shutdown();
        }
        
        // 第二次启动（重用同一个logger实例）
        {
            logger::Logger::Config config;
            config.filename = "test_restart.log";
            config.level = logger::Logger::Level::DEBUG;
            config.async = false;
            
            bool init_result = logger.initialize(config);
            tm.verify(init_result, "第二次启动成功");
            
            LOG_DEBUG("第二次启动的DEBUG日志");
            LOG_INFO("第二次启动的INFO日志");
            
            logger.shutdown();
        }
        
        // 验证两次启动的日志都保存了
        auto files = TestUtil::get_log_files("test_restart");
        size_t total_lines = TestUtil::count_total_log_lines("test_restart");
        tm.verify(total_lines >= 3, "重启测试日志保存成功，总行数: " + std::to_string(total_lines));
        
        return true;
    } catch (const std::exception& e) {
        tm.end_test(false, std::string("异常: ") + e.what());
        return false;
    }
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "         日志系统全面测试开始" << std::endl;
    std::cout << "================================================================" << std::endl;
    
    TestManager tm;
    
    // 清理所有之前的测试文件
    TestUtil::clear_test_files();
    
    // 运行所有测试
    bool all_passed = true;
    
    // 1. LogQueue单元测试
    try {
        all_passed &= test_log_queue(tm);
        tm.end_test(all_passed, "LogQueue单元测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "LogQueue单元测试异常");
    }
    
    // 2. 基本功能测试
    try {
        all_passed &= test_basic_functionality(tm);
        tm.end_test(all_passed, "基本功能测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "基本功能测试异常");
    }
    
    // 3. 异步模式测试
    try {
        all_passed &= test_async_functionality(tm);
        tm.end_test(all_passed, "异步模式测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "异步模式测试异常");
    }
    
    // 4. 多线程并发测试
    try {
        all_passed &= test_multithreading(tm);
        tm.end_test(all_passed, "多线程并发测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "多线程并发测试异常");
    }
    
    // 5. 文件切割测试
    try {
        all_passed &= test_file_rotation_by_lines(tm);
        tm.end_test(all_passed, "文件切割测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "文件切割测试异常");
    }
    
    // 6. 错误处理测试
    try {
        all_passed &= test_error_handling(tm);
        tm.end_test(all_passed, "错误处理测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "错误处理测试异常");
    }
    
    // 7. 性能测试
    try {
        all_passed &= test_performance(tm);
        tm.end_test(all_passed, "性能测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "性能测试异常");
    }
    
    // 8. 边界条件测试
    try {
        all_passed &= test_edge_cases(tm);
        tm.end_test(all_passed, "边界条件测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "边界条件测试异常");
    }
    
    // 9. 重启测试
    try {
        all_passed &= test_restart(tm);
        tm.end_test(all_passed, "重启测试完成");
    } catch (...) {
        all_passed = false;
        tm.end_test(false, "重启测试异常");
    }
    
    // 显示测试摘要
    tm.print_summary();
    
    // 清理测试文件
    std::cout << "\n清理测试文件..." << std::endl;
    // TestUtil::clear_test_files();
    
    return all_passed ? 0 : 1;
}