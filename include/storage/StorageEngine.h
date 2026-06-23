#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <thread>
#include <atomic>
#include "storage/DictEntry.h"

namespace kvstore {
namespace storage {

class StorageEngine {
public:
    explicit StorageEngine(size_t num_shards = 16);
    ~StorageEngine();
    
    // Memory configuration
    void set_max_memory(size_t bytes);
    size_t memory_usage() const;
    size_t get_max_memory() const { return max_memory_bytes_.load(std::memory_order_relaxed); }

    // Stats Getters
    uint64_t get_keyspace_hits() const { return keyspace_hits_.load(std::memory_order_relaxed); }
    uint64_t get_keyspace_misses() const { return keyspace_misses_.load(std::memory_order_relaxed); }
    uint64_t get_evicted_keys() const { return evicted_keys_.load(std::memory_order_relaxed); }
    uint64_t get_expired_keys() const { return expired_keys_.load(std::memory_order_relaxed); }

    // Core String Operations
    // Returns false if out of memory
    bool set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    
    // Multi-key Operations
    int del(const std::vector<std::string>& keys);
    int exists(const std::vector<std::string>& keys);
    
    // Numeric Operations
    // Returns new value on success, std::nullopt if value is not an integer
    std::optional<int64_t> incr(const std::string& key);
    std::optional<int64_t> decr(const std::string& key);

    // TTL Operations
    bool expire(const std::string& key, int64_t ms_offset);
    bool expire_at(const std::string& key, uint64_t absolute_ms);
    int64_t pttl(const std::string& key);
    bool persist(const std::string& key);

private:
    struct Shard {
        std::unordered_map<std::string, DictEntry> dict;
        std::shared_mutex mutex;
    };

    size_t get_shard_index(const std::string& key) const;
    void background_sweeper();
    
    // Attempts to free `needed_bytes` from `shard` using approximate LRU. Returns bytes freed.
    size_t evict_from_shard(Shard& shard, size_t needed_bytes);
    
    std::vector<Shard> shards_;
    std::thread sweeper_thread_;
    std::atomic<bool> running_{true};
    std::atomic<size_t> total_memory_usage_{0};
    std::atomic<size_t> max_memory_bytes_{0}; // 0 = unlimited

    // Telemetry
    std::atomic<uint64_t> keyspace_hits_{0};
    std::atomic<uint64_t> keyspace_misses_{0};
    std::atomic<uint64_t> evicted_keys_{0};
    std::atomic<uint64_t> expired_keys_{0};
};

} // namespace storage
} // namespace kvstore
