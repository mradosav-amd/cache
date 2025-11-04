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
constexpr auto   CACHE_FILE_FLUSH_TIMEOUT = 10;  // ms

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

template <typename Type>
inline void
check_is_type_supported()
{
    constexpr bool is_supported_type = type_traits::supported_types::is_supported<Type>;
    static_assert(is_supported_type,
                  "Supported types are std::string_view"
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");
}

template <typename Type>
constexpr size_t
get_size_helper(Type&& val)
{
    check_is_type_supported<Type>();

    if constexpr(type_traits::is_string_view_v<Type> ||
                 std::is_same_v<std::decay_t<Type>, std::vector<uint8_t>>)
    {
        return val.size() + sizeof(size_t);
    }
    else
    {
        return sizeof(Type);
    }
}

template <typename Type>
void
store_value(const Type& value, uint8_t* buffer, size_t& position)
{
    check_is_type_supported<Type>();

    size_t len  = 0;
    auto*  dest = buffer + position;
    if constexpr(type_traits::is_string_view_v<Type> ||
                 std::is_same_v<std::decay_t<Type>, std::vector<uint8_t>>)
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

template <typename Type>
static void
parse_value(Type& arg, uint8_t*& data_pos)
{
    check_is_type_supported<Type>();

    if constexpr(type_traits::is_string_view_v<Type>)
    {
        size_t string_size = *reinterpret_cast<const size_t*>(data_pos);
        data_pos += sizeof(size_t);

        arg = std::string_view{ (const char*) data_pos, string_size };
        data_pos += string_size;
    }
    else if constexpr(std::is_same_v<Type, std::vector<uint8_t>>)
    {
        size_t vector_size = *reinterpret_cast<const size_t*>(data_pos);
        data_pos += sizeof(size_t);
        arg.reserve(vector_size);
        std::copy_n(data_pos, vector_size, std::back_inserter(arg));
        data_pos += vector_size;
    }
    else
    {
        arg = *reinterpret_cast<const Type*>(data_pos);
        data_pos += sizeof(Type);
    }
}

}  // namespace utility
}  // namespace trace_cache