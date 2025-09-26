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
#include <thread>
#include <type_traits>

#include "cacheable.hpp"

struct flush_worker
{
    void execute() { std::cout << "test" << std::endl; }
};

struct my_worker_factory
{
    flush_worker get_worker() { return {}; };
};

template <typename worker_factory>
class buffered_storage
{
public:
    // buffered_storage()
    //     : m_flushing_thread(std::thread([this]() {
    //         auto filepath = get_buffered_storage_filename(0, 0);
    //         std::ofstream _ofs(filepath, std::ios::binary | std::ios::out);

    //         if (!_ofs) {
    //           std::stringstream _ss;
    //           _ss << "Error opening file for writing: " << filepath;
    //           throw std::runtime_error(_ss.str());
    //         }

    //         auto execute_flush = [&](std::ofstream &ofs, bool force = false) {
    //           size_t _head, _tail;
    //           {
    //             std::lock_guard guard{m_mutex};
    //             _head = m_head;
    //             _tail = m_tail;

    //             if (_head == _tail) {
    //               return;
    //             }

    //             auto used_space = m_head > m_tail
    //                                   ? (m_head - m_tail)
    //                                   : (buffer_size - m_tail + m_head);
    //             if (!force && used_space < flush_threshold) {
    //               return;
    //             }
    //             m_tail = m_head;
    //           }

    //           if (_head > _tail) {
    //             ofs.write(
    //                 reinterpret_cast<const char *>(m_buffer->data() + _tail),
    //                 _head - _tail);
    //           } else {
    //             ofs.write(
    //                 reinterpret_cast<const char *>(m_buffer->data() + _tail),
    //                 buffer_size - _tail);
    //             ofs.write(reinterpret_cast<const char *>(m_buffer->data()),
    //                       _head);
    //           }
    //         };

    //         std::mutex _shutdown_condition_mutex;
    //         while (m_running) {
    //           execute_flush(_ofs);
    //           std::unique_lock _lock{_shutdown_condition_mutex};
    //           m_shutdown_condition.wait_for(
    //               _lock, std::chrono::milliseconds(CACHE_FILE_FLUSH_TIMEOUT),
    //               [&]() { return !m_running; });
    //         }

    //         execute_flush(_ofs, true);
    //         _ofs.close();
    //         m_exit_finished = true;
    //         m_exit_condition.notify_one();
    //       })) {}

    template <typename Tp>
    auto store(const Tp& val)
    {
        check_type<Tp>();
        size_t sample_size = get_size(val);
        size_t to_reserve  = header_size + sample_size;
        auto*  buf         = reserve_memory_space(to_reserve);
        size_t position    = 0;
        store_value(static_cast<std::underlying_type_t<decltype(Tp::type_identifier)>>(
                        Tp::type_identifier),
                    buf, position);
        store_value(sample_size, buf, position);
        serialize(buf + position, val);
    }

    void shutdown()
    {
        // std::cout << "Buffer storage shutting down..";
        // m_running = false;
        // m_shutdown_condition.notify_all();

        // std::mutex _exit_mutex;
        // std::unique_lock _exit_lock{_exit_mutex};
        // m_exit_condition.wait(_exit_lock, [&]() { return m_exit_finished; });

        worker_factory f;
        auto           worker = f.get_worker();
        worker.execute();

        auto          filepath = get_buffered_storage_filename(0, 0);
        std::ofstream _ofs(filepath, std::ios::binary | std::ios::out);

        if(!_ofs)
        {
            std::stringstream _ss;
            _ss << "Error opening file for writing: " << filepath;
            throw std::runtime_error(_ss.str());
        }

        auto execute_flush = [&](std::ofstream& ofs, bool force = false) {
            size_t _head, _tail;
            {
                std::lock_guard guard{ m_mutex };
                _head = m_head;
                _tail = m_tail;

                if(_head == _tail)
                {
                    return;
                }

                auto used_space =
                    m_head > m_tail ? (m_head - m_tail) : (buffer_size - m_tail + m_head);
                if(!force && used_space < flush_threshold)
                {
                    return;
                }
                m_tail = m_head;
            }

            if(_head > _tail)
            {
                ofs.write(reinterpret_cast<const char*>(m_buffer->data() + _tail),
                          _head - _tail);
            }
            else
            {
                ofs.write(reinterpret_cast<const char*>(m_buffer->data() + _tail),
                          buffer_size - _tail);
                ofs.write(reinterpret_cast<const char*>(m_buffer->data()), _head);
            }
        };

        execute_flush(_ofs, true);
    }

private:
    void fragment_memory()
    {
        auto* _data = m_buffer->data();
        memset(_data + m_head, 0xFFFF, buffer_size - m_head);
        *reinterpret_cast<type_identifier_t*>(_data + m_head) =
            type_identifier_t::fragmented_space;

        size_t remaining_bytes = buffer_size - m_head - header_size;
        *reinterpret_cast<size_t*>(_data + m_head + sizeof(type_identifier_t)) =
            remaining_bytes;
        m_head = 0;
    }

    uint8_t* reserve_memory_space(size_t len)
    {
        size_t _size;
        {
            std::lock_guard scope{ m_mutex };

            if((m_head + len + header_size) > buffer_size)
            {
                fragment_memory();
            }
            _size  = m_head;
            m_head = m_head + len;
        }

        auto* _result = m_buffer->data() + _size;
        memset(_result, 0, len);
        return _result;
    }

    bool is_running() const { return m_running; }

private:
    std::mutex              m_mutex;
    std::condition_variable m_exit_condition;
    bool                    m_exit_finished{ false };
    bool                    m_running{ true };
    std::condition_variable m_shutdown_condition;
    std::thread             m_flushing_thread;

    size_t                          m_head{ 0 };
    size_t                          m_tail{ 0 };
    std::unique_ptr<buffer_array_t> m_buffer{ std::make_unique<buffer_array_t>() };
    pid_t                           m_created_process;
};
