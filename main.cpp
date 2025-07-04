#include "cache_storage.hpp"
#include "metadata.hpp"
#include "benchmark.hpp"
#include <string>


void run_multithread_example()
{

  const auto number_of_iterations = 10000000;
  cache::storage buffered_storage;

  std::vector<std::thread> threads;

  threads.push_back(std::thread([&]() {

    auto node_id = 0;
    auto process_id = 1;
    auto thread_id = 2;
    auto count = 0;
    do {
      rps_bencmark::start<benchmark::category::WriteTrack>();
      cache::store_track(buffered_storage, "GPU 1", node_id++, process_id++, thread_id++, "{}");
      rps_bencmark::end<benchmark::category::WriteTrack>();
      count++;
    }
    while(count != number_of_iterations);
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
      rps_bencmark::start<benchmark::category::WriteProcess>();
      cache::store_process(buffered_storage, "GUID", node_id++, parent_process_id++,process_id++,
                           init++, fini++, start++, end++,
                           "/my/command.exe", "{ BBBBBBBBBBBBBB }",
                           "{AAAAAAAAAAAAAAAAAA}");
                           count++;
      rps_bencmark::end<benchmark::category::WriteProcess>();
    }
    while(count != number_of_iterations);
  }));

  for(auto& thread : threads) {
    thread.join();
  }

  buffered_storage.shutdown();

  std::cout << "Writing to cache is done. Validating cache.." << std::endl;

  auto read_count = cache::storage_parser::load({ cache::filename });
  auto expected_count = threads.size() * number_of_iterations;
  std::cout << (read_count != expected_count ? "Validation failed." : "Validation successful.") << std::endl;
}

void run_metadata_example()
{
  metadata::storage metadata;
  cache::storage buffered_storage;
  const auto number_of_iterations = 1000;

  const auto nid = 1024;
  const auto pid = 2048;

  metadata.set_current_node({
    .nid = nid,
    .hostname = "my host",
    .hash = 12345
  });

  metadata.set_current_process({
    .node_id = nid,
    .process_id = pid
  });

  metadata.add_agent({
    .nid = nid,
    .pid = pid,
    .absolute_index  = 0,
    .type = "GPU"
  });

  metadata.add_agent({
    .nid = nid,
    .pid = pid,
    .absolute_index  = 1,
    .type = "CPU"
  });

  metadata.add_pmc_info({
    .nid = nid,
    .pid = pid,
    .agent_index = 0,
    .unique_name = "gpu_temp",
    .unit = "C"
  });

  metadata.add_pmc_info({
    .nid = nid,
    .pid = pid,
    .agent_index = 1,
    .unique_name = "cpu_busy",
    .unit = "%"
  });

  std::vector<std::thread> threads;

  threads.push_back(std::thread([&]() {
    auto node_id = 0;
    auto process_id = 1;
    auto thread_id = 2;
    auto count = 0;
    while(count != number_of_iterations) {
      // std::cout << "track" << std::endl;
      std::string name = "PLACEHOLDER TEXT" + std::to_string(count);
      rps_bencmark::start<benchmark::category::WriteTrack>();
      cache::store_track(buffered_storage, name.c_str(), node_id++, process_id++, thread_id++, "{}");
      rps_bencmark::end<benchmark::category::WriteTrack>();
      count++;
    }

  }));

  threads.push_back(std::thread([&]() {
    auto count = 0;
    do {
      // rps_bencmark::start<benchmark::category::WriteProcess>();

      // std::cout << "gpu event 1" << std::endl;
      cache::store_pmc_event(buffered_storage, "gpu_temp", (count % 10) + 50);

      // rps_bencmark::end<benchmark::category::WriteProcess>();
      count++;
    }
    while(count != number_of_iterations );
  }));

  threads.push_back(std::thread([&]() {
    auto count = 0;
    do {
      // rps_bencmark::start<benchmark::category::WriteProcess>();

      // std::cout << "cpu event 2" << std::endl;
      cache::store_pmc_event(buffered_storage, "cpu_busy", count % 100);

      // rps_bencmark::end<benchmark::category::WriteProcess>();
      count++;
    }
    while(count != number_of_iterations );
  }));

  for(auto& thread : threads) {
    thread.join();
  }
  std::cout << "threads done" << std::endl;

  buffered_storage.shutdown();

  cache::storage_parser::load({ cache::filename }, metadata);
}


int main() {

  rps_bencmark::init_from_env();

  // run_multithread_example();
  run_metadata_example();

  rps_bencmark::show_results();
}
