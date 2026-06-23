#pragma once

#include <string>

namespace kvstore {
namespace storage {

struct DictEntry {
    std::string value;
    uint64_t expire_at_ms{0}; 
    uint64_t last_access_ms{0};
    // Extensibility placeholders for later phases:
    // uint8_t type_tag{0}; // e.g. 0=string, 1=hash, etc.
};

} // namespace storage
} // namespace kvstore
