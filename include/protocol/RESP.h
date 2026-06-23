#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace kvstore {
namespace protocol {

enum class RESPType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Null
};

struct RESPValue {
    RESPType type;
    std::string string_value;
    int64_t integer_value{0};
    std::vector<RESPValue> array_value;

    static RESPValue create_simple_string(const std::string& s) { return {RESPType::SimpleString, s, 0, {}}; }
    static RESPValue create_error(const std::string& e) { return {RESPType::Error, e, 0, {}}; }
    static RESPValue create_integer(int64_t i) { return {RESPType::Integer, "", i, {}}; }
    static RESPValue create_bulk_string(const std::string& s) { return {RESPType::BulkString, s, 0, {}}; }
    static RESPValue create_array(const std::vector<RESPValue>& arr) { return {RESPType::Array, "", 0, arr}; }
    static RESPValue create_null() { return {RESPType::Null, "", 0, {}}; }
    
    std::string serialize() const;
};

class RESPParser {
public:
    // Parses a buffer. If a complete RESP value is found, returns it and updates 'consumed'.
    // If the buffer doesn't contain a complete frame, returns std::nullopt.
    static std::optional<RESPValue> parse(const std::string& buffer, size_t& consumed);

private:
    static std::optional<RESPValue> parse_internal(const std::string& buffer, size_t& pos);
    static std::optional<std::string> read_line(const std::string& buffer, size_t& pos);
};

} // namespace protocol
} // namespace kvstore
