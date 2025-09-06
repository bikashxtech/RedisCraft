#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <sstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

class RedisClient {
private:
    int sockfd;
    std::string host;
    int port;
    
    bool connected;
    
public:
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379) 
        : host(host), port(port), connected(false), sockfd(-1) {}
    
    ~RedisClient() {
        disconnect();
    }
    
    bool connect() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Error creating socket" << std::endl;
            return false;
        }
        
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address/Address not supported" << std::endl;
            return false;
        }
        
        if (::connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }
        
        connected = true;
        return true;
    }
    
    void disconnect() {
        if (connected) {
            close(sockfd);
            connected = false;
            sockfd = -1;
        }
    }
    
    bool isConnected() const {
        return connected;
    }
    
    std::string sendCommand(const std::string& command) {
        if (!connected) {
            return "Not connected to server";
        }
        
        
        ssize_t n = send(sockfd, command.c_str(), command.length(), 0);
        if (n < 0) {
            return "Error sending command";
        }
        
        
        std::string response;
        char buffer[4096];
        
        while (true) {
            n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                break;
            }
            
            buffer[n] = '\0';
            response += buffer;
            
            
            if (isCompleteResponse(response)) {
                break;
            }
        }
        
        return response;
    }
    
private:
    bool isCompleteResponse(const std::string& response) {
        if (response.empty()) return false;
        
        char firstChar = response[0];
        
        switch (firstChar) {
            case '+': 
            case '-': 
            case ':': 
                
                return response.find("\r\n") != std::string::npos;
                
            case '$': 
            {
                
                size_t crlf_pos = response.find("\r\n");
                if (crlf_pos == std::string::npos) return false;
                
                
                std::string len_str = response.substr(1, crlf_pos - 1);
                try {
                    int length = std::stoi(len_str);
                    if (length == -1) return true; 
                    
                    
                    size_t expected_size = crlf_pos + 2 + length + 2;
                    return response.size() >= expected_size;
                } catch (...) {
                    return false;
                }
            }
                
            case '*': 
            {
                
                size_t crlf_pos = response.find("\r\n");
                if (crlf_pos == std::string::npos) return false;
                
                
                std::string count_str = response.substr(1, crlf_pos - 1);
                try {
                    int count = std::stoi(count_str);
                    if (count == -1) return true; 
                    if (count == 0) return true;  
                    
                    
                    
                    
                    size_t pos = crlf_pos + 2;
                    for (int i = 0; i < count; i++) {
                        if (pos >= response.size()) return false;
                        
                        char elemType = response[pos];
                        switch (elemType) {
                            case '+':
                            case '-':
                            case ':':
                            {
                                size_t elem_end = response.find("\r\n", pos);
                                if (elem_end == std::string::npos) return false;
                                pos = elem_end + 2;
                                break;
                            }
                            case '$':
                            {
                                size_t len_end = response.find("\r\n", pos);
                                if (len_end == std::string::npos) return false;
                                
                                std::string len_str = response.substr(pos + 1, len_end - pos - 1);
                                try {
                                    int length = std::stoi(len_str);
                                    if (length == -1) {
                                        pos = len_end + 2;
                                    } else {
                                        size_t data_end = len_end + 2 + length + 2;
                                        if (response.size() < data_end) return false;
                                        pos = data_end;
                                    }
                                } catch (...) {
                                    return false;
                                }
                                break;
                            }
                            case '*':
                                
                                
                                return true;
                            default:
                                return false;
                        }
                    }
                    
                    return pos <= response.size();
                } catch (...) {
                    return false;
                }
            }
                
            default:
                return false;
        }
    }
};

std::string formatCommand(const std::vector<std::string>& args) {
    if (args.empty()) return "";
    
    std::string command = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        command += "$" + std::to_string(arg.length()) + "\r\n" + arg + "\r\n";
    }
    
    return command;
}


std::vector<std::string> splitCommand(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    bool in_quotes = false;
    char quote_char = '"';
    std::string current_token;
    
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        
        if (in_quotes) {
            if (c == quote_char) {
                if (!current_token.empty()) {
                    tokens.push_back(current_token);
                    current_token.clear();
                }
                in_quotes = false;
            } else if (c == '\\' && i + 1 < input.size()) {
                i++;
                current_token += input[i];
            } else {
                current_token += c;
            }
        } else {
            if (std::isspace(c)) {
                if (!current_token.empty()) {
                    tokens.push_back(current_token);
                    current_token.clear();
                }
            } else if (c == '"' || c == '\'') {
                in_quotes = true;
                quote_char = c;
            } else {
                current_token += c;
            }
        }
    }
    
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    
    return tokens;
}

void printResponse(const std::string& response) {
    if (response.empty()) {
        std::cout << "(empty response)" << std::endl;
        return;
    }
    
    char firstChar = response[0];
    
    switch (firstChar) {
        case '+': 
            std::cout << response.substr(1, response.find("\r\n") - 1) << std::endl;
            break;
            
        case '-':
            std::cout << "Error: " << response.substr(1, response.find("\r\n") - 1) << std::endl;
            break;
            
        case ':':
            std::cout << "(integer) " << response.substr(1, response.find("\r\n") - 1) << std::endl;
            break;
            
        case '$': 
        {
            size_t crlf_pos = response.find("\r\n");
            if (crlf_pos == std::string::npos) {
                std::cout << response << std::endl;
                break;
            }
            
            std::string len_str = response.substr(1, crlf_pos - 1);
            try {
                int length = std::stoi(len_str);
                if (length == -1) {
                    std::cout << "(nil)" << std::endl;
                } else {
                    std::string content = response.substr(crlf_pos + 2, length);
                    std::cout << "\"" << content << "\"" << std::endl;
                }
            } catch (...) {
                std::cout << response << std::endl;
            }
            break;
        }
            
        case '*': 
        {
            size_t crlf_pos = response.find("\r\n");
            if (crlf_pos == std::string::npos) {
                std::cout << response << std::endl;
                break;
            }
            
            std::string count_str = response.substr(1, crlf_pos - 1);
            try {
                int count = std::stoi(count_str);
                if (count == -1) {
                    std::cout << "(nil)" << std::endl;
                } else if (count == 0) {
                    std::cout << "(empty list or set)" << std::endl;
                } else {
                    size_t pos = crlf_pos + 2;
                    for (int i = 1; i <= count; i++) {
                        std::cout << i << ") ";
                        
                        if (pos >= response.size()) {
                            std::cout << "(incomplete response)" << std::endl;
                            break;
                        }
                        
                        char elemType = response[pos];
                        switch (elemType) {
                            case '+':
                            case '-':
                            case ':':
                            {
                                size_t elem_end = response.find("\r\n", pos);
                                if (elem_end == std::string::npos) {
                                    std::cout << response.substr(pos) << std::endl;
                                    pos = response.size();
                                } else {
                                    std::cout << response.substr(pos, elem_end - pos) << std::endl;
                                    pos = elem_end + 2;
                                }
                                break;
                            }
                            case '$':
                            {
                                size_t len_end = response.find("\r\n", pos);
                                if (len_end == std::string::npos) {
                                    std::cout << response.substr(pos) << std::endl;
                                    pos = response.size();
                                    break;
                                }
                                
                                std::string len_str = response.substr(pos + 1, len_end - pos - 1);
                                try {
                                    int length = std::stoi(len_str);
                                    if (length == -1) {
                                        std::cout << "(nil)" << std::endl;
                                        pos = len_end + 2;
                                    } else {
                                        size_t data_start = len_end + 2;
                                        size_t data_end = data_start + length;
                                        if (data_end + 2 > response.size()) {
                                            std::cout << "(incomplete bulk string)" << std::endl;
                                            pos = response.size();
                                        } else {
                                            std::string content = response.substr(data_start, length);
                                            std::cout << "\"" << content << "\"" << std::endl;
                                            pos = data_end + 2;
                                        }
                                    }
                                } catch (...) {
                                    std::cout << response.substr(pos) << std::endl;
                                    pos = response.size();
                                }
                                break;
                            }
                            case '*':
                                std::cout << "(nested array)" << std::endl;
                                return;
                            default:
                                std::cout << response.substr(pos) << std::endl;
                                pos = response.size();
                                break;
                        }
                    }
                }
            } catch (...) {
                std::cout << response << std::endl;
            }
            break;
        }
            
        default:
            std::cout << response << std::endl;
            break;
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else {
            std::cout << "Usage: " << argv[0] << " [-h host] [-p port]" << std::endl;
            return 1;
        }
    }
    
    RedisClient client(host, port);
    
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    if (!client.connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to server. Type commands or 'quit' to exit." << std::endl;
    
    std::string input;
    while (true) {
        std::cout << host << ":" << port << "> ";
        std::getline(std::cin, input);
        
        if (input.empty()) {
            continue;
        }
        
        if (input == "quit" || input == "exit") {
            break;
        }
        
        auto args = splitCommand(input);
        if (args.empty()) {
            continue;
        }
        
        std::string command = formatCommand(args);
        std::string response = client.sendCommand(command);
        
        printResponse(response);
    }
    
    client.disconnect();
    std::cout << "Disconnected from server" << std::endl;
    
    return 0;
}