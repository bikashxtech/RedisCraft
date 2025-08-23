#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <netdb.h>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct ValueWithExpiry {
    std::string value;
    TimePoint expiry;
};

std::unordered_map<std::string, ValueWithExpiry> redis_storage;
std::unordered_map<std::string, std::vector<std::string>> lists;
std::mutex storage_mutex;

void cleanup_expired_keys() {
    std::lock_guard<std::mutex> lock(storage_mutex);
    auto now = Clock::now();
    for (auto it = redis_storage.begin(); it != redis_storage.end(); ) {
        if (it -> second.expiry != TimePoint::min() && it->second.expiry <= now) {
            it = redis_storage.erase(it);
        } else {
            ++it;
        }
    }
}

void expiry_monitor() {
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cleanup_expired_keys();
    }
}

std::string parse_bulk_string(const char* resp, size_t& pos) {
    if(resp[pos] != '$') return "";

    pos++;
    int length = 0;

    while (resp[pos] >= '0' && resp[pos] <= '9') {
        length = length * 10 + (resp[pos++] - '0');
    }

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return "";

    pos += 2;

    std::string result(&resp[pos], length);
    pos += length;

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return "";
    pos += 2;

    return result;
}

std::vector<std::string> parse_resp_array(const char* resp) {
    size_t pos = 0;
    if (resp[pos] != '*') return {};
    pos++;

    int count = 0;
    while (resp[pos] >= '0' && resp[pos] <= '9') {
        count = count * 10 + (resp[pos++] - '0');
    }

    if (resp[pos] != '\r' || resp[pos + 1] != '\n') return {};
    pos += 2;

    std::vector<std::string> parts;
    for(int i = 0; i < count; i++) {
        std::string bulk = parse_bulk_string(resp, pos);
        if (bulk.empty()) return {};
        parts.push_back(bulk);
    }
    return parts;
}

std::string handle_set(const char* resp) {
  auto parts = parse_resp_array(resp);
  if(parts.size() < 3) {
      return "-ERR Invalid SET Command\r\n";
  }

  std::transform(parts[0].begin(), parts[0].end(), parts[0].begin(), ::tolower);
  if(parts[0] != "set") {
      return "-ERR Invalid SET Command\r\n";
  }

  std::string key = parts[1];
  std::string value = parts[2];
  TimePoint expiry = TimePoint::min();

  // Handle optional PX argument
  if(parts.size() == 5) {
      std::string option = parts[3];
      std::transform(option.begin(), option.end(), option.begin(), ::tolower);
      if(option == "px") {
          try {
              int ms = std::stoi(parts[4]);
              expiry = Clock::now() + std::chrono::milliseconds(ms);
          } catch(...) {
              return "-ERR Invalid PX value\r\n";
          }
      } else {
          return "-ERR Syntax error\r\n";
      }
  } else if(parts.size() != 3) {
      return "-ERR Syntax error\r\n";
  }

  {
      std::lock_guard<std::mutex> lock(storage_mutex);
      redis_storage[key] = {value, expiry};
  }

  return "+OK\r\n";
}

std::string handle_get(const char* resp) {
  auto parts = parse_resp_array(resp);
  if(parts.size() != 2) {
      return "-ERR Invalid GET command\r\n";
  }

  std::transform(parts[0].begin(), parts[0].end(), parts[0].begin(),
  [](unsigned char c){ return std::tolower(c); });
  if(parts[0] != "get") {
      return "-ERR Invalid GET command\r\n";
  }
  
  std::string key = parts[1];
  ValueWithExpiry val;
  
  {
      std::lock_guard<std::mutex> lock(storage_mutex);
      auto it = redis_storage.find(key);
      if(it == redis_storage.end()) {
          return "$-1\r\n";
      }
      val = it->second;
  }

  if(val.expiry != TimePoint::min() && Clock::now() >= val.expiry) {
      std::lock_guard<std::mutex> lock(storage_mutex);
      redis_storage.erase(key);
      return "$-1\r\n";
  }

  return "$" + std::to_string(val.value.size()) + "\r\n" + val.value + "\r\n";
}

std::string handle_RPUSH(const char* resp) {
    auto parts = parse_resp_array(resp);
    if(parts.size() < 3) {
        return "-ERR Invalid RPUSH Command\r\n";
    }

    std::transform(parts[0].begin(), parts[0].end(), parts[0].begin(), ::tolower);

    if (parts[0] != "rpush") {
        return "-ERR Invalid RPUSH Command\r\n";
    }

    auto listName = parts[1];

    std::lock_guard<std::mutex> lock(storage_mutex);
    auto& lst = lists[listName];
    
    for (size_t i = 2; i < parts.size(); ++i) lst.push_back(parts[i]);
    return ":" + std::to_string(lst.size()) + "\r\n";
}

std::string handle_LPUSH(const char* resp) {
  auto parts = parse_resp_array(resp);
  if(parts.size() < 3) {
      return "-ERR Invalid LPUSH Command\r\n";
  }

  std::transform(parts[0].begin(), parts[0].end(), parts[0].begin(), ::tolower);

  if (parts[0] != "lpush") {
      return "-ERR Invalid LPUSH Command\r\n";
  }

  auto listName = parts[1];

  std::lock_guard<std::mutex> lock(storage_mutex);
  auto& lst = lists[listName];
  
  for (size_t i = 2; i < parts.size(); ++i) lst.insert(lst.begin(), parts[i]);
  return ":" + std::to_string(lst.size()) + "\r\n";
}

std::string handle_LRANGE(const char* resp) {
  auto parts = parse_resp_array(resp);
  if (parts.size() != 4) {
      return "-ERR Invalid LRANGE Command\r\n";
  }

  std::transform(parts[0].begin(), parts[0].end(), parts[0].begin(),
  [](unsigned char c){ return std::tolower(c); });
  if (parts[0] != "lrange") {
      return "-ERR Invalid LRANGE Command\r\n";
  }

  const std::string& listName = parts[1];

  std::vector<std::string> snapshot;
  {
      std::lock_guard<std::mutex> lock(storage_mutex);
      auto it = lists.find(listName);
      if (it == lists.end()) return "*0\r\n";
      snapshot = it -> second;
  }

  int n = static_cast<int>(snapshot.size());
  int start, end;

  try{
      start = std::stoi(parts[2]);
      end = std::stoi(parts[3]);
  } catch (...) {
      return "-ERR Invalid LRANGE indices\r\n";
  }
  
  if (start < 0) start = n + start;
  if (end < 0) end = n + end;

  if (start < 0) start = 0;
  if (end >= n) end = n - 1;

  if (start > end || start >= n) return "*0\r\n";

  std::string res = "*" + std::to_string(end - start + 1) + "\r\n";

  for (int i = start; i <= end; i++) {
      const std::string& elem = snapshot[i];
      res += "$" + std::to_string(elem.size()) + "\r\n" + elem + "\r\n";
  }

  return res;
}

std::string handle_LLEN(const char* resp) {
    auto parts = parse_resp_array(resp);
    if(parts.size() != 2) {
        return "-ERR Invalid LLEN Command\r\n";
    }

    std::transform(parts[0].begin(),parts[0].end(), parts[0].begin(), [](unsigned char& c){
        return ::tolower(c);
    });

    if(parts[0] != "llen") {
        return "-ERR Invalid LLEN Command\r\n";
    }
    std::lock_guard<std::mutex> lock(storage_mutex);
    auto it = lists.find(parts[1]);

    if(it == lists.end()) return ":0\r\n";

    return ":" + std::to_string(it -> second.size()) + "\r\n";
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::thread(expiry_monitor).detach();
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  std::vector<pollfd> poll_fds;

  poll_fds.push_back({server_fd, POLLIN, 0});

  while (true) {
    int poll_count = poll(poll_fds.data(), poll_fds.size(), -1);
    if (poll_count < 0) {
      std::cerr <<"Poll failed\n";
      break;
    }

    if (poll_fds[0].revents & POLLIN) {
      sockaddr_in client_addr {};
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);

      if(client_fd >= 0) {
        std::cout << "New client connected: FD " << client_fd << std::endl;

        poll_fds.push_back({client_fd, POLLIN, 0});
      }
    }
    
    for(size_t i = 1; i < poll_fds.size();) {
      if (poll_fds[i].revents & POLLIN) {
        char buffer[1024];
        ssize_t bytes_read = read(poll_fds[i].fd, buffer, sizeof(buffer) - 1);
        if(bytes_read <= 0) {
          std::cout << "Client disconnected: FD " << poll_fds[i].fd << std::endl;
          close(poll_fds[i].fd);
          poll_fds.erase(poll_fds.begin() + i);
          continue;
        } else {
          buffer[bytes_read] = '\0';

          char* p = buffer;
          std::string cmd(buffer);

          if(*p != '*') {
            if(cmd.find("PING") != std::string::npos) {
              const char* pong = "+PONG\r\n";
              send(poll_fds[i].fd, pong, strlen(pong), 0);
            } else {
              const char* err = "-ERR unknown command\r\n";
              send(poll_fds[i].fd, err, strlen(err), 0);
            }
          } else {
            if (cmd.find("PING") != std::string::npos) {
              const char* pong = "+PONG\r\n";
              send(poll_fds[i].fd, pong, strlen(pong), 0);
            } else {
                int idx = cmd.find("ECHO");
                if (idx != std::string::npos) {
                    int arg_pos = cmd.find("\r\n", idx);
                    if (arg_pos != std::string::npos) {
                        arg_pos += 2;
                        if (cmd[arg_pos] == '$') {
                            int len_start = arg_pos + 1;
                            int len_end = cmd.find("\r\n", len_start);
                            if (len_end != std::string::npos) {
                                int len = std::stoi(cmd.substr(len_start, len_end - len_start));
                                int data_start = len_end + 2;
                                std::string message = cmd.substr(data_start, len);

                                std::string res = "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
                                send(poll_fds[i].fd, res.c_str(), res.size(), 0);
                            }
                        }
                    }
                } else {
                  if (cmd.find("SET") != std::string::npos) {
                      const char* res =  handle_set(p).c_str();
                      send(poll_fds[i].fd, res, strlen(res), 0);
                  } else if(cmd.find("GET") != std::string::npos) {
                      const char* res = handle_get(p).c_str();
                      send(poll_fds[i].fd, res, strlen(res), 0);
                  } else if(cmd.find("RPUSH") != std::string::npos) {
                      std::string res = handle_RPUSH(p);
                      send(poll_fds[i].fd, res.c_str(), res.size(), 0);
                  } else if(cmd.find("LRANGE") != std::string::npos) {
                      std::string res = handle_LRANGE(p);
                      send(poll_fds[i].fd, res.c_str(), res.size(), 0);
                  } else if(cmd.find("LPUSH") != std::string::npos) {
                      std::string res = handle_LPUSH(p);
                      send(poll_fds[i].fd, res.c_str(), res.size(), 0);
                  } else if(cmd.find("LLEN") != std::string::npos) {
                      std::string res = handle_LLEN(p);
                      send(poll_fds[i].fd, res.c_str(), res.size(), 0);
                  } else {
                      const char* err = "-ERR Invalid Unknown Command";
                      send(poll_fds[i].fd, err, strlen(err), 0);
                  }
                }
            }
            
            
           } 
          }
        }
        i++;
      }

    }
  for(auto &pfd : poll_fds) close(pfd.fd);
  close(server_fd);
  
   return 0;
}