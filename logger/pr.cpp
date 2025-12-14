#include "pr.hpp"
#include <sstream>

namespace logger {

LogLevel g_pr_level = LogLevel::INFO;

void pr_set_level(LogLevel level) {
    g_pr_level = level;
}

LogLevel pr_get_level() {
    return g_pr_level;
}

std::string thread_id_to_string() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

uint64_t thread_id_to_uint64() {
    std::string tid_str = thread_id_to_string();
    return std::stoull(tid_str);
}

} // namespace logger