#include "commands.hpp"
#include "parser.hpp"
#include "storage.hpp"
#include "StreamHandler.hpp"

#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

static bool is_expired(const ValueWithExpiry& v) {
    return v.expiry != TimePoint::min() && Clock::now() >= v.expiry;
}

bool send_response(int fd, const std::string& response) {
    ssize_t n = send(fd, response.c_str(), response.size(), 0);
    return n == static_cast<ssize_t>(response.size());
}

std::string handle_set(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 3) return "-ERR Invalid SET Command\r\n";
    if (to_lower(parts[0]) != "set") return "-ERR Invalid SET Command\r\n";

    std::string key = parts[1];
    std::string value = parts[2];
    TimePoint expiry = TimePoint::min();

    if (parts.size() == 5) {
        if (to_lower(parts[3]) != "px") return "-ERR Syntax error\r\n";
        try {
            int ms = std::stoi(parts[4]);
            expiry = Clock::now() + std::chrono::milliseconds(ms);
        } catch (...) {
            return "-ERR Invalid PX value\r\n";
        }
    } else if (parts.size() != 3) {
        return "-ERR Syntax error\r\n";
    }

    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        redis_storage[key] = { value, expiry };
    }
    return "+OK\r\n";
}

std::string handle_get(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 2) return "-ERR Invalid GET command\r\n";
    if (to_lower(parts[0]) != "get") return "-ERR Invalid GET command\r\n";

    std::string key = parts[1];
    ValueWithExpiry v;

    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        auto it = redis_storage.find(key);
        if (it == redis_storage.end()) return "$-1\r\n";
        v = it->second;
        if (is_expired(v)) {
            redis_storage.erase(key);
            return "$-1\r\n";
        }
    }

    return "$" + std::to_string(v.value.size()) + "\r\n" + v.value + "\r\n";
}

std::string handle_INCR(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 2) return "-ERR wrong number of arguments for 'incr' command\r\n";
    if (to_lower(parts[0]) != "incr") return "-ERR Invalid INCR Command\r\n";

    std::string key = parts[1];
    int64_t value = 0;
    bool key_exists = false;

    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        auto it = redis_storage.find(key);
        
        if (it != redis_storage.end()) {
            key_exists = true;
            
            const std::string& str_val = it->second.value;
            try {
                value = std::stoll(str_val);
            } catch (...) {
                return "-ERR value is not an integer or out of range\r\n";
            }
        }
        
        value++;
        
        redis_storage[key] = {std::to_string(value), TimePoint::min()};
    }

    return ":" + std::to_string(value) + "\r\n";
}

std::string handle_MULTI(const char* resp, int client_fd) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 1) return "-ERR wrong number of arguments for 'multi' command\r\n";
    if (to_lower(parts[0]) != "multi") return "-ERR Invalid MULTI Command\r\n";

    {
        std::lock_guard<std::mutex> lock(transaction_mutex);
        client_transactions[client_fd] = {true, {}};
    }

    return "+OK\r\n";
}

std::string handle_EXEC(const char* resp, int client_fd) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 1) return "-ERR wrong number of arguments for 'exec' command\r\n";
    if (to_lower(parts[0]) != "exec") return "-ERR Invalid EXEC Command\r\n";

    TransactionState transaction;
    bool has_transaction = false;

    {
        std::lock_guard<std::mutex> lock(transaction_mutex);
        auto it = client_transactions.find(client_fd);
        if (it != client_transactions.end()) {
            transaction = it->second;
            has_transaction = true;
            client_transactions.erase(it); 
        }
    }

    if (!has_transaction) {
        return "-ERR EXEC without MULTI\r\n";
    }

    if (transaction.queued_commands.empty()) {
        return "*0\r\n";
    }

    std::vector<std::string> responses;
    for (const auto& cmd : transaction.queued_commands) {
        std::string response;
        auto parts = parse_resp_array(cmd.c_str());
        if (parts.empty()) return "-ERR Protocol error\r\n";
        std::string op = to_lower(parts[0]);

        if (op == "ping") {
            response =  "+PONG\r\n";
        } else if (op == "echo") {
            if (parts.size() != 2) return "-ERR wrong number of arguments for 'echo'\r\n";
            const auto& message = parts[1];
            response = "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
        } else if (op == "set") {
            response = handle_set(cmd.c_str());
        } else if (op == "get") {
            response = handle_get(cmd.c_str());
        } else if (op == "incr") {
            response = handle_INCR(cmd.c_str());
        } else if (op == "rpush") {
            response = handle_RPUSH(cmd.c_str());
        } else if (op == "lpush") {
            response = handle_LPUSH(cmd.c_str());
        } else if (op == "lpop") {
            response = handle_LPOP(cmd.c_str());
        } else if (op == "lrange") {
            response = handle_LRANGE(cmd.c_str());
        } else if (op == "llen") {
            response = handle_LLEN(cmd.c_str());
        } else if (op == "blpop") {
            response = handle_BLPOP(cmd.c_str(), client_fd); 
        } else if (op == "type") {
            response = handle_TYPE(cmd.c_str());
        } else if (op == "xadd") {
            response = handle_XADD(cmd.c_str());
        } else if(op == "xrange") {
            response = handle_XRANGE(cmd.c_str());
        } else if(op == "xread") {
            response = handle_XREAD(cmd.c_str(), client_fd); 
        } else {
            response = "-ERR Invalid Unknown Command\r\n";
        }
        responses.push_back(response);
    }

    std::string result = "*" + std::to_string(responses.size()) + "\r\n";
    for (const auto& response : responses) {
        result += response;
    }
    
    return result;
}

std::string handle_LPUSH(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 3) return "-ERR Invalid LPUSH Command\r\n";
    if (to_lower(parts[0]) != "lpush") return "-ERR Invalid LPUSH Command\r\n";
    const std::string listName = parts[1];

    std::lock_guard<std::mutex> lk(storage_mutex);
    auto& lst = lists[listName];
    for (size_t i = 2; i < parts.size(); ++i) {
        lst.insert(lst.begin(), parts[i]);
    }
    return ":" + std::to_string(lst.size()) + "\r\n";
}

std::string handle_RPUSH(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 3) return "-ERR Invalid RPUSH Command\r\n";
    if (to_lower(parts[0]) != "rpush") return "-ERR Invalid RPUSH Command\r\n";
    const std::string listName = parts[1];

    std::unique_lock<std::mutex> lock(storage_mutex);
    auto& lst = lists[listName];

    
    for (size_t i = 2; i < parts.size(); ++i) {
        lst.push_back(parts[i]);
    }

    int size_before_unblock = static_cast<int>(lst.size());

    lock.unlock();

    while (true) {
        int client_fd = -1;
        {
            std::scoped_lock lk(storage_mutex, blocked_mutex);
            auto it = blocked_clients.find(listName);
            if (it == blocked_clients.end() || it->second.empty()) break;
            client_fd = it->second.front();
            it->second.pop();
        }

        std::string popped;
        {
            std::lock_guard<std::mutex> lock(storage_mutex);
            auto itList = lists.find(listName);
            if (itList == lists.end() || itList->second.empty()) break;
            popped = itList->second.front();
            itList->second.erase(itList->second.begin());
        }

        std::string response = "*2\r\n";
        response += "$" + std::to_string(listName.size()) + "\r\n" + listName + "\r\n";
        response += "$" + std::to_string(popped.size()) + "\r\n" + popped + "\r\n";

        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent != static_cast<ssize_t>(response.size())) {
            std::cerr << "Failed to send unblock response to client fd " << client_fd << "\n";
            close(client_fd);
            remove_blocked_client_fd(client_fd);
            continue;
        }

        {
            std::scoped_lock lk(blocked_mutex, storage_mutex);
            client_blocked_on_list.erase(client_fd);
            blocked_fds.erase(client_fd);
        }
    }

    return ":" + std::to_string(size_before_unblock) + "\r\n";
}


std::string handle_LPOP(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 2) return "-ERR Invalid LPOP Command\r\n";
    if (to_lower(parts[0]) != "lpop") return "-ERR Invalid LPOP Command\r\n";

    const std::string& key = parts[1];
    bool hasCount = parts.size() == 3;
    int count = 1;

    std::lock_guard<std::mutex> lk(storage_mutex);
    auto it = lists.find(key);
    if (it == lists.end() || it->second.empty()) {
        return hasCount ? "*0\r\n" : "$-1\r\n";
    }

    if (hasCount) {
        try {
            count = std::stoi(parts[2]);
            if (count < 0) return "-ERR value is not an integer or out of range\r\n";
        } catch (...) {
            return "-ERR Invalid Argument\r\n";
        }
        int n = static_cast<int>(it->second.size());
        if (count > n) count = n;

        std::string res = "*" + std::to_string(count) + "\r\n";
        while (count--) {
            std::string elem = it->second.front();
            it->second.erase(it->second.begin());
            res += "$" + std::to_string(elem.size()) + "\r\n" + elem + "\r\n";
        }
        return res;
    } else {
        std::string elem = it->second.front();
        it->second.erase(it->second.begin());
        return "$" + std::to_string(elem.size()) + "\r\n" + elem + "\r\n";
    }
}

std::string handle_LRANGE(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 4) return "-ERR Invalid LRANGE Command\r\n";
    if (to_lower(parts[0]) != "lrange") return "-ERR Invalid LRANGE Command\r\n";

    const std::string &listName = parts[1];

    std::vector<std::string> snapshot;
    {
        std::lock_guard<std::mutex> lk(storage_mutex);
        auto it = lists.find(listName);
        if (it == lists.end()) return "*0\r\n";
        snapshot = it->second;
    }
    int n = static_cast<int>(snapshot.size());
    int start, end;
    try {
        start = std::stoi(parts[2]);
        end   = std::stoi(parts[3]);
    } catch (...) {
        return "-ERR Invalid LRANGE indices\r\n";
    }

    if (start < 0) start = n + start;
    if (end   < 0) end   = n + end;
    if (start < 0) start = 0;
    if (end >= n) end = n - 1;
    if (start > end || start >= n) return "*0\r\n";

    std::string res = "*" + std::to_string(end - start + 1) + "\r\n";
    for (int i = start; i <= end; i++) {
        const std::string& e = snapshot[i];
        res += "$" + std::to_string(e.size()) + "\r\n" + e + "\r\n";
    }
    return res;
}


std::string handle_LLEN(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 2) return "-ERR Invalid LLEN Command\r\n";
    if (to_lower(parts[0]) != "llen") return "-ERR Invalid LLEN Command\r\n";

    std::lock_guard<std::mutex> lk(storage_mutex);
    auto it = lists.find(parts[1]);
    if (it == lists.end()) return ":0\r\n";
    return ":" + std::to_string(it->second.size()) + "\r\n";
}


std::string handle_BLPOP(const char* resp, int client_fd) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 3) return "-ERR Invalid BLPOP Arguments\r\n";
    if (to_lower(parts[0]) != "blpop") return "-ERR Invalid BLPOP Command\r\n";

    const std::string list_name = parts[1];

    double timeout_seconds = 0.0;
    try {
        timeout_seconds = std::stod(parts[2]);
    } catch (...) {
        return "-ERR Invalid Timeout Argument\r\n";
    }
    if (timeout_seconds < 0.0) return "-ERR Invalid Timeout Argument\r\n";

    {
        std::lock_guard<std::mutex> lk(storage_mutex);
        auto it = lists.find(list_name);
        if (it != lists.end() && !it->second.empty()) {
            
            std::string popped = it->second.front();
            it->second.erase(it->second.begin());
            std::string resp = "*2\r\n";
            resp += "$" + std::to_string(list_name.size()) + "\r\n" + list_name + "\r\n";
            resp += "$" + std::to_string(popped.size()) + "\r\n" + popped + "\r\n";
            return resp;
        }
    }

    
    if (timeout_seconds == 0.0) {
        
        std::lock_guard<std::mutex> lk(blocked_mutex);
        blocked_clients[list_name].push(client_fd);
        client_blocked_on_list[client_fd] = list_name;
        blocked_fds.insert(client_fd);
        return "";
    }

    
    TimePoint expiry = Clock::now() + std::chrono::milliseconds(static_cast<int>(timeout_seconds * 1000));
    {
        std::scoped_lock lk(blocked_mutex, storage_mutex);
        blocked_clients[list_name].push(client_fd);
        client_blocked_on_list[client_fd] = list_name;
        blocked_fds.insert(client_fd);
        blocked_clients_info[client_fd] = {client_fd, list_name, expiry};
    }

    return "";
}

std::string handle_TYPE(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 2) return "-ERR wrong number of arguments for 'type'\r\n";
    if (to_lower(parts[0]) != "type") return "-ERR Invalid TYPE Command\r\n";

    std::string key = parts[1];

    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        if (redis_storage.find(key) != redis_storage.end()) {
            return "+string\r\n";
        }
    }

    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        if (streams.find(key) != streams.end()) {
            return "+stream\r\n";
        }
    }

    return "+none\r\n";
}

std::string handle_XADD(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 4) return "-ERR Invalid XADD Command\r\n";
    if (to_lower(parts[0]) != "xadd") return "-ERR Invalid XADD Command\r\n";

    std::string stream_key = parts[1];
    std::string entry_id = parts[2];

    if ((parts.size() - 3) % 2 != 0)
        return "-ERR Invalid field-value pairs\r\n";

    uint64_t ms_part = 0;
    uint64_t seq_part = 0;
    bool seq_wildcard = false;
    bool full_wildcard = false;

    if (!parse_entry_id(entry_id, ms_part, seq_part, seq_wildcard, full_wildcard)) {
        return "-ERR Invalid entry ID format\r\n";
    }

    std::string new_entry_id;
    StreamEntry new_entry;

    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        auto& stream = streams[stream_key];

        if (full_wildcard) {
            uint64_t now_ms = current_unix_time_ms();

            uint64_t last_seq_for_ms = 0;
            bool found = false;
            for (auto it = stream.rbegin(); it != stream.rend(); ++it) {
                uint64_t entry_ms, entry_seq;
                bool dummy_sw, dummy_fw;
                if (!parse_entry_id(it->first, entry_ms, entry_seq, dummy_sw, dummy_fw)) continue;
                if (entry_ms == now_ms) {
                    last_seq_for_ms = entry_seq;
                    found = true;
                    break;
                }
                if (entry_ms < now_ms) break;
            }
            seq_part = found ? last_seq_for_ms + 1 : 0;
            ms_part = now_ms;
            new_entry_id = std::to_string(ms_part) + "-" + std::to_string(seq_part);
        } else if (seq_wildcard) {
            uint64_t last_seq_for_ms = 0;
            bool found = false;
            for (auto it = stream.rbegin(); it != stream.rend(); ++it) {
                uint64_t entry_ms, entry_seq;
                bool dummy_sw, dummy_fw;
                if (!parse_entry_id(it->first, entry_ms, entry_seq, dummy_sw, dummy_fw)) continue;
                if (entry_ms == ms_part) {
                    last_seq_for_ms = entry_seq;
                    found = true;
                    break;
                }
                if (entry_ms < ms_part) break;
            }
            if (!found) {
                seq_part = (ms_part == 0) ? 1 : 0;
            } else {
                seq_part = last_seq_for_ms + 1;
            }
            new_entry_id = std::to_string(ms_part) + "-" + std::to_string(seq_part);
        } else {
            new_entry_id = entry_id;
        }

        uint64_t new_ms, new_seq;
        bool dummy_sw, dummy_fw;
        if (!parse_entry_id(new_entry_id, new_ms, new_seq, dummy_sw, dummy_fw))
            return "-ERR Invalid entry ID format\r\n";

        if (new_ms == 0 && new_seq == 0)
            return "-ERR The ID specified in XADD must be greater than 0-0\r\n";

        if (!stream.empty()) {
            const std::string& last_id = stream.back().first;
            uint64_t last_ms, last_seq;
            if (!parse_entry_id(last_id, last_ms, last_seq, dummy_sw, dummy_fw)) {
                last_ms = 0; last_seq = 0;
            }
            if (!is_id_greater(new_ms, new_seq, last_ms, last_seq))
                return "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n";
        }

        for (size_t i = 3; i < parts.size(); i += 2) {
            new_entry[parts[i]] = parts[i + 1];
        }
        stream.emplace_back(new_entry_id, new_entry);
    }

    std::vector<int> clients_to_unblock;
    {
        std::scoped_lock lk(blocked_mutex, streams_mutex);
        auto it = blocked_stream_clients.find(stream_key);
        if (it != blocked_stream_clients.end()) {
            auto& clients = it->second;
            for (auto client_it = clients.begin(); client_it != clients.end();) {
                if (Clock::now() > client_it->expiry) {
                    blocked_stream_fds.erase(client_it->fd);
                    client_it = clients.erase(client_it);
                    continue;
                }
                
                uint64_t client_ms, client_seq;
                if (parse_range_id(client_it->last_id, client_ms, client_seq)) {
                    uint64_t new_ms, new_seq;
                    bool dummy_sw, dummy_fw;
                    if (parse_entry_id(new_entry_id, new_ms, new_seq, dummy_sw, dummy_fw)) {
                        if (is_id_greater(new_ms, new_seq, client_ms, client_seq)) {
                            clients_to_unblock.push_back(client_it->fd);
                            blocked_stream_fds.erase(client_it->fd);
                            client_it = clients.erase(client_it);
                            continue;
                        }
                    }
                }
                ++client_it;
            }
        }
    }
    
    for (int fd : clients_to_unblock) {
        std::string response = "*1\r\n*2\r\n";
        response += resp_bulk_string(stream_key);
        response += "*1\r\n*2\r\n";
        response += resp_bulk_string(new_entry_id);
        
        std::vector<std::string> kv_list;
        for (const auto& [k, v] : new_entry) {
            kv_list.push_back(k);
            kv_list.push_back(v);
        }
        response += resp_array(kv_list);
        
        send_response(fd, response);
    }
    
    return "$" + std::to_string(new_entry_id.size()) + "\r\n" + new_entry_id + "\r\n";
}

std::string handle_XRANGE(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 4) return "-ERR Invalid XRANGE Command\r\n";
    if (to_lower(parts[0]) != "xrange") return "-ERR Invalid XRANGE Command\r\n";

    std::string stream_key = parts[1];
    std::string start_id = parts[2];
    std::string end_id = parts[3];

    uint64_t start_ms, start_seq, end_ms, end_seq;
    if (!parse_range_id(start_id, start_ms, start_seq)) return "-ERR Invalid start ID\r\n";
    if (!parse_range_id(end_id, end_ms, end_seq)) return "-ERR Invalid end ID\r\n";

    std::vector<std::pair<std::string, StreamEntry>> result_entries;

    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        auto it = streams.find(stream_key);
        if (it == streams.end()) {
            return "*0\r\n";
        }
        const Stream& stream = it->second;

        for (const auto& [entry_id, entry_kv] : stream) {
            uint64_t entry_ms, entry_seq;
            if (!parse_range_id(entry_id, entry_ms, entry_seq)) continue;

            if (id_less_equal(start_ms, start_seq, entry_ms, entry_seq) &&
                id_less_equal(entry_ms, entry_seq, end_ms, end_seq)) {
                result_entries.push_back({entry_id, entry_kv});
            }
            if (!id_less_equal(entry_ms, entry_seq, end_ms, end_seq)) break;
        }
    }

    return encode_xrange_response(result_entries);
}


std::string format_xread_response(const std::vector<std::pair<std::string, std::vector<std::pair<std::string, StreamEntry>>>>& result) {
    
    bool has_any_entries = false;
    for (const auto& [key, entries] : result) {
        if (!entries.empty()) {
            has_any_entries = true;
            break;
        }
    }
    
    if (!has_any_entries) {
        return "*-1\r\n";
    }
    
    std::string resp_out = "*" + std::to_string(result.size()) + "\r\n";
    for (const auto& [key, entries] : result) {
        if (entries.empty()) continue; 
        
        resp_out += "*2\r\n";                  
        resp_out += resp_bulk_string(key);
        resp_out += "*" + std::to_string(entries.size()) + "\r\n"; 
        
        for (const auto& [entry_id, kvs] : entries) {
            resp_out += "*2\r\n"; 
            resp_out += resp_bulk_string(entry_id);

            std::vector<std::string> kv_list;
            for (const auto& [k, v] : kvs) {
                kv_list.push_back(k);
                kv_list.push_back(v);
            }
            resp_out += resp_array(kv_list);
        }
    }
    
    return resp_out;
}

std::string handle_XREAD(const char* resp, int client_fd) {
    auto parts = parse_resp_array(resp);
    if (parts.size() < 4) return "-ERR Invalid XREAD Command\r\n";
    if (to_lower(parts[0]) != "xread") return "-ERR Invalid XREAD Command\r\n";

    
    bool block = false;
    int64_t block_timeout_ms = 0;
    size_t block_pos = 1;
    
    if (to_lower(parts[1]) == "block" && parts.size() >= 5) {
        block = true;
        try {
            block_timeout_ms = std::stoll(parts[2]);
            if (block_timeout_ms < 0) return "-ERR Invalid block timeout\r\n";
        } catch (...) {
            return "-ERR Invalid block timeout\r\n";
        }
        block_pos = 3;
    }

    auto it_streams = std::find(parts.begin() + block_pos, parts.end(), "streams");
    if (it_streams == parts.end()) return "-ERR Missing STREAMS keyword\r\n";

    size_t streams_pos = std::distance(parts.begin(), it_streams);
    size_t total_args_after_streams = parts.size() - (streams_pos + 1);
    if (total_args_after_streams % 2 != 0) return "-ERR Mismatched keys and IDs count\r\n";

    size_t num_streams = total_args_after_streams / 2;
    std::vector<std::string> keys(parts.begin() + streams_pos + 1, parts.begin() + streams_pos + 1 + num_streams);
    std::vector<std::string> ids(parts.begin() + streams_pos + 1 + num_streams, parts.end());

    std::vector<std::pair<std::string, std::vector<std::pair<std::string, StreamEntry>>>> result;
    bool has_data = false;

    {
        std::lock_guard lock(streams_mutex);
        for (size_t i = 0; i < num_streams; ++i) {
            auto& key = keys[i];
            auto& id = ids[i];
            uint64_t last_ms, last_seq;
            if (!parse_range_id(id, last_ms, last_seq)) {
                return "-ERR Invalid stream ID format\r\n";
            }

            auto sit = streams.find(key);
            if (sit == streams.end()) {
                result.emplace_back(key, std::vector<std::pair<std::string, StreamEntry>>{});
                continue;
            }

            const Stream& stream = sit->second;
            std::vector<std::pair<std::string, StreamEntry>> entries;

            if (last_ms == UINT64_MAX - 1 && last_seq == UINT64_MAX - 1) {
                result.emplace_back(key, std::vector<std::pair<std::string, StreamEntry>>{});
                continue;
            }

            for (const auto& [entry_id, entry_kv] : stream) {
                uint64_t entry_ms, entry_seq;
                if (!parse_range_id(entry_id, entry_ms, entry_seq)) continue;

                if ((entry_ms > last_ms) || (entry_ms == last_ms && entry_seq > last_seq)) {
                    entries.push_back({entry_id, entry_kv});
                }
            }

            if (!entries.empty()) {
                has_data = true;
            }
            result.emplace_back(key, std::move(entries));
        }
    }

    if (has_data) {
        return format_xread_response(result);
    }

    if (block) {
        TimePoint expiry;
        
        if (block_timeout_ms == 0) {
            expiry = TimePoint::max();
        } else {
            expiry = Clock::now() + std::chrono::milliseconds(block_timeout_ms);
        }
        
        {
            std::scoped_lock lk(blocked_mutex, streams_mutex);
            for (size_t i = 0; i < num_streams; ++i) {
                const std::string& key = keys[i];
                const std::string& last_id = ids[i];
                
                std::string actual_last_id = last_id;
                if (last_id == "$") {
                    auto sit = streams.find(key);
                    if (sit != streams.end() && !sit->second.empty()) {
                        actual_last_id = sit->second.back().first;
                    } else {
                        actual_last_id = "0-0";
                    }
                }
                
                blocked_stream_clients[key].push_back({client_fd, actual_last_id, expiry});
            }
            blocked_stream_fds.insert(client_fd);
        }

        return "";
    }
    return "*-1\r\n";
}

std::string handle_SAVE(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 1) return "-ERR wrong number of arguments for 'save' command\r\n";
    if (to_lower(parts[0]) != "save") return "-ERR Invalid SAVE Command\r\n";
    
    if (rdb_save(rdb_filename)) {
        return "+OK\r\n";
    } else {
        return "-ERR Failed to save RDB file\r\n";
    }
}

std::string handle_BGSAVE(const char* resp) {
    auto parts = parse_resp_array(resp);
    if (parts.size() != 1) return "-ERR wrong number of arguments for 'bgsave' command\r\n";
    if (to_lower(parts[0]) != "bgsave") return "-ERR Invalid BGSAVE Command\r\n";
    
    // In a real implementation, you'd spawn a separate process for this
    // For simplicity, we'll just do it in the current thread
    std::thread([]() {
        std::cout << "Background saving started by BGSAVE" << std::endl;
        if (rdb_save(rdb_filename)) {
            std::cout << "Background saving completed" << std::endl;
        } else {
            std::cerr << "Background saving failed" << std::endl;
        }
    }).detach();
    
    return "+Background saving started\r\n";
}