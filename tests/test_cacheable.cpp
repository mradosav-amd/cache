#include "cacheable.hpp"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

class CacheableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        buffer.fill(0);
        position = 0;
    }

    std::array<uint8_t, 1024> buffer;
    size_t                    position;
};

TEST_F(CacheableTest, StoreValueInt)
{
    int value = 42;
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, sizeof(int));
    int stored_value = *reinterpret_cast<int*>(buffer.data());
    EXPECT_EQ(stored_value, 42);
}

TEST_F(CacheableTest, StoreValueDouble)
{
    double value = 3.14159;
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, sizeof(double));
    double stored_value = *reinterpret_cast<double*>(buffer.data());
    EXPECT_DOUBLE_EQ(stored_value, 3.14159);
}

TEST_F(CacheableTest, StoreValueUnsignedLong)
{
    unsigned long value = 123456789UL;
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, sizeof(unsigned long));
    unsigned long stored_value = *reinterpret_cast<unsigned long*>(buffer.data());
    EXPECT_EQ(stored_value, 123456789UL);
}

TEST_F(CacheableTest, StoreValueUnsignedChar)
{
    unsigned char value = 255;
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, sizeof(unsigned char));
    unsigned char stored_value = *reinterpret_cast<unsigned char*>(buffer.data());
    EXPECT_EQ(stored_value, 255);
}

TEST_F(CacheableTest, StoreValueStringLiteral)
{
    const char* value = "Hello World";
    trace_cache::utility::store_value(value, buffer.data(), position);

    size_t expected_size = strlen(value) + 1;
    EXPECT_EQ(position, expected_size);

    std::string stored_value(reinterpret_cast<const char*>(buffer.data()));
    EXPECT_EQ(stored_value, "Hello World");
}

TEST_F(CacheableTest, StoreValueEmptyString)
{
    const char* value = "";
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, 1);
    EXPECT_EQ(buffer[0], '\0');
}

TEST_F(CacheableTest, StoreValueVector)
{
    std::vector<uint8_t> value = { 1, 2, 3, 4, 5 };
    trace_cache::utility::store_value(value, buffer.data(), position);

    size_t expected_size = value.size() + sizeof(size_t);
    EXPECT_EQ(position, expected_size);

    size_t stored_size = *reinterpret_cast<size_t*>(buffer.data());
    EXPECT_EQ(stored_size, 5);

    uint8_t* data_start = buffer.data() + sizeof(size_t);
    for(size_t i = 0; i < value.size(); ++i)
    {
        EXPECT_EQ(data_start[i], value[i]);
    }
}

TEST_F(CacheableTest, StoreValueEmptyVector)
{
    std::vector<uint8_t> value;
    trace_cache::utility::store_value(value, buffer.data(), position);

    EXPECT_EQ(position, sizeof(size_t));
    size_t stored_size = *reinterpret_cast<size_t*>(buffer.data());
    EXPECT_EQ(stored_size, 0);
}

TEST_F(CacheableTest, StoreMultipleValues)
{
    int         int_val    = 100;
    double      double_val = 2.718;
    const char* str_val    = "test";

    trace_cache::utility::store_value(int_val, buffer.data(), position);
    trace_cache::utility::store_value(double_val, buffer.data(), position);
    trace_cache::utility::store_value(str_val, buffer.data(), position);

    size_t expected_total = sizeof(int) + sizeof(double) + strlen(str_val) + 1;
    EXPECT_EQ(position, expected_total);
}

TEST_F(CacheableTest, ParseValueInt)
{
    int original_value = 987;
    trace_cache::utility::store_value(original_value, buffer.data(), position);

    uint8_t* data_pos = buffer.data();
    int      parsed_value;
    trace_cache::utility::parse_value(data_pos, parsed_value);

    EXPECT_EQ(parsed_value, 987);
    EXPECT_EQ(data_pos, buffer.data() + sizeof(int));
}

TEST_F(CacheableTest, ParseValueDouble)
{
    double original_value = 1.618033988;
    trace_cache::utility::store_value(original_value, buffer.data(), position);

    uint8_t* data_pos = buffer.data();
    double   parsed_value;
    trace_cache::utility::parse_value(data_pos, parsed_value);

    EXPECT_DOUBLE_EQ(parsed_value, 1.618033988);
    EXPECT_EQ(data_pos, buffer.data() + sizeof(double));
}

TEST_F(CacheableTest, ParseValueUnsignedLong)
{
    unsigned long original_value = 0xDEADBEEF;
    trace_cache::utility::store_value(original_value, buffer.data(), position);

    uint8_t*      data_pos = buffer.data();
    unsigned long parsed_value;
    trace_cache::utility::parse_value(data_pos, parsed_value);

    EXPECT_EQ(parsed_value, 0xDEADBEEF);
    EXPECT_EQ(data_pos, buffer.data() + sizeof(unsigned long));
}

TEST_F(CacheableTest, ParseValueString)
{
    const char* original_value = "Parse this string";
    trace_cache::utility::store_value(original_value, buffer.data(), position);

    uint8_t*    data_pos = buffer.data();
    std::string parsed_value;
    trace_cache::utility::parse_value(data_pos, parsed_value);

    EXPECT_EQ(parsed_value, "Parse this string");
    EXPECT_EQ(data_pos, buffer.data() + strlen(original_value) + 1);
}

TEST_F(CacheableTest, ParseValueEmptyString)
{
    const char* original_value = "";
    trace_cache::utility::store_value(original_value, buffer.data(), position);

    uint8_t*    data_pos = buffer.data();
    std::string parsed_value;
    trace_cache::utility::parse_value(data_pos, parsed_value);

    EXPECT_EQ(parsed_value, "");
    EXPECT_EQ(data_pos, buffer.data() + 1);
}

TEST_F(CacheableTest, ParseMultipleValues)
{
    int           int_val    = 42;
    double        double_val = 3.14;
    const char*   str_val    = "multi";
    unsigned char uchar_val  = 128;

    trace_cache::utility::store_value(int_val, buffer.data(), position);
    trace_cache::utility::store_value(double_val, buffer.data(), position);
    trace_cache::utility::store_value(str_val, buffer.data(), position);
    trace_cache::utility::store_value(uchar_val, buffer.data(), position);

    uint8_t* data_pos = buffer.data();

    int parsed_int;
    trace_cache::utility::parse_value(data_pos, parsed_int);
    EXPECT_EQ(parsed_int, 42);

    double parsed_double;
    trace_cache::utility::parse_value(data_pos, parsed_double);
    EXPECT_DOUBLE_EQ(parsed_double, 3.14);

    std::string parsed_string;
    trace_cache::utility::parse_value(data_pos, parsed_string);
    EXPECT_EQ(parsed_string, "multi");

    unsigned char parsed_uchar;
    trace_cache::utility::parse_value(data_pos, parsed_uchar);
    EXPECT_EQ(parsed_uchar, 128);
}

TEST_F(CacheableTest, GetSizeHelperInt)
{
    int    value = 42;
    size_t size  = trace_cache::utility::get_size_helper(value);
    EXPECT_EQ(size, sizeof(int));
}

TEST_F(CacheableTest, GetSizeHelperDouble)
{
    double value = 3.14;
    size_t size  = trace_cache::utility::get_size_helper(value);
    EXPECT_EQ(size, sizeof(double));
}

TEST_F(CacheableTest, GetSizeHelperStringLiteral)
{
    const char* value = "test string";
    size_t      size  = trace_cache::utility::get_size_helper(value);
    EXPECT_EQ(size, strlen(value) + 1);
}

TEST_F(CacheableTest, GetSizeHelperVector)
{
    std::vector<uint8_t> value = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    size_t               size  = trace_cache::utility::get_size_helper(value);
    EXPECT_EQ(size, value.size() + sizeof(size_t));
}

TEST_F(CacheableTest, RoundTripInt)
{
    int original = -12345;
    trace_cache::utility::store_value(original, buffer.data(), position);

    uint8_t* data_pos = buffer.data();
    int      parsed;
    trace_cache::utility::parse_value(data_pos, parsed);

    EXPECT_EQ(original, parsed);
}

TEST_F(CacheableTest, RoundTripString)
{
    const char* original = "Round trip test string with spaces and numbers 123!";
    trace_cache::utility::store_value(original, buffer.data(), position);

    uint8_t*    data_pos = buffer.data();
    std::string parsed;
    trace_cache::utility::parse_value(data_pos, parsed);

    EXPECT_EQ(std::string(original), parsed);
}

TEST_F(CacheableTest, GetBufferedStorageFilename)
{
    int ppid = 1234;
    int pid  = 5678;

    std::string filename = trace_cache::utility::get_buffered_storage_filename(ppid, pid);
    std::string expected = "/tmp/buffered_storage_1234_5678.bin";

    EXPECT_EQ(filename, expected);
}