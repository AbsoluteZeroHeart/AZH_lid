#include "logger.hpp"
#include <chrono>
#include <ctime>
#include <cstdarg>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <mutex>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <vector>
#include <algorithm>

namespace logger {

// 保护 localtime_* 的互斥锁（localtime 不线程安全）
static std::mutex time_mutex_;

// 默认构造与析构
Logger::Logger() = default;
Logger::~Logger() {
    shutdown();
}

// 获取格式化时间字符串，格式：YYYY-MM-DD HH:MM:SS.mmm
std::string Logger::get_formatted_time() const {
    std::lock_guard<std::mutex> lock(time_mutex_);

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now_c);
#else
    localtime_r(&now_c, &tm_buf);
#endif

    char time_buf[64];
    snprintf(time_buf, sizeof(time_buf),
             "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             static_cast<int>(ms.count()));

    return std::string(time_buf);
}

// 获取当前年内的天数，用于判断是否跨天
int Logger::get_current_day_of_year() const {
    std::lock_guard<std::mutex> lock(time_mutex_);
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now_c);
#else
    localtime_r(&now_c, &tm_buf);
#endif
    return tm_buf.tm_yday;
}

// 创建新的日志文件（调用方需持有 file_mutex_）
bool Logger::create_new_log_file() {
    // 关闭旧文件（如果存在）
    if (file_) {
        fflush(file_);
        fclose(file_);
        file_ = nullptr;
    }

    try {
        // 获取当前日期信息
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &now_c);
#else
        localtime_r(&now_c, &tm_buf);
#endif

        today_ = tm_buf.tm_yday;

        // 去掉文件名扩展名（若有）
        std::string base_name = file_name_;
        size_t dot_pos = base_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            base_name = base_name.substr(0, dot_pos);
        }

        // 日期字符串 YYYYMMDD
        char date_buf[32];
        snprintf(date_buf, sizeof(date_buf), "%04d%02d%02d",
                 tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);

        // 查找目录下同名日志文件，确定最大序号
        int max_index = 0;
        std::string search_pattern = base_name + "_" + date_buf;
        std::string search_dir = dir_name_.empty() ? "." : dir_name_;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(search_dir)) {
                std::string filename = entry.path().filename().string();

                if (filename.find(search_pattern) == 0 && filename.find(".log") != std::string::npos) {
                    std::string num_part = filename.substr(search_pattern.length());

                    // 移除后缀 .log
                    size_t dot_pos2 = num_part.find(".log");
                    if (dot_pos2 != std::string::npos) {
                        num_part = num_part.substr(0, dot_pos2);
                    }

                    if (!num_part.empty() && num_part[0] == '_') {
                        num_part = num_part.substr(1);
                    }

                    if (!num_part.empty()) {
                        try {
                            int index = std::stoi(num_part);
                            if (index > max_index) max_index = index;
                        } catch (...) {
                            // 忽略非数字部分
                        }
                    } else {
                        max_index = std::max(max_index, 1);
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // 目录不存在或不能访问，后续会尝试创建目录
        }

        int new_index = (max_index == 0) ? 1 : (max_index + 1);

        std::string full_filename;
        if (new_index == 1) {
            full_filename = base_name + "_" + date_buf + ".log";
        } else {
            full_filename = base_name + "_" + date_buf + "_" + std::to_string(new_index) + ".log";
        }

        // 如果指定了目录，创建目录并拼接完整路径
        if (!dir_name_.empty()) {
            std::filesystem::create_directories(dir_name_);
            full_filename = dir_name_ + "/" + full_filename;
        }

        // 打开日志文件（追加）
        file_ = fopen(full_filename.c_str(), "a");
        if (!file_) {
            std::cerr << "Failed to open log file: " << full_filename
                      << ", error: " << std::strerror(errno) << std::endl;
            return false;
        }

        // 重置行计数
        line_count_ = 0;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Create log file error: " << e.what() << std::endl;
        return false;
    }
}

// 同步写入（负责切割判断和实际写入）
void Logger::sync_write(const std::string& log) {
    if (log.empty()) return;

    std::lock_guard<std::mutex> lock(file_mutex_);

    bool need_new_file = false;

    if (!file_) {
        need_new_file = true;
    } else if (max_lines_ > 0 && line_count_ >= max_lines_) {
        need_new_file = true;
    } else {
        int current_day = get_current_day_of_year();
        if (today_ != current_day) {
            need_new_file = true;
        }
    }

    if (need_new_file) {
        if (!create_new_log_file()) {
            // 如果无法创建文件，则回退到标准输出
            std::fwrite(log.c_str(), 1, log.size(), stdout);
            return;
        }
    }

    if (file_) {
        size_t written = fwrite(log.c_str(), 1, log.size(), file_);
        if (written != log.size()) {
            std::cerr << "Write incomplete: " << written << " of " << log.size() << " bytes" << std::endl;
        }
        fflush(file_);
        ++line_count_;
    } else {
        // 兜底：输出到 stdout
        std::fwrite(log.c_str(), 1, log.size(), stdout);
    }
}

// 写入接口（带可变参数）
void Logger::write(Level level, const char* file, const char* func, int line,
                   const char* format, ...) {
    if (!initialized_) return;

    if (static_cast<int>(level) > static_cast<int>(current_level_.load())) {
        return;
    }

    char message[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    message[sizeof(message) - 1] = '\0';
    va_end(args);

    const char* level_str = nullptr;
    switch (level) {
        case Level::DEBUG: level_str = "DEBUG"; break;
        case Level::INFO:  level_str = "INFO";  break;
        case Level::WARN:  level_str = "WARN";  break;
        case Level::ERROR: level_str = "ERROR"; break;
        default:           level_str = "UNKNOWN"; break;
    }

    std::string log = get_formatted_time() + " [" + level_str + "] " +
                      "[" + file + ":" + func + ":" + std::to_string(line) + "] " +
                      message + "\n";

    if (async_) {
        if (log_queue_) {
            if (!log_queue_->push(log, 100)) {
                // 推送失败回退到同步写入（直接写到 stdout）
                std::fwrite(log.c_str(), 1, log.size(), stdout);
            }
        } else {
            sync_write(log);
        }
    } else {
        sync_write(log);
    }
}

// 异步线程函数：从队列中弹出并同步写入
void Logger::async_write_thread() {
    std::string log;
    while (!shutdown_requested_) {
        if (log_queue_ && log_queue_->pop(log, 100)) {
            sync_write(log);
        }
    }

    // 退出前处理残留日志
    if (log_queue_) {
        while (log_queue_->pop(log, 0)) {
            sync_write(log);
        }
    }
}

// 设置/获取日志级别
void Logger::set_level(Level level) {
    current_level_ = level;
}
Logger::Level Logger::get_level() const {
    return current_level_.load();
}

// 刷新缓冲
void Logger::flush() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_) fflush(file_);
}

// 初始化 logger
bool Logger::initialize(const Config& config) {
    if (initialized_) {
        std::cerr << "Logger already initialized" << std::endl;
        return false;
    }

    current_level_ = config.level;
    max_lines_ = config.max_lines;
    buffer_size_ = config.buffer_size;
    file_name_ = config.filename;

    // 分割目录与文件名
    size_t sep_pos = file_name_.find_last_of("/\\");
    if (sep_pos != std::string::npos) {
        dir_name_ = file_name_.substr(0, sep_pos);
        file_name_ = file_name_.substr(sep_pos + 1);
    }

    buffer_ = std::make_unique<char[]>(buffer_size_);

    // 异步模式初始化
    if (config.async && config.queue_capacity > 0) {
        async_ = true;
        log_queue_ = std::make_unique<LogQueue<std::string>>(config.queue_capacity);
        try {
            async_thread_ = std::make_unique<std::thread>(&Logger::async_write_thread, this);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create async thread: " << e.what() << std::endl;
            async_ = false;
            log_queue_.reset();
        }
    }

    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (!create_new_log_file()) {
            std::cerr << "Failed to create initial log file" << std::endl;
            return false;
        }
    }

    initialized_ = true;
    return true;
}

// 关闭 logger，等待异步线程并释放资源
void Logger::shutdown() {
    if (!initialized_) return;

    shutdown_requested_ = true;

    if (async_thread_ && async_thread_->joinable()) {
        if (log_queue_) {
            log_queue_->notify_all();

            int wait_count = 0;
            while (wait_count < 100 && log_queue_ && !log_queue_->empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ++wait_count;
            }
        }

        async_thread_->join();
        async_thread_.reset();
    }

    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_) {
            fflush(file_);
            fclose(file_);
            file_ = nullptr;
        }
    }

    log_queue_.reset();
    buffer_.reset();
    initialized_ = false;
    shutdown_requested_ = false;
}

} // namespace logger
