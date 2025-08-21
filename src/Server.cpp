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
#include <netdb.h>

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
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
                  const char* err = "-ERR unknown command\r\n";
                  send(poll_fds[i].fd, err, strlen(err), 0);
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