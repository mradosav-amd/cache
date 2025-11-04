#include "mocked_types.hpp"
#include "storage_parser.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <numeric>
#include <vector>

class sample_processor_t
{
public:
    sample_processor_t() = default;

    void set_expected_samples_1(const std::vector<test_sample_1>& samples)
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        m_expected_samples_1 = samples;
    }

    void set_expected_samples_2(const std::vector<test_sample_2>& samples)
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        m_expected_samples_2 = samples;
    }

    void set_expected_samples_3(const std::vector<test_sample_3>& samples)
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        m_expected_samples_3 = samples;
    }

    void execute_sample_processing(test_type_identifier_t          type_identifier,
                                    const trace_cache::cacheable_t& value)
    {
        switch(type_identifier)
        {
            case test_type_identifier_t::sample_type_1:
            {
                const auto&                 sample = static_cast<const test_sample_1&>(value);
                std::lock_guard<std::mutex> lock(m_data_mutex);
                size_t                      idx = m_sample_1_count++;
                check_sample_1(idx, sample);
                break;
            }
            case test_type_identifier_t::sample_type_2:
            {
                const auto&                 sample = static_cast<const test_sample_2&>(value);
                std::lock_guard<std::mutex> lock(m_data_mutex);
                size_t                      idx = m_sample_2_count++;
                check_sample_2(idx, sample);
                break;
            }
            case test_type_identifier_t::sample_type_3:
            {
                const auto&                 sample = static_cast<const test_sample_3&>(value);
                std::lock_guard<std::mutex> lock(m_data_mutex);
                size_t                      idx = m_sample_3_count++;
                check_sample_3(idx, sample);
                break;
            }
            default: m_unknown_count++; break;
        }
    }

    int get_sample_1_count() const { return m_sample_1_count.load(); }
    int get_sample_2_count() const { return m_sample_2_count.load(); }
    int get_sample_3_count() const { return m_sample_3_count.load(); }
    int get_unknown_count() const { return m_unknown_count.load(); }

private:
    void check_sample_1(size_t index, const test_sample_1& sample)
    {
        if(index < m_expected_samples_1.size())
        {
            EXPECT_EQ(m_expected_samples_1[index], sample);
        }
    }

    void check_sample_2(size_t index, const test_sample_2& sample)
    {
        if(index < m_expected_samples_2.size())
        {
            EXPECT_EQ(m_expected_samples_2[index], sample);
        }
    }

    void check_sample_3(size_t index, const test_sample_3& sample)
    {
        if(index < m_expected_samples_3.size())
        {
            EXPECT_EQ(m_expected_samples_3[index], sample);
        }
    }

    std::atomic<int>           m_sample_1_count{ 0 };
    std::atomic<int>           m_sample_2_count{ 0 };
    std::atomic<int>           m_sample_3_count{ 0 };
    std::atomic<int>           m_unknown_count{ 0 };
    std::vector<test_sample_1> m_expected_samples_1;
    std::vector<test_sample_2> m_expected_samples_2;
    std::vector<test_sample_3> m_expected_samples_3;
    std::mutex                 m_data_mutex;
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
    }

    void TearDown() override { cleanup_test_file(); }

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

            std::vector<uint8_t> buffer;
            buffer.reserve(header.sample_size);
            buffer.assign(header.sample_size, 0xFF);

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

    auto processor = std::make_unique<sample_processor_t>();
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 0);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 0);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 0);

    EXPECT_FALSE(std::filesystem::exists(test_file_path));
}

TEST_F(StorageParserTest, load_single_sample_type_1)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(42, "test_string"),
                                             test_sample_1(100, "another_test") };

    create_test_file_with_samples(samples_1, {}, {});

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(samples_1);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 2);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 0);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 0);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_multiple_sample_types)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(123, "mixed_test") };
    std::vector<test_sample_2> samples_2 = { test_sample_2(3.14159, 555),
                                             test_sample_2(2.71828, 777) };
    std::vector<test_sample_3> samples_3 = { test_sample_3({ 0x01, 0x02, 0x03 }) };

    create_test_file_with_samples(samples_1, samples_2, samples_3);

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(samples_1);
    processor->set_expected_samples_2(samples_2);
    processor->set_expected_samples_3(samples_3);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 1);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 2);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 1);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_unsupported_sample_type)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(123, "mixed_test") };
    std::vector<test_sample_2> samples_2 = { test_sample_2(3.14159, 555),
                                             test_sample_2(2.71828, 777) };
    std::vector<test_sample_3> samples_3 = { test_sample_3({ 0x01, 0x02, 0x03 }) };

    create_test_file_with_samples(samples_1, samples_2, samples_3);

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(samples_1);
    processor->set_expected_samples_2(samples_2);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 1);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 2);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 0);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_file_with_zero_sized_samples)
{
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

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1({ valid_sample });
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());
    EXPECT_EQ(processor_ptr->get_sample_1_count(), 1);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_nonexisting_file)
{
    auto processor = std::make_unique<sample_processor_t>();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser("non_existent_file.bin", std::move(processor));

    EXPECT_THROW(parser.load(), std::runtime_error);
}

TEST_F(StorageParserTest, FinishedCallbackRegistrationAndExecution)
{
    std::vector<test_sample_1> samples_1 = { test_sample_1(777, "callback_test") };

    create_test_file_with_samples(samples_1, {}, {});

    bool callback_called = false;
    auto callback        = std::make_unique<std::function<void()>>(
        [&callback_called]() { callback_called = true; });

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(samples_1);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    parser.register_on_finished_callback(std::move(callback));

    EXPECT_NO_THROW(parser.load());

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(processor_ptr->get_sample_1_count(), 1);
    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_without_finished_callback)
{
    std::vector<test_sample_2> samples_2 = { test_sample_2(9.87, 321) };

    create_test_file_with_samples({}, samples_2, {});

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_2(samples_2);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_2_count(), 1);
    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_large_sample_data)
{
    std::vector<uint8_t> large_payload(10000);
    std::iota(large_payload.begin(), large_payload.end(), 0);

    std::vector<test_sample_3> samples_3 = { test_sample_3(large_payload) };

    create_test_file_with_samples({}, {}, samples_3);

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_3(samples_3);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_3_count(), 1);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}

TEST_F(StorageParserTest, load_many_small_samples)
{
    constexpr auto num_of_elements = 15;

    std::vector<std::string> strings;
    strings.reserve(num_of_elements);
    std::vector<test_sample_1> many_samples;
    many_samples.reserve(num_of_elements);

    for(int i = 0; i < num_of_elements; ++i)
    {
        auto x = "sample_" + std::to_string(i);
        strings.push_back(x);
        many_samples.push_back({ 0, strings[i] });
    }

    create_test_file_with_samples(many_samples, {}, {});

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(many_samples);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), num_of_elements);
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

    auto processor = std::make_unique<sample_processor_t>();
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 0);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 0);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 0);

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

    auto processor = std::make_unique<sample_processor_t>();
    processor->set_expected_samples_1(samples_1);
    processor->set_expected_samples_2(samples_2);
    processor->set_expected_samples_3(samples_3);
    auto processor_ptr = processor.get();

    trace_cache::storage_parser<test_type_identifier_t, sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path, std::move(processor));

    EXPECT_NO_THROW(parser.load());

    EXPECT_EQ(processor_ptr->get_sample_1_count(), 1);
    EXPECT_EQ(processor_ptr->get_sample_2_count(), 2);
    EXPECT_EQ(processor_ptr->get_sample_3_count(), 1);

    EXPECT_NE(std::remove(test_file_path.c_str()), 0);
}
