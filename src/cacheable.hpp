#pragma once
#include "cache_type_traits.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>
#include <type_traits>
#include <vector>

namespace trace_cache
{

struct cacheable_t
{
    cacheable_t() = default;
};

constexpr auto   KByte                    = 1024;
constexpr auto   MByte                    = 1024 * 1024;
constexpr size_t buffer_size              = 100 * MByte;
constexpr size_t flush_threshold          = 80 * MByte;
constexpr auto   CACHE_FILE_FLUSH_TIMEOUT = 10 * 1000;  // ms

template <typename TypeIdentifierEnum>
constexpr size_t header_size = sizeof(TypeIdentifierEnum) + sizeof(size_t);
using buffer_array_t         = std::array<uint8_t, buffer_size>;

const auto tmp_directory = std::string{ "/tmp/" };

namespace utility
{

const auto get_buffered_storage_filename = [](const int& ppid, const int& pid) {
    return std::string{ tmp_directory + "buffered_storage_" + std::to_string(ppid) + "_" +
                        std::to_string(pid) + ".bin" };
};
// helper functions

template <typename T>
constexpr size_t
get_size_helper(T&& val)
{
    constexpr bool is_supported_type = type_traits::supported_types::is_supported<T>;
    static_assert(is_supported_type,
                  "Supported types are const char*, char*, "
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");

    if constexpr(type_traits::is_string_literal_v<T>)
    {
        size_t count = 0;
        while(val[count] != '\0')
        {
            ++count;
        }
        return ++count;
    }
    else if constexpr(std::is_same_v<std::decay_t<T>, std::vector<uint8_t>>)
    {
        return val.size() + sizeof(size_t);
    }
    else
    {
        return sizeof(T);
    }
}

template <typename Type>
void
store_value(const Type& value, uint8_t* buffer, size_t& position)
{
    constexpr bool is_supported_type = type_traits::supported_types::is_supported<Type>;
    static_assert(is_supported_type,
                  "Supported types are const char*, char*, "
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");

    size_t len  = 0;
    auto*  dest = buffer + position;
    if constexpr(type_traits::is_string_literal_v<Type>)
    {
        len = get_size_helper(value);
        std::memcpy(dest, value, len);  // will include \0
    }
    else if constexpr(std::is_same_v<std::decay_t<Type>, std::vector<uint8_t>>)
    {
        size_t elem_count = value.size();
        len               = elem_count + sizeof(size_t);
        std::memcpy(dest, &elem_count, sizeof(size_t));
        std::memcpy(dest + sizeof(size_t), value.data(), value.size());
    }
    else
    {
        using ClearType                     = std::decay_t<decltype(value)>;
        len                                 = sizeof(ClearType);
        *reinterpret_cast<ClearType*>(dest) = value;
    }
    position += len;
};

template <typename T>
static void
parse_value(uint8_t*& data_pos, T& arg)
{
    if constexpr(std::is_same_v<T, std::string>)
    {
        arg = std::string((const char*) data_pos);
        data_pos += get_size_helper((const char*) data_pos);
    }
    else if constexpr(std::is_same_v<T, std::vector<uint8_t>>)
    {
        size_t vector_size = *reinterpret_cast<const size_t*>(data_pos);
        data_pos += sizeof(size_t);
        arg.reserve(vector_size);
        std::copy_n(data_pos, vector_size, std::back_inserter(arg));
        data_pos += vector_size;
    }
    else
    {
        arg = *reinterpret_cast<const T*>(data_pos);
        data_pos += sizeof(T);
    }
}

}  // namespace utility
}  // namespace trace_cache