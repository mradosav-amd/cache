#pragma once
#include <array>
#include <bits/chrono.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

#include "cacheable.hpp"

namespace trace_cache
{

using ofs_t             = std::basic_ostream<char>;
using worker_function_t = std::function<void(ofs_t& ofs, bool force)>;

struct worker_synchronization_t
{
    std::condition_variable is_running_condition;
    bool                    is_running{ false };

    std::condition_variable exit_finished_condition;
    bool                    exit_finished{ false };

    pid_t origin_pid;
};
using worker_synchronization_ptr_t = std::shared_ptr<worker_synchronization_t>;

struct flush_worker_t
{
    explicit flush_worker_t(worker_function_t            worker_function,
                            worker_synchronization_ptr_t worker_synchronization_ptr,
                            std::string                  filepath)

    : m_worker_function(worker_function)
    , m_worker_synchronization(std::move(worker_synchronization_ptr))
    , m_filepath(std::move(filepath))
    {}

    void start(const pid_t& current_pid)
    {
        m_ofs = std::ofstream{ m_filepath, std::ios::binary | std::ios::out };

        if(!m_ofs.good())
        {
            std::stringstream _ss;
            _ss << "Error opening file for writing: " << m_filepath;
            throw std::runtime_error(_ss.str());
        }

        m_worker_synchronization->origin_pid = current_pid;
        m_worker_synchronization->is_running = true;

        m_flushing_thread = std::make_unique<std::thread>([&]() {
            std::mutex _shutdown_condition_mutex;
            while(m_worker_synchronization->is_running)
            {
                m_worker_function(m_ofs, false);
                std::unique_lock _lock{ _shutdown_condition_mutex };
                m_worker_synchronization->is_running_condition.wait_for(
                    _lock, std::chrono::milliseconds(CACHE_FILE_FLUSH_TIMEOUT),
                    [&]() { return !m_worker_synchronization->is_running; });
            }

            m_worker_function(m_ofs, true);
            m_ofs.close();
            m_worker_synchronization->exit_finished = true;
            m_worker_synchronization->exit_finished_condition.notify_one();
        });
    }

    void stop(const pid_t& current_pid)
    {
        const bool flushing_thread_exist = m_flushing_thread != nullptr;
        const bool worker_is_running =
            m_worker_synchronization != nullptr && m_worker_synchronization->is_running;

        if(flushing_thread_exist && worker_is_running)
        {
            std::cout << "Buffer storage shutting down.." << std::endl;
            m_worker_synchronization->is_running = false;
            m_worker_synchronization->is_running_condition.notify_all();

            const bool thread_is_created_in_this_process =
                current_pid == m_worker_synchronization->origin_pid;
            if(!thread_is_created_in_this_process)
            {
                std::cout
                    << "Buffer storage is not created in same process as shutting down.."
                    << std::endl;
                return;
            }

            std::mutex       _exit_mutex;
            std::unique_lock _exit_lock{ _exit_mutex };
            m_worker_synchronization->exit_finished_condition.wait(
                _exit_lock, [&]() { return m_worker_synchronization->exit_finished; });

            if(m_flushing_thread->joinable())
            {
                m_flushing_thread->detach();
                m_flushing_thread.reset();
            }
        }
    }

private:
    worker_function_t            m_worker_function;
    worker_synchronization_ptr_t m_worker_synchronization;
    std::string                  m_filepath;
    std::ofstream                m_ofs;
    std::unique_ptr<std::thread> m_flushing_thread;
};

struct flush_worker_factory_t
{
    using worker_t = flush_worker_t;

    flush_worker_factory_t()                                    = delete;
    flush_worker_factory_t(flush_worker_factory_t&)             = delete;
    flush_worker_factory_t& operator=(flush_worker_factory_t&)  = delete;
    flush_worker_factory_t(flush_worker_factory_t&&)            = delete;
    flush_worker_factory_t& operator=(flush_worker_factory_t&&) = delete;

    static std::shared_ptr<worker_t> get_worker(
        worker_function_t                   worker_function,
        const worker_synchronization_ptr_t& worker_synchronization_ptr,
        std::string                         filepath)
    {
        return std::make_shared<worker_t>(worker_function, worker_synchronization_ptr,
                                          std::move(filepath));
    }
};

template <typename WorkerFactory, typename TypeIdentifierEnum>
class buffered_storage
{
    static_assert(type_traits::is_enum_class_v<TypeIdentifierEnum>,
                  "TypeIdentifierEnum must be an enum class");

public:
    explicit buffered_storage(std::string filepath)
    : m_worker{ std::move(WorkerFactory::get_worker(
          [this](ofs_t& ofs, bool force) { execute_flush(ofs, force); },
          m_worker_synchronization, std::move(filepath))) }
    {}

    ~buffered_storage() { shutdown(); }

    void start(const pid_t& current_pid = getpid())
    {
        if(m_worker == nullptr)
        {
            throw std::runtime_error("Worker is null unable to start buffered storage.");
        }
        if(m_worker_synchronization && m_worker_synchronization->is_running)
        {
            return;
        }

        m_worker->start(current_pid);
    }

    void shutdown(const pid_t& current_pid = getpid())
    {
        if(m_worker_synchronization == nullptr || m_worker == nullptr)
        {
            return;
        }

        if(!m_worker_synchronization->is_running)
        {
            return;
        }

        m_worker->stop(current_pid);
    }

    template <typename Type>
    auto store(const Type& value)
    {
        if(!is_running())
        {
            throw std::runtime_error(
                "Trying to use buffered storage while it is not running");
            return;
        }

        type_traits::check_type<Type, TypeIdentifierEnum>();

        using TypeIdentifierEnumUderlayingType =
            std::underlying_type_t<TypeIdentifierEnum>;

        size_t sample_size      = get_size(value);
        size_t bytes_to_reserve = header_size<TypeIdentifierEnum> + sample_size;
        auto*  buf              = reserve_memory_space(bytes_to_reserve);
        size_t position         = 0;
        auto   type_identifier_value =
            static_cast<TypeIdentifierEnumUderlayingType>(Type::type_identifier);

        utility::store_value(type_identifier_value, buf, position);
        utility::store_value(sample_size, buf, position);
        serialize(buf + position, value);
    }

private:
    void execute_flush(ofs_t& ofs, bool force)
    {
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
    }

    void fragment_memory()
    {
        auto* _data = m_buffer->data();
        memset(_data + m_head, 0xFFFF, buffer_size - m_head);
        *reinterpret_cast<TypeIdentifierEnum*>(_data + m_head) =
            TypeIdentifierEnum::fragmented_space;

        size_t remaining_bytes = buffer_size - m_head - header_size<TypeIdentifierEnum>;
        *reinterpret_cast<size_t*>(_data + m_head + sizeof(TypeIdentifierEnum)) =
            remaining_bytes;
        m_head = 0;
    }

    __attribute__((always_inline)) inline uint8_t* reserve_memory_space(const size_t& number_of_bytes)
    {
        size_t _size;
        {
            std::lock_guard scope{ m_mutex };

            if(__builtin_expect((m_head + number_of_bytes + header_size<TypeIdentifierEnum>) > buffer_size, 0))
            {
                fragment_memory();
            }
            _size  = m_head;
            m_head += number_of_bytes;
        }

        return m_buffer->data() + _size;
    }

    bool is_running() const { return m_worker_synchronization->is_running; }

private:
    worker_synchronization_ptr_t m_worker_synchronization{
        std::make_shared<worker_synchronization_t>()
    };

    std::shared_ptr<typename WorkerFactory::worker_t> m_worker;

    std::mutex                      m_mutex;
    size_t                          m_head{ 0 };
    size_t                          m_tail{ 0 };
    std::unique_ptr<buffer_array_t> m_buffer{ std::make_unique<buffer_array_t>() };
};

}  // namespace trace_cache