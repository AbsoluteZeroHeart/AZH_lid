#ifndef LOGGER_LOGGER_H
#define LOGGER_LOGGER_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include "log_queue.hpp"

namespace logger {

class Logger {
public:
    // 日志级别枚举：级别从高到低为 ERROR > WARN > INFO > DEBUG
    enum class Level {
        ERROR,  // 错误级别：严重错误，需要立即处理
        WARN,   // 警告级别：潜在问题，不影响程序运行
        INFO,   // 信息级别：正常运行状态信息
        DEBUG   // 调试级别：开发调试用详细信息
    };

    // 单例模式：保证全局唯一实例
    static Logger& instance() {
        static Logger logger;  // 静态局部变量，首次调用时初始化，线程安全
        return logger;
    }

    // 日志配置结构体：封装所有可配置参数，便于初始化
    struct Config {
        std::string filename;          // 日志文件基础名（如 "app.log"）
        Level       level        = Level::INFO;  // 默认日志级别为INFO
        size_t      buffer_size  = 8192;         // 日志写入缓冲区大小（8KB）
        size_t      max_lines    = 5000;         // 单个日志文件最大行数（超过则切割）
        size_t      queue_capacity = 10000;      // 异步日志队列容量
        bool        async        = false;        // 是否启用异步写入（默认同步）
        bool        stdout_fallback = true;      // 写入失败时是否回退到标准输出
    };

    /**
     * 初始化日志器
     * @param config 日志配置参数
     * @return 初始化成功返回true，否则返回false
     */
    bool initialize(const Config& config);

    /**
     * 检查日志器是否已初始化
     * @return 已初始化返回true，否则返回false
     */
    bool is_initialized() const { return initialized_; }

    /**
     * 关闭日志器
     * 功能：停止异步线程、刷新缓冲区、关闭文件
     */
    void shutdown();

    /**
     * 设置日志级别（运行时可动态调整）
     * @param level 新的日志级别
     */
    void set_level(Level level);

    /**
     * 获取当前日志级别
     * @return 当前生效的日志级别
     */
    Level get_level() const;

    /**
     * 强制刷新缓冲区
     * 功能：将缓冲区中的日志立即写入文件
     */
    void flush();

    /**
     * 内部核心写入函数（对外建议使用宏调用）
     * @param level 日志级别
     * @param file  日志所在源文件（由__FILE__宏自动填充）
     * @param func  日志所在函数（由__FUNCTION__宏自动填充）
     * @param line  日志所在行号（由__LINE__宏自动填充）
     * @param format 格式化字符串（如 "user %d login"）
     * @param ...   可变参数（对应格式化字符串的占位符）
     */
    void write(Level level, const char* file, const char* func, int line,
               const char* format, ...);

private:
    // 单例模式：禁止外部构造、拷贝和赋值
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * 异步写入线程的核心函数
     * 功能：循环从队列获取日志并写入文件
     */
    void async_write_thread();

    /**
     * 同步写入函数
     * 功能：直接将日志字符串写入文件（带锁保护）
     * @param log 待写入的日志字符串
     */
    void sync_write(const std::string& log);

    /**
     * 创建新的日志文件
     * 功能：处理文件切割、日期目录、文件打开等逻辑
     * @return 创建成功返回true，否则返回false
     */
    bool create_new_log_file();

    /**
     * 获取格式化的时间字符串
     * @return 格式化时间（如 "2025-12-08 10:00:00"）
     */
    std::string get_formatted_time() const;

    /**
     * 获取当前日期（按年、月、日）
     * @return 当年的第几天（用于按天切割日志）
     */
    int get_current_day_of_year() const;

    std::atomic<bool>  initialized_{false};        // 是否初始化完成（原子变量保证线程安全）
    std::atomic<bool>  shutdown_requested_{false}; // 是否收到关闭请求（原子变量）
    std::atomic<Level> current_level_{Level::INFO};// 当前生效的日志级别（原子变量）

    std::string        dir_name_;        // 日志文件所在目录名
    std::string        file_name_;       // 日志文件基础名
    size_t             max_lines_;       // 单个日志文件最大行数
    size_t             buffer_size_;     // 写入缓冲区大小
    std::atomic<uint64_t> line_count_{0};// 当前日志文件已写入行数（原子变量）
    int                today_;           // 今日日期（用于按天切割日志）

    FILE*              file_{nullptr};   // 日志文件指针（C风格文件操作）
    std::unique_ptr<char[]> buffer_;     // 写入缓冲区（智能指针自动管理内存）

    bool               async_;           // 是否启用异步模式
    std::unique_ptr<LogQueue<std::string>> log_queue_; // 异步日志队列
    std::unique_ptr<std::thread> async_thread_;        // 异步写入线程

    mutable std::mutex file_mutex_;      // 文件操作互斥锁（mutable允许const函数修改）
};

/**
 * 日志宏定义：封装write函数，自动填充文件、函数、行号
 * ##__VA_ARGS__ 是GCC扩展，处理可变参数为空的情况（避免编译错误）
 */
#define LOG_DEBUG(format, ...) \
    logger::Logger::instance().write(logger::Logger::Level::DEBUG, \
                                     __FILE__, __FUNCTION__, __LINE__, \
                                     format, ##__VA_ARGS__)

#define LOG_INFO(format, ...) \
    logger::Logger::instance().write(logger::Logger::Level::INFO, \
                                     __FILE__, __FUNCTION__, __LINE__, \
                                     format, ##__VA_ARGS__)

#define LOG_WARN(format, ...) \
    logger::Logger::instance().write(logger::Logger::Level::WARN, \
                                     __FILE__, __FUNCTION__, __LINE__, \
                                     format, ##__VA_ARGS__)

#define LOG_ERROR(format, ...) \
    logger::Logger::instance().write(logger::Logger::Level::ERROR, \
                                     __FILE__, __FUNCTION__, __LINE__, \
                                     format, ##__VA_ARGS__)

} // namespace logger

#endif // LOGGER_LOGGER_H