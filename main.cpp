#include "cache_storage.hpp"

struct track_sample : public cacheable_t {
  std::string track_name;
  size_t node_id;
  size_t process_id;
  size_t thread_id;
  std::string extdata;
};

struct process_sample : public cacheable_t {
  std::string guid;
  size_t node_id;
  size_t parent_process_id;
  size_t process_id;
  size_t init;
  size_t fini;
  size_t start;
  size_t end;
  std::string command;
  std::string env;
  std::string extdata;
};

void run_multithread_example(buffered_storage &buffered_storage) {
  const auto number_of_iterations = 10000000;

  std::vector<std::thread> threads;

  threads.push_back(std::thread([&]() {
    auto node_id = 0;
    auto process_id = 1;
    auto thread_id = 2;
    auto count = 0;
    do {
      // cache::store_track(buffered_storage, "GPU 1", node_id++, process_id++,
      // thread_id++, "{}");
      count++;
    } while (count != number_of_iterations);
  }));

  threads.push_back(std::thread([&]() {
    auto node_id = 0;

    auto parent_process_id = 1;
    auto process_id = 1;
    auto init = 2;
    auto fini = 2;
    auto start = 2;
    auto end = 2;
    auto count = 0;
    do {
      // cache::store_process(buffered_storage, "GUID", node_id++,
      // parent_process_id++,process_id++,
      //                      init++, fini++, start++, end++,
      //                      "/my/command.exe", "{ BBBBBBBBBBBBBB }",
      //                      "{AAAAAAAAAAAAAAAAAA}");
      //                      count++;
    } while (count != number_of_iterations);
  }));

  for (auto &thread : threads) {
    thread.join();
  }

  // buffered_storage.shutdown();

  std::cout << "Writing to cache is done. Validating cache.." << std::endl;

  //  cache::storage_parser::load({ cache::filename });
  // auto expected_count = threads.size() * number_of_iterations;
  // std::cout << (read_count != expected_count ? "Validation failed."
  //                                            : "Validation successful.")
  //           << std::endl;
}

int main() {
  buffered_storage buffered_storage;
  run_multithread_example(buffered_storage);
}
