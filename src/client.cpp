#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

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

    bool connect() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address/Address not supported" << std::endl;
            return false;
        }

        if (::connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }

        return true;
    }

public:

    std::string send_command(const std::string& command) {
        if (send(sockfd, command.c_str(), command.size(), 0) < 0) {
            return "-ERR Send failed";
        }

        char buffer[4096];
        ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            return "-ERR Receive failed";
        }
        buffer[n] = '\0';
        return std::string(buffer, n);
    }
    RedisClient(const std::string& h = "127.0.0.1", int p = 6379) : host(h), port(p), sockfd(-1) {}

    ~RedisClient() {
        if (sockfd >= 0) {
            close(sockfd);
        }
    }

    bool initialize() {
        return connect();
    }

    // Helper to create RESP array commands
    std::string create_resp_command(const std::vector<std::string>& parts) {
        std::string command = "*" + std::to_string(parts.size()) + "\r\n";
        for (const auto& part : parts) {
            command += "$" + std::to_string(part.size()) + "\r\n" + part + "\r\n";
        }
        return command;
    }

    // Basic commands
    std::string ping() {
        return send_command(create_resp_command({"PING"}));
    }

    std::string echo(const std::string& message) {
        return send_command(create_resp_command({"ECHO", message}));
    }

    std::string set(const std::string& key, const std::string& value) {
        return send_command(create_resp_command({"SET", key, value}));
    }

    std::string get(const std::string& key) {
        return send_command(create_resp_command({"GET", key}));
    }

    std::string incr(const std::string& key) {
        return send_command(create_resp_command({"INCR", key}));
    }

    // List commands
    std::string lpush(const std::string& key, const std::vector<std::string>& values) {
        std::vector<std::string> parts = {"LPUSH", key};
        parts.insert(parts.end(), values.begin(), values.end());
        return send_command(create_resp_command(parts));
    }

    std::string rpush(const std::string& key, const std::vector<std::string>& values) {
        std::vector<std::string> parts = {"RPUSH", key};
        parts.insert(parts.end(), values.begin(), values.end());
        return send_command(create_resp_command(parts));
    }

    std::string lpop(const std::string& key, int count = 1) {
        if (count == 1) {
            return send_command(create_resp_command({"LPOP", key}));
        } else {
            return send_command(create_resp_command({"LPOP", key, std::to_string(count)}));
        }
    }

    std::string lrange(const std::string& key, int start, int end) {
        return send_command(create_resp_command({"LRANGE", key, std::to_string(start), std::to_string(end)}));
    }

    std::string llen(const std::string& key) {
        return send_command(create_resp_command({"LLEN", key}));
    }

    // Transaction commands
    std::string multi() {
        return send_command(create_resp_command({"MULTI"}));
    }

    std::string exec() {
        return send_command(create_resp_command({"EXEC"}));
    }

    std::string discard() {
        return send_command(create_resp_command({"DISCARD"}));
    }

    // Type command
    std::string type(const std::string& key) {
        return send_command(create_resp_command({"TYPE", key}));
    }

    // Stream commands (simplified)
    std::string xadd(const std::string& stream, const std::string& id, 
                    const std::vector<std::pair<std::string, std::string>>& fields) {
        std::vector<std::string> parts = {"XADD", stream, id};
        for (const auto& field : fields) {
            parts.push_back(field.first);
            parts.push_back(field.second);
        }
        return send_command(create_resp_command(parts));
    }

    // Simple inline commands for testing
    std::string inline_ping() {
        return send_command("PING\r\n");
    }

    std::string inline_set(const std::string& key, const std::string& value) {
        return send_command("SET " + key + " " + value + "\r\n");
    }
};

void test_basic_commands(RedisClient& client) {
    std::cout << "=== Testing Basic Commands ===" << std::endl;
    
    std::cout << "PING: " << client.ping();
    std::cout << "ECHO: " << client.echo("Hello World");
    
    std::cout << "SET foo bar: " << client.set("foo", "bar");
    std::cout << "GET foo: " << client.get("foo");
    
    std::cout << "SET counter 10: " << client.set("counter", "10");
    std::cout << "INCR counter: " << client.incr("counter");
    std::cout << "GET counter: " << client.get("counter");
    
    std::cout << "GET nonexistent: " << client.get("nonexistent");
    std::cout << std::endl;
}

void test_list_commands(RedisClient& client) {
    std::cout << "=== Testing List Commands ===" << std::endl;
    
    std::cout << "LPUSH mylist a b c: " << client.lpush("mylist", {"a", "b", "c"});
    std::cout << "LRANGE mylist 0 -1: " << client.lrange("mylist", 0, -1);
    std::cout << "LLEN mylist: " << client.llen("mylist");
    
    std::cout << "RPUSH mylist d e: " << client.rpush("mylist", {"d", "e"});
    std::cout << "LRANGE mylist 0 -1: " << client.lrange("mylist", 0, -1);
    
    std::cout << "LPOP mylist: " << client.lpop("mylist");
    std::cout << "LRANGE mylist 0 -1: " << client.lrange("mylist", 0, -1);
    
    std::cout << "LPOP mylist 2: " << client.lpop("mylist", 2);
    std::cout << "LRANGE mylist 0 -1: " << client.lrange("mylist", 0, -1);
    std::cout << std::endl;
}

void test_transactions(RedisClient& client) {
    std::cout << "=== Testing Transactions ===" << std::endl;
    
    // Test connection 1 - starts transaction
    RedisClient client1;
    client1.initialize();
    
    std::cout << "Client1 MULTI: " << client1.multi();
    std::cout << "Client1 SET trans_key initial: " << client1.set("trans_key", "initial");
    std::cout << "Client1 INCR trans_counter: " << client1.incr("trans_counter");
    
    // Test connection 2 - checks that data is not visible during transaction
    RedisClient client2;
    client2.initialize();
    std::cout << "Client2 GET trans_key (should be empty): " << client2.get("trans_key");
    std::cout << "Client2 GET trans_counter (should be empty): " << client2.get("trans_counter");
    
    // Execute transaction
    std::cout << "Client1 EXEC: " << client1.exec();
    
    // Now data should be visible
    std::cout << "Client2 GET trans_key (after EXEC): " << client2.get("trans_key");
    std::cout << "Client2 GET trans_counter (after EXEC): " << client2.get("trans_counter");
    
    // Test discard
    std::cout << "Client1 MULTI: " << client1.multi();
    std::cout << "Client1 SET should_discard value: " << client1.set("should_discard", "value");
    std::cout << "Client1 DISCARD: " << client1.discard();
    std::cout << "Client2 GET should_discard (should be empty): " << client2.get("should_discard");
    
    std::cout << std::endl;
}

void test_concurrent_transactions() {
    std::cout << "=== Testing Concurrent Transactions ===" << std::endl;
    
    auto test_client = [](int client_id) {
        RedisClient client;
        client.initialize();
        
        std::string key = "concurrent_" + std::to_string(client_id);
        
        std::cout << "Client " << client_id << " MULTI: " << client.multi();
        std::cout << "Client " << client_id << " SET " << key << " value: " << client.set(key, "value");
        std::cout << "Client " << client_id << " EXEC: " << client.exec();
        std::cout << "Client " << client_id << " GET " << key << ": " << client.get(key);
    };
    
    std::thread t1(test_client, 1);
    std::thread t2(test_client, 2);
    std::thread t3(test_client, 3);
    
    t1.join();
    t2.join();
    t3.join();
    
    std::cout << std::endl;
}

void test_error_cases(RedisClient& client) {
    std::cout << "=== Testing Error Cases ===" << std::endl;
    
    std::cout << "EXEC without MULTI: " << client.exec();
    std::cout << "DISCARD without MULTI: " << client.discard();
    
    std::cout << "MULTI: " << client.multi();
    std::cout << "Invalid command in transaction: " << client.send_command("INVALID_COMMAND\r\n");
    std::cout << "EXEC: " << client.exec();
    
    std::cout << "TYPE string_key: " << client.type("foo");
    std::cout << "TYPE list_key: " << client.type("mylist");
    std::cout << "TYPE nonexistent: " << client.type("nonexistent_key");
    
    std::cout << std::endl;
}

void test_stream_commands(RedisClient& client) {
    std::cout << "=== Testing Stream Commands ===" << std::endl;
    
    std::vector<std::pair<std::string, std::string>> fields = {
        {"field1", "value1"},
        {"field2", "value2"}
    };
    
    std::cout << "XADD mystream *: " << client.xadd("mystream", "*", fields);
    std::cout << "XADD mystream 1000-0: " << client.xadd("mystream", "1000-0", fields);
    
    std::cout << "TYPE mystream: " << client.type("mystream");
    std::cout << std::endl;
}

void test_performance(RedisClient& client) {
    std::cout << "=== Testing Performance ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    const int iterations = 1000;
    for (int i = 0; i < iterations; i++) {
        client.set("perf_key_" + std::to_string(i), "value_" + std::to_string(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Time for " << iterations << " SET operations: " << duration.count() << "ms" << std::endl;
    std::cout << "Operations per second: " << (iterations * 1000.0 / duration.count()) << std::endl;
    std::cout << std::endl;
}

int main() {
    RedisClient client;
    
    if (!client.initialize()) {
        std::cerr << "Failed to connect to Redis server" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to Redis server successfully!" << std::endl;
    
    // Run tests
    test_basic_commands(client);
    test_list_commands(client);
    test_transactions(client);
    test_concurrent_transactions();
    test_error_cases(client);
    test_stream_commands(client);
    test_performance(client);
    
    std::cout << "All tests completed!" << std::endl;
    
    return 0;
}