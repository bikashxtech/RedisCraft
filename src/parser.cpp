#include "parser.hpp"
#include <cctype>

std::string to_lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string parse_bulk_string(const char* resp, size_t& pos) {
    if (resp[pos] != '$') return "";
    pos++;
    long long length = 0;
    bool neg = false;
    if (resp[pos] == '-') { neg = true; pos++; }
    while (resp[pos] >= '0' && resp[pos] <= '9') {
        length = length * 10 + (resp[pos++] - '0');
    }
    if (neg) length = -length;

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return "";
    pos += 2;

    if (length == -1) return ""; // Null bulk string

    std::string result(&resp[pos], static_cast<size_t>(length));
    pos += static_cast<size_t>(length);

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return "";
    pos += 2;
    return result;
}

std::vector<std::string> parse_resp_array(const char* resp) {
    size_t pos = 0;
    if (resp[pos] != '*') return {};
    pos++;
    long long count = 0;
    bool neg = false;
    if (resp[pos] == '-') { neg = true; pos++; }
    while (resp[pos] >= '0' && resp[pos] <= '9') {
        count = count * 10 + (resp[pos++] - '0');
    }
    if (neg) count = -count;

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return {};
    pos += 2;

    if (count <= 0) return {};

    std::vector<std::string> parts;
    parts.reserve(static_cast<size_t>(count));
    for (long long i = 0; i < count; i++) {
        std::string bulk = parse_bulk_string(resp, pos);
        if (bulk.empty()) return {};
        parts.push_back(bulk);
    }
    return parts;
}

std::string resp_bulk_string(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string resp_array(const std::vector<std::string>& elems) {
    std::string out = "*" + std::to_string(elems.size()) + "\r\n";
    for (const auto& e : elems) {
        out += resp_bulk_string(e);
    }
    return out;
}
