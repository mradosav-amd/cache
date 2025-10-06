#include "cache_storage.hpp"
#include "cacheable.hpp"
#include "mocked_types.hpp"
#include "storage_parser.hpp"

#include "gmock/gmock.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

class CachingModuleIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path =
            "integration_test_cache_" + std::to_string(test_counter++) + ".bin";
        std::remove(test_file_path.c_str());
        processed_samples.clear();
        set_sample_processor(this);
    }

    void TearDown() override
    {
        std::remove(test_file_path.c_str());
        clear_sample_processor();
    }

    template <typename StorageType, typename SampleContainer>
    void store_and_shutdown(StorageType& storage, const SampleContainer& samples)
    {
        storage.start();
        for(const auto& sample : samples)
        {
            storage.store(sample);
        }
        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    static void set_sample_processor(CachingModuleIntegrationTest* instance)
    {
        current_test_instance = instance;
    }

    static void clear_sample_processor() { current_test_instance = nullptr; }

    using storage_type =
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>;

    std::string test_file_path;
    std::vector<std::variant<test_sample_1, test_sample_2, test_sample_3>>
        processed_samples;

public:
    static std::atomic<int>              test_counter;
    static CachingModuleIntegrationTest* current_test_instance;

    void process_sample(test_type_identifier_t          type_id,
                        const trace_cache::cacheable_t& item)
    {
        switch(type_id)
        {
            case test_type_identifier_t::sample_type_1:
                processed_samples.push_back(static_cast<const test_sample_1&>(item));
                break;
            case test_type_identifier_t::sample_type_2:
                processed_samples.push_back(static_cast<const test_sample_2&>(item));
                break;
            case test_type_identifier_t::sample_type_3:
                processed_samples.push_back(static_cast<const test_sample_3&>(item));
                break;
            default: break;
        }
    }
};

std::atomic<int>              CachingModuleIntegrationTest::test_counter{ 0 };
CachingModuleIntegrationTest* CachingModuleIntegrationTest::current_test_instance{
    nullptr
};

struct integration_test_sample_processor_t
{
    static void execute_sample_processing(test_type_identifier_t          type_identifier,
                                          const trace_cache::cacheable_t& value)
    {
        if(CachingModuleIntegrationTest::current_test_instance)
        {
            CachingModuleIntegrationTest::current_test_instance->process_sample(
                type_identifier, value);
        }
    }
};

TEST_F(CachingModuleIntegrationTest, BufferFragmentationHandling)
{
    std::vector<test_sample_1> large_samples;
    std::vector<test_sample_3> small_samples;

    for(int i = 0; i < 100; ++i)
    {
        std::string large_text(1000, 'A' + (i % 26));
        large_samples.emplace_back(i, large_text);

        std::vector<uint8_t> small_payload(10, static_cast<uint8_t>(i));
        small_samples.emplace_back(small_payload);
    }

    {
        storage_type storage(test_file_path);
        storage.start();

        for(size_t i = 0; i < large_samples.size(); ++i)
        {
            storage.store(large_samples[i]);
            if(i % 3 == 0 && i < small_samples.size())
            {
                storage.store(small_samples[i]);
            }
        }

        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    trace_cache::storage_parser<test_type_identifier_t,
                                integration_test_sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);
    std::cout << "LOAD 11111" << std::endl;
    parser.load();

    EXPECT_GE(processed_samples.size(), 100);

    size_t large_count = 0;
    size_t small_count = 0;
    for(const auto& sample : processed_samples)
    {
        if(std::holds_alternative<test_sample_1>(sample)) large_count++;
        if(std::holds_alternative<test_sample_3>(sample)) small_count++;
    }

    EXPECT_EQ(large_count, 100);
    EXPECT_GE(small_count, 30);
}

TEST_F(CachingModuleIntegrationTest, ContentValidationEdgeCases)
{
    test_sample_1 max_int(std::numeric_limits<int>::max(), "max_value");
    test_sample_1 min_int(std::numeric_limits<int>::min(), "min_value");
    test_sample_1 zero_int(0, "");
    test_sample_1 special_chars(123, "Special\n\t\r\0chars");

    test_sample_2 max_double(std::numeric_limits<double>::max(),
                             std::numeric_limits<uint32_t>::max());
    test_sample_2 min_double(std::numeric_limits<double>::lowest(), 0);
    test_sample_2 infinity(std::numeric_limits<double>::infinity(), 42);
    test_sample_2 neg_infinity(-std::numeric_limits<double>::infinity(), 43);

    std::vector<uint8_t> max_vector(10000, 0xFF);
    test_sample_3        large_payload(max_vector);
    test_sample_3        empty_payload;
    std::vector<uint8_t> single_zero = { 0x00 };
    test_sample_3        zero_payload(single_zero);

    std::vector<std::variant<test_sample_1, test_sample_2, test_sample_3>>
        test_samples = { max_int,       min_int,       zero_int,    special_chars,
                         max_double,    min_double,    infinity,    neg_infinity,
                         large_payload, empty_payload, zero_payload };

    {
        storage_type storage(test_file_path);
        storage.start();
        for(const auto& sample : test_samples)
        {
            std::visit([&storage](const auto& s) { storage.store(s); }, sample);
        }
        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    trace_cache::storage_parser<test_type_identifier_t,
                                integration_test_sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), test_samples.size());

    EXPECT_EQ(std::get<test_sample_1>(processed_samples[0]), max_int);
    EXPECT_EQ(std::get<test_sample_1>(processed_samples[1]), min_int);
    EXPECT_EQ(std::get<test_sample_1>(processed_samples[2]), zero_int);
    EXPECT_EQ(std::get<test_sample_2>(processed_samples[4]), max_double);
    EXPECT_EQ(std::get<test_sample_2>(processed_samples[5]), min_double);
    EXPECT_EQ(std::get<test_sample_3>(processed_samples[8]), large_payload);
    EXPECT_EQ(std::get<test_sample_3>(processed_samples[9]), empty_payload);
    EXPECT_EQ(std::get<test_sample_3>(processed_samples[10]), zero_payload);
}

TEST_F(CachingModuleIntegrationTest, StressTestMultipleFragmentations)
{
    const int iterations            = 5;
    const int samples_per_iteration = 200;

    std::mt19937                          rng(42);
    std::uniform_int_distribution<int>    value_dist(1, 1000);
    std::uniform_int_distribution<size_t> size_dist(1, 500);

    std::vector<test_sample_1> all_samples;
    all_samples.reserve(iterations * samples_per_iteration);

    {
        storage_type storage(test_file_path);
        storage.start();

        for(int iter = 0; iter < iterations; ++iter)
        {
            for(int i = 0; i < samples_per_iteration; ++i)
            {
                int         value     = value_dist(rng);
                size_t      text_size = size_dist(rng);
                std::string text(text_size, 'X');

                test_sample_1 sample(value, text);
                all_samples.push_back(sample);
                storage.store(sample);

                if(i % 50 == 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }

        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    trace_cache::storage_parser<test_type_identifier_t,
                                integration_test_sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), all_samples.size());

    for(size_t i = 0; i < all_samples.size(); ++i)
    {
        EXPECT_EQ(std::get<test_sample_1>(processed_samples[i]), all_samples[i]);
    }
}

TEST_F(CachingModuleIntegrationTest, PerformanceWriteTest)
{
    const int    sample_count = 1000;
    const size_t payload_size = 100;

    std::vector<test_sample_1> samples;
    samples.reserve(sample_count);
    for(int i = 0; i < sample_count; ++i)
    {
        std::string payload(payload_size, 'P');
        samples.emplace_back(i, payload);
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    {
        storage_type storage(test_file_path);
        storage.start();

        for(const auto& sample : samples)
        {
            storage.store(sample);
        }

        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    double avg_write_time = static_cast<double>(duration.count()) / sample_count;
    double throughput = (sample_count * payload_size) / (duration.count() / 1000000.0);

    EXPECT_LT(avg_write_time, 1000.0);
    EXPECT_GT(throughput, 1000.0);

    trace_cache::storage_parser<test_type_identifier_t,
                                integration_test_sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), sample_count);
}

TEST_F(CachingModuleIntegrationTest, ConcurrentWriteReadValidation)
{
    const int                thread_count       = 4;
    const int                samples_per_thread = 250;
    std::vector<std::thread> writers;
    std::atomic<int>         write_counter{ 0 };

    {
        storage_type storage(test_file_path);
        storage.start();

        for(int t = 0; t < thread_count; ++t)
        {
            writers.emplace_back([&storage, &write_counter, t, samples_per_thread]() {
                for(int i = 0; i < samples_per_thread; ++i)
                {
                    int           unique_id = t * samples_per_thread + i;
                    test_sample_1 sample(unique_id, "thread_" + std::to_string(t) +
                                                        "_sample_" + std::to_string(i));
                    storage.store(sample);
                    write_counter++;

                    if(i % 10 == 0)
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
            });
        }

        for(auto& writer : writers)
        {
            writer.join();
        }

        storage.shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_EQ(write_counter.load(), thread_count * samples_per_thread);

    trace_cache::storage_parser<test_type_identifier_t,
                                integration_test_sample_processor_t, test_sample_1,
                                test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), thread_count * samples_per_thread);

    std::set<int> unique_values;
    for(const auto& sample : processed_samples)
    {
        int value = std::get<test_sample_1>(sample).value;
        unique_values.insert(value);
    }

    EXPECT_EQ(unique_values.size(), thread_count * samples_per_thread);
}