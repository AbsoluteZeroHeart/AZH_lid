#ifndef LOGGER_PR_H
#define LOGGER_PR_H

#include <stdio.h>
#include <thread>
#include <string>

namespace logger {

// 使用枚举代替宏定义
enum class LogLevel {
    ERROR = 0,
    WARN  = 1,
    INFO  = 2,
    DEBUG = 3
};

extern LogLevel g_pr_level;

// 线程安全的日志级别设置
void pr_set_level(LogLevel level);
LogLevel pr_get_level();

// 辅助函数
std::string thread_id_to_string();
uint64_t thread_id_to_uint64();

// 主日志宏
#define PR_INTERNAL(level, tag, format, ...) \
    do { \
        if (static_cast<int>(level) <= static_cast<int>(logger::g_pr_level)) { \
            printf("[%-5s][%s:%d][TID:%s] " format, \
                   tag, __FUNCTION__, __LINE__, \
                   logger::thread_id_to_string().c_str(), ##__VA_ARGS__); \
        } \
    } while (0)

// 具体日志级别宏
#define PR_DEBUG(format, ...) \
    PR_INTERNAL(logger::LogLevel::DEBUG, "DEBUG", format, ##__VA_ARGS__)

#define PR_INFO(format, ...) \
    PR_INTERNAL(logger::LogLevel::INFO, "INFO", format, ##__VA_ARGS__)

#define PR_WARN(format, ...) \
    PR_INTERNAL(logger::LogLevel::WARN, "WARN", format, ##__VA_ARGS__)

#define PR_ERROR(format, ...) \
    PR_INTERNAL(logger::LogLevel::ERROR, "ERROR", format, ##__VA_ARGS__)

} // namespace logger

#endif // LOGGER_PR_H