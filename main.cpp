#include "src/cache_storage.hpp"
#include "src/cacheable.hpp"
#include "src/storage_parser.hpp"
#include <memory>
#include <string>

// ---------------- Samples Definitions ----------------

enum class type_identifier_t : uint32_t
{
    track_sample     = 0,
    process_sample   = 1,
    fragmented_space = 0xFFFF
};

struct track_sample : public trace_cache::cacheable_t
{
    explicit track_sample(std::string _track_name, size_t _node_id, size_t _process_id,
                          size_t _thread_id, std::string _extdata)
    : track_name(std::move(_track_name))
    , node_id(_node_id)
    , process_id(_process_id)
    , thread_id(_thread_id)
    , extdata(_extdata)
    {}
    track_sample() = default;

    static constexpr type_identifier_t type_identifier = type_identifier_t::track_sample;

    std::string track_name;
    size_t      node_id;
    size_t      process_id;
    size_t      thread_id;
    std::string extdata;
};

template <>
void
trace_cache::serialize(uint8_t* buffer, const track_sample& item)
{
    size_t position = 0;
    utility::store_value(item.track_name.c_str(), buffer, position);
    utility::store_value(item.node_id, buffer, position);
    utility::store_value(item.process_id, buffer, position);
    utility::store_value(item.thread_id, buffer, position);
    utility::store_value(item.extdata.c_str(), buffer, position);
}

template <>
track_sample
trace_cache::deserialize(uint8_t*& buffer)
{
    track_sample result;
    utility::parse_value(result.track_name, buffer);
    utility::parse_value(result.node_id, buffer);
    utility::parse_value(result.process_id, buffer);
    utility::parse_value(result.thread_id, buffer);
    utility::parse_value(result.extdata, buffer);
    return result;
}

template <>
size_t
trace_cache::get_size(const track_sample& item)
{
    return utility::get_size_helper(item.track_name.c_str()) +
           utility::get_size_helper(item.node_id) +
           utility::get_size_helper(item.process_id) +
           utility::get_size_helper(item.thread_id) +
           utility::get_size_helper(item.extdata.c_str());
}

struct process_sample : public trace_cache::cacheable_t
{
    explicit process_sample(std::string _guid, size_t _node_id, size_t _parent_process_id,
                            size_t _process_id, size_t _init, size_t _fini, size_t _start,
                            size_t _end, std::string _command, std::string _env,
                            std::string _extdata)
    : guid(std::move(_guid))
    , node_id(_node_id)
    , parent_process_id(_parent_process_id)
    , process_id(_process_id)
    , init(_init)
    , fini(_fini)
    , start(_start)
    , end(_end)
    , command(std::move(_command))
    , env(std::move(_env))
    , extdata(std::move(_extdata))
    {}
    process_sample() = default;

    static constexpr type_identifier_t type_identifier =
        type_identifier_t::process_sample;

    std::string guid;
    size_t      node_id;
    size_t      parent_process_id;
    size_t      process_id;
    size_t      init;
    size_t      fini;
    size_t      start;
    size_t      end;
    std::string command;
    std::string env;
    std::string extdata;
};

template <>
void
trace_cache::serialize(uint8_t* buffer, const process_sample& item)
{
    size_t position = 0;
    utility::store_value(item.guid.c_str(), buffer, position);
    utility::store_value(item.node_id, buffer, position);
    utility::store_value(item.parent_process_id, buffer, position);
    utility::store_value(item.process_id, buffer, position);
    utility::store_value(item.init, buffer, position);
    utility::store_value(item.fini, buffer, position);
    utility::store_value(item.start, buffer, position);
    utility::store_value(item.end, buffer, position);
    utility::store_value(item.command.c_str(), buffer, position);
    utility::store_value(item.env.c_str(), buffer, position);
    utility::store_value(item.extdata.c_str(), buffer, position);
}

template <>
process_sample
trace_cache::deserialize(uint8_t*& buffer)
{
    process_sample result;
    utility::parse_value(result.guid, buffer);
    utility::parse_value(result.node_id, buffer);
    utility::parse_value(result.parent_process_id, buffer);
    utility::parse_value(result.process_id, buffer);
    utility::parse_value(result.init, buffer);
    utility::parse_value(result.fini, buffer);
    utility::parse_value(result.start, buffer);
    utility::parse_value(result.end, buffer);
    utility::parse_value(result.command, buffer);
    utility::parse_value(result.env, buffer);
    utility::parse_value(result.extdata, buffer);
    return result;
}

template <>
size_t
trace_cache::get_size(const process_sample& item)
{
    return utility::get_size_helper(item.guid.c_str()) +
           utility::get_size_helper(item.node_id) +
           utility::get_size_helper(item.parent_process_id) +
           utility::get_size_helper(item.process_id) +
           utility::get_size_helper(item.init) + utility::get_size_helper(item.fini) +
           utility::get_size_helper(item.start) + utility::get_size_helper(item.end) +
           utility::get_size_helper(item.command.c_str()) +
           utility::get_size_helper(item.env.c_str()) +
           utility::get_size_helper(item.extdata.c_str());
}

// ---------------- Post Processing ----------------

struct handler_t
{
    virtual ~handler_t()                               = default;
    virtual void handle_track(const track_sample&)     = 0;
    virtual void handle_process(const process_sample&) = 0;
};
struct rocpd_format_handler_t : handler_t
{
    void handle_track(const track_sample& track) override
    {
        std::cout << "rocpd_format: " << track.track_name << std::endl;
    };

    void handle_process(const process_sample& process) override
    {
        std::cout << "rocpd_format: " << process.command << std::endl;
    };
};
struct perfetto_format_handler_t : handler_t
{
    void handle_track(const track_sample& track) override
    {
        std::cout << "perfetto_format: " << track.track_name << std::endl;
    };

    void handle_process(const process_sample& process) override
    {
        std::cout << "perfetto_format: " << process.command << std::endl;
    };
};

struct type_processing_t
{
    static void clear_formats() { s_enabled_formats.clear(); }

    static void add_format(std::unique_ptr<handler_t> format)
    {
        s_enabled_formats.push_back(std::move(format));
    }

    static void execute_sample_processing(type_identifier_t               type_identifier,
                                          const trace_cache::cacheable_t& value)
    {
        for(const auto& handler : s_enabled_formats)
        {
            switch(type_identifier)
            {
                case type_identifier_t::track_sample:
                {
                    auto track = static_cast<const track_sample&>(value);
                    handler->handle_track(track);
                    break;
                }
                case type_identifier_t::process_sample:
                {
                    auto process = static_cast<const process_sample&>(value);
                    handler->handle_process(process);
                    break;
                }
                default: break;
            }
        }
    }

private:
    static std::vector<std::unique_ptr<handler_t>> s_enabled_formats;
};

std::vector<std::unique_ptr<handler_t>> type_processing_t::s_enabled_formats{};

// ---------------- Example ----------------

void
run_multithread_example()
{
    auto filepath = trace_cache::utility::get_buffered_storage_filename(0, 0);
    trace_cache::buffered_storage<trace_cache::flush_worker_factory_t, type_identifier_t>
        buffered_storage(filepath);
    buffered_storage.start();

    const auto number_of_iterations = 1000;

    std::vector<std::thread> threads;

    threads.push_back(std::thread([&]() {
        size_t node_id    = 1;
        size_t process_id = 2;
        size_t thread_id  = 3;
        size_t count      = 0;

        do
        {
            auto track_name = "track_name_" + std::to_string(node_id);
            buffered_storage.store(
                track_sample{ track_name, node_id, process_id, thread_id, "{}" });

            node_id++;
            process_id++;
            thread_id++;
            count++;
        } while(count != number_of_iterations);
    }));

    threads.push_back(std::thread([&]() {
        size_t count = 0;
        do
        {
            process_sample ps("", 0, 0, 0, 0, 0, 0, 0, std::to_string(count), "", "");
            buffered_storage.store(ps);
            count++;
        } while(count != number_of_iterations);
    }));

    for(auto& thread : threads)
    {
        thread.join();
    }

    buffered_storage.shutdown();

    // Prepare formats
    type_processing_t::add_format(std::make_unique<rocpd_format_handler_t>());

    trace_cache::storage_parser<type_identifier_t, type_processing_t, track_sample,
                                process_sample>
        parser(filepath);

    parser.load();
}

int
main()
{
    run_multithread_example();
}
