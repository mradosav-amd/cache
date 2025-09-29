#pragma once

#include "cacheable.hpp"
#include "type_registry.hpp"
#include <algorithm>
#include <bits/chrono.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

using postprocessing_callback      = std::function<void(const cacheable_t&)>;
using postprocessing_callback_list = std::vector<postprocessing_callback>;

template <typename... SupportedTypes>
struct storage_post_processing
{
    using supported_types = std::tuple<SupportedTypes...>;

    template <typename Type>
    postprocessing_callback_list get_callbacks() const
    {
        static_assert(false, "Define concrete storage config.");
        return {};
    }
};

template <typename TypeIdentifierEnum, typename StoragePostProcessing>
class storage_parser
{
    static_assert(is_enum_class_v<TypeIdentifierEnum>,
                  "TypeIdentifierEnum must be an enum class");

public:
    storage_parser(std::string _filename, const StoragePostProcessing& _config)
    : m_filename(std::move(_filename))
    {
        register_all_callbacks(_config);
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
            TypeIdentifierEnum type;
            size_t             sample_size;
        };

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
                std::visit([this, type = header.type](
                               const auto& value) { invoke_callbacks(type, value); },
                           sample_value.value());
            }
            else
            {
                std::cout << "Unsupported type detected";
                continue;
            }
        }

        ifs.close();
        std::cout << "File parsing finished. Removing" << m_filename
                  << "from file system." << std::endl;
        std::remove(m_filename.c_str());

        if(m_on_finished_callback != nullptr)
        {
            (*m_on_finished_callback)();
        }
    }

private:
    template <std::size_t I = 0>
    inline void register_callbacks_for_types(const StoragePostProcessing& _postprocessing)
    {
        if constexpr(I <
                     std::tuple_size_v<typename StoragePostProcessing::supported_types>)
        {
            using Type =
                std::tuple_element_t<I, typename StoragePostProcessing::supported_types>;
            auto callbacks = _postprocessing.template get_callbacks<Type>();
            m_callbacks[Type::type_identifier] = std::move(callbacks);
            register_callbacks_for_types<I + 1>(_postprocessing);
        }
    }

    inline void register_all_callbacks(const StoragePostProcessing& _postprocessing)
    {
        register_callbacks_for_types(_postprocessing);
    }

    inline void invoke_callbacks(TypeIdentifierEnum type, const cacheable_t& parsed)
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

    std::string                                                m_filename;
    std::map<TypeIdentifierEnum, postprocessing_callback_list> m_callbacks;
    std::unique_ptr<std::function<void()>> m_on_finished_callback{ nullptr };
    type_registry<typename StoragePostProcessing::supported_types, TypeIdentifierEnum>
        m_registry;
};