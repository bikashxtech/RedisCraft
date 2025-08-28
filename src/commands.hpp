#pragma once
#include <string>

// Command handlers
std::string handle_set(const char* resp);
std::string handle_get(const char* resp);
std::string handle_RPUSH(const char* resp);
std::string handle_LPUSH(const char* resp);
std::string handle_LPOP(const char* resp);
std::string handle_LRANGE(const char* resp);
std::string handle_LLEN(const char* resp);
std::string handle_BLPOP(const char* resp, int client_fd);
std::string handle_TYPE(const char* resp);
std::string handle_XADD(const char* resp);
std::string handle_XRANGE(const char* resp);
std::string handle_XREAD(const char* resp);

// Util for safe send
bool send_response(int fd, const std::string& response);
