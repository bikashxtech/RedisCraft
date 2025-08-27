#include "storage.hpp"
#include <thread>

std::unordered_map<std::string, ValueWithExpiry> redis_storage;
std::unordered_map<std::string, std::vector<std::string>> lists;
std::unordered_map<int, std::string> pending_responses;
std::mutex pending_responses_mutex;


std::unordered_map<std::string, std::queue<int>> blocked_clients;
std::unordered_map<int, std::string> client_blocked_on_list;
std::unordered_set<int> blocked_fds;

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
        // Rebuild the queue without fd
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
