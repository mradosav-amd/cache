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

struct cache_buffer_t
{
    std::mutex                      mutex;
    size_t                          head{ 0 };
    size_t                          tail{ 0 };
    std::unique_ptr<buffer_array_t> array{ std::make_unique<buffer_array_t>() };
};
using cache_buffer_ptr_t = std::shared_ptr<cache_buffer_t>;

struct worker_synchronization_t
{
    std::condition_variable is_running_condition;
    bool                    is_running{ true };

    std::condition_variable exit_finished_condition;
    bool                    exit_finished{ false };
};
using worker_synchronization_ptr_t = std::shared_ptr<worker_synchronization_t>;

struct flush_worker
{
    explicit flush_worker(cache_buffer_ptr_t           cache_buffer_ptr,
                          worker_synchronization_ptr_t worker_synchronization_ptr)
    : m_buffer_ptr(std::move(cache_buffer_ptr))
    , m_worker_synchronization(std::move(worker_synchronization_ptr))
    {}

    void execute_flush(std::ofstream& ofs, bool force = false)
    {
        size_t _head, _tail;
        {
            std::lock_guard guard{ m_buffer_ptr->mutex };
            _head = m_buffer_ptr->head;
            _tail = m_buffer_ptr->tail;

            if(_head == _tail)
            {
                return;
            }

            auto used_space =
                m_buffer_ptr->head > m_buffer_ptr->tail
                    ? (m_buffer_ptr->head - m_buffer_ptr->tail)
                    : (buffer_size - m_buffer_ptr->tail + m_buffer_ptr->head);
            if(!force && used_space < flush_threshold)
            {
                return;
            }
            m_buffer_ptr->tail = m_buffer_ptr->head;
        }

        if(_head > _tail)
        {
            ofs.write(reinterpret_cast<const char*>(m_buffer_ptr->array->data() + _tail),
                      _head - _tail);
        }
        else
        {
            ofs.write(reinterpret_cast<const char*>(m_buffer_ptr->array->data() + _tail),
                      buffer_size - _tail);
            ofs.write(reinterpret_cast<const char*>(m_buffer_ptr->array->data()), _head);
        }
    };

    void operator()()
    {
        auto          filepath = get_buffered_storage_filename(0, 0);
        std::ofstream _ofs(filepath, std::ios::binary | std::ios::out);

        if(!_ofs)
        {
            std::stringstream _ss;
            _ss << "Error opening file for writing: " << filepath;
            throw std::runtime_error(_ss.str());
        }

        std::mutex _shutdown_condition_mutex;
        while(m_worker_synchronization->is_running)
        {
            execute_flush(_ofs);
            std::unique_lock _lock{ _shutdown_condition_mutex };
            m_worker_synchronization->is_running_condition.wait_for(
                _lock, std::chrono::milliseconds(CACHE_FILE_FLUSH_TIMEOUT),
                [&]() { return !m_worker_synchronization->is_running; });
        }

        execute_flush(_ofs, true);
        _ofs.close();
        m_worker_synchronization->exit_finished = true;
        m_worker_synchronization->exit_finished_condition.notify_one();
    }

private:
    cache_buffer_ptr_t           m_buffer_ptr;
    worker_synchronization_ptr_t m_worker_synchronization;
};

struct worker_factory
{
    worker_factory()                            = delete;
    worker_factory(worker_factory&)             = delete;
    worker_factory& operator=(worker_factory&)  = delete;
    worker_factory(worker_factory&&)            = delete;
    worker_factory& operator=(worker_factory&&) = delete;

    template <typename WorkerType>
    static WorkerType get_worker(
        const cache_buffer_ptr_t&           cache_buffer_ptr,
        const worker_synchronization_ptr_t& worker_synchronization_ptr)
    {
        return WorkerType(cache_buffer_ptr, worker_synchronization_ptr);
    };
};

template <typename WorkerType, typename TypeIdentifierEnum>
class buffered_storage
{
    static_assert(is_enum_class_v<TypeIdentifierEnum>,
                  "TypeIdentifierEnum must be an enum class");

public:
    explicit buffered_storage()
    : m_flushing_thread{ worker_factory::get_worker<WorkerType>(
          m_cache_buffer_ptr, m_worker_synchronization) }
    {}

    ~buffered_storage() { m_flushing_thread.detach(); }

    template <typename Type>
    auto store(const Type& value)
    {
        check_type<Type, TypeIdentifierEnum>();

        using TypeIdentifierEnumUderlayingType =
            std::underlying_type_t<TypeIdentifierEnum>;

        size_t sample_size      = get_size(value);
        size_t bytes_to_reserve = header_size<TypeIdentifierEnum> + sample_size;
        auto*  buf              = reserve_memory_space(bytes_to_reserve);
        size_t position         = 0;
        auto   type_identifier_value =
            static_cast<TypeIdentifierEnumUderlayingType>(Type::type_identifier);

        store_value(type_identifier_value, buf, position);
        store_value(sample_size, buf, position);
        serialize(buf + position, value);
    }

    void shutdown()
    {
        std::cout << "Buffer storage shutting down.." << std::endl;
        m_worker_synchronization->is_running = false;
        m_worker_synchronization->is_running_condition.notify_all();

        std::mutex       _exit_mutex;
        std::unique_lock _exit_lock{ _exit_mutex };
        m_worker_synchronization->exit_finished_condition.wait(
            _exit_lock, [&]() { return m_worker_synchronization->exit_finished; });
    }

private:
    void fragment_memory()
    {
        auto* _data = m_cache_buffer_ptr->array->data();
        memset(_data + m_cache_buffer_ptr->head, 0xFFFF,
               buffer_size - m_cache_buffer_ptr->head);
        *reinterpret_cast<TypeIdentifierEnum*>(_data + m_cache_buffer_ptr->head) =
            TypeIdentifierEnum::fragmented_space;

        size_t remaining_bytes =
            buffer_size - m_cache_buffer_ptr->head - header_size<TypeIdentifierEnum>;
        *reinterpret_cast<size_t*>(_data + m_cache_buffer_ptr->head +
                                   sizeof(TypeIdentifierEnum)) = remaining_bytes;
        m_cache_buffer_ptr->head                               = 0;
    }

    uint8_t* reserve_memory_space(const size_t& number_of_bytes)
    {
        size_t _size;
        {
            std::lock_guard scope{ m_cache_buffer_ptr->mutex };

            if((m_cache_buffer_ptr->head + number_of_bytes +
                header_size<TypeIdentifierEnum>) > buffer_size)
            {
                fragment_memory();
            }
            _size                    = m_cache_buffer_ptr->head;
            m_cache_buffer_ptr->head = m_cache_buffer_ptr->head + number_of_bytes;
        }

        auto* _result = m_cache_buffer_ptr->array->data() + _size;
        memset(_result, 0, number_of_bytes);
        return _result;
    }

    bool is_running() const { return m_worker_synchronization->is_running; }

private:
    worker_synchronization_ptr_t m_worker_synchronization{
        std::make_shared<worker_synchronization_t>()
    };

    cache_buffer_ptr_t m_cache_buffer_ptr{ std::make_shared<cache_buffer_t>() };
    std::thread        m_flushing_thread;
    pid_t              m_created_process;
};
