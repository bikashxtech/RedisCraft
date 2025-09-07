// Add to storage.hpp or create a new rdb.hpp file
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

const uint8_t RDB_OPCODE_EOF = 0xFF;
const uint8_t RDB_OPCODE_SELECTDB = 0xFE;
const uint8_t RDB_OPCODE_RESIZEDB = 0xFB;
const uint8_t RDB_OPCODE_EXPIRETIME_MS = 0xFC;
const uint8_t RDB_OPCODE_AUX = 0xFA;
const uint8_t RDB_STRING_ENCODING = 0x00;
const uint8_t RDB_LIST_ENCODING = 0x01;
const uint8_t RDB_STREAM_ENCODING = 0x02;

std::string rdb_encode_length(uint64_t len);
bool rdb_save_string(std::ofstream& file, const std::string& str);
bool rdb_load_string(std::ifstream& file, std::string& str);
uint64_t rdb_load_length(std::ifstream& file);
bool rdb_save(const std::string& filename);
bool rdb_load(const std::string& filename);