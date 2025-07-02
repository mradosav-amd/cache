#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ratio>
#include <stdint.h>
#include <string.h>
#include <string>
#include <thread>
#include <type_traits>
#include <condition_variable>
#include "benchmark.hpp"

struct track_sample {
  std::string track_name;
  size_t node_id;
  size_t process_id;
  size_t thread_id;
  std::string extdata;
};

struct process_sample {
  std::string guid;
  size_t node_id;
  size_t parent_process_id;
  size_t process_id;
  size_t init;
  size_t fini;
  size_t start;
  size_t end;
  std::string command;
  std::string env;
  std::string extdata;
};

namespace cache {
enum class sample_type : uint32_t { track = 1, process, fragmented_space = 0xFFFF };

constexpr auto KByte = 1024;
constexpr auto MByte = 1024 * 1024;
constexpr auto buffer_size = 10 * MByte;
constexpr auto flush_treshhold = 5 * MByte;
constexpr auto filename = "buffered_storage.bin";

constexpr auto minimal_fragmented_memory_size = sizeof(sample_type) + sizeof(size_t);
using buffer_array = std::array<uint8_t, buffer_size>;

class storage {
public:
  storage()
      : m_flushing_thread(std::thread([this]() {
          std::filesystem::path path{filename};
          std::ofstream ofs(path, std::ios::binary | std::ios::out);
          std::mutex flushing_mutex;

          if (!ofs) {
            std::cerr << "Error opening file for writing: " << path
                      << std::endl;
            return;
          }

          auto execute_flush = [&](std::ofstream &ofs, bool force = false) {
            size_t head,tail;
            {
              std::lock_guard guard{m_mutex};
              head = m_head;
              tail = m_tail;

              if (head == tail) {
                return;
              }

              auto used_space = m_head > m_tail
                                    ? (m_head - m_tail)
                                    : (buffer_size - m_tail + m_head);
              if (!force && used_space < flush_treshhold) {
                return;
              }
              m_tail = m_head;
            }

            if (head > tail) {
              ofs.write(reinterpret_cast<const char *>(m_buffer->data() + tail),
                        head - tail);
            } else {
              ofs.write(reinterpret_cast<const char *>(m_buffer->data() + tail),
                        buffer_size - tail);
              ofs.write(reinterpret_cast<const char *>(m_buffer->data()), head);
            }
          };

          while (!m_shutdown) {
            execute_flush(ofs);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
          }

          execute_flush(ofs, true);
          ofs.close();
        }
      )) {}


  template <typename... T> void store(sample_type type, T &&...values)
  {
    auto arg_size = get_size(values...);
    size_t total_size = arg_size + sizeof(type) + sizeof(size_t);
    auto reserved_memory = reserve_memory_space(total_size);
    size_t position = 0;

    auto store_value = [&](const auto &val) {
      using Type = decltype(val);
      size_t len = 0;
      auto dest = reserved_memory + position;
      if constexpr (std::is_same_v<std::decay_t<Type>, const char *>) {
        len = strlen(val) + 1;
        std::memcpy(dest, val, len);
      } else {
        using ClearType = std::remove_const_t<std::remove_reference_t<decltype(val)>>;
        len = sizeof(ClearType);
        *reinterpret_cast<ClearType*>(dest) = val;
      }
      position += len;
    };

    store_value(type);
    store_value(arg_size);

    (store_value(values), ...);
  }

  void shutdown()
  {
    m_shutdown = false;
    m_flushing_thread.join();
  }

private:

  void fragment_memory()
  {
      auto data = m_buffer->data();
      memset(data + m_head, 0xFFFF, buffer_size - m_head);
      *reinterpret_cast<sample_type *>(data + m_head) =
          sample_type::fragmented_space;

      size_t remining_bytes = buffer_size - m_head - minimal_fragmented_memory_size;
      *reinterpret_cast<size_t *>(data + m_head + sizeof(sample_type)) =
          remining_bytes;
      m_head = 0;
  }


  uint8_t *reserve_memory_space(size_t len)
  {
    size_t size;
    {
      std::lock_guard scope{ m_mutex };

      if ((m_head + len + minimal_fragmented_memory_size) > buffer_size) {
        fragment_memory();
      }
      size = m_head;
      m_head = m_head + len;
    }

    auto result = m_buffer->data() + size;
    memset(result, 0, len);
    return result;
  };

  template <typename... T> size_t get_size(T &...val)
  {
    auto get_size_impl = [&](auto val) {
      using Type = decltype(val);
      if constexpr (std::is_same_v<Type, const char *>) {
        return strlen(val) + 1;
      } else {
        return sizeof(Type);
      }
    };

    auto total_size = 0;
    ((total_size += get_size_impl(val)), ...);
    return total_size;
  }

private:
  std::mutex m_mutex;
  bool m_shutdown { true };
  std::thread m_flushing_thread;
  size_t m_head{0};
  size_t m_tail{0};
  std::unique_ptr<buffer_array> m_buffer { std::make_unique<buffer_array>() };
};

class storage_parser
{

  template <typename T>
  static void process_arg(const uint8_t *&data_pos, T &arg) {
    if constexpr (std::is_same_v<T, std::string>) {
      arg = std::string((const char *)data_pos);
      data_pos += arg.size() + 1;
    } else {
      arg = *reinterpret_cast<const T *>(data_pos);
      data_pos += sizeof(T);
    }
  }

  template <typename... Args>
  static void parse_data(const uint8_t *data_pos, Args &...args) {
    (process_arg(data_pos, args), ...);
  }
public:
  static size_t load(std::filesystem::path path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
      std::cerr << "Error opening file for writing: " << path << std::endl;
      return 0;
    }
    std::ifstream::pos_type file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    sample_type type;
    size_t sample_size;
    size_t total_count = 0;
    size_t track_count = 0;
    size_t process_count = 0;

    while(!ifs.eof())
    {

      ifs.read(reinterpret_cast<char*>(&type), sizeof(type));
      ifs.read(reinterpret_cast<char*>(&sample_size), sizeof(sample_size));


      if(sample_size == 0 || ifs.eof())
      {
        continue;
      }

      std::vector<uint8_t> sample;
      sample.reserve(sample_size);
      ifs.read(reinterpret_cast<char*>(sample.data()), sample_size);

      switch (type) {
      case sample_type::track: {
        track_sample data;
        parse_data(sample.data(), data.track_name, data.node_id,
                   data.process_id, data.thread_id, data.extdata);

        if(data.node_id == track_count++) {
          total_count++;
        }
        break;
      }
      case sample_type::process: {
        process_sample data;
        parse_data(sample.data(), data.guid, data.node_id,
                   data.parent_process_id, data.process_id, data.init,
                   data.fini, data.start, data.end, data.command, data.env,
                   data.extdata);
        if(data.node_id == process_count++) {
          total_count++;
        }

        break;
      }
      default:
        break;
      }
    }

    ifs.close();
    return total_count;
  }
};

void store_track(storage& buffered_storage, const char *track_name, size_t node_id, size_t process_id,
                 size_t thread_id, const char *extdata) {
  buffered_storage.store(sample_type::track, track_name, node_id, process_id,
                         thread_id, extdata);
}
void store_process(storage& buffered_storage, std::string guid, size_t node_id, size_t parent_process_id,
                   size_t process_id, size_t init, size_t fini, size_t start,
                   size_t end, std::string command, std::string env,
                   std::string extdata) {
  buffered_storage.store(sample_type::process, guid.c_str(), node_id,
                         parent_process_id, process_id, init, fini, start, end,
                         command.c_str(), env.c_str(), extdata.c_str());
}
} // namespace cache

int main() {

  rps_bencmark::init_from_env();

  const auto number_of_iterations = 10000000;
  cache::storage buffered_storage;

  std::vector<std::thread> threads;

  threads.push_back(std::thread([&]() {

    auto node_id = 0;
    auto process_id = 1;
    auto thread_id = 2;
    auto count = 0;
    do {
      rps_bencmark::start<benchmark::category::WriteTrack>();
      cache::store_track(buffered_storage, "GPU 1", node_id++, process_id++, thread_id++, "{}");
      rps_bencmark::end<benchmark::category::WriteTrack>();
      count++;
    }
    while(count != number_of_iterations);
  }));

  threads.push_back(std::thread([&]() {
    auto node_id = 0;

    auto parent_process_id = 1;
    auto process_id = 1;
    auto init = 2;
    auto fini = 2;
    auto start = 2;
    auto end = 2;
    auto count = 0;
    do {
      rps_bencmark::start<benchmark::category::WriteProcess>();
      cache::store_process(buffered_storage, "GUID", node_id++, parent_process_id++,process_id++,
                           init++, fini++, start++, end++,
                           "/my/command.exe", "{ BBBBBBBBBBBBBB }",
                           "{AAAAAAAAAAAAAAAAAA}");
                           count++;
      rps_bencmark::end<benchmark::category::WriteProcess>();
    }
    while(count != number_of_iterations);
  }));

  for(auto& thread : threads) {
    thread.join();
  }

  buffered_storage.shutdown();

  std::cout << "Writing to cache is done. Validating cache.." << std::endl;

  auto read_count = cache::storage_parser::load({ cache::filename });
  auto expected_count = threads.size() * number_of_iterations;
  std::cout << (read_count != expected_count ? "Validation failed." : "Validation successful.") << std::endl;

  rps_bencmark::show_results();
  return 0;
}
