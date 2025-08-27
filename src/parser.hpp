#pragma once
#include <string>
#include <vector>

// RESP helpers
std::string parse_bulk_string(const char* resp, size_t& pos);
std::vector<std::string> parse_resp_array(const char* resp);

// Convenience
std::string to_lower(std::string s);