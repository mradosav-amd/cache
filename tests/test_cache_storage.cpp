#include "cache_storage.hpp"
#include "cacheable.hpp"
#include "mocked_types.hpp"

#include "gmock/gmock.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

struct buffer_observer_worker
{
    explicit buffer_observer_worker(
        trace_cache::cache_buffer_ptr_t           cache_buffer_ptr,
        trace_cache::worker_synchronization_ptr_t worker_synchronization_ptr,
        std::string                               filepath)
    : m_buffer_ptr(std::move(cache_buffer_ptr))
    , m_worker_synchronization(std::move(worker_synchronization_ptr))
    , m_filepath(std::move(filepath))
    , observations(0)
    , max_buffer_usage(0)
    {}

    buffer_observer_worker(const buffer_observer_worker&)            = delete;
    buffer_observer_worker& operator=(const buffer_observer_worker&) = delete;

    buffer_observer_worker(buffer_observer_worker&& other) noexcept
    : m_buffer_ptr(std::move(other.m_buffer_ptr))
    , m_worker_synchronization(std::move(other.m_worker_synchronization))
    , m_filepath(std::move(other.m_filepath))
    , observations(other.observations.load())
    , max_buffer_usage(other.max_buffer_usage.load())
    {}

    buffer_observer_worker& operator=(buffer_observer_worker&& other) noexcept
    {
        if(this != &other)
        {
            m_buffer_ptr             = std::move(other.m_buffer_ptr);
            m_worker_synchronization = std::move(other.m_worker_synchronization);
            m_filepath               = std::move(other.m_filepath);
            observations             = other.observations.load();
            max_buffer_usage         = other.max_buffer_usage.load();
        }
        return *this;
    }

    void operator()()
    {
        while(m_worker_synchronization->is_running)
        {
            size_t current_usage = 0;
            {
                std::lock_guard guard(m_buffer_ptr->mutex);
                current_usage = (m_buffer_ptr->head >= m_buffer_ptr->tail)
                                    ? (m_buffer_ptr->head - m_buffer_ptr->tail)
                                    : (trace_cache::buffer_size - m_buffer_ptr->tail +
                                       m_buffer_ptr->head);
            }

            observations++;
            max_buffer_usage = std::max(max_buffer_usage.load(), current_usage);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        m_worker_synchronization->exit_finished = true;
        m_worker_synchronization->exit_finished_condition.notify_one();
    }

    trace_cache::cache_buffer_ptr_t           m_buffer_ptr;
    trace_cache::worker_synchronization_ptr_t m_worker_synchronization;
    std::string                               m_filepath;
    std::atomic<size_t>                       observations;
    std::atomic<size_t>                       max_buffer_usage;
};

class CacheStorageTestBase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path = "test_cache_" + std::to_string(test_counter++) + ".bin";
        std::remove(test_file_path.c_str());
    }

    void TearDown() override { std::remove(test_file_path.c_str()); }

    std::string             test_file_path;
    static std::atomic<int> test_counter;
};

std::atomic<int> CacheStorageTestBase::test_counter{ 0 };

TEST_F(CacheStorageTestBase, StoreEventSamples)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    test_sample_1 sample{ 10, "test string" };
    EXPECT_NO_THROW(storage.store(sample));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, MixedSampleTypes)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    test_sample_1 sample1(42, "event_data");
    test_sample_2 sample2(3.14159, 1001);
    test_sample_3 sample3({ 0xAA, 0xBB, 0xCC, 0xDD });

    EXPECT_NO_THROW(storage.store(sample1));
    EXPECT_NO_THROW(storage.store(sample2));
    EXPECT_NO_THROW(storage.store(sample3));
    EXPECT_NO_THROW(storage.store(sample1));

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, LargePayloadHandling)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    std::vector<uint8_t> large_payload(5000, 0xFF);
    test_sample_3        large_sample(large_payload);

    EXPECT_NO_THROW(storage.store(large_sample));

    for(int i = 0; i < 10; ++i)
    {
        std::vector<uint8_t> payload(1000 + i * 100, static_cast<uint8_t>(i));
        test_sample_3        sample(payload);
        EXPECT_NO_THROW(storage.store(sample));
    }

    for(int i = 0; i < 5; ++i)
    {
        std::string   large_string(1000 + i * 200, 'A' + (i % 26));
        test_sample_1 large_text_sample(i * 1000, large_string);
        EXPECT_NO_THROW(storage.store(large_text_sample));
    }

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, RapidSequentialStores)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const int rapid_count = 2000;
    for(int i = 0; i < rapid_count; ++i)
    {
        test_sample_2 metric(i * 0.1, i);
        EXPECT_NO_THROW(storage.store(metric));
    }

    EXPECT_NO_THROW(storage.shutdown());
}
TEST_F(CacheStorageTestBase, ConcurrentMixedTypeStores)
{
    trace_cache::buffered_storage<trace_cache::flush_worker, test_type_identifier>
        storage(test_file_path, getpid());

    const int                num_threads      = 6;
    const int                items_per_thread = 50;
    std::vector<std::thread> threads;
    std::atomic<int>         total_stored{ 0 };

    for(int t = 0; t < 2; ++t)
    {
        threads.emplace_back([&, t]() {
            for(int i = 0; i < items_per_thread; ++i)
            {
                test_sample_1 sample(t * 1000 + i, "thread_" + std::to_string(t) + "_" +
                                                       std::to_string(i));
                EXPECT_NO_THROW(storage.store(sample));
                total_stored++;
            }
        });
    }

    for(int t = 2; t < 4; ++t)
    {
        threads.emplace_back([&, t]() {
            for(int i = 0; i < items_per_thread; ++i)
            {
                test_sample_2 sample(t * 100.0 + i * 0.5, t * 1000 + i);
                EXPECT_NO_THROW(storage.store(sample));
                total_stored++;
            }
        });
    }

    for(int t = 4; t < 6; ++t)
    {
        threads.emplace_back([&, t]() {
            for(int i = 0; i < items_per_thread; ++i)
            {
                std::vector<uint8_t> payload(20 + t, static_cast<uint8_t>(i));
                test_sample_3        sample(payload);
                EXPECT_NO_THROW(storage.store(sample));
                total_stored++;
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_NO_THROW(storage.shutdown());
    EXPECT_EQ(total_stored, num_threads * items_per_thread);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::ifstream file(test_file_path, std::ios::binary | std::ios::ate);
    if(file.is_open())
    {
        size_t file_size = static_cast<size_t>(file.tellg());
        EXPECT_GT(file_size, 0);
        file.close();
    }
    else
    {
        std::ifstream retry_file(test_file_path, std::ios::binary);
        EXPECT_TRUE(retry_file.good());
    }
}

TEST_F(CacheStorageTestBase, EmptyPayload)
{
    trace_cache::buffered_storage<trace_cache::flush_worker, test_type_identifier>
        storage(test_file_path, getpid());

    EXPECT_NO_THROW(storage.store(test_sample_1(0, "")));
    EXPECT_NO_THROW(storage.store(test_sample_3{}));
    EXPECT_NO_THROW(storage.shutdown());

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::ifstream file(test_file_path, std::ios::binary | std::ios::ate);
    if(file.is_open())
    {
        size_t file_size = static_cast<size_t>(file.tellg());
        EXPECT_GT(file_size, 0);

        size_t expected_minimum =
            (trace_cache::get_size(test_sample_1(0, "")) +
             trace_cache::header_size<
                 test_type_identifier>) +(trace_cache::get_size(test_sample_3{}) +
                                          trace_cache::header_size<test_type_identifier>);

        EXPECT_GE(file_size, expected_minimum);
        file.close();
    }
    else
    {
        std::ifstream retry_file(test_file_path, std::ios::binary);
        EXPECT_TRUE(retry_file.good());
    }
}

TEST_F(CacheStorageTestBase, FileCreationVerification)
{
    {
        trace_cache::buffered_storage<trace_cache::flush_worker, test_type_identifier>
            storage(test_file_path, getpid());

        test_sample_1 sample(123, "verify_creation");
        storage.store(sample);
        storage.shutdown();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::ifstream file(test_file_path, std::ios::binary);
    EXPECT_TRUE(file.good());
}

TEST_F(CacheStorageTestBase, ImmediateShutdown)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, StoreAfterMultipleTypes)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    for(int i = 0; i < 10; ++i)
    {
        storage.store(test_sample_1(i, "sample_" + std::to_string(i)));
        storage.store(test_sample_2(i * 1.5, i + 100));
        storage.store(test_sample_3(std::vector<uint8_t>(5, i)));
    }

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, BufferFragmentation)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const size_t         large_payload_size = trace_cache::buffer_size / 4;
    std::vector<uint8_t> large_payload(large_payload_size, 0xAB);

    for(int i = 0; i < 3; ++i)
    {
        test_sample_3 large_sample(large_payload);
        EXPECT_NO_THROW(storage.store(large_sample));
    }

    test_sample_1 small_sample(999, "trigger_fragmentation");
    EXPECT_NO_THROW(storage.store(small_sample));

    for(int i = 0; i < 5; ++i)
    {
        test_sample_2 sample(i * 2.5, i + 1000);
        EXPECT_NO_THROW(storage.store(sample));
    }

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, ExactBufferBoundaryStores)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    test_sample_1 test_sample(1, "test");
    size_t        single_entry_size = trace_cache::get_size(test_sample) +
                               trace_cache::header_size<test_type_identifier>;

    size_t entries_to_fill = trace_cache::buffer_size / single_entry_size;

    for(size_t i = 0; i < entries_to_fill; ++i)
    {
        test_sample_1 sample(static_cast<int>(i), "boundary_test");
        EXPECT_NO_THROW(storage.store(sample));
    }

    test_sample_1 overflow_sample(999, "overflow");
    EXPECT_NO_THROW(storage.store(overflow_sample));

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, MaximumSinglePayload)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    size_t max_payload =
        trace_cache::buffer_size - trace_cache::header_size<test_type_identifier> - 64;
    std::vector<uint8_t> max_payload_data(max_payload, 0xCC);

    test_sample_3 max_sample(max_payload_data);
    EXPECT_NO_THROW(storage.store(max_sample));

    test_sample_1 small_sample(42, "after_max");
    EXPECT_NO_THROW(storage.store(small_sample));

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, RepeatedFragmentation)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const size_t         fragment_trigger_size = trace_cache::buffer_size / 3;
    std::vector<uint8_t> fragment_payload(fragment_trigger_size, 0xDD);

    for(int cycle = 0; cycle < 5; ++cycle)
    {
        for(int i = 0; i < 2; ++i)
        {
            test_sample_3 sample(fragment_payload);
            EXPECT_NO_THROW(storage.store(sample));
        }

        test_sample_1 small_sample(cycle, "cycle_" + std::to_string(cycle));
        EXPECT_NO_THROW(storage.store(small_sample));
    }

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, ConcurrentStoresDuringFragmentation)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const int                num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<int>         fragmentation_triggers{ 0 };

    for(int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, t]() {
            const size_t         large_size = trace_cache::buffer_size / 6;
            std::vector<uint8_t> payload(large_size, static_cast<uint8_t>(t));

            for(int i = 0; i < 10; ++i)
            {
                test_sample_3 sample(payload);
                EXPECT_NO_THROW(storage.store(sample));

                if(i % 3 == 0)
                {
                    test_sample_1 small_sample(t * 100 + i,
                                               "thread_" + std::to_string(t));
                    EXPECT_NO_THROW(storage.store(small_sample));
                    fragmentation_triggers++;
                }
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_GT(fragmentation_triggers.load(), 0);
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, StoreAfterShutdown)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    test_sample_1 before_shutdown(1, "before");
    EXPECT_NO_THROW(storage.store(before_shutdown));

    EXPECT_NO_THROW(storage.shutdown());

    test_sample_1 after_shutdown(2, "after");
    EXPECT_THROW(storage.store(after_shutdown), std::runtime_error);
}

TEST_F(CacheStorageTestBase, MultipleShutdowns)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    test_sample_1 sample(123, "multi_shutdown");
    EXPECT_NO_THROW(storage.store(sample));

    EXPECT_NO_THROW(storage.shutdown());
    EXPECT_NO_THROW(storage.shutdown());
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, ZeroSizePayloads)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    EXPECT_NO_THROW(storage.store(test_sample_1(0, "")));
    EXPECT_NO_THROW(storage.store(test_sample_2(0.0, 0)));
    EXPECT_NO_THROW(storage.store(test_sample_3(std::vector<uint8_t>{})));

    EXPECT_NO_THROW(storage.store(test_sample_1(42, "non_empty")));
    EXPECT_NO_THROW(storage.store(test_sample_1(0, "")));

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, BufferWrapAroundBehavior)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const size_t         small_payload_size = 100;
    std::vector<uint8_t> small_payload(small_payload_size, 0xEE);

    size_t iterations =
        (trace_cache::buffer_size * 3) /
        (small_payload_size + trace_cache::header_size<test_type_identifier>);

    for(size_t i = 0; i < iterations; ++i)
    {
        test_sample_3 sample(small_payload);
        EXPECT_NO_THROW(storage.store(sample));

        if(i % 10 == 0)
        {
            test_sample_1 text_sample(static_cast<int>(i), "wrap_" + std::to_string(i));
            EXPECT_NO_THROW(storage.store(text_sample));
        }
    }

    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, ExtremeConcurrency)
{
    trace_cache::buffered_storage<buffer_observer_worker, test_type_identifier> storage(
        test_file_path, getpid());

    const int                num_threads       = 16;
    const int                stores_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int>         successful_stores{ 0 };

    for(int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, t]() {
            std::random_device              rd;
            std::mt19937                    gen(rd());
            std::uniform_int_distribution<> size_dist(10, 500);

            for(int i = 0; i < stores_per_thread; ++i)
            {
                int sample_type = (t + i) % 3;

                try
                {
                    switch(sample_type)
                    {
                        case 0:
                        {
                            std::string data(size_dist(gen), 'A' + (t % 26));
                            storage.store(test_sample_1(t * 1000 + i, data));
                            break;
                        }
                        case 1:
                        {
                            storage.store(test_sample_2(t * 3.14 + i, t * 100 + i));
                            break;
                        }
                        case 2:
                        {
                            std::vector<uint8_t> payload(size_dist(gen),
                                                         static_cast<uint8_t>(t));
                            storage.store(test_sample_3(payload));
                            break;
                        }
                    }
                    successful_stores++;
                } catch(...)
                {
                    // Count failures but don't fail test - some errors expected under
                    // extreme load
                }
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(successful_stores.load(), num_threads * stores_per_thread);
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(CacheStorageTestBase, ShutdownFromDifferentProcess)
{
    trace_cache::buffered_storage<trace_cache::flush_worker, test_type_identifier>
        storage(test_file_path, getpid());

    test_sample_1 sample(42, "cross_process_test");
    EXPECT_NO_THROW(storage.store(sample));

    pid_t different_pid = getpid() + 999;

    auto start_time = std::chrono::steady_clock::now();
    EXPECT_NO_THROW(storage.shutdown(different_pid));
    auto end_time = std::chrono::steady_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    EXPECT_LT(duration.count(), 50);

    std::ifstream file_after_different_pid(test_file_path, std::ios::binary);
    EXPECT_FALSE(file_after_different_pid.good());

    EXPECT_NO_THROW(storage.shutdown());

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::ifstream file_after_original_pid(test_file_path, std::ios::binary);
    EXPECT_TRUE(file_after_original_pid.good());
}