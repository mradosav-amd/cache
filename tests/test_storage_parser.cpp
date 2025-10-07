#include "mocked_types.hpp"
#include "storage_parser.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

struct processing_tracker
{
    std::atomic<int> sample_1_count{ 0 };
    std::atomic<int> sample_2_count{ 0 };
    std::atomic<int> sample_3_count{ 0 };
    std::atomic<int> unknown_count{ 0 };

    std::vector<test_sample_1> processed_sample_1;
    std::vector<test_sample_2> processed_sample_2;
    std::vector<test_sample_3> processed_sample_3;
    std::mutex                 data_mutex;

    void reset()
    {
        sample_1_count = 0;
        sample_2_count = 0;
        sample_3_count = 0;
        unknown_count  = 0;

        std::lock_guard<std::mutex> lock(data_mutex);
        processed_sample_1.clear();
        processed_sample_2.clear();
        processed_sample_3.clear();
    }
};

static processing_tracker g_tracker;

struct sample_processor_t
{
    static void execute_sample_processing(test_type_identifier_t          type_identifier,
                                          const trace_cache::cacheable_t& value)
    {
        switch(type_identifier)
        {
            case test_type_identifier_t::sample_type_1:
            {
                const auto& sample = static_cast<const test_sample_1&>(value);
                g_tracker.sample_1_count++;
                std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
                g_tracker.processed_sample_1.push_back(sample);
                break;
            }
            case test_type_identifier_t::sample_type_2:
            {
                const auto& sample = static_cast<const test_sample_2&>(value);
                g_tracker.sample_2_count++;
                std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
                g_tracker.processed_sample_2.push_back(sample);
                break;
            }
            case test_type_identifier_t::sample_type_3:
            {
                const auto& sample = static_cast<const test_sample_3&>(value);
                g_tracker.sample_3_count++;
                std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
                g_tracker.processed_sample_3.push_back(sample);
                break;
            }
            default: g_tracker.unknown_count++; break;
        }
    }
};

struct __attribute__((packed)) sample_header
{
    test_type_identifier_t type;
    size_t                 sample_size;
};

class StorageParserTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path = "test_storage_parser_" + std::to_string(test_counter++) + ".bin";
        cleanup_test_file();
        g_tracker.reset();
    }

    void TearDown() override
    {
        cleanup_test_file();
        g_tracker.reset();
    }

    void cleanup_test_file() { std::remove(test_file_path.c_str()); }

    template <typename T>
    void write_vector(std::ofstream& ofs, const std::vector<T>& vec,
                      test_type_identifier_t identifier)
    {
        for(const auto& sample : vec)
        {
            sample_header header;
            header.type        = identifier;
            header.sample_size = trace_cache::get_size(sample);

            ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

            std::vector<uint8_t> buffer(header.sample_size);
            trace_cache::serialize(buffer.data(), sample);
            ofs.write(reinterpret_cast<const char*>(buffer.data()), header.sample_size);
        }
    }

    void create_test_file_with_samples(const std::vector<test_sample_1>& samples_1,
                                       const std::vector<test_sample_2>& samples_2,
                                       const std::vector<test_sample_3>& samples_3)
    {
        std::ofstream ofs(test_file_path, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());

        write_vector(ofs, samples_1, test_type_identifier_t::sample_type_1);
        write_vector(ofs, samples_2, test_type_identifier_t::sample_type_2);
        write_vector(ofs, samples_3, test_type_identifier_t::sample_type_3);

        ofs.close();
    }

    std::string             test_file_path;
    static std::atomic<int> test_counter;
};

std::atomic<int> StorageParserTest::test_counter{ 0 };

TEST_F(StorageParserTest, load_empty_file)
{
    std::ofstream ofs(test_file_path, std::ios::binary);
    ofs.close();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 0);
    EXPECT_EQ(g_tracker.sample_2_count, 0);
    EXPECT_EQ(g_tracker.sample_3_count, 0);

    EXPECT_FALSE(std::filesystem::exists(test_file_path));
}

TEST_F(StorageParserTest, load_single_sample_type_1)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(42, "test_string"),
                                             test_sample_1(100, "another_test") };

    create_test_file_with_samples(samples_1, {}, {});

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 2);
    EXPECT_EQ(g_tracker.sample_2_count, 0);
    EXPECT_EQ(g_tracker.sample_3_count, 0);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    ASSERT_EQ(g_tracker.processed_sample_1.size(), 2);
    EXPECT_EQ(g_tracker.processed_sample_1[0], samples_1[0]);
    EXPECT_EQ(g_tracker.processed_sample_1[1], samples_1[1]);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_multiple_sample_types)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(123, "mixed_test") };

    std::vector<test_sample_2> samples_2 = { test_sample_2(3.14159, 555),
                                             test_sample_2(2.71828, 777) };

    std::vector<test_sample_3> samples_3 = { test_sample_3({ 0x01, 0x02, 0x03 }) };

    create_test_file_with_samples(samples_1, samples_2, samples_3);

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 1);
    EXPECT_EQ(g_tracker.sample_2_count, 2);
    EXPECT_EQ(g_tracker.sample_3_count, 1);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    EXPECT_EQ(g_tracker.processed_sample_1[0], samples_1[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[0], samples_2[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[1], samples_2[1]);
    EXPECT_EQ(g_tracker.processed_sample_3[0], samples_3[0]);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_unsupported_sample_type)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(123, "mixed_test") };

    std::vector<test_sample_2> samples_2 = { test_sample_2(3.14159, 555),
                                             test_sample_2(2.71828, 777) };

    std::vector<test_sample_3> samples_3 = { test_sample_3({ 0x01, 0x02, 0x03 }) };

    create_test_file_with_samples(samples_1, samples_2, samples_3);

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 1);
    EXPECT_EQ(g_tracker.sample_2_count, 2);
    EXPECT_EQ(g_tracker.sample_3_count, 0);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    EXPECT_EQ(g_tracker.processed_sample_1[0], samples_1[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[0], samples_2[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[1], samples_2[1]);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_file_with_zero_sized_samples)
{
    // Prepare test
    test_sample_1 valid_sample(42, "valid");
    {
        std::ofstream ofs(test_file_path, std::ios::binary);

        sample_header zero_header;
        zero_header.type        = test_type_identifier_t::sample_type_1;
        zero_header.sample_size = 0;

        ofs.write(reinterpret_cast<const char*>(&zero_header), sizeof(zero_header));
        ofs.write(reinterpret_cast<const char*>(&zero_header), sizeof(zero_header));

        sample_header valid_header;
        valid_header.type        = test_type_identifier_t::sample_type_1;
        valid_header.sample_size = trace_cache::get_size(valid_sample);

        ofs.write(reinterpret_cast<const char*>(&valid_header), sizeof(valid_header));

        std::vector<uint8_t> buffer(valid_header.sample_size);
        trace_cache::serialize(buffer.data(), valid_sample);
        ofs.write(reinterpret_cast<const char*>(buffer.data()), valid_header.sample_size);

        ofs.close();
    }

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());
    EXPECT_EQ(g_tracker.sample_1_count, 1);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    EXPECT_EQ(g_tracker.processed_sample_1[0], valid_sample);
    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_nonexisting_file)
{
    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser("non_existent_file.bin");

    EXPECT_THROW(parser.load(), std::runtime_error);
}

TEST_F(StorageParserTest, FinishedCallbackRegistrationAndExecution)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(777, "callback_test") };

    create_test_file_with_samples(samples_1, {}, {});

    bool callback_called = false;
    auto callback        = std::make_unique<std::function<void()>>(
        [&callback_called]() { callback_called = true; });

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    parser.register_on_finished_callback(std::move(callback));

    EXPECT_NO_THROW(parser.load());

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(g_tracker.sample_1_count, 1);
    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_without_finished_callback)
{
    std::vector<test_sample_2> samples_2 = { test_sample_2(9.87, 321) };

    create_test_file_with_samples({}, samples_2, {});

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    // Don't register callback
    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_2_count, 1);
    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_large_sample_data)
{
    std::vector<uint8_t> large_payload(10000);
    std::iota(large_payload.begin(), large_payload.end(), 0);

    std::vector<test_sample_3> samples_3 = { test_sample_3(large_payload) };

    create_test_file_with_samples({}, {}, samples_3);

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_3_count, 1);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    EXPECT_EQ(g_tracker.processed_sample_3[0], samples_3[0]);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_many_small_samples)
{
    std::vector<test_sample_1> many_samples;
    for(int i = 0; i < 1000; ++i)
    {
        many_samples.emplace_back(i, "sample_" + std::to_string(i));
    }

    create_test_file_with_samples(many_samples, {}, {});

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 1000);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    ASSERT_EQ(g_tracker.processed_sample_1.size(), 1000);

    for(int i = 0; i < 1000; ++i)
    {
        EXPECT_EQ(g_tracker.processed_sample_1[i], many_samples[i]);
    }

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, write_less_than_expected)
{
    std::ofstream ofs(test_file_path, std::ios::binary);

    sample_header header;
    header.type        = test_type_identifier_t::sample_type_1;
    header.sample_size = 100;

    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

    std::vector<uint8_t> partial_data(50, 0xAA);
    ofs.write(reinterpret_cast<const char*>(partial_data.data()), partial_data.size());

    ofs.close();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 0);
    EXPECT_EQ(g_tracker.sample_2_count, 0);
    EXPECT_EQ(g_tracker.sample_3_count, 0);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, read_fragmented_space)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(123,
                                                           "fragmented-space test") };
    std::vector<test_sample_2> samples_2 = { test_sample_2(3.14159, 555),
                                             test_sample_2(2.71828, 777) };
    std::vector<test_sample_3> samples_3 = { test_sample_3({ 0x01, 0x02, 0x03 }) };
    {
        std::ofstream ofs(test_file_path, std::ios::binary);

        write_vector(ofs, samples_1, test_type_identifier_t::sample_type_1);

        sample_header header;
        header.sample_size = 100;
        header.type        = test_type_identifier_t::fragmented_space;
        std::vector<uint8_t> fragmented_space;
        fragmented_space.reserve(header.sample_size);
        fragmented_space.assign(header.sample_size, 0);

        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        ofs.write(reinterpret_cast<const char*>(fragmented_space.data()),
                  header.sample_size);

        write_vector(ofs, samples_2, test_type_identifier_t::sample_type_2);
        write_vector(ofs, samples_3, test_type_identifier_t::sample_type_3);

        ofs.close();
    }

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(g_tracker.sample_1_count, 1);
    EXPECT_EQ(g_tracker.sample_2_count, 2);
    EXPECT_EQ(g_tracker.sample_3_count, 1);

    std::lock_guard<std::mutex> lock(g_tracker.data_mutex);
    EXPECT_EQ(g_tracker.processed_sample_1[0], samples_1[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[0], samples_2[0]);
    EXPECT_EQ(g_tracker.processed_sample_2[1], samples_2[1]);
    EXPECT_EQ(g_tracker.processed_sample_3[0], samples_3[0]);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}
