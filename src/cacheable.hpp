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
__attribute__((always_inline)) inline constexpr size_t
get_size_helper(Type&& val)
{
    using DecayedType = std::decay_t<Type>;
    static_assert(type_traits::supported_types::is_supported<DecayedType>,
                  "Unsupported type in get_size_helper");

    if constexpr(type_traits::is_string_view_v<DecayedType> ||
                 std::is_same_v<DecayedType, std::vector<uint8_t>>)
    {
        return val.size() + sizeof(size_t);
    }
    else
    {
        return sizeof(DecayedType);
    }
}

template <typename Type>
__attribute__((always_inline)) inline void
store_value(const Type& value, uint8_t* buffer, size_t& position)
{
    using DecayedType = std::decay_t<Type>;
    static_assert(type_traits::supported_types::is_supported<DecayedType>,
                  "Unsupported type in store_value");

    auto* dest = buffer + position;

    if constexpr(type_traits::is_string_view_v<DecayedType> ||
                 std::is_same_v<DecayedType, std::vector<uint8_t>>)
    {
        const size_t elem_count = value.size();
        *reinterpret_cast<size_t*>(dest) = elem_count;
        std::memcpy(dest + sizeof(size_t), value.data(), elem_count);
        position += elem_count + sizeof(size_t);
    }
    else
    {
        *reinterpret_cast<DecayedType*>(dest) = value;
        position += sizeof(DecayedType);
    }
};

template <typename Type>
__attribute__((always_inline)) inline static void
parse_value(Type& arg, uint8_t*& data_pos)
{
    using DecayedType = std::decay_t<Type>;
    static_assert(type_traits::supported_types::is_supported<DecayedType>,
                  "Unsupported type in parse_value");

    if constexpr(type_traits::is_string_view_v<DecayedType>)
    {
        const size_t string_size = *reinterpret_cast<const size_t*>(data_pos);
        data_pos += sizeof(size_t);
        arg = std::string_view{ reinterpret_cast<const char*>(data_pos), string_size };
        data_pos += string_size;
    }
    else if constexpr(std::is_same_v<DecayedType, std::vector<uint8_t>>)
    {
        const size_t vector_size = *reinterpret_cast<const size_t*>(data_pos);
        data_pos += sizeof(size_t);
        arg.reserve(vector_size);
        std::copy_n(data_pos, vector_size, std::back_inserter(arg));
        data_pos += vector_size;
    }
    else
    {
        arg = *reinterpret_cast<const DecayedType*>(data_pos);
        data_pos += sizeof(DecayedType);
    }
}

}  // namespace utility
}  // namespace trace_cache