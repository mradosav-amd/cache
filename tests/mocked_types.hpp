#pragma once

#include "cacheable.hpp"
#include <cmath>
#include <cstddef>
#include <vector>

enum class test_type_identifier_t : uint32_t
{
    sample_type_1    = 1,
    sample_type_2    = 2,
    sample_type_3    = 3,
    fragmented_space = 0xFFFF
};
struct test_sample_1 : public trace_cache::cacheable_t
{
    static constexpr test_type_identifier_t type_identifier =
        test_type_identifier_t::sample_type_1;

    test_sample_1() = default;
    test_sample_1(int v, std::string_view s)
    : value(v)
    , text(s)
    {}

    int              value = 0;
    std::string_view text;

    bool operator==(const test_sample_1& other) const
    {
        return value == other.value && text == other.text;
    }
};

struct test_sample_2 : public trace_cache::cacheable_t
{
    static constexpr test_type_identifier_t type_identifier =
        test_type_identifier_t::sample_type_2;

    test_sample_2() = default;
    test_sample_2(double d, uint32_t id)
    : data(d)
    , sample_id(id)
    {}

    double   data      = 0.0;
    uint32_t sample_id = 0;

    bool operator==(const test_sample_2& other) const
    {
        if(sample_id != other.sample_id) return false;

        if(std::isnan(data) && std::isnan(other.data)) return true;

        if(std::isinf(data) && std::isinf(other.data))
            return std::signbit(data) == std::signbit(other.data);

        return std::abs(data - other.data) < 1e-9;
    }
};

struct test_sample_3 : public trace_cache::cacheable_t
{
    static constexpr test_type_identifier_t type_identifier =
        test_type_identifier_t::sample_type_3;

    test_sample_3() = default;
    test_sample_3(std::vector<uint8_t> p)
    : payload(std::move(p))
    {}

    std::vector<uint8_t> payload;

    bool operator==(const test_sample_3& other) const { return payload == other.payload; }
};

template <>
inline void
trace_cache::serialize(uint8_t* buffer, const test_sample_1& item)
{
    trace_cache::utility::store_value(buffer, item.value, item.text);
}

template <>
inline test_sample_1
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_1 result;
    trace_cache::utility::parse_value(buffer, result.value, result.text);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_1& item)
{
    return trace_cache::utility::get_size(item.value, item.text);
}

template <>
inline void
trace_cache::serialize(uint8_t* buffer, const test_sample_2& item)
{
    trace_cache::utility::store_value(buffer, item.data, item.sample_id);
}

template <>
inline test_sample_2
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_2 result;
    trace_cache::utility::parse_value(buffer, result.data, result.sample_id);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_2& item)
{
    return trace_cache::utility::get_size(item.data, item.sample_id);
}

template <>
inline void
trace_cache::serialize(uint8_t* buffer, const test_sample_3& item)
{
    trace_cache::utility::store_value(buffer, item.payload);
}

template <>
inline test_sample_3
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_3 result;
    trace_cache::utility::parse_value(buffer, result.payload);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_3& item)
{
    return trace_cache::utility::get_size(item.payload);
}
