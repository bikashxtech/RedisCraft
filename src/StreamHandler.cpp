#include "StreamHandler.hpp"

bool parse_entry_id(const std::string& id, uint64_t& ms_time, uint64_t& seq_num, bool& seq_wildcard, bool& full_wildcard) {
    static std::regex id_regex(R"(^(\d+)-(\*|\d+)$)");
    seq_wildcard = false;
    full_wildcard = false;

    if (id == "*") {
        full_wildcard = true;
        ms_time = seq_num = UINT64_MAX; 
        return true;
    }

    std::smatch match;
    if (!std::regex_match(id, match, id_regex)) return false;

    try {
        ms_time = std::stoull(match[1].str());
        if (match[2] == "*") {
            seq_wildcard = true;
            seq_num = UINT64_MAX;
        } else {
            seq_num = std::stoull(match[2].str());
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool is_id_greater(uint64_t new_ms, uint64_t new_seq, uint64_t last_ms, uint64_t last_seq) {
    if (new_ms > last_ms) return true;
    if (new_ms == last_ms && new_seq > last_seq) return true;
    return false;
}


uint64_t current_unix_time_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string encode_xrange_response(const std::vector<std::pair<std::string, StreamEntry>>& entries) {
    std::string resp = "*" + std::to_string(entries.size()) + "\r\n";
    for (const auto& [entry_id, kvs] : entries) {
        resp += "*2\r\n";                   
        resp += resp_bulk_string(entry_id);  

        std::vector<std::string> kv_list;
        for (const auto& [k, v] : kvs) {
            kv_list.push_back(k);
            kv_list.push_back(v);
        }
        resp += resp_array(kv_list); 
    }
    return resp;
}

bool parse_range_id(const std::string& id, uint64_t& ms_time, uint64_t& seq_num) {
    if (id == "-") {
        ms_time = 0;
        seq_num = 0;
        return true;
    }
    if (id == "+") {
        ms_time = UINT64_MAX;
        seq_num = UINT64_MAX;
        return true;
    }
    if (id == "$") {
        
        ms_time = UINT64_MAX - 1; 
        seq_num = UINT64_MAX - 1;
        return true;
    }
    static std::regex re(R"(^(\d+)(?:-(\d+))?$)");
    std::smatch match;
    if (!std::regex_match(id, match, re)) return false;
    try {
        ms_time = std::stoull(match[1].str());
        if (match[2].matched) {
            seq_num = std::stoull(match[2].str());
        } else {
            seq_num = 0;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool id_less_equal(uint64_t a_ms, uint64_t a_seq, uint64_t b_ms, uint64_t b_seq) {
    return (a_ms < b_ms) || (a_ms == b_ms && a_seq <= b_seq);
}