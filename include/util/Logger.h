#pragma once

#include <string>
#include <mutex>
#include <iostream>

namespace kvstore {
namespace util {

class Logger {
public:
    static void info(const std::string& msg);
    static void error(const std::string& msg);

private:
    static std::mutex mutex_;
};

} // namespace util
} // namespace kvstore
