#pragma once

#include <string>

namespace kvstore {
namespace net {

class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    // Disable copy
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    // Enable move
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    // Read up to max_bytes into the provided buffer
    // Returns number of bytes read. 0 means client disconnected. <0 means error.
    int read_bytes(char* buffer, size_t max_bytes);

    // Write exact number of bytes. Returns true if successful.
    bool write_bytes(const std::string& data);

    int fd() const { return fd_; }
    void close();

private:
    int fd_;
};

} // namespace net
} // namespace kvstore
