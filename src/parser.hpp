#pragma once
#include <string>
#include <vector>

std::string parse_bulk_string(const char* resp, size_t& pos);
std::vector<std::string> parse_resp_array(const char* resp);
std::string resp_bulk_string(const std::string& s);
std::string resp_array(const std::vector<std::string>& elems);

std::string to_lower(std::string s);