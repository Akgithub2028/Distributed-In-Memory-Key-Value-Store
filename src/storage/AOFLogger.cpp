#include "storage/AOFLogger.h"

namespace kvstore {
namespace storage {

AOFLogger::AOFLogger(const std::string& filepath) : filepath_(filepath) {
    // Open in append mode, create if doesn't exist
    stream_.open(filepath_, std::ios::app | std::ios::binary);
    if (!stream_.is_open()) {
        // In a real system, handle this gracefully
        throw std::runtime_error("Failed to open AOF file: " + filepath_);
    }
}

AOFLogger::~AOFLogger() {
    if (stream_.is_open()) {
        flush(); // Final flush on shutdown
        stream_.close();
    }
}

void AOFLogger::log(const std::string& serialized_command) {
    if (!stream_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    stream_.write(serialized_command.data(), serialized_command.size());
    stream_.flush();
}

void AOFLogger::log_raw(const std::string& data) {
    if (!stream_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    stream_.write(data.data(), data.size());
    stream_.flush();
}

void AOFLogger::flush() {
    if (!stream_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    stream_.flush();
}

size_t AOFLogger::get_file_size() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_.is_open()) return 0;
    return stream_.tellp();
}

} // namespace storage
} // namespace kvstore
