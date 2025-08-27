#pragma once
#include <regex>
#include <stdexcept>
#include <cstdint>
#include <chrono>
using namespace std;

bool parse_entry_id(const std::string& id, uint64_t& ms_time, uint64_t& seq_num, bool& seq_wildcard, bool& full_wildcard);
bool is_id_greater(uint64_t new_ms, uint64_t new_seq, uint64_t last_ms, uint64_t last_seq);
uint64_t current_unix_time_ms();