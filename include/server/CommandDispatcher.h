#pragma once

#include "protocol/RESP.h"
#include "storage/StorageEngine.h"
#include "storage/AOFLogger.h"

namespace kvstore {
namespace server {

struct DispatchResult {
    protocol::RESPValue response;
    bool close_connection{false};
    bool is_psync{false};
    size_t psync_offset{0};
};

class CommandDispatcher {
public:
    static DispatchResult dispatch(const protocol::RESPValue& request, storage::StorageEngine& storage, storage::AOFLogger* aof = nullptr, bool is_read_only = false, bool bypass_readonly = false);
};

} // namespace server
} // namespace kvstore
