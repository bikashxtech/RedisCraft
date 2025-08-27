#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <chrono>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct ValueWithExpiry {
    std::string value;
    TimePoint expiry;
};

// Global state
extern std::unordered_map<std::string, ValueWithExpiry> redis_storage;
extern std::unordered_map<std::string, std::vector<std::string>> lists;

extern std::unordered_map<int, std::string> pending_responses;
extern std::mutex pending_responses_mutex;


extern std::unordered_map<std::string, std::queue<int>> blocked_clients; // list -> queued fds
extern std::unordered_map<int, std::string> client_blocked_on_list;      // fd -> list
extern std::unordered_set<int> blocked_fds;

extern std::mutex storage_mutex;
extern std::mutex blocked_mutex;

// Expiry helpers
void cleanup_expired_keys();
void expiry_monitor();

// Helper to remove a disconnected fd from blocked queues
void remove_blocked_client_fd(int fd);
