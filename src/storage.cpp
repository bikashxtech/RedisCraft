#include "storage.hpp"
#include <thread>

std::unordered_map<int, BlockedClientInfo> blocked_clients_info;
std::unordered_map<std::string, ValueWithExpiry> redis_storage;
std::unordered_map<std::string, std::vector<std::string>> lists;
std::unordered_map<int, std::string> pending_responses;
std::mutex pending_responses_mutex;


std::unordered_map<std::string, std::queue<int>> blocked_clients;
std::unordered_map<int, std::string> client_blocked_on_list;
std::unordered_set<int> blocked_fds;

std::unordered_map<std::string, Stream> streams;
std::mutex streams_mutex;

std::unordered_map<std::string, std::vector<StreamBlockedClient>> blocked_stream_clients;
std::unordered_set<int> blocked_stream_fds;

std::unordered_map<int, TransactionState> client_transactions;
std::mutex transaction_mutex;

std::mutex storage_mutex;
std::mutex blocked_mutex;

void cleanup_expired_keys() {
    std::lock_guard<std::mutex> lock(storage_mutex);
    auto now = Clock::now();
    for (auto it = redis_storage.begin(); it != redis_storage.end();) {
        if (it->second.expiry != TimePoint::min() && it->second.expiry <= now) {
            it = redis_storage.erase(it);
        } else {
            ++it;
        }
    }
}

void expiry_monitor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cleanup_expired_keys();
    }
}

void remove_blocked_client_fd(int fd) {
    std::scoped_lock lk(blocked_mutex, storage_mutex);
    auto it = client_blocked_on_list.find(fd);
    if (it != client_blocked_on_list.end()) {
        const std::string list = it->second;
        
        std::queue<int> rebuilt;
        auto &q = blocked_clients[list];
        while (!q.empty()) {
            int cur = q.front(); q.pop();
            if (cur != fd) rebuilt.push(cur);
        }
        q.swap(rebuilt);
        client_blocked_on_list.erase(fd);
    }
    blocked_fds.erase(fd);
}

void remove_blocked_stream_client_fd(int fd) {
    std::scoped_lock lk(blocked_mutex, streams_mutex);
    
    for (auto& [stream_key, clients] : blocked_stream_clients) {
        for (auto it = clients.begin(); it != clients.end();) {
            if (it->fd == fd) {
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    blocked_stream_fds.erase(fd);
}

void remove_client_transaction(int fd) {
    std::lock_guard<std::mutex> lock(transaction_mutex);
    client_transactions.erase(fd);
}

std::string rdb_filename = "dump.rdb";
int rdb_save_interval = 60; 
bool rdb_enabled = true;

void rdb_background_saver() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(rdb_save_interval));
        if (rdb_enabled) {
            std::cout << "Background saving started" << std::endl;
            if (rdb_save(rdb_filename)) {
                std::cout << "Background saving completed" << std::endl;
            } else {
                std::cerr << "Background saving failed" << std::endl;
            }
        }
    }
}