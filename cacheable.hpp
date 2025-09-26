#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>

struct cacheable_t {};

enum class entry_type : uint32_t {
  track_sample = 0,
  process_sample = 1,
  fragmented_space = 0xFFFF
};

constexpr auto KByte = 1024;
constexpr auto MByte = 1024 * 1024;
constexpr size_t buffer_size = 100 * MByte;
constexpr size_t flush_threshold = 80 * MByte;
constexpr auto CACHE_FILE_FLUSH_TIMEOUT = 10 * 1000; // ms

const auto tmp_directory = std::string{"/tmp/"};

const auto get_buffered_storage_filename = [](const int &ppid, const int &pid) {
  return std::string{tmp_directory + "buffered_storage_" +
                     std::to_string(ppid) + "_" + std::to_string(pid) + ".bin"};
};

constexpr size_t minimal_fragmented_memory_size =
    sizeof(entry_type) + sizeof(size_t);
using buffer_array_t = std::array<uint8_t, buffer_size>;