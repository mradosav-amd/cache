#pragma once
#include <array>
#include <bits/chrono.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdint.h>
#include <string.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "cacheable.hpp"

struct pmc_event {
  std::string pmc_info_name;
  uint32_t value;
};

class buffered_storage {
public:
  buffered_storage()
      : m_flushing_thread(std::thread([this]() {
          auto filepath = get_buffered_storage_filename(0, 0);
          std::ofstream _ofs(filepath, std::ios::binary | std::ios::out);

          if (!_ofs) {
            std::stringstream _ss;
            _ss << "Error opening file for writing: " << filepath;
            throw std::runtime_error(_ss.str());
          }

          auto execute_flush = [&](std::ofstream &ofs, bool force = false) {
            size_t _head, _tail;
            {
              std::lock_guard guard{m_mutex};
              _head = m_head;
              _tail = m_tail;

              if (_head == _tail) {
                return;
              }

              auto used_space = m_head > m_tail
                                    ? (m_head - m_tail)
                                    : (buffer_size - m_tail + m_head);
              if (!force && used_space < flush_threshold) {
                return;
              }
              m_tail = m_head;
            }

            if (_head > _tail) {
              ofs.write(
                  reinterpret_cast<const char *>(m_buffer->data() + _tail),
                  _head - _tail);
            } else {
              ofs.write(
                  reinterpret_cast<const char *>(m_buffer->data() + _tail),
                  buffer_size - _tail);
              ofs.write(reinterpret_cast<const char *>(m_buffer->data()),
                        _head);
            }
          };

          std::mutex _shutdown_condition_mutex;
          while (m_running) {
            execute_flush(_ofs);
            std::unique_lock _lock{_shutdown_condition_mutex};
            m_shutdown_condition.wait_for(
                _lock, std::chrono::milliseconds(CACHE_FILE_FLUSH_TIMEOUT),
                [&]() { return !m_running; });
          }

          execute_flush(_ofs, true);
          _ofs.close();
          m_exit_finished = true;
          m_exit_condition.notify_one();
        })) {}

  template <typename... T> void store(entry_type type, T &&...values) {

    constexpr bool is_supported_type =
        (supported_types::is_supported<T> && ...);
    static_assert(is_supported_type,
                  "Supported types are const char*, char*, "
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");

    auto arg_size = get_size(values...);
    auto total_size = arg_size + sizeof(type) + sizeof(size_t);
    auto *reserved_memory = reserve_memory_space(total_size);
    size_t position = 0;

    auto store_value = [&](const auto &val) {
      using Type = decltype(val);
      size_t len = 0;
      auto *dest = reserved_memory + position;
      if constexpr (std::is_same_v<std::decay_t<Type>, const char *>) {
        len = strlen(val) + 1;
        std::memcpy(dest, val, len);
      } else if constexpr (std::is_same_v<std::decay_t<Type>,
                                          std::vector<uint8_t>>) {
        size_t elem_count = val.size();
        len = elem_count + sizeof(size_t);
        std::memcpy(dest, &elem_count, sizeof(size_t));
        std::memcpy(dest + sizeof(size_t), val.data(), val.size());
      } else {
        using ClearType = std::decay_t<decltype(val)>;
        len = sizeof(ClearType);
        *reinterpret_cast<ClearType *>(dest) = val;
      }
      position += len;
    };

    store_value(type);
    store_value(arg_size);

    (store_value(values), ...);
  }

private:
  void shutdown() {
    std::cout << "Buffer storage shutting down..";
    m_running = false;
    m_shutdown_condition.notify_all();

    std::mutex _exit_mutex;
    std::unique_lock _exit_lock{_exit_mutex};
    m_exit_condition.wait(_exit_lock, [&]() { return m_exit_finished; });
  }

  void fragment_memory() {
    auto *_data = m_buffer->data();
    memset(_data + m_head, 0xFFFF, buffer_size - m_head);
    *reinterpret_cast<entry_type *>(_data + m_head) =
        entry_type::fragmented_space;

    size_t remaining_bytes =
        buffer_size - m_head - minimal_fragmented_memory_size;
    *reinterpret_cast<size_t *>(_data + m_head + sizeof(entry_type)) =
        remaining_bytes;
    m_head = 0;
  }

  uint8_t *reserve_memory_space(size_t len) {
    size_t _size;
    {
      std::lock_guard scope{m_mutex};

      if ((m_head + len + minimal_fragmented_memory_size) > buffer_size) {
        fragment_memory();
      }
      _size = m_head;
      m_head = m_head + len;
    }

    auto *_result = m_buffer->data() + _size;
    memset(_result, 0, len);
    return _result;
  }

  bool is_running() const { return m_running; }

  template <typename... Types> struct typelist {
    template <typename T>
    constexpr static bool is_supported =
        (std::is_same_v<std::decay_t<T>, Types> || ...);
  };

  using supported_types =
      typelist<const char *, char *, uint64_t, int32_t, uint32_t,
               std::vector<uint8_t>, uint8_t, int64_t, double>;

  template <typename T>
  static constexpr bool is_string_literal_v =
      std::is_same_v<std::decay_t<T>, const char *> ||
      std::is_same_v<std::decay_t<T>, char *>;

  template <typename T> constexpr size_t get_size_impl(T &&val) {
    if constexpr (is_string_literal_v<T>) {
      size_t size = 0;
      while (val[size] != '\0') {
        size++;
      }
      return ++size;
    } else if constexpr (std::is_same_v<std::decay_t<T>,
                                        std::vector<uint8_t>>) {
      return val.size() + sizeof(size_t);
    } else {
      return sizeof(T);
    }
  }

  template <typename... T> constexpr size_t get_size(T &&...val) {
    auto total_size = 0;
    ((total_size += get_size_impl(val)), ...);
    return total_size;
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_exit_condition;
  bool m_exit_finished{false};
  bool m_running{true};
  std::condition_variable m_shutdown_condition;
  std::thread m_flushing_thread;

  size_t m_head{0};
  size_t m_tail{0};
  std::unique_ptr<buffer_array_t> m_buffer{std::make_unique<buffer_array_t>()};
  pid_t m_created_process;
};
