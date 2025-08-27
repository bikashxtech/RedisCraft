#include "StreamIDVerifier.hpp"

bool parse_entry_id(const std::string& id, uint64_t& ms_time, uint64_t& seq_num, bool& seq_wildcard, bool& full_wildcard) {
    static std::regex id_regex(R"(^(\d+)-(\*|\d+)$)");
    seq_wildcard = false;
    full_wildcard = false;

    if (id == "*") {
        full_wildcard = true;
        ms_time = seq_num = UINT64_MAX; // mark full wildcard internally
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

// Get current unix time in milliseconds
uint64_t current_unix_time_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}