#include "util/Logger.h"
#include <chrono>
#include <iomanip>

namespace kvstore {
namespace util {

std::mutex Logger::mutex_;

static std::string current_time() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %X", std::localtime(&in_time_t));
    return std::string(buf);
}

void Logger::info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[" << current_time() << "] [INFO] " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << "[" << current_time() << "] [ERROR] " << msg << std::endl;
}

} // namespace util
} // namespace kvstore
