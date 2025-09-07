#include "rdb.hpp"
#include "storage.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <arpa/inet.h>
#include <netinet/in.h>

std::string rdb_encode_length(uint64_t len) {
    std::string result;
    if (len < (1 << 6)) {
        // 6-bit length
        result.push_back(static_cast<char>(len & 0x3F));
    } else if (len < (1 << 14)) {
        // 14-bit length
        result.push_back(static_cast<char>(((len >> 8) & 0x3F) | 0x40));
        result.push_back(static_cast<char>(len & 0xFF));
    } else {
        // 32-bit length
        result.push_back(0x80);
        result.push_back(static_cast<char>((len >> 24) & 0xFF));
        result.push_back(static_cast<char>((len >> 16) & 0xFF));
        result.push_back(static_cast<char>((len >> 8) & 0xFF));
        result.push_back(static_cast<char>(len & 0xFF));
    }
    return result;
}

bool rdb_save_string(std::ofstream& file, const std::string& str) {
    std::string len_enc = rdb_encode_length(str.size());
    file.write(len_enc.c_str(), len_enc.size());
    file.write(str.c_str(), str.size());
    return file.good();
}

bool rdb_load_string(std::ifstream& file, std::string& str) {
    uint64_t len = rdb_load_length(file);
    if (file.fail()) return false;
    
    str.resize(len);
    file.read(&str[0], len);
    return file.good();
}

uint64_t rdb_load_length(std::ifstream& file) {
    unsigned char byte;
    file.read(reinterpret_cast<char*>(&byte), 1);
    if (file.fail()) return 0;
    
    uint64_t len;
    if ((byte & 0xC0) == 0) {
        // 6-bit length
        len = byte & 0x3F;
    } else if ((byte & 0xC0) == 0x40) {
        // 14-bit length
        len = (byte & 0x3F) << 8;
        file.read(reinterpret_cast<char*>(&byte), 1);
        if (file.fail()) return 0;
        len |= byte;
    } else if (byte == 0x80) {
        // 32-bit length
        uint32_t len32;
        file.read(reinterpret_cast<char*>(&len32), 4);
        if (file.fail()) return 0;
        // Convert from big-endian to host byte order
        len = (static_cast<uint64_t>(ntohl(len32)));
    } else {
        file.setstate(std::ios::failbit);
        return 0;
    }
    return len;
}

bool rdb_save(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open RDB file for writing: " << filename << std::endl;
        return false;
    }
    
    // Write Redis RDB header "REDIS0001"
    const char header[] = "REDIS0001";
    file.write(header, 9);
    
    // Write AUX field (optional)
    std::string aux_key = "redis-ver";
    std::string aux_val = "6.0.0";
    file.put(RDB_OPCODE_AUX);
    rdb_save_string(file, aux_key);
    rdb_save_string(file, aux_val);
    
    // Write current time as AUX field
    aux_key = "redis-bits";
    aux_val = std::to_string(64); // 64-bit system
    file.put(RDB_OPCODE_AUX);
    rdb_save_string(file, aux_key);
    rdb_save_string(file, aux_val);
    
    // Write SELECTDB opcode
    file.put(RDB_OPCODE_SELECTDB);
    
    // Write database size (we only use DB 0)
    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        uint64_t db_size = redis_storage.size() + lists.size() + streams.size();
        std::string db_size_enc = rdb_encode_length(db_size);
        file.write(db_size_enc.c_str(), db_size_enc.size());
    }
    
    // Save strings
    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        for (const auto& [key, value] : redis_storage) {
            // Skip expired keys
            if (value.expiry != TimePoint::min() && 
                Clock::now() >= value.expiry) {
                continue;
            }
            
            // Write value type (string)
            file.put(RDB_STRING_ENCODING);
            
            // Write key
            rdb_save_string(file, key);
            
            // Write expiry if needed
            if (value.expiry != TimePoint::min()) {
                file.put(RDB_OPCODE_EXPIRETIME_MS);
                auto expiry_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    value.expiry.time_since_epoch()).count();
                file.write(reinterpret_cast<const char*>(&expiry_ms), sizeof(expiry_ms));
            }
            
            // Write value
            rdb_save_string(file, value.value);
        }
    }
    
    // Save lists
    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        for (const auto& [key, list] : lists) {
            // Write value type (list)
            file.put(RDB_LIST_ENCODING);
            
            // Write key
            rdb_save_string(file, key);
            
            // Write list size
            std::string list_size_enc = rdb_encode_length(list.size());
            file.write(list_size_enc.c_str(), list_size_enc.size());
            
            // Write list elements
            for (const auto& element : list) {
                rdb_save_string(file, element);
            }
        }
    }
    
    // Save streams
    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        for (const auto& [key, stream] : streams) {
            // Write value type (stream)
            file.put(RDB_STREAM_ENCODING);
            
            // Write key
            rdb_save_string(file, key);
            
            // Write stream size
            std::string stream_size_enc = rdb_encode_length(stream.size());
            file.write(stream_size_enc.c_str(), stream_size_enc.size());
            
            // Write stream entries
            for (const auto& [entry_id, entry_data] : stream) {
                // Write entry ID
                rdb_save_string(file, entry_id);
                
                // Write entry field count
                std::string field_count_enc = rdb_encode_length(entry_data.size());
                file.write(field_count_enc.c_str(), field_count_enc.size());
                
                // Write entry fields
                for (const auto& [field, value] : entry_data) {
                    rdb_save_string(file, field);
                    rdb_save_string(file, value);
                }
            }
        }
    }
    
    // Write EOF opcode
    file.put(RDB_OPCODE_EOF);
    
    // Write CRC64 checksum (simplified - in a real implementation, you'd calculate this)
    uint64_t crc = 0; // Placeholder for CRC
    file.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    
    file.close();
    return file.good();
}

bool rdb_load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "No RDB file found: " << filename << std::endl;
        return false;
    }
    
    // Read and verify header
    char header[10];
    file.read(header, 9);
    header[9] = '\0';
    if (std::string(header) != "REDIS0001") {
        std::cerr << "Invalid RDB file format" << std::endl;
        return false;
    }
    
    // Clear existing data
    {
        std::lock_guard<std::mutex> lock1(storage_mutex);
        std::lock_guard<std::mutex> lock2(streams_mutex);
        redis_storage.clear();
        lists.clear();
        streams.clear();
    }
    
    // Read file contents
    while (file.good()) {
        unsigned char opcode;
        file.read(reinterpret_cast<char*>(&opcode), 1);
        if (!file.good()) break;
        
        switch (opcode) {
            case RDB_OPCODE_AUX: {
                // Skip AUX fields
                std::string aux_key, aux_val;
                if (!rdb_load_string(file, aux_key) || !rdb_load_string(file, aux_val)) {
                    std::cerr << "Failed to read AUX field" << std::endl;
                    return false;
                }
                break;
            }
            
            case RDB_OPCODE_SELECTDB: {
                // We only support DB 0, so just read and ignore the DB number
                uint64_t db_num = rdb_load_length(file);
                if (file.fail()) {
                    std::cerr << "Failed to read DB number" << std::endl;
                    return false;
                }
                break;
            }
            
            case RDB_OPCODE_RESIZEDB: {
                // Read hash table and expiry hash table sizes (we ignore these)
                uint64_t db_size = rdb_load_length(file);
                uint64_t expiry_size = rdb_load_length(file);
                if (file.fail()) {
                    std::cerr << "Failed to read DB size info" << std::endl;
                    return false;
                }
                break;
            }
            
            case RDB_OPCODE_EXPIRETIME_MS: {
                // This should be followed by a value type, which we'll handle in the value reading code
                // For now, we'll just note that the next value has an expiry
                int64_t expiry_ms;
                file.read(reinterpret_cast<char*>(&expiry_ms), sizeof(expiry_ms));
                if (file.fail()) {
                    std::cerr << "Failed to read expiry time" << std::endl;
                    return false;
                }
                // We'll handle this in the value reading code
                break;
            }
            
            case RDB_OPCODE_EOF: {
                // End of file reached successfully
                return true;
            }
            
            case RDB_STRING_ENCODING: {
                // Read string value
                std::string key, value;
                if (!rdb_load_string(file, key) || !rdb_load_string(file, value)) {
                    std::cerr << "Failed to read string value" << std::endl;
                    return false;
                }
                
                {
                    std::lock_guard<std::mutex> lock(storage_mutex);
                    redis_storage[key] = {value, TimePoint::min()};
                }
                break;
            }
            
            case RDB_LIST_ENCODING: {
                // Read list value
                std::string key;
                if (!rdb_load_string(file, key)) {
                    std::cerr << "Failed to read list key" << std::endl;
                    return false;
                }
                
                uint64_t list_size = rdb_load_length(file);
                if (file.fail()) {
                    std::cerr << "Failed to read list size" << std::endl;
                    return false;
                }
                
                std::vector<std::string> list;
                for (uint64_t i = 0; i < list_size; i++) {
                    std::string element;
                    if (!rdb_load_string(file, element)) {
                        std::cerr << "Failed to read list element" << std::endl;
                        return false;
                    }
                    list.push_back(element);
                }
                
                {
                    std::lock_guard<std::mutex> lock(storage_mutex);
                    lists[key] = list;
                }
                break;
            }
            
            case RDB_STREAM_ENCODING: {
                // Read stream value
                std::string key;
                if (!rdb_load_string(file, key)) {
                    std::cerr << "Failed to read stream key" << std::endl;
                    return false;
                }
                
                uint64_t stream_size = rdb_load_length(file);
                if (file.fail()) {
                    std::cerr << "Failed to read stream size" << std::endl;
                    return false;
                }
                
                Stream stream;
                for (uint64_t i = 0; i < stream_size; i++) {
                    std::string entry_id;
                    if (!rdb_load_string(file, entry_id)) {
                        std::cerr << "Failed to read stream entry ID" << std::endl;
                        return false;
                    }
                    
                    uint64_t field_count = rdb_load_length(file);
                    if (file.fail()) {
                        std::cerr << "Failed to read stream field count" << std::endl;
                        return false;
                    }
                    
                    StreamEntry entry;
                    for (uint64_t j = 0; j < field_count; j++) {
                        std::string field, value;
                        if (!rdb_load_string(file, field) || !rdb_load_string(file, value)) {
                            std::cerr << "Failed to read stream field" << std::endl;
                            return false;
                        }
                        entry[field] = value;
                    }
                    
                    stream.push_back({entry_id, entry});
                }
                
                {
                    std::lock_guard<std::mutex> lock(streams_mutex);
                    streams[key] = stream;
                }
                break;
            }
            
            default:
                std::cerr << "Unknown RDB opcode: " << static_cast<int>(opcode) << std::endl;
                return false;
        }
    }
    
    return true;
}