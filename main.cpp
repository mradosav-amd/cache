#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
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

constexpr auto KByte = 1024;
constexpr auto MByte = 1024 * 1024;
constexpr auto buffer_size = 10 * KByte;
// constexpr auto buffer_size = 150;
constexpr auto filename = "buffered_storage.bin";

using BufferArray = std::array<uint8_t, buffer_size>;

enum class sample_type : uint32_t { track = 1, process, fragmented_space = 0xFFFF };

constexpr auto minimal_fragmented_memory_size = sizeof(sample_type) + sizeof(size_t);

class storage {
public:
  storage()
      : m_flushing_thread(std::thread([this]() {
          std::filesystem::path path{filename};
          std::ofstream ofs(path, std::ios::binary | std::ios::out);

          if (!ofs) {
            std::cerr << "Error opening file for writing: " << path
                      << std::endl;
            return;
          }

          auto execute_flush = [&](std::ofstream &ofs, bool force = false) {
            auto head = m_head.load();
            auto tail = m_tail.load();

            if (head == tail) {
              return;
            }

            if (head > tail) {
              // std::cout << "HEAD > TAIL : tail: " << tail << " :: count " << head - tail << std::endl;

              if(!force && head - tail < 5 * KByte)
              {
                return;
              }

              ofs.write(reinterpret_cast<const char *>(m_buffer->data() + tail),
                        head - tail);
            } else {

              if(!force && buffer_size - tail + head < 5 * KByte )
              {
                return;
              }

              // std::cout << "HEAD < TAIL : tail: " << tail << " :: count " << buffer_size - tail << std::endl;
              ofs.write(reinterpret_cast<const char *>(m_buffer->data() + tail),
                        buffer_size - tail);

              // std::cout << "HEAD < TAIL : head: " << head << std::endl;
              ofs.write(reinterpret_cast<const char *>(m_buffer->data()), head);
            }
            // std:: cout << "------- FLUSHING --------" << std::endl;
            m_tail.store(head);
          };

          while (m_do_flushing) {
            execute_flush(ofs);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
          }

          execute_flush(ofs, true);
          ofs.close();
        })) {}

  void set_do_flushing(const bool& value)
  {
    m_do_flushing = value;
    std::cout << "--- END ---" << std::endl ;
    m_flushing_thread.join();
    std::cout << "--- END ---" << std::endl ;
  }

  template <typename... T> void store(sample_type type, T &&...values) {

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


private:

  void fragment_memory()
  {
      std::lock_guard lock {m_fragment_mutex};
      auto data = m_buffer->data();
      auto head = m_head.load();

      *reinterpret_cast<sample_type *>(data + head) =
          sample_type::fragmented_space;

      size_t remining_bytes = buffer_size - head - minimal_fragmented_memory_size;

      *reinterpret_cast<size_t *>(data + head + sizeof(sample_type)) =
          remining_bytes;

      // std::cout << "fragmenting memory head at " << head << " / buffer size "
      //           << buffer_size << std::endl;
      m_head.store(0);
  }

  uint8_t *reserve_memory_space(size_t len) {

    // std::cout << m_head.load() << " + " << len << " + " << minimal_fragmented_memory_size << " > " << buffer_size << std::endl;

    auto is_fragmentation_needed = (m_head.load() + len + minimal_fragmented_memory_size) > buffer_size;
    if(is_fragmentation_needed)
    {
      fragment_memory();
    }
    auto size = m_head.fetch_add(len);
    auto result = m_buffer->data() + size;
    memset(result, 0, len);
    return result;
  };

  template <typename... T> size_t get_size(T &...val) {
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
  std::thread m_flushing_thread;
  std::unique_ptr<BufferArray> m_buffer { std::make_unique<BufferArray>() };
  std::atomic_size_t m_head{0};
  std::atomic_size_t m_tail{0};
  std::mutex m_fragment_mutex;
  bool m_do_flushing { true };
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
  static void load(std::filesystem::path path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
      std::cerr << "Error opening file for writing: " << path << std::endl;
      return;
    }
    std::ifstream::pos_type file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    sample_type type;
    size_t sample_size;
    size_t count = 0;
    while(!ifs.eof())
    {
      ifs.read(reinterpret_cast<char*>(&type), sizeof(type));
      ifs.read(reinterpret_cast<char*>(&sample_size), sizeof(sample_size));

      if(sample_size == 0 || ifs.eof())
      {
        continue;
      }

      std::vector<uint8_t> sample;
      // std::cout << "type:" << (int)type << "\n";
      // std::cout << "sample_size:" << sample_size << "\n";
      sample.reserve(sample_size);
      ifs.read(reinterpret_cast<char*>(sample.data()), sample_size);

      switch (type) {
      case sample_type::track: {
        track_sample data;
        parse_data(sample.data(), data.track_name, data.node_id,
                   data.process_id, data.thread_id, data.extdata);

        count++;
        // std::cout << data.track_name << " " << data.node_id << " "
        //           << data.process_id << " " << data.thread_id << " "
        //           << data.extdata << std::endl;
      } break;
      case sample_type::process: {
        process_sample data;
        parse_data(sample.data(), data.guid, data.node_id,
                   data.parent_process_id, data.process_id, data.init,
                   data.fini, data.start, data.end, data.command, data.env,
                   data.extdata);

        // std::cout << data.guid << " " << data.node_id << " "
        //           << data.parent_process_id << " " << data.process_id << " "
        //           << data.init << " " << data.fini << " " << data.start << " "
        //           << data.end << " " << data.command << " " << data.env << " "
        //           << data.extdata << std::endl;
      }
      default:
        break;
      }
    }

    ifs.close();
    std::cout << "Total tracks" << count << std::endl;
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

  // rps_bencmark::init_from_env();

  // auto thread1 = std::thread([]() {
  //   auto node_id = 0;
  //   auto process_id = 1;
  //   auto thread_id = 2;
  //   auto count = 0;
  //   do {
  //     rps_bencmark::start<benchmark::category::WriteTrack>();
  //     cache::store_track("GPU 1", node_id++, process_id++, thread_id++, "{}");
  //     rps_bencmark::end<benchmark::category::WriteTrack>();
  //     count++;
  //   }
  //   while(count != 1000);
  // });

  // auto thread2 = std::thread([]() {
  //   auto node_id = 0;

  //   auto parent_process_id = 1;
  //   auto process_id = 1;
  //   auto init = 2;
  //   auto fini = 2;
  //   auto start = 2;
  //   auto end = 2;
  //   auto count = 0;
  //   do {
  //     rps_bencmark::start<benchmark::category::WriteProcess>();
  //     cache::store_process("GUID", ++node_id, ++parent_process_id,++process_id,
  //                          ++init, ++fini, ++start, ++end,
  //                          "/my/command.exe", "{ BBBBBBBBBBBBBB }",
  //                          "{AAAAAAAAAAAAAAAAAA}");
  //                          count++;
  //     rps_bencmark::end<benchmark::category::WriteProcess>();
  //   }
  //   while(count != 1000);
  // });


  // thread1.join();
  // thread2.join();


  // cache::storage empty;
  // empty.load({"buffered_storage.bin"});
  // empty.read();

  // rps_bencmark::show_results();

  cache::storage buffered_storage;
  size_t node_id = 0;
  auto process_id = 1;
  auto thread_id = 2;
  int count = 0;
  while (count < 100000)
  {
    cache::store_track(buffered_storage, (std::string("GPU ") + std::to_string(count)).c_str(), node_id++, process_id++, thread_id++, "{}");
    count++;
    std::this_thread::sleep_for(std::chrono::microseconds(3));
  }
  buffered_storage.set_do_flushing(false);

  std::cout << "Writing done" << std::endl;

  cache::storage_parser::load({ cache::filename });

  return 0;
}
