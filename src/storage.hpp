#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <queue>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using StreamEntry = std::unordered_map<std::string, std::string>;
using Stream = std::vector<std::pair<std::string, StreamEntry>>;

extern std::unordered_map<std::string, Stream> streams;
extern std::mutex streams_mutex;

struct ValueWithExpiry {
    std::string value;
    TimePoint expiry;
};

struct BlockedClientInfo {
    int fd;
    std::string list_name;
    TimePoint expiry;
};

// Add to storage.hpp
struct StreamBlockedClient {
    int fd;
    std::string last_id;
    TimePoint expiry;
};

extern std::unordered_map<std::string, std::vector<StreamBlockedClient>> blocked_stream_clients;
extern std::unordered_set<int> blocked_stream_fds;

// Global state
extern std::unordered_map<int, BlockedClientInfo> blocked_clients_info;
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
