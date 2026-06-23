#include "protocol/RESP.h"
#include <charconv>

namespace kvstore {
namespace protocol {

std::string RESPValue::serialize() const {
    std::string out;
    switch (type) {
        case RESPType::SimpleString:
            out += "+" + string_value + "\r\n";
            break;
        case RESPType::Error:
            out += "-" + string_value + "\r\n";
            break;
        case RESPType::Integer:
            out += ":" + std::to_string(integer_value) + "\r\n";
            break;
        case RESPType::BulkString:
            out += "$" + std::to_string(string_value.size()) + "\r\n" + string_value + "\r\n";
            break;
        case RESPType::Array:
            out += "*" + std::to_string(array_value.size()) + "\r\n";
            for (const auto& item : array_value) {
                out += item.serialize();
            }
            break;
        case RESPType::Null:
            out += "$-1\r\n";
            break;
    }
    return out;
}

std::optional<std::string> RESPParser::read_line(const std::string& buffer, size_t& pos) {
    size_t crlf = buffer.find("\r\n", pos);
    if (crlf == std::string::npos) {
        return std::nullopt;
    }
    std::string line = buffer.substr(pos, crlf - pos);
    pos = crlf + 2;
    return line;
}

std::optional<RESPValue> RESPParser::parse_internal(const std::string& buffer, size_t& pos) {
    if (pos >= buffer.size()) return std::nullopt;

    char prefix = buffer[pos];
    size_t line_start_pos = pos;
    auto line_opt = read_line(buffer, pos);
    if (!line_opt) {
        pos = line_start_pos;
        return std::nullopt;
    }

    std::string line = line_opt.value();
    if (line.empty()) {
        pos = line_start_pos;
        return std::nullopt;
    }

    std::string content = line.substr(1);

    switch (prefix) {
        case '+':
            return RESPValue::create_simple_string(content);
        case '-':
            return RESPValue::create_error(content);
        case ':': {
            int64_t val;
            auto [ptr, ec] = std::from_chars(content.data(), content.data() + content.size(), val);
            if (ec == std::errc()) {
                return RESPValue::create_integer(val);
            }
            break;
        }
        case '$': {
            int64_t len;
            auto [ptr, ec] = std::from_chars(content.data(), content.data() + content.size(), len);
            if (ec != std::errc()) break;

            if (len == -1) {
                return RESPValue::create_null();
            }

            if (pos + len + 2 > buffer.size()) { // length + \r\n
                pos = line_start_pos;
                return std::nullopt;
            }

            std::string bulk_str = buffer.substr(pos, len);
            pos += len + 2; // skip the string and the trailing \r\n
            return RESPValue::create_bulk_string(bulk_str);
        }
        case '*': {
            int64_t count;
            auto [ptr, ec] = std::from_chars(content.data(), content.data() + content.size(), count);
            if (ec != std::errc()) break;

            if (count == -1) {
                return RESPValue::create_null();
            }

            std::vector<RESPValue> arr;
            arr.reserve(count);
            for (int64_t i = 0; i < count; ++i) {
                auto elem = parse_internal(buffer, pos);
                if (!elem) {
                    pos = line_start_pos;
                    return std::nullopt;
                }
                arr.push_back(elem.value());
            }
            return RESPValue::create_array(arr);
        }
    }
    
    pos = line_start_pos;
    return std::nullopt;
}

std::optional<RESPValue> RESPParser::parse(const std::string& buffer, size_t& consumed) {
    size_t pos = 0;
    auto val = parse_internal(buffer, pos);
    if (val) {
        consumed = pos;
    }
    return val;
}

} // namespace protocol
} // namespace kvstore
