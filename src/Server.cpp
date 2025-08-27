#include "parser.hpp"
#include "commands.hpp"
#include "storage.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <thread>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <netdb.h>

static std::string dispatch(const std::string& cmd, int fd) {
    // Fast-path for inline PING/ECHO when resp parsing is not used
    if (!cmd.empty() && cmd[0] != '*') {
        if (cmd.find("PING") != std::string::npos) return "+PONG\r\n";
        return "-ERR unknown command\r\n";
    }

    // RESP array
    auto parts = parse_resp_array(cmd.c_str());
    if (parts.empty()) return "-ERR Protocol error\r\n";
    std::string op = to_lower(parts[0]);

    if (op == "ping") {
        return "+PONG\r\n";
    } else if (op == "echo") {
        // Format: *2\r\n$4\r\nECHO\r\n$<n>\r\n<data>\r\n
        // Just echo argument back as bulk
        if (parts.size() != 2) return "-ERR wrong number of arguments for 'echo'\r\n";
        const auto& message = parts[1];
        return "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
    } else if (op == "set") {
        return handle_set(cmd.c_str());
    } else if (op == "get") {
        return handle_get(cmd.c_str());
    } else if (op == "rpush") {
        return handle_RPUSH(cmd.c_str());
    } else if (op == "lpush") {
        return handle_LPUSH(cmd.c_str());
    } else if (op == "lpop") {
        return handle_LPOP(cmd.c_str());
    } else if (op == "lrange") {
        return handle_LRANGE(cmd.c_str());
    } else if (op == "llen") {
        return handle_LLEN(cmd.c_str());
    } else if (op == "blpop") {
        return handle_BLPOP(cmd.c_str(), fd); // may be empty string to indicate "blocked"
    } else if (op == "type") {
        return handle_TYPE(cmd.c_str());
    } else if (op == "xadd") {
        return handle_XADD(cmd.c_str());
    } else {
        return "-ERR Invalid Unknown Command\r\n";
    }
}

void blpop_timeout_monitor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        TimePoint now = Clock::now();

        std::vector<int> timed_out_clients;

        {
            std::lock_guard<std::mutex> lk(blocked_mutex);
            for (const auto& [fd, info] : blocked_clients_info) {
                if (info.expiry <= now) {
                    timed_out_clients.push_back(fd);
                }
            }
        }

        for (int fd : timed_out_clients) {
            std::string list_name;
            {
                std::scoped_lock lk(blocked_mutex, storage_mutex);
                auto it_info = blocked_clients_info.find(fd);
                if (it_info == blocked_clients_info.end()) continue;
                list_name = it_info->second.list_name;

                // Remove client from blocked_clients queue
                auto& q = blocked_clients[list_name];
                std::queue<int> new_queue;
                while (!q.empty()) {
                    int front = q.front();
                    q.pop();
                    if (front != fd) new_queue.push(front);
                }
                q.swap(new_queue);

                blocked_clients_info.erase(fd);
                client_blocked_on_list.erase(fd);
                blocked_fds.erase(fd);
            }

            // Send null bulk string response for timeout
            std::string response = "$-1\r\n";
            send_response(fd, response);
        }
    }
}

int main() {
    std::thread(expiry_monitor).detach();
    std::thread(blpop_timeout_monitor).detach();
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }
    if (listen(server_fd, 64) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    std::vector<pollfd> poll_fds;
    poll_fds.push_back({ server_fd, POLLIN, 0 });

    while (true) {
        int rc = poll(poll_fds.data(), poll_fds.size(), -1);
        if (rc < 0) {
            std::cerr << "Poll failed\n";
            break;
        }

        // New connections
        if (poll_fds[0].revents & POLLIN) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                std::cout << "New client connected: FD " << client_fd << std::endl;
                poll_fds.push_back({ client_fd, POLLIN, 0 }); // <-- ADD to poll list
            }
        }

        // Existing clients
        for (size_t i = 1; i < poll_fds.size();) {
            int fd = poll_fds[i].fd;
        
            // Now always try to read from client if poll says data available,
            // even if client was previously blocked and now unblocked.
            if (poll_fds[i].revents & POLLIN) {
                char buffer[4096];
                ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
                if (n <= 0) {
                    std::cout << "Client disconnected: FD " << fd << std::endl;
                    close(fd);
                    remove_blocked_client_fd(fd);
                    poll_fds.erase(poll_fds.begin() + i);
                    continue;
                }
                buffer[n] = '\0';
                std::string cmd(buffer);
        
                std::string res = dispatch(cmd, fd);
                if (!res.empty()) {
                    send_response(fd, res);
                }
            }
            ++i;
        }
        
        
    }

    for (auto &pfd : poll_fds) close(pfd.fd);
    close(server_fd);
    return 0;
}
