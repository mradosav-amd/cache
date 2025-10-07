#include "cache_storage.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

class FlushWorkerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_file_path =
            "flush_test_" + std::to_string(test_counter.fetch_add(1)) + ".bin";
        std::remove(test_file_path.c_str());
        worker_sync = std::make_shared<trace_cache::worker_synchronization_t>();
    }

    void TearDown() override { std::remove(test_file_path.c_str()); }

    trace_cache::worker_synchronization_ptr_t worker_sync;
    std::string                               test_file_path;
    static std::atomic<int>                   test_counter;
};

std::atomic<int> FlushWorkerTest::test_counter{ 0 };

TEST_F(FlushWorkerTest, start_worker_in_correct_state)
{
    bool worker_called   = false;
    auto worker_function = [&](trace_cache::ofs_t&, bool) { worker_called = true; };

    trace_cache::flush_worker_t worker(worker_function, worker_sync, test_file_path);
    pid_t                       current_pid = getpid();

    worker.start(current_pid);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_TRUE(worker_sync->is_running);
    EXPECT_EQ(worker_sync->origin_pid, current_pid);

    worker.stop(current_pid);
}

TEST_F(FlushWorkerTest, stop_worker_complete)
{
    std::atomic<bool> worker_called{ false };
    auto worker_function = [&](trace_cache::ofs_t&, bool) { worker_called = true; };

    trace_cache::flush_worker_t worker(worker_function, worker_sync, test_file_path);
    pid_t                       current_pid = getpid();

    worker.start(current_pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    worker.stop(current_pid);

    EXPECT_TRUE(worker_sync->exit_finished);
    EXPECT_FALSE(worker_sync->is_running);
    EXPECT_TRUE(worker_called);
}

TEST_F(FlushWorkerTest, worker_function_called_on_stop)
{
    std::atomic<int>  call_count{ 0 };
    std::atomic<bool> force_flag{ false };
    auto              worker_function = [&](trace_cache::ofs_t&, bool force) {
        call_count++;
        force_flag = force;
    };

    trace_cache::flush_worker_t worker(worker_function, worker_sync, test_file_path);
    pid_t                       current_pid = getpid();

    worker.start(current_pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    worker.stop(current_pid);

    EXPECT_GE(call_count.load(), 1);
    EXPECT_TRUE(force_flag);
}

TEST_F(FlushWorkerTest, multiple_stop_calls_are_safe)
{
    auto worker_function = [](trace_cache::ofs_t&, bool) {};

    trace_cache::flush_worker_t worker(worker_function, worker_sync, test_file_path);
    pid_t                       current_pid = getpid();

    worker.start(current_pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    worker.stop(current_pid);
    worker.stop(current_pid);
    worker.stop(current_pid);

    EXPECT_TRUE(worker_sync->exit_finished);
    EXPECT_FALSE(worker_sync->is_running);
}

TEST_F(FlushWorkerTest, worker_factory_creates_valid_object)
{
    auto worker_function = [](trace_cache::ofs_t&, bool) {};

    auto worker = trace_cache::flush_worker_factory_t::get_worker(
        worker_function, worker_sync, test_file_path);

    EXPECT_NE(worker, nullptr);
    EXPECT_EQ(typeid(*worker), typeid(trace_cache::flush_worker_t));
}

TEST_F(FlushWorkerTest, worker_handles_invalid_path)
{
    auto        worker_function = [](trace_cache::ofs_t&, bool) {};
    std::string invalid_path    = "/invalid/path/file.bin";

    trace_cache::flush_worker_t worker(worker_function, worker_sync, invalid_path);
    pid_t                       current_pid = getpid();

    EXPECT_THROW(worker.start(current_pid), std::runtime_error);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    worker.stop(current_pid);

    EXPECT_FALSE(worker_sync->exit_finished);
    EXPECT_FALSE(worker_sync->is_running);
}

TEST_F(FlushWorkerTest, different_pid_start_stop)
{
    std::atomic<bool> worker_called{ false };
    auto worker_function = [&](trace_cache::ofs_t&, bool) { worker_called = true; };

    trace_cache::flush_worker_t worker(worker_function, worker_sync, test_file_path);
    pid_t                       parent_pid = getpid();

    worker.start(parent_pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_TRUE(worker_sync->is_running);
    EXPECT_EQ(worker_sync->origin_pid, parent_pid);

    pid_t child_pid = fork();
    if(child_pid == 0)
    {
        pid_t current_child_pid = getpid();
        worker.stop(current_child_pid);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        bool still_running = worker_sync->is_running;
        bool exit_finished = worker_sync->exit_finished;

        exit(still_running ? 1 : (exit_finished ? 2 : 0));
    }
    else
    {
        int status;
        waitpid(child_pid, &status, 0);
        int child_exit_code = WEXITSTATUS(status);

        EXPECT_EQ(child_exit_code, 0);
        EXPECT_FALSE(worker_sync->exit_finished);
        EXPECT_TRUE(worker_sync->is_running);

        worker.stop(parent_pid);
        EXPECT_TRUE(worker_sync->exit_finished);
        EXPECT_FALSE(worker_sync->is_running);
        EXPECT_TRUE(worker_called);
    }
}