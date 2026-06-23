#include "server/Metrics.h"

namespace kvstore {
namespace server {

std::atomic<uint64_t> Metrics::commands_processed{0};
std::atomic<uint32_t> Metrics::connected_clients{0};

} // namespace server
} // namespace kvstore
