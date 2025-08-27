#pragma once
#include <regex>
#include <stdexcept>
#include <cstdint>
using namespace std;

bool parse_entry_id(const std::string& id, uint64_t& ms_time, uint64_t& seq_num);
bool is_id_greater(uint64_t new_ms, uint64_t new_seq, uint64_t last_ms, uint64_t last_seq);