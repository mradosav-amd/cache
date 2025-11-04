#include "src/cache_storage.hpp"
#include "src/cacheable.hpp"
#include "src/storage_parser.hpp"
#include <memory>
#include <string>
#include <unistd.h>

// ---------------- Samples Definitions ----------------

enum class type_identifier_t : uint32_t
{
    track_sample     = 0,
    process_sample   = 1,
    fragmented_space = 0xFFFF
};

struct track_sample : public trace_cache::cacheable_t
{
    explicit track_sample(std::string_view _track_name, size_t _node_id,
                          size_t _process_id, size_t _thread_id,
                          std::string_view _extdata)
    : track_name(std::move(_track_name))
    , node_id(_node_id)
    , process_id(_process_id)
    , thread_id(_thread_id)
    , extdata(_extdata)
    {}
    track_sample() = default;

    static constexpr type_identifier_t type_identifier = type_identifier_t::track_sample;

    std::string_view track_name;
    size_t           node_id;
    size_t           process_id;
    size_t           thread_id;
    std::string_view extdata;
};

template <>
void
trace_cache::serialize(uint8_t* buffer, const track_sample& item)
{
    utility::store_value(buffer, item.track_name, item.node_id,
                         item.process_id, item.thread_id, item.extdata);
}

template <>
track_sample
trace_cache::deserialize(uint8_t*& buffer)
{
    track_sample result;
    utility::parse_value(buffer, result.track_name, result.node_id,
                         result.process_id, result.thread_id, result.extdata);
    return result;
}

template <>
size_t
trace_cache::get_size(const track_sample& item)
{
    return utility::get_size(item.track_name, item.node_id,
                                     item.process_id, item.thread_id, item.extdata);
}

struct process_sample : public trace_cache::cacheable_t
{
    explicit process_sample(std::string_view _guid, size_t _node_id,
                            size_t _parent_process_id, size_t _process_id, size_t _init,
                            size_t _fini, size_t _start, size_t _end,
                            std::string_view _command, std::string_view _env,
                            std::string_view _extdata)
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

    std::string_view guid;
    size_t           node_id;
    size_t           parent_process_id;
    size_t           process_id;
    size_t           init;
    size_t           fini;
    size_t           start;
    size_t           end;
    std::string_view command;
    std::string_view env;
    std::string_view extdata;
};

template <>
void
trace_cache::serialize(uint8_t* buffer, const process_sample& item)
{
    utility::store_value(buffer, item.guid, item.node_id,
                         item.parent_process_id, item.process_id, item.init,
                         item.fini, item.start, item.end, item.command,
                         item.env, item.extdata);
}

template <>
process_sample
trace_cache::deserialize(uint8_t*& buffer)
{
    process_sample result;
    utility::parse_value(buffer, result.guid, result.node_id,
                         result.parent_process_id, result.process_id,
                         result.init, result.fini, result.start, result.end,
                         result.command, result.env, result.extdata);
    return result;
}

template <>
size_t
trace_cache::get_size(const process_sample& item)
{
    return utility::get_size(item.guid, item.node_id, item.parent_process_id,
                                     item.process_id, item.init, item.fini,
                                     item.start, item.end, item.command,
                                     item.env, item.extdata);
}

// ---------------- Post Processing ----------------

template <typename T>
struct handler_t
{
    void handle_track(const track_sample& track)
    {
        static_cast<T*>(this)->handle_track_impl(track);
    }

    void handle_process(const process_sample& process)
    {
        static_cast<T*>(this)->handle_process_impl(process);
    }

protected:
    ~handler_t() = default;
};

struct rocpd_format_handler_t : handler_t<rocpd_format_handler_t>
{
    void handle_track_impl(const track_sample& track)
    {
        // std::cout << "rocpd_format: " << track.track_name << std::endl;
        (void) track.track_name;
    }

    void handle_process_impl(const process_sample& process)
    {
        // std::cout << "rocpd_format: " << process.command << std::endl;
        (void) process.command;
    }
};

struct perfetto_format_handler_t : handler_t<perfetto_format_handler_t>
{
    void handle_track_impl(const track_sample& track)
    {
        // std::cout << "perfetto_format: " << track.track_name << std::endl;
        (void) track.track_name;
    }

    void handle_process_impl(const process_sample& process)
    {
        // std::cout << "perfetto_format: " << process.command << std::endl;
        (void) process.command;
    }
};

struct handler_view
{
    template <typename T>
    explicit handler_view(T& t)
    : object{ &t }
    , handle_track_impl{ [](void* obj, const track_sample& track) {
        static_cast<T*>(obj)->handle_track(track);
    } }
    , handle_process_impl{ [](void* obj, const process_sample& process) {
        static_cast<T*>(obj)->handle_process(process);
    } }
    {}

    void handle_track(const track_sample& track) const
    {
        handle_track_impl(object, track);
    }

    void handle_process(const process_sample& process) const
    {
        handle_process_impl(object, process);
    }

private:
    void*                                             object;
    std::function<void(void*, const track_sample&)>   handle_track_impl;
    std::function<void(void*, const process_sample&)> handle_process_impl;
};

struct type_processing_t
{
    static void clear_formats() { s_enabled_formats.clear(); }

    template <typename T>
    static void add_format(T& format)
    {
        s_enabled_formats.emplace_back(format);
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
                    handler.handle_track(static_cast<const track_sample&>(value));
                    break;
                }
                case type_identifier_t::process_sample:
                {
                    handler.handle_process(static_cast<const process_sample&>(value));
                    break;
                }
                default: break;
            }
        }
    }

private:
    static std::vector<handler_view> s_enabled_formats;
};

std::vector<handler_view> type_processing_t::s_enabled_formats{};

// ---------------- Example ----------------

constexpr auto number_of_iterations = 10000000;

void
run_multithread_example()
{
    std::vector<std::thread> threads;
    threads.reserve(2);

    rocpd_format_handler_t rocpd_handler;
    type_processing_t::add_format(rocpd_handler);

    threads.push_back(std::thread([]() {
        auto filepath = trace_cache::utility::get_buffered_storage_filename(0, 0);
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      type_identifier_t>
            buffered_storage(filepath);
        buffered_storage.start();

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

        buffered_storage.shutdown();

        trace_cache::storage_parser<type_identifier_t, type_processing_t, track_sample,
                                    process_sample>
            parser(filepath);

        parser.load();
    }));

    threads.push_back(std::thread([]() {
        auto filepath = trace_cache::utility::get_buffered_storage_filename(1, 1);
        trace_cache::buffered_storage<trace_cache::flush_worker_factory_t,
                                      type_identifier_t>
            buffered_storage(filepath);
        buffered_storage.start();

        size_t count = 0;
        do
        {
            process_sample ps("", 0, 0, 0, 0, 0, 0, 0, std::to_string(count), "", "");
            buffered_storage.store(ps);
            count++;
        } while(count != number_of_iterations);

        trace_cache::storage_parser<type_identifier_t, type_processing_t, track_sample,
                                    process_sample>
            parser(filepath);

        parser.load();
    }));

    for(auto& thread : threads)
    {
        thread.join();
    }
}

int
main()
{
    run_multithread_example();
}
