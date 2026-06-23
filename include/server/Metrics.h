#pragma once

#include <atomic>
#include <cstdint>

namespace kvstore {
namespace server {

struct Metrics {
    static std::atomic<uint64_t> commands_processed;
    static std::atomic<uint32_t> connected_clients;
};

} // namespace server
} // namespace kvstore
