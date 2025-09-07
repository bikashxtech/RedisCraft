#pragma once
#include <string>
#include <thread>


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
std::string handle_XREAD(const char* resp, int fd = -1);
std::string handle_INCR(const char* resp);
std::string handle_MULTI(const char* resp, int client_fd); 
std::string handle_EXEC(const char* resp, int client_fd);
std::string handle_SAVE(const char* resp);
std::string handle_BGSAVE(const char* resp);

bool send_response(int fd, const std::string& response);
