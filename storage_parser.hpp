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

template <typename TypeIdentifierEnum>
void
execute_sample_processing(TypeIdentifierEnum type_identifier, const cacheable_t& value);

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
struct track_sample;
struct process_sample;

template <typename TypeIdentifierEnum, typename... SupportedTypes>
class storage_parser
{
    static_assert(is_enum_class_v<TypeIdentifierEnum>,
                  "TypeIdentifierEnum must be an enum class");

public:
    storage_parser(std::string _filename)
    : m_filename(std::move(_filename))
    {}

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

        struct __attribute__((packed)) sample_header
        {
            TypeIdentifierEnum type;
            size_t             sample_size;
        };

        sample_header header;

        while(!ifs.eof())
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

            if(header.type == TypeIdentifierEnum::fragmented_space)
            {
                continue;
            }

            auto data = sample.data();

            auto sample_value = m_registry.get_type(header.type, data);
            if(sample_value.has_value())
            {
                const cacheable_t& cacheable_value = std::visit(
                    [](auto& arg) -> cacheable_t& {
                        return static_cast<cacheable_t&>(arg);
                    },
                    sample_value.value());

                execute_sample_processing(header.type, cacheable_value);
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
    std::string                            m_filename;
    std::unique_ptr<std::function<void()>> m_on_finished_callback{ nullptr };
    type_registry<TypeIdentifierEnum, SupportedTypes...> m_registry;  // todo
};