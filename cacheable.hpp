#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>
#include <type_traits>
#include <vector>

// define types

struct cacheable_t
{
    cacheable_t() = default;
};

enum class type_identifier_t : uint32_t
{
    track_sample     = 0,
    process_sample   = 1,
    fragmented_space = 0xFFFF
};

// utility

constexpr auto   KByte                    = 1024;
constexpr auto   MByte                    = 1024 * 1024;
constexpr size_t buffer_size              = 100 * MByte;
constexpr size_t flush_threshold          = 80 * MByte;
constexpr auto   CACHE_FILE_FLUSH_TIMEOUT = 10 * 1000;  // ms

const auto tmp_directory = std::string{ "/tmp/" };

const auto get_buffered_storage_filename = [](const int& ppid, const int& pid) {
    return std::string{ tmp_directory + "buffered_storage_" + std::to_string(ppid) + "_" +
                        std::to_string(pid) + ".bin" };
};

constexpr size_t header_size = sizeof(type_identifier_t) + sizeof(size_t);
using buffer_array_t         = std::array<uint8_t, buffer_size>;

// template definitions

template <typename T>
struct always_false : std::false_type
{};

template <typename T>
void
serialize(uint8_t*, const T&)
{
    static_assert(always_false<T>::value, "serialize<T> not specialized");
}

template <typename T>
T
deserialize(uint8_t*&)
{
    static_assert(always_false<T>::value, "deserialize<T> not specialized");
    return T{};
}

template <typename T>
size_t
get_size(const T&)
{
    static_assert(always_false<T>::value, "get_size(T) not specialized");
    return 0;
}

// type traits

template <class...>
using void_t = void;

// serialize type trait

template <typename T, typename = void>
struct has_serialize : std::false_type
{};

template <typename T>
struct has_serialize<T, std::void_t<decltype(serialize(std::declval<uint8_t*>(),
                                                       std::declval<const T&>()))>>
: std::true_type
{};

// deserialize type trait

template <typename T, typename = void>
struct has_deserialize : std::false_type
{};

template <typename T>
struct has_deserialize<
    T, void_t<std::is_same<decltype(deserialize<T>(std::declval<uint8_t*&>())), T>>>
: std::true_type
{};

// get_size type trait

template <typename T, typename = void>
struct has_get_size : std::false_type
{};

template <typename T>
struct has_get_size<T, void_t<decltype(get_size(std::declval<const T&>()))>>
: std::true_type
{};

template <class T, class = void>
struct has_type_identifier : std::false_type
{};

template <class T>
struct has_type_identifier<T, void_t<decltype(T::type_identifier)>>
: std::bool_constant<
      std::is_convertible_v<decltype(T::type_identifier), type_identifier_t>>
{};

template <typename Tp>
void
check_type()
{
    static_assert(has_serialize<Tp>::value, "Type don't have `serialize` function.");
    static_assert(has_deserialize<Tp>::value, "Type don't have `deserialize` function.");
    static_assert(has_get_size<Tp>::value, "Type don't have `get_size` function.");
    static_assert(has_type_identifier<Tp>::value,
                  "Type don't have `type_identifier` member of type type_identifier_t.");
}

template <typename... Types>
struct typelist
{
    template <typename T>
    constexpr static bool is_supported = (std::is_same_v<std::decay_t<T>, Types> || ...);
};

using supported_types = typelist<const char*, char*, uint64_t, int32_t, uint32_t,
                                 std::vector<uint8_t>, uint8_t, int64_t, double>;

template <typename T>
static constexpr bool is_string_literal_v =
    std::is_same_v<std::decay_t<T>, const char*> ||
    std::is_same_v<std::decay_t<T>, char*>;

// helper functions

template <typename T>
constexpr size_t
get_size_helper(T&& val)
{
    constexpr bool is_supported_type = supported_types::is_supported<T>;
    static_assert(is_supported_type,
                  "Supported types are const char*, char*, "
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");

    if constexpr(is_string_literal_v<T>)
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
    constexpr bool is_supported_type = supported_types::is_supported<Type>;
    static_assert(is_supported_type,
                  "Supported types are const char*, char*, "
                  "unsigned long, unsigned int, long, unsigned "
                  "char, std::vector<unsigned char>, double, and int.");

    size_t len  = 0;
    auto*  dest = buffer + position;
    if constexpr(is_string_literal_v<Type>)
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
process_arg(uint8_t*& data_pos, T& arg)
{
    if constexpr(std::is_same_v<T, std::string>)
    {
        arg = std::string((const char*) data_pos);
        data_pos += get_size_helper((const char*) data_pos);
    }
    else
    {
        arg = *reinterpret_cast<const T*>(data_pos);
        data_pos += sizeof(T);
    }
}
