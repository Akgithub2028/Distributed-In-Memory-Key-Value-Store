#pragma once

#include "protocol/RESP.h"
#include <string>
#include <fstream>
#include <mutex>

namespace kvstore {
namespace storage {

class AOFLogger {
public:
    explicit AOFLogger(const std::string& filepath);
    ~AOFLogger();

    // Logs a command to the AOF file. The string must be a fully serialized RESP array.
    void log(const std::string& serialized_command);

    // Flushes internal buffers to disk
    void flush();
    
    // Returns the filepath
    std::string get_filepath() const { return filepath_; }

    // Returns current size of AOF file
    size_t get_file_size();

    // Log raw string directly
    void log_raw(const std::string& data);

private:
    std::string filepath_;
    std::ofstream stream_;
    std::mutex mutex_;
};

} // namespace storage
} // namespace kvstore
