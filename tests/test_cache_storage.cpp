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
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

struct mock_worker_t
{
    explicit mock_worker_t(trace_cache::worker_function_t            worker_function,
                           trace_cache::worker_synchronization_ptr_t sync,
                           std::string                               filepath)

    : m_worker_function(worker_function)
    , m_sync(std::move(sync))
    , m_filepath(std::move(filepath))
    {}

    MOCK_METHOD(void, start, (const pid_t&) );
    MOCK_METHOD(void, stop, (const pid_t&) );

    void execute_flush(bool force = false)
    {
        m_worker_function(m_output_string_stream, force);
    }

    std::ostringstream m_output_string_stream;

    trace_cache::worker_function_t            m_worker_function;
    trace_cache::worker_synchronization_ptr_t m_sync;
    std::string                               m_filepath;
};

std::shared_ptr<mock_worker_t> g_mock_worker;

struct mock_worker_factory_t
{
    using worker_t = mock_worker_t;

    mock_worker_factory_t()                                   = delete;
    mock_worker_factory_t(mock_worker_factory_t&)             = delete;
    mock_worker_factory_t& operator=(mock_worker_factory_t&)  = delete;
    mock_worker_factory_t(mock_worker_factory_t&&)            = delete;
    mock_worker_factory_t& operator=(mock_worker_factory_t&&) = delete;

    static std::shared_ptr<worker_t> get_worker(
        trace_cache::worker_function_t                   worker_function,
        const trace_cache::worker_synchronization_ptr_t& worker_synchronization_ptr,
        std::string                                      filepath)
    {
        g_mock_worker = std::make_shared<worker_t>(
            worker_function, worker_synchronization_ptr, std::move(filepath));
        return g_mock_worker;
    }
};

struct mock_invalid_worker_factory_t
{
    using worker_t = mock_worker_t;

    mock_invalid_worker_factory_t()                                           = delete;
    mock_invalid_worker_factory_t(mock_invalid_worker_factory_t&)             = delete;
    mock_invalid_worker_factory_t& operator=(mock_invalid_worker_factory_t&)  = delete;
    mock_invalid_worker_factory_t(mock_invalid_worker_factory_t&&)            = delete;
    mock_invalid_worker_factory_t& operator=(mock_invalid_worker_factory_t&&) = delete;

    static std::shared_ptr<worker_t> get_worker(
        trace_cache::worker_function_t, const trace_cache::worker_synchronization_ptr_t&,
        std::string)
    {
        return nullptr;
    }
};

class BufferedStorageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path = "test_cache_" + std::to_string(test_counter++) + ".bin";
        std::remove(test_file_path.c_str());  // Be sure that file is created by test case
    }

    void TearDown() override
    {
        std::remove(test_file_path.c_str());
        g_mock_worker.reset();
    }

    std::string             test_file_path;
    static std::atomic<int> test_counter;

    void SetUpOnCall()
    {
        ON_CALL(*g_mock_worker, start).WillByDefault([] {
            g_mock_worker->m_sync->is_running = true;
        });

        ON_CALL(*g_mock_worker, stop).WillByDefault([] {
            g_mock_worker->m_sync->is_running = false;
        });
    }

    template <typename T>
    void verify_buffer_contains(const T& sample, const uint8_t* buffer,
                                size_t& buffer_pos)
    {
        auto type_id =
            *reinterpret_cast<const test_type_identifier_t*>(buffer + buffer_pos);
        EXPECT_EQ(type_id, T::type_identifier);
        buffer_pos += sizeof(test_type_identifier_t);

        auto size = *reinterpret_cast<const size_t*>(buffer + buffer_pos);
        EXPECT_EQ(size, trace_cache::get_size(sample));
        buffer_pos += sizeof(size_t);

        uint8_t* deserialize_ptr = const_cast<uint8_t*>(buffer + buffer_pos);
        auto     deserialized    = trace_cache::deserialize<T>(deserialize_ptr);
        EXPECT_EQ(deserialized, sample);
        buffer_pos += size;
    }
};

std::atomic<int> BufferedStorageTest::test_counter{ 0 };

TEST_F(BufferedStorageTest, TestStart)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    EXPECT_CALL(*g_mock_worker, start).Times(1).WillOnce([] {
        g_mock_worker->m_sync->is_running = true;
    });
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    EXPECT_NO_THROW(storage.start());
    EXPECT_NO_THROW(storage.start());
    EXPECT_EQ(test_file_path, g_mock_worker->m_filepath);
}

TEST_F(BufferedStorageTest, TestStartStop)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);

    EXPECT_CALL(*g_mock_worker, start).Times(1).WillOnce([] {
        g_mock_worker->m_sync->is_running = true;
    });
    EXPECT_CALL(*g_mock_worker, stop).Times(1).WillOnce([] {
        g_mock_worker->m_sync->is_running = false;
    });

    storage.start();
    storage.shutdown();
}

TEST_F(BufferedStorageTest, TryStoreEventSampleThrow)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);

    EXPECT_CALL(*g_mock_worker, start).Times(0);
    EXPECT_CALL(*g_mock_worker, stop).Times(0);

    test_sample_1 sample{ 10, "test string" };
    EXPECT_THROW(storage.store(sample), std::runtime_error);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(BufferedStorageTest, StoreAfterShutdown)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();

    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    storage.start();
    test_sample_1 before_shutdown(1, "before");
    EXPECT_NO_THROW(storage.store(before_shutdown));

    EXPECT_NO_THROW(storage.shutdown());

    test_sample_1 after_shutdown(2, "after");
    EXPECT_THROW(storage.store(after_shutdown), std::runtime_error);
}

TEST_F(BufferedStorageTest, InvalidWorker)
{
    trace_cache::buffered_storage<mock_invalid_worker_factory_t, test_type_identifier_t>
        storage(test_file_path);

    EXPECT_THROW(storage.start(), std::runtime_error);
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(BufferedStorageTest, StoreEventSamples)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();

    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    EXPECT_NO_THROW(storage.start());
    test_sample_1 sample{ 10, "test string" };
    EXPECT_NO_THROW(storage.store(sample));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(BufferedStorageTest, ImmediatelyFlush)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    EXPECT_NO_THROW(storage.start());
    EXPECT_NO_THROW(g_mock_worker->execute_flush(true));
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(BufferedStorageTest, FlushBelowThreshold)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    EXPECT_NO_THROW(storage.start());

    test_sample_1 sample{ 10, "test string" };
    EXPECT_NO_THROW(storage.store(sample));

    EXPECT_NO_THROW(g_mock_worker->execute_flush());
    EXPECT_EQ(g_mock_worker->m_output_string_stream.str().size(), 0);
    EXPECT_NO_THROW(storage.shutdown());
}

TEST_F(BufferedStorageTest, MixedSampleTypes)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    storage.start();
    test_sample_1 sample1(42, "event_data");
    test_sample_2 sample2(3.14159, 1001);
    test_sample_3 sample3({ 0xAA, 0xBB, 0xCC, 0xDD });

    // Empty samples
    test_sample_3 sample4;
    test_sample_1 sample5;

    EXPECT_NO_THROW(storage.store(sample1));
    EXPECT_NO_THROW(storage.store(sample2));
    EXPECT_NO_THROW(storage.store(sample3));
    EXPECT_NO_THROW(storage.store(sample1));
    EXPECT_NO_THROW(storage.store(sample4));
    EXPECT_NO_THROW(storage.store(sample5));

    g_mock_worker->execute_flush(true);

    EXPECT_NO_THROW(storage.shutdown());

    std::string buffer_data = g_mock_worker->m_output_string_stream.str();
    ASSERT_FALSE(buffer_data.empty());

    const uint8_t* buffer     = reinterpret_cast<const uint8_t*>(buffer_data.data());
    size_t         buffer_pos = 0;

    verify_buffer_contains(sample1, buffer, buffer_pos);
    verify_buffer_contains(sample2, buffer, buffer_pos);
    verify_buffer_contains(sample3, buffer, buffer_pos);
    verify_buffer_contains(sample1, buffer, buffer_pos);
    verify_buffer_contains(sample4, buffer, buffer_pos);
    verify_buffer_contains(sample5, buffer, buffer_pos);

    EXPECT_EQ(buffer_pos, buffer_data.size());
}

TEST_F(BufferedStorageTest, LargePayloadHandling)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    storage.start();
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

TEST_F(BufferedStorageTest, ConcurrentMixedTypeStores)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    storage.start();

    const int                num_threads      = 4;
    const int                items_per_thread = 10;
    std::vector<std::thread> threads;

    for(int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, t]() {
            for(int i = 0; i < items_per_thread; ++i)
            {
                switch(t % 3)
                {
                    case 0:
                        EXPECT_NO_THROW(
                            storage.store(test_sample_1(t * 100 + i, "data")));
                        break;
                    case 1:
                        EXPECT_NO_THROW(storage.store(test_sample_2(t * 2.5 + i, t + i)));
                        break;
                    case 2:
                        EXPECT_NO_THROW(
                            storage.store(test_sample_3(std::vector<uint8_t>(10, t))));
                        break;
                }
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }

    g_mock_worker->execute_flush(true);
    EXPECT_NO_THROW(storage.shutdown());

    std::string buffer_data = g_mock_worker->m_output_string_stream.str();
    EXPECT_FALSE(buffer_data.empty());

    size_t actual_samples = 0;
    size_t buffer_pos     = 0;

    while(buffer_pos < buffer_data.size())
    {
        auto type_id = *reinterpret_cast<const test_type_identifier_t*>(
            buffer_data.data() + buffer_pos);
        buffer_pos += sizeof(test_type_identifier_t);

        auto size = *reinterpret_cast<const size_t*>(buffer_data.data() + buffer_pos);
        buffer_pos += sizeof(size_t) + size;

        if(type_id != test_type_identifier_t::fragmented_space)
        {
            actual_samples++;
        }
    }

    EXPECT_EQ(actual_samples, num_threads * items_per_thread);
}

TEST_F(BufferedStorageTest, RepeatedFragmentation)
{
    trace_cache::buffered_storage<mock_worker_factory_t, test_type_identifier_t> storage(
        test_file_path);
    SetUpOnCall();
    EXPECT_CALL(*g_mock_worker, start).Times(1);
    EXPECT_CALL(*g_mock_worker, stop).Times(1);

    storage.start();

    const size_t         fragment_trigger_size = trace_cache::buffer_size / 5;
    std::vector<uint8_t> fragment_payload(fragment_trigger_size, 0xDD);
    const int            cycle_count = 3;
    const int            iter_count  = 2;

    for(int cycle = 0; cycle < cycle_count; ++cycle)
    {
        for(int i = 0; i < iter_count; ++i)
        {
            test_sample_3 sample(fragment_payload);
            EXPECT_NO_THROW(storage.store(sample));
        }

        test_sample_1 small_sample(cycle, "cycle_" + std::to_string(cycle));
        EXPECT_NO_THROW(storage.store(small_sample));

        g_mock_worker->execute_flush(true);
    }

    EXPECT_NO_THROW(storage.shutdown());

    std::string    buffer_data = g_mock_worker->m_output_string_stream.str();
    const uint8_t* buffer      = reinterpret_cast<const uint8_t*>(buffer_data.data());
    size_t         buffer_pos  = 0;
    size_t         fragmented_space_count = 0;
    size_t         sample1_count          = 0;
    size_t         sample3_count          = 0;

    while(buffer_pos < buffer_data.size())
    {
        auto type_id =
            *reinterpret_cast<const test_type_identifier_t*>(buffer + buffer_pos);
        buffer_pos += sizeof(test_type_identifier_t);

        auto size = *reinterpret_cast<const size_t*>(buffer + buffer_pos);
        buffer_pos += sizeof(size_t) + size;

        switch(type_id)
        {
            case test_type_identifier_t::sample_type_1: sample1_count++; break;
            case test_type_identifier_t::sample_type_3: sample3_count++; break;
            case test_type_identifier_t::fragmented_space:
                fragmented_space_count++;
                break;
            case test_type_identifier_t::sample_type_2:
                FAIL() << "Unexpected sample type";
                break;
        }
    }

    EXPECT_EQ(sample1_count, cycle_count);
    EXPECT_EQ(sample3_count, cycle_count * iter_count);
    EXPECT_GT(fragmented_space_count, 0);
}
