#include "cache_storage.hpp"
#include "mocked_types.hpp"
#include "storage_parser.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <random>
#include <thread>
#include <vector>

namespace
{
template <typename StorageType, typename SampleContainer>
void
store_and_shutdown(StorageType& storage, const SampleContainer& samples)
{
    storage.start();
    for(const auto& sample : samples)
    {
        storage.store(sample);
    }
    storage.shutdown();
}
}  // namespace

class CachingModuleIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path =
            "integration_test_cache_" + std::to_string(test_counter++) + ".bin";
        std::remove(test_file_path.c_str());
        CachingModuleIntegrationTest::processed_samples.clear();
    }

    void TearDown() override { std::remove(test_file_path.c_str()); }

    std::string test_file_path;

public:
    static std::atomic<int> test_counter;
    static std::vector<std::variant<test_sample_1, test_sample_2, test_sample_3>>
        processed_samples;

    static void execute_sample_processing(test_type_identifier_t          type_id,
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

std::atomic<int> CachingModuleIntegrationTest::test_counter{ 0 };
std::vector<std::variant<test_sample_1, test_sample_2, test_sample_3>>
    CachingModuleIntegrationTest::processed_samples{};

TEST_F(CachingModuleIntegrationTest, buffer_fragmentation_handling)
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
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>
            storage(test_file_path);
        storage.start();

        for(size_t i = 0; i < large_samples.size(); ++i)
        {
            storage.store(large_samples[i]);
            if(i % 2 == 0 && i < small_samples.size())
            {
                storage.store(small_samples[i]);
            }
        }

        storage.shutdown();
    }

    trace_cache::storage_parser<test_type_identifier_t, CachingModuleIntegrationTest,
                                test_sample_1, test_sample_2, test_sample_3>
        parser(test_file_path);

    parser.load();

    EXPECT_EQ(processed_samples.size(), 150);

    size_t large_count = 0;
    size_t small_count = 0;
    for(const auto& sample : processed_samples)
    {
        if(std::holds_alternative<test_sample_1>(sample)) large_count++;
        if(std::holds_alternative<test_sample_3>(sample)) small_count++;
    }

    EXPECT_EQ(large_count, 100);
    EXPECT_EQ(small_count, 50);
}

TEST_F(CachingModuleIntegrationTest, content_validation_edge_cases)
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
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>
            storage(test_file_path);
        storage.start();
        for(const auto& sample : test_samples)
        {
            std::visit([&storage](const auto& s) { storage.store(s); }, sample);
        }
        storage.shutdown();
    }

    trace_cache::storage_parser<test_type_identifier_t, CachingModuleIntegrationTest,
                                test_sample_1, test_sample_2, test_sample_3>
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

TEST_F(CachingModuleIntegrationTest, stress_test_multiple_fragmentations)
{
    const int iterations            = 50;
    const int samples_per_iteration = 100000;

    std::mt19937                          rng(42);
    std::uniform_int_distribution<int>    value_dist(1, 1000);
    std::uniform_int_distribution<size_t> size_dist(1, 500);

    std::vector<test_sample_1> all_samples;
    all_samples.reserve(iterations * samples_per_iteration);

    {
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>
            storage(test_file_path);
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
            }
        }

        storage.shutdown();
    }

    trace_cache::storage_parser<test_type_identifier_t, CachingModuleIntegrationTest,
                                test_sample_1, test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), all_samples.size());

    for(size_t i = 0; i < all_samples.size(); ++i)
    {
        EXPECT_EQ(std::get<test_sample_1>(processed_samples[i]), all_samples[i]);
    }
}

TEST_F(CachingModuleIntegrationTest, performance_write_test)
{
    const int    sample_count = 500000;
    const size_t payload_size = 1024 * 5;

    std::vector<test_sample_1> samples;
    samples.reserve(sample_count);
    for(int i = 0; i < sample_count; ++i)
    {
        std::string payload(payload_size, (i % 255));
        samples.emplace_back(i, payload);
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    {
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>
            storage(test_file_path);
        storage.start();

        for(const auto& sample : samples)
        {
            storage.store(sample);
        }

        storage.shutdown();
    }

    using unit = std::chrono::microseconds;

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_in_microseconds =
        std::chrono::duration_cast<unit>(end_time - start_time);
    auto period = static_cast<double>(unit::period().den);

    double avg_write_time =
        static_cast<double>(duration_in_microseconds.count()) / sample_count;
    double throughput =
        (sample_count * payload_size) / (duration_in_microseconds.count() / period);

    EXPECT_LT(avg_write_time, 50.0);     // maximum 10 microseconds per 5KB sample
    EXPECT_GT(throughput, 10 * 1024.0);  // at least 10KB per sec

    trace_cache::storage_parser<test_type_identifier_t, CachingModuleIntegrationTest,
                                test_sample_1, test_sample_2, test_sample_3>
        parser(test_file_path);
    parser.load();

    ASSERT_EQ(processed_samples.size(), sample_count);
}

TEST_F(CachingModuleIntegrationTest, concurrent_write_read_validation)
{
    const int                thread_count       = 4;
    const int                samples_per_thread = 250;
    std::vector<std::thread> writers;
    std::atomic<int>         write_counter{ 0 };

    {
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      test_type_identifier_t>
            storage(test_file_path);
        storage.start();

        for(int t = 0; t < thread_count; ++t)
        {
            writers.emplace_back([&storage, &write_counter, t]() {
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

    trace_cache::storage_parser<test_type_identifier_t, CachingModuleIntegrationTest,
                                test_sample_1, test_sample_2, test_sample_3>
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