#include <chrono>
#include <unordered_map>
#include <array>
#include <string>
#include <iostream>
#include <iomanip>
#include <type_traits>
#include <limits>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <bitset>

namespace benchmark
{
  template <bool Enabled, typename CategoryEnum,
            CategoryEnum... EnabledCategories>
  struct benchmark_impl
  {
    template <CategoryEnum... Categories> static void start() {}
    template <CategoryEnum... Categories> static void end() {}
    static void initFromEnv(const char * = nullptr) {}
    static void printResults() {}
  };

  template <typename CategoryEnum, CategoryEnum... EnabledCategories>
  class benchmark_impl<true, CategoryEnum, EnabledCategories...>
  {
    static_assert(std::is_enum_v<CategoryEnum>,
                  "CategoryEnum must be an enum");

  public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    static constexpr size_t kMaxCategories
      = static_cast<size_t>(CategoryEnum::Count);

    template <CategoryEnum... Categories> static void start()
    {
      const TimePoint now = Clock::now();
      std::lock_guard lock(mutex_);
      (..., (if_compiled<Categories>([&] {
         if(runtimeEnabled_.test(to_index(Categories)))
           startTimes_[to_index(Categories)] = now;
       })));
    }

    template <CategoryEnum... Categories> static void end()
    {
      const TimePoint endTime = Clock::now();
      std::lock_guard lock(mutex_);
      (..., (if_compiled<Categories>([&] {
         if(runtimeEnabled_.test(to_index(Categories)))
           endCategory(endTime, Categories);
       })));
    }

    static void init_from_env(const char *envVar = "BENCHMARK_CATEGORIES")
    {
      std::lock_guard lock(mutex_);
      const char *env = std::getenv(envVar);
      if(!env || std::string(env).empty())
        {
          std::cerr << "No benchmark_impl categories specified in environment "
                       "variable: "
                    << envVar << "\n";
          return;
        }
      std::string str(env);
      std::istringstream ss(str);
      std::string token;

      while(std::getline(ss, token, ','))
        {
          token.erase(0, token.find_first_not_of(" \t"));
          token.erase(token.find_last_not_of(" \t") + 1);
          for(CategoryEnum cat : compiledCategories)
            {
              if(to_string(cat) == token)
                {
                  runtimeEnabled_.set(to_index(cat));
                }
            }
        }
    }

    static void show_results()
    {
      std::lock_guard lock(mutex_);
      std::vector<std::pair<CategoryEnum, result_data> > sorted;

      for(CategoryEnum cat : compiledCategories)
        {
          const auto &data = results_[to_index(cat)];
          if(data.count > 0)
            {
              sorted.emplace_back(cat, data);
            }
        }

      std::sort(sorted.begin(), sorted.end(),
                [](const auto &a, const auto &b) {
                  return a.second.totalTime > b.second.totalTime;
                });

      constexpr int wCategory = 15;
      constexpr int wCalls = 8;
      constexpr int wTotal = 12;
      constexpr int wAvg = 10;
      constexpr int wMin = 10;
      constexpr int wMax = 10;

      std::cout << "\n============ Benchmark Results (Sorted by Total Time) "
                   "============\n";
      std::cout << std::left << std::setw(wCategory) << "Category"
                << std::right << std::setw(wCalls) << "Calls"
                << std::setw(wTotal) << "Total(ms)" << std::setw(wAvg)
                << "Avg(us)" << std::setw(wMin) << "Min(us)" << std::setw(wMax)
                << "Max(us)" << "\n";

      std::cout << std::string(
        wCategory + wCalls + wTotal + wAvg + wMin + wMax, '-')
                << "\n";

      for(const auto &[cat, data] : sorted)
        {
          double totalMs = static_cast<double>(data.totalTime) / 1000.0;
          double avgUs = static_cast<double>(data.totalTime) / data.count;

          std::cout << std::left << std::setw(wCategory) << to_string(cat)
                    << std::right << std::setw(wCalls) << data.count
                    << std::setw(wTotal) << std::fixed << std::setprecision(3)
                    << totalMs << std::setw(wAvg) << std::fixed
                    << std::setprecision(1) << avgUs << std::setw(wMin)
                    << data.minTime << std::setw(wMax) << data.maxTime << "\n";
        }

      std::cout << std::string(
        wCategory + wCalls + wTotal + wAvg + wMin + wMax, '=')
                << "\n\n";
    }

  private:
    struct result_data
    {
      long long totalTime = 0;
      int count = 0;
      long long minTime = std::numeric_limits<long long>::max();
      long long maxTime = std::numeric_limits<long long>::min();

      void update(long long duration)
      {
        totalTime += duration;
        count += 1;
        if(duration < minTime)
          minTime = duration;
        if(duration > maxTime)
          maxTime = duration;
      }
    };

    static constexpr size_t to_index(CategoryEnum cat)
    {
      return static_cast<size_t>(cat);
    }

    static void endCategory(const TimePoint &endTime, CategoryEnum cat)
    {
      const size_t idx = to_index(cat);
      auto it = startTimes_.find(idx);
      if(it == startTimes_.end())
        {
          std::cerr << "Benchmark error: no start time for category "
                    << to_string(cat) << "\n";
          return;
        }

      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                        endTime - it->second)
                        .count();
      startTimes_.erase(it);
      results_[idx].update(duration);
    }

    template <CategoryEnum Cat, typename Func>
    static constexpr void if_compiled(Func &&f)
    {
      if constexpr(((Cat == EnabledCategories) || ...))
        {
          f();
        }
    }

    static std::string to_string(CategoryEnum cat)
    {
      switch(cat)
        {
        case CategoryEnum::WriteTrack: return "WriteTrack";
        case CategoryEnum::WriteProcess: return "WriteProcess";
        case CategoryEnum::WritePmcEvent1: return "WritePmcEvent1";
        case CategoryEnum::WritePmcEvent2: return "WritePmcEvent2";
        default: return "Unknown";
        }
    }

    static constexpr std::array<CategoryEnum, sizeof...(EnabledCategories)>
      compiledCategories = {EnabledCategories...};

    static inline std::unordered_map<size_t, TimePoint> startTimes_;
    static inline std::array<result_data, kMaxCategories> results_{};
    static inline std::bitset<kMaxCategories> runtimeEnabled_;
    static inline std::mutex mutex_;
  };


  enum class category
  {
    WriteTrack,
    WriteProcess,
    WritePmcEvent1,
    WritePmcEvent2,
    Count
  };

} // namespace benchmark

using rps_bencmark = benchmark::benchmark_impl<
  true, benchmark::category, benchmark::category::WriteTrack, benchmark::category::WriteProcess, benchmark::category::WritePmcEvent1, benchmark::category::WritePmcEvent2>;