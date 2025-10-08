#pragma once

#include "cacheable.hpp"
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
    test_sample_1(int v, std::string s)
    : value(v)
    , text(std::move(s))
    {}

    int         value = 0;
    std::string text;

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
        return std::abs(data - other.data) < 1e-9 && sample_id == other.sample_id;
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
    size_t position = 0;
    trace_cache::utility::store_value(item.value, buffer, position);
    trace_cache::utility::store_value(item.text.c_str(), buffer, position);
}

template <>
inline test_sample_1
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_1 result;
    trace_cache::utility::parse_value(result.value, buffer);
    trace_cache::utility::parse_value(result.text, buffer);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_1& item)
{
    return trace_cache::utility::get_size_helper(item.value) +
           trace_cache::utility::get_size_helper(item.text.c_str());
}

template <>
inline void
trace_cache::serialize(uint8_t* buffer, const test_sample_2& item)
{
    size_t position = 0;
    trace_cache::utility::store_value(item.data, buffer, position);
    trace_cache::utility::store_value(item.sample_id, buffer, position);
}

template <>
inline test_sample_2
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_2 result;
    trace_cache::utility::parse_value(result.data, buffer);
    trace_cache::utility::parse_value(result.sample_id, buffer);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_2& item)
{
    return trace_cache::utility::get_size_helper(item.data) +
           trace_cache::utility::get_size_helper(item.sample_id);
}

template <>
inline void
trace_cache::serialize(uint8_t* buffer, const test_sample_3& item)
{
    size_t position = 0;
    trace_cache::utility::store_value(item.payload, buffer, position);
}

template <>
inline test_sample_3
trace_cache::deserialize(uint8_t*& buffer)
{
    test_sample_3 result;
    trace_cache::utility::parse_value(result.payload, buffer);
    return result;
}

template <>
inline size_t
trace_cache::get_size(const test_sample_3& item)
{
    return trace_cache::utility::get_size_helper(item.payload);
}
