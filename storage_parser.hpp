#pragma once

#include "cacheable.hpp"
#include "type_registry.hpp"
#include <algorithm>
#include <array>
#include <bits/chrono.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using postprocessing_callback = std::function<void(const cacheable_t&)>;

namespace
{
struct type_callback_invocator
{
    explicit type_callback_invocator(const std::vector<postprocessing_callback>& list)
    : m_callback_list(list)
    {}

    void operator()(const cacheable_t& obj)
    {
        for(auto& cb : m_callback_list)
        {
            cb(obj);
        }
    }

private:
    std::vector<postprocessing_callback> m_callback_list;
};
}  // namespace

template <typename... SupportedTypes>
class storage_parser
{
public:
    storage_parser(std::string _filename)
    : m_filename(std::move(_filename))
    {}

    void register_type_callback(const type_identifier_t&                       type,
                                const std::function<void(const cacheable_t&)>& callback)
    {
        m_callbacks[type].push_back(callback);
    }

    void register_on_finished_callback(std::unique_ptr<std::function<void()>> callback)
    {
        m_on_finished_callback = std::move(callback);
    }

    void load()
    {
        std::cout << "Consuming buffered storage with filename:" << m_filename
                  << std::endl;

        std::ifstream ifs(m_filename, std::ios::binary);
        if(!ifs)
        {
            std::stringstream ss;
            ss << "Error opening file for reading: " << m_filename << "\n";
            throw std::runtime_error(ss.str());
        }

        bool _parsing_needed = !m_callbacks.empty();

        struct __attribute__((packed)) sample_header
        {
            type_identifier_t type;
            size_t            sample_size;
        };

        std::map<type_identifier_t, std::shared_ptr<type_callback_invocator>> _invocators;

        for(const auto& callback_pair : m_callbacks)
        {
            auto invocator =
                std::make_shared<type_callback_invocator>(callback_pair.second);
            _invocators[callback_pair.first] = invocator;
        }

        sample_header header;

        while(!ifs.eof() && _parsing_needed)
        {
            ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

            if(header.sample_size == 0 || ifs.eof())
            {
                continue;
            }

            std::vector<uint8_t> sample;
            sample.reserve(header.sample_size);
            ifs.read(reinterpret_cast<char*>(sample.data()), header.sample_size);

            if(ifs.bad())
            {
                std::cout << "Bad read while consuming buffered storage. Filename: "
                          << m_filename
                          << " Bytes read: " << static_cast<int>(ifs.tellg())
                          << std::endl;
                continue;
            }

            auto data = sample.data();

            auto sample_value = m_registry.get_type(header.type, data);
            if(sample_value.has_value())
            {
                std::visit(*_invocators[header.type], sample_value.value());
            }
            else
            {
                std::cout << "unsupported type detected";
                continue;
            }
        }

        ifs.close();
        std::cout << "File parsing finished. Removing" << m_filename << "from file system"
                  << std::endl;
        std::remove(m_filename.c_str());

        if(m_on_finished_callback != nullptr)
        {
            (*m_on_finished_callback)();
        }
    }

private:
    template <typename... Args>
    static void parse_data(const uint8_t* data_pos, Args&... args)
    {
        (process_arg(data_pos, args), ...);
    }

    void invoke_callbacks(type_identifier_t type, const cacheable_t& parsed)
    {
        auto _callback_list = m_callbacks.find(type);
        if(_callback_list == m_callbacks.end())
        {
            std::cout << "Callback not found for cache postprocessing" << std::endl;
            return;
        }

        for(auto& cb : _callback_list->second)
        {
            cb(parsed);
        }
    }

    std::string                                                       m_filename;
    std::map<type_identifier_t, std::vector<postprocessing_callback>> m_callbacks;
    std::unique_ptr<std::function<void()>> m_on_finished_callback{ nullptr };
    type_registry<SupportedTypes...>       m_registry;
};