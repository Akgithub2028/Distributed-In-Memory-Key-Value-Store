#include "server/CommandDispatcher.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include "server/Metrics.h"

namespace kvstore {
namespace server {

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

static uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static bool is_mutating_command(const std::string& cmd) {
    return cmd == "SET" || cmd == "DEL" || cmd == "INCR" || cmd == "DECR" || 
           cmd == "EXPIRE" || cmd == "PEXPIRE" || cmd == "PEXPIREAT" || cmd == "PERSIST";
}

DispatchResult CommandDispatcher::dispatch(const protocol::RESPValue& request, storage::StorageEngine& storage, storage::AOFLogger* aof, bool is_read_only, bool bypass_readonly) {
    using namespace protocol;
    
    Metrics::commands_processed.fetch_add(1, std::memory_order_relaxed);
    
    if (request.type != RESPType::Array || request.array_value.empty()) {
        return {RESPValue::create_error("ERR invalid request type, expected array of bulk strings"), false, false, 0};
    }

    const auto& cmd_val = request.array_value[0];
    if (cmd_val.type != RESPType::BulkString && cmd_val.type != RESPType::SimpleString) {
        return {RESPValue::create_error("ERR invalid command format"), false, false, 0};
    }

    std::string cmd = to_upper(cmd_val.string_value);

    // Read-only check
    if (is_read_only && !bypass_readonly && is_mutating_command(cmd)) {
        return {RESPValue::create_error("READONLY You can't write against a read only replica"), false, false, 0};
    }

    if (cmd == "PING") {
        if (request.array_value.size() > 2) return {RESPValue::create_error("ERR wrong number of arguments for 'ping' command"), false, false, 0};
        if (request.array_value.size() == 2) return {RESPValue::create_bulk_string(request.array_value[1].string_value), false, false, 0};
        return {RESPValue::create_simple_string("PONG"), false, false, 0};
    } 
    else if (cmd == "ECHO") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'echo' command"), false, false, 0};
        return {RESPValue::create_bulk_string(request.array_value[1].string_value), false, false, 0};
    }
    else if (cmd == "QUIT") {
        if (request.array_value.size() != 1) return {RESPValue::create_error("ERR wrong number of arguments for 'quit' command"), false, false, 0};
        return {RESPValue::create_simple_string("OK"), true, false, 0};
    }
    else if (cmd == "SET") {
        if (request.array_value.size() < 3) return {RESPValue::create_error("ERR wrong number of arguments for 'set' command"), false, false, 0};
        const std::string& key = request.array_value[1].string_value;
        const std::string& value = request.array_value[2].string_value;
        if (!storage.set(key, value)) {
            return {RESPValue::create_error("OOM command not allowed when used memory > 'maxmemory'"), false, false, 0};
        }
        if (aof && !bypass_readonly) aof->log(request.serialize());
        return {RESPValue::create_simple_string("OK"), false, false, 0};
    }
    else if (cmd == "GET") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'get' command"), false, false, 0};
        auto val_opt = storage.get(request.array_value[1].string_value);
        if (val_opt) return {RESPValue::create_bulk_string(val_opt.value()), false, false, 0};
        return {RESPValue::create_null(), false, false, 0};
    }
    else if (cmd == "DEL") {
        if (request.array_value.size() < 2) return {RESPValue::create_error("ERR wrong number of arguments for 'del' command"), false, false, 0};
        std::vector<std::string> keys;
        for (size_t i = 1; i < request.array_value.size(); ++i) {
            keys.push_back(request.array_value[i].string_value);
        }
        int deleted = storage.del(keys);
        if (aof && !bypass_readonly && deleted > 0) aof->log(request.serialize());
        return {RESPValue::create_integer(deleted), false, false, 0};
    }
    else if (cmd == "EXISTS") {
        if (request.array_value.size() < 2) return {RESPValue::create_error("ERR wrong number of arguments for 'exists' command"), false, false, 0};
        std::vector<std::string> keys;
        for (size_t i = 1; i < request.array_value.size(); ++i) {
            keys.push_back(request.array_value[i].string_value);
        }
        return {RESPValue::create_integer(storage.exists(keys)), false, false, 0};
    }
    else if (cmd == "INCR") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'incr' command"), false, false, 0};
        auto res = storage.incr(request.array_value[1].string_value);
        if (res) {
            if (aof && !bypass_readonly) aof->log(request.serialize());
            return {RESPValue::create_integer(res.value()), false, false, 0};
        }
        return {RESPValue::create_error("ERR value is not an integer or out of range"), false, false, 0};
    }
    else if (cmd == "DECR") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'decr' command"), false, false, 0};
        auto res = storage.decr(request.array_value[1].string_value);
        if (res) {
            if (aof && !bypass_readonly) aof->log(request.serialize());
            return {RESPValue::create_integer(res.value()), false, false, 0};
        }
        return {RESPValue::create_error("ERR value is not an integer or out of range"), false, false, 0};
    }
    else if (cmd == "EXPIRE" || cmd == "PEXPIRE") {
        if (request.array_value.size() != 3) return {RESPValue::create_error("ERR wrong number of arguments for '" + cmd + "' command"), false, false, 0};
        const std::string& key = request.array_value[1].string_value;
        const std::string& time_str = request.array_value[2].string_value;
        
        int64_t time_val;
        auto [ptr, ec] = std::from_chars(time_str.data(), time_str.data() + time_str.size(), time_val);
        if (ec != std::errc()) return {RESPValue::create_error("ERR value is not an integer or out of range"), false, false, 0};
        
        int64_t ms_offset = (cmd == "EXPIRE") ? time_val * 1000 : time_val;
        bool res = storage.expire(key, ms_offset);
        
        if (aof && !bypass_readonly && res) {
            uint64_t absolute_ms = now_ms() + ms_offset;
            RESPValue pexpireat_cmd;
            pexpireat_cmd.type = RESPType::Array;
            pexpireat_cmd.array_value.push_back(RESPValue::create_bulk_string("PEXPIREAT"));
            pexpireat_cmd.array_value.push_back(RESPValue::create_bulk_string(key));
            pexpireat_cmd.array_value.push_back(RESPValue::create_bulk_string(std::to_string(absolute_ms)));
            aof->log(pexpireat_cmd.serialize());
        }
        
        return {RESPValue::create_integer(res ? 1 : 0), false, false, 0};
    }
    else if (cmd == "PEXPIREAT") {
        if (request.array_value.size() != 3) return {RESPValue::create_error("ERR wrong number of arguments for 'pexpireat' command"), false, false, 0};
        const std::string& key = request.array_value[1].string_value;
        const std::string& time_str = request.array_value[2].string_value;
        
        uint64_t abs_ms;
        auto [ptr, ec] = std::from_chars(time_str.data(), time_str.data() + time_str.size(), abs_ms);
        if (ec != std::errc()) return {RESPValue::create_error("ERR value is not an integer or out of range"), false, false, 0};
        
        bool res = storage.expire_at(key, abs_ms);
        if (aof && !bypass_readonly && res) aof->log(request.serialize());
        return {RESPValue::create_integer(res ? 1 : 0), false, false, 0};
    }
    else if (cmd == "TTL" || cmd == "PTTL") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for '" + cmd + "' command"), false, false, 0};
        const std::string& key = request.array_value[1].string_value;
        
        int64_t ms_left = storage.pttl(key);
        if (ms_left < 0) {
            return {RESPValue::create_integer(ms_left), false, false, 0};
        }
        
        int64_t ret = (cmd == "TTL") ? ms_left / 1000 : ms_left;
        return {RESPValue::create_integer(ret), false, false, 0};
    }
    else if (cmd == "PERSIST") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'persist' command"), false, false, 0};
        const std::string& key = request.array_value[1].string_value;
        bool res = storage.persist(key);
        if (aof && !bypass_readonly && res) aof->log(request.serialize());
        return {RESPValue::create_integer(res ? 1 : 0), false, false, 0};
    }
    else if (cmd == "INFO") {
        if (request.array_value.size() > 2) return {RESPValue::create_error("ERR wrong number of arguments for 'info' command"), false, false, 0};
        std::string section = "all";
        if (request.array_value.size() == 2) {
            section = to_upper(request.array_value[1].string_value);
        }
        
        std::string info_str = "";
        if (section == "ALL" || section == "SERVER") {
            info_str += "# Server\r\n";
            info_str += "redis_mode:standalone\r\n";
        }
        if (section == "ALL" || section == "CLIENTS") {
            info_str += "# Clients\r\n";
            info_str += "connected_clients:" + std::to_string(Metrics::connected_clients.load(std::memory_order_relaxed)) + "\r\n";
        }
        if (section == "ALL" || section == "MEMORY") {
            info_str += "# Memory\r\n";
            info_str += "used_memory:" + std::to_string(storage.memory_usage()) + "\r\n";
            info_str += "maxmemory:" + std::to_string(storage.get_max_memory()) + "\r\n";
        }
        if (section == "ALL" || section == "PERSISTENCE") {
            info_str += "# Persistence\r\n";
            info_str += "aof_enabled:1\r\n";
            if (aof) info_str += "aof_current_size:" + std::to_string(aof->get_file_size()) + "\r\n";
        }
        if (section == "ALL" || section == "STATS") {
            info_str += "# Stats\r\n";
            info_str += "total_commands_processed:" + std::to_string(Metrics::commands_processed.load(std::memory_order_relaxed)) + "\r\n";
            info_str += "keyspace_hits:" + std::to_string(storage.get_keyspace_hits()) + "\r\n";
            info_str += "keyspace_misses:" + std::to_string(storage.get_keyspace_misses()) + "\r\n";
            info_str += "evicted_keys:" + std::to_string(storage.get_evicted_keys()) + "\r\n";
            info_str += "expired_keys:" + std::to_string(storage.get_expired_keys()) + "\r\n";
        }
        if (section == "ALL" || section == "REPLICATION") {
            info_str += "# Replication\r\n";
            info_str += "role:" + std::string(is_read_only ? "slave" : "master") + "\r\n";
            if (aof) info_str += "master_repl_offset:" + std::to_string(aof->get_file_size()) + "\r\n";
        }
        return {RESPValue::create_bulk_string(info_str), false, false, 0};
    }
    else if (cmd == "CONFIG") {
        if (request.array_value.size() != 4 || to_upper(request.array_value[1].string_value) != "SET") {
            return {RESPValue::create_error("ERR unsupported config command"), false, false, 0};
        }
        std::string param = to_upper(request.array_value[2].string_value);
        if (param == "MAXMEMORY") {
            int64_t val;
            const std::string& vstr = request.array_value[3].string_value;
            auto [ptr, ec] = std::from_chars(vstr.data(), vstr.data() + vstr.size(), val);
            if (ec == std::errc()) {
                storage.set_max_memory(val);
                return {RESPValue::create_simple_string("OK"), false, false, 0};
            }
            return {RESPValue::create_error("ERR invalid maxmemory value"), false, false, 0};
        }
        return {RESPValue::create_error("ERR unsupported config parameter"), false, false, 0};
    }
    else if (cmd == "PSYNC") {
        if (request.array_value.size() != 2) return {RESPValue::create_error("ERR wrong number of arguments for 'psync' command"), false, false, 0};
        const std::string& offset_str = request.array_value[1].string_value;
        size_t offset = 0;
        auto [ptr, ec] = std::from_chars(offset_str.data(), offset_str.data() + offset_str.size(), offset);
        if (ec != std::errc()) return {RESPValue::create_error("ERR invalid offset"), false, false, 0};
        
        return {RESPValue::create_simple_string("CONTINUE"), false, true, offset};
    }

    return {RESPValue::create_error("ERR unknown command '" + cmd + "'"), false, false, 0};
}

} // namespace server
} // namespace kvstore
