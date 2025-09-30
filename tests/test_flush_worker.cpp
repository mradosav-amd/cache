#include "cache_storage.hpp"
#include "cacheable.hpp"
#include "mocked_types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <thread>
#include <vector>

class FlushWorkerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path =
            "flush_test_" + std::to_string(test_counter.fetch_add(1)) + ".bin";
        std::remove(test_file_path.c_str());

        cache_buffer = std::make_shared<trace_cache::cache_buffer_t>();
        worker_sync  = std::make_shared<trace_cache::worker_synchronization_t>();
    }

    void TearDown() override { std::remove(test_file_path.c_str()); }

    void fill_buffer_safely(const std::string& pattern, size_t size)
    {
        std::lock_guard guard(cache_buffer->mutex);
        size_t          safe_size = std::min(size, trace_cache::buffer_size);
        for(size_t i = 0; i < safe_size; ++i)
        {
            cache_buffer->array->data()[i] = pattern[i % pattern.size()];
        }
        cache_buffer->head = safe_size;
        cache_buffer->tail = 0;
    }

    trace_cache::cache_buffer_ptr_t           cache_buffer;
    trace_cache::worker_synchronization_ptr_t worker_sync;
    std::string                               test_file_path;
    static std::atomic<int>                   test_counter;
};

std::atomic<int> FlushWorkerTest::test_counter{ 0 };
TEST_F(FlushWorkerTest, FlushWithoutForceThreshold)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    const size_t small_data_size = trace_cache::flush_threshold / 2;
    fill_buffer_safely("BELOW_THRESHOLD", small_data_size);

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, false);
    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(ifs.tellg(), 0);

    {
        std::lock_guard guard(cache_buffer->mutex);
        EXPECT_NE(cache_buffer->head, cache_buffer->tail);
    }
}

TEST_F(FlushWorkerTest, FlushAboveThreshold)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    const size_t large_data_size = trace_cache::flush_threshold + 100;
    fill_buffer_safely("ABOVE_THRESHOLD", large_data_size);

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, false);
    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(static_cast<size_t>(ifs.tellg()), large_data_size);
}

TEST_F(FlushWorkerTest, FlushExactThreshold)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    fill_buffer_safely("EXACT_THRESHOLD", trace_cache::flush_threshold);

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, false);
    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(static_cast<size_t>(ifs.tellg()), trace_cache::flush_threshold);
}

TEST_F(FlushWorkerTest, FlushFullBuffer)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    fill_buffer_safely("FULL_BUFFER_TEST", trace_cache::buffer_size);

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, true);
    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(static_cast<size_t>(ifs.tellg()), trace_cache::buffer_size);
}

TEST_F(FlushWorkerTest, MultipleFlushCycles)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    std::ofstream ofs(test_file_path, std::ios::binary);

    for(int cycle = 0; cycle < 5; ++cycle)
    {
        std::string pattern = "CYCLE_" + std::to_string(cycle) + "_";
        fill_buffer_safely(pattern, 500);
        worker.execute_flush(ofs, true);
    }

    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(static_cast<size_t>(ifs.tellg()), 5 * 500);
}

TEST_F(FlushWorkerTest, FlushWrappedBufferExactBoundary)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    {
        std::lock_guard guard(cache_buffer->mutex);

        const size_t boundary_pos = trace_cache::buffer_size - 10;
        std::memset(cache_buffer->array->data() + boundary_pos, 0xAA, 10);
        std::memset(cache_buffer->array->data(), 0xBB, 15);

        cache_buffer->tail = boundary_pos;
        cache_buffer->head = 15;
    }

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, true);
    ofs.close();

    std::ifstream     ifs(test_file_path, std::ios::binary);
    std::vector<char> content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());

    EXPECT_EQ(content.size(), 25);
    for(size_t i = 0; i < 10; ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(content[i]), 0xAA);
    }
    for(size_t i = 10; i < 25; ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(content[i]), 0xBB);
    }
}

TEST_F(FlushWorkerTest, ConcurrentBufferAccess)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    std::ofstream     ofs(test_file_path, std::ios::binary);
    std::atomic<bool> flush_started{ false };
    std::atomic<bool> buffer_modified{ false };

    std::thread modifier([&]() {
        while(!flush_started)
        {
            std::this_thread::yield();
        }

        std::lock_guard guard(cache_buffer->mutex);
        cache_buffer->array->data()[0] = 0xFF;
        buffer_modified                = true;
    });

    fill_buffer_safely("CONCURRENT_TEST", 1000);

    flush_started = true;
    worker.execute_flush(ofs, true);

    modifier.join();
    ofs.close();

    EXPECT_TRUE(buffer_modified);

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(static_cast<size_t>(ifs.tellg()), 1000);
}

TEST_F(FlushWorkerTest, InvalidFileStream)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    fill_buffer_safely("INVALID_STREAM_TEST", 100);

    std::ofstream invalid_ofs;
    EXPECT_FALSE(invalid_ofs.is_open());

    worker.execute_flush(invalid_ofs, true);

    {
        std::lock_guard guard(cache_buffer->mutex);
        EXPECT_EQ(cache_buffer->head, cache_buffer->tail);
    }
}

TEST_F(FlushWorkerTest, WorkerConstructorParameters)
{
    auto        test_buffer = std::make_shared<trace_cache::cache_buffer_t>();
    auto        test_sync   = std::make_shared<trace_cache::worker_synchronization_t>();
    std::string test_path   = "constructor_test.bin";

    trace_cache::flush_worker worker(test_buffer, test_sync, test_path);

    {
        std::lock_guard guard(test_buffer->mutex);
        std::string     test_data = "CONSTRUCTOR_TEST";
        std::memcpy(test_buffer->array->data(), test_data.c_str(), test_data.size());
        test_buffer->head = test_data.size();
        test_buffer->tail = 0;
    }

    std::ofstream ofs(test_path, std::ios::binary);
    worker.execute_flush(ofs, true);
    ofs.close();

    std::ifstream ifs(test_path, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "CONSTRUCTOR_TEST");
    std::remove(test_path.c_str());
}

TEST_F(FlushWorkerTest, WorkerThreadExceptionHandling)
{
    std::string               invalid_path = "/invalid/directory/file.bin";
    trace_cache::flush_worker worker(cache_buffer, worker_sync, invalid_path);

    EXPECT_THROW(worker(), std::runtime_error);
}

TEST_F(FlushWorkerTest, WorkerThreadTimeout)
{
    worker_sync->is_running = true;
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    std::thread worker_thread(std::ref(worker));

    std::this_thread::sleep_for(
        std::chrono::milliseconds(trace_cache::CACHE_FILE_FLUSH_TIMEOUT + 50));

    worker_sync->is_running = false;
    worker_sync->is_running_condition.notify_all();

    std::mutex       exit_mutex;
    std::unique_lock exit_lock(exit_mutex);
    bool             finished = worker_sync->exit_finished_condition.wait_for(
        exit_lock, std::chrono::seconds(2), [&]() { return worker_sync->exit_finished; });

    EXPECT_TRUE(finished);

    if(worker_thread.joinable())
    {
        worker_thread.join();
    }
}

TEST_F(FlushWorkerTest, FlushZeroSizedData)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    {
        std::lock_guard guard(cache_buffer->mutex);
        cache_buffer->head = 0;
        cache_buffer->tail = 0;
    }

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, true);
    ofs.close();

    std::ifstream ifs(test_file_path, std::ios::binary | std::ios::ate);
    EXPECT_EQ(ifs.tellg(), 0);
}

TEST_F(FlushWorkerTest, BufferStateAfterFlush)
{
    trace_cache::flush_worker worker(cache_buffer, worker_sync, test_file_path);

    const size_t initial_head = 500;
    const size_t initial_tail = 100;

    {
        std::lock_guard guard(cache_buffer->mutex);
        cache_buffer->head = initial_head;
        cache_buffer->tail = initial_tail;
    }

    std::ofstream ofs(test_file_path, std::ios::binary);
    worker.execute_flush(ofs, true);
    ofs.close();

    {
        std::lock_guard guard(cache_buffer->mutex);
        EXPECT_EQ(cache_buffer->head, initial_head);
        EXPECT_EQ(cache_buffer->tail, initial_head);
    }
}