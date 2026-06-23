#include "storage/StorageEngine.h"
#include <functional>
#include <charconv>
#include <mutex>
#include <chrono>
#include <random>

namespace kvstore {
namespace storage {

static uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static size_t entry_cost(const std::string& key, const std::string& value) {
    return key.capacity() + value.capacity() + sizeof(DictEntry) + 32; // 32 bytes approx unordered_map node overhead
}

StorageEngine::StorageEngine(size_t num_shards) : shards_(num_shards) {
    sweeper_thread_ = std::thread(&StorageEngine::background_sweeper, this);
}

StorageEngine::~StorageEngine() {
    running_ = false;
    if (sweeper_thread_.joinable()) {
        sweeper_thread_.join();
    }
}

void StorageEngine::set_max_memory(size_t bytes) {
    max_memory_bytes_ = bytes;
}

size_t StorageEngine::memory_usage() const {
    return total_memory_usage_.load(std::memory_order_relaxed);
}

size_t StorageEngine::get_shard_index(const std::string& key) const {
    std::hash<std::string> hasher;
    return hasher(key) % shards_.size();
}

size_t StorageEngine::evict_from_shard(Shard& shard, size_t needed_bytes) {
    size_t freed = 0;
    
    // We expect the shard.mutex to be locked via unique_lock by the caller (set function).
    // We sample 5 random keys, pick the one with oldest last_access_ms, and delete it.
    
    std::mt19937 rng(now_ms());

    while (freed < needed_bytes && !shard.dict.empty()) {
        size_t num_buckets = shard.dict.bucket_count();
        if (num_buckets == 0) break;

        std::uniform_int_distribution<size_t> dist(0, num_buckets - 1);
        
        auto best_it = shard.dict.end();
        uint64_t oldest_time = std::numeric_limits<uint64_t>::max();
        
        // Sample up to 5 elements
        for (int i = 0; i < 5; ++i) {
            size_t bucket_idx = dist(rng);
            if (shard.dict.bucket_size(bucket_idx) > 0) {
                auto local_it = shard.dict.begin(bucket_idx);
                // Convert local_iterator to iterator
                auto it = shard.dict.find(local_it->first);
                if (it != shard.dict.end() && it->second.last_access_ms < oldest_time) {
                    oldest_time = it->second.last_access_ms;
                    best_it = it;
                }
            }
        }
        
        if (best_it == shard.dict.end()) {
            // Fallback: just take the first element if random sampling missed
            best_it = shard.dict.begin();
        }
        
        size_t cost = entry_cost(best_it->first, best_it->second.value);
        freed += cost;
        total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
        shard.dict.erase(best_it);
        evicted_keys_.fetch_add(1, std::memory_order_relaxed);
    }
    
    return freed;
}

bool StorageEngine::set(const std::string& key, const std::string& value) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    
    size_t new_cost = entry_cost(key, value);
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    
    size_t old_cost = 0;
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        old_cost = entry_cost(key, it->second.value);
    }
    
    size_t current_mem = total_memory_usage_.load(std::memory_order_relaxed);
    size_t max_mem = max_memory_bytes_.load(std::memory_order_relaxed);
    
    if (max_mem > 0 && current_mem + new_cost > max_mem + old_cost) {
        size_t needed = (current_mem + new_cost) - (max_mem + old_cost);
        size_t freed = evict_from_shard(shard, needed);
        if (freed < needed) {
            // Cannot satisfy memory requirement
            return false;
        }
    }
    
    total_memory_usage_.fetch_add(new_cost - old_cost, std::memory_order_relaxed);
    
    auto [insert_it, inserted] = shard.dict.try_emplace(key, DictEntry{value, 0, now});
    if (!inserted) {
        insert_it->second.value = value;
        insert_it->second.expire_at_ms = 0;
        insert_it->second.last_access_ms = now;
    }
    return true;
}

std::optional<std::string> StorageEngine::get(const std::string& key) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex); // Upgraded to unique_lock for lazy delete and LRU update
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            expired_keys_.fetch_add(1, std::memory_order_relaxed);
            keyspace_misses_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        it->second.last_access_ms = now;
        keyspace_hits_.fetch_add(1, std::memory_order_relaxed);
        return it->second.value;
    }
    keyspace_misses_.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
}

int StorageEngine::del(const std::vector<std::string>& keys) {
    int deleted = 0;
    for (const auto& key : keys) {
        size_t idx = get_shard_index(key);
        auto& shard = shards_[idx];
        
        std::unique_lock lock(shard.mutex);
        auto it = shard.dict.find(key);
        if (it != shard.dict.end()) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            deleted++;
        }
    }
    return deleted;
}

int StorageEngine::exists(const std::vector<std::string>& keys) {
    int count = 0;
    uint64_t now = now_ms();
    for (const auto& key : keys) {
        size_t idx = get_shard_index(key);
        auto& shard = shards_[idx];
        
        std::unique_lock lock(shard.mutex);
        auto it = shard.dict.find(key);
        if (it != shard.dict.end()) {
            if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
                size_t cost = entry_cost(key, it->second.value);
                total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
                shard.dict.erase(it);
                expired_keys_.fetch_add(1, std::memory_order_relaxed);
                keyspace_misses_.fetch_add(1, std::memory_order_relaxed);
            } else {
                it->second.last_access_ms = now;
                keyspace_hits_.fetch_add(1, std::memory_order_relaxed);
                count++;
            }
        } else {
            keyspace_misses_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return count;
}

std::optional<int64_t> StorageEngine::incr(const std::string& key) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    
    int64_t val = 0;
    uint64_t expire_at = 0;
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            it = shard.dict.end(); // invalidate
        } else {
            const std::string& str_val = it->second.value;
            auto [ptr, ec] = std::from_chars(str_val.data(), str_val.data() + str_val.size(), val);
            if (ec != std::errc() || ptr != str_val.data() + str_val.size()) {
                return std::nullopt;
            }
            expire_at = it->second.expire_at_ms; // Retain TTL on INCR
        }
    }
    
    val++;
    std::string new_val_str = std::to_string(val);
    size_t new_cost = entry_cost(key, new_val_str);
    size_t old_cost = (it != shard.dict.end()) ? entry_cost(key, it->second.value) : 0;
    
    size_t current_mem = total_memory_usage_.load(std::memory_order_relaxed);
    size_t max_mem = max_memory_bytes_.load(std::memory_order_relaxed);
    
    if (max_mem > 0 && current_mem + new_cost > max_mem + old_cost) {
        size_t needed = (current_mem + new_cost) - (max_mem + old_cost);
        size_t freed = evict_from_shard(shard, needed);
        if (freed < needed) return std::nullopt; // OOM on INCR
    }
    
    total_memory_usage_.fetch_add(new_cost - old_cost, std::memory_order_relaxed);
    shard.dict[key] = DictEntry{new_val_str, expire_at, now};
    return val;
}

std::optional<int64_t> StorageEngine::decr(const std::string& key) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    
    int64_t val = 0;
    uint64_t expire_at = 0;
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            it = shard.dict.end();
        } else {
            const std::string& str_val = it->second.value;
            auto [ptr, ec] = std::from_chars(str_val.data(), str_val.data() + str_val.size(), val);
            if (ec != std::errc() || ptr != str_val.data() + str_val.size()) {
                return std::nullopt;
            }
            expire_at = it->second.expire_at_ms;
        }
    }
    
    val--;
    std::string new_val_str = std::to_string(val);
    size_t new_cost = entry_cost(key, new_val_str);
    size_t old_cost = (it != shard.dict.end()) ? entry_cost(key, it->second.value) : 0;
    
    size_t current_mem = total_memory_usage_.load(std::memory_order_relaxed);
    size_t max_mem = max_memory_bytes_.load(std::memory_order_relaxed);
    
    if (max_mem > 0 && current_mem + new_cost > max_mem + old_cost) {
        size_t needed = (current_mem + new_cost) - (max_mem + old_cost);
        size_t freed = evict_from_shard(shard, needed);
        if (freed < needed) return std::nullopt;
    }
    
    total_memory_usage_.fetch_add(new_cost - old_cost, std::memory_order_relaxed);
    shard.dict[key] = DictEntry{new_val_str, expire_at, now};
    return val;
}

bool StorageEngine::expire(const std::string& key, int64_t ms_offset) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            expired_keys_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        it->second.expire_at_ms = now + ms_offset;
        it->second.last_access_ms = now;
        return true;
    }
    return false;
}

bool StorageEngine::expire_at(const std::string& key, uint64_t absolute_ms) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            expired_keys_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        it->second.expire_at_ms = absolute_ms;
        it->second.last_access_ms = now;
        return true;
    }
    return false;
}

int64_t StorageEngine::pttl(const std::string& key) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0) {
            if (it->second.expire_at_ms <= now) {
                size_t cost = entry_cost(key, it->second.value);
                total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
                shard.dict.erase(it);
                return -2;
            }
            return static_cast<int64_t>(it->second.expire_at_ms - now);
        }
        return -1; // Exists but no TTL
    }
    return -2; // Does not exist
}

bool StorageEngine::persist(const std::string& key) {
    size_t idx = get_shard_index(key);
    auto& shard = shards_[idx];
    uint64_t now = now_ms();
    
    std::unique_lock lock(shard.mutex);
    auto it = shard.dict.find(key);
    if (it != shard.dict.end()) {
        if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
            size_t cost = entry_cost(key, it->second.value);
            total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
            shard.dict.erase(it);
            return false;
        }
        if (it->second.expire_at_ms == 0) {
            return false; // Already persistent
        }
        it->second.expire_at_ms = 0;
        it->second.last_access_ms = now;
        return true;
    }
    return false;
}

void StorageEngine::background_sweeper() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!running_) break;

        uint64_t now = now_ms();
        for (auto& shard : shards_) {
            std::unique_lock lock(shard.mutex, std::try_to_lock);
            if (!lock.owns_lock()) continue;
            
            int checked = 0;
            for (auto it = shard.dict.begin(); it != shard.dict.end() && checked < 20; ) {
                if (it->second.expire_at_ms > 0 && it->second.expire_at_ms <= now) {
                    size_t cost = entry_cost(it->first, it->second.value);
                    total_memory_usage_.fetch_sub(cost, std::memory_order_relaxed);
                    it = shard.dict.erase(it);
                    expired_keys_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++it;
                }
                checked++;
            }
        }
    }
}

} // namespace storage
} // namespace kvstore
