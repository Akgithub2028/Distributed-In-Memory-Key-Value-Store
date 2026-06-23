#include "net/Connection.h"
#include <unistd.h>
#include <sys/socket.h>

namespace kvstore {
namespace net {

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() {
    close();
}

Connection::Connection(Connection&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int Connection::read_bytes(char* buffer, size_t max_bytes) {
    if (fd_ < 0) return -1;
    return ::read(fd_, buffer, max_bytes);
}

bool Connection::write_bytes(const std::string& data) {
    if (fd_ < 0) return false;
    
    size_t total_written = 0;
    while (total_written < data.size()) {
        ssize_t bytes_written = ::write(fd_, data.data() + total_written, data.size() - total_written);
        if (bytes_written <= 0) {
            return false;
        }
        total_written += bytes_written;
    }
    return true;
}

void Connection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace net
} // namespace kvstore
