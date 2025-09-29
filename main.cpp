#include "cache_storage.hpp"
#include "cacheable.hpp"
#include "storage_parser.hpp"
#include <string>

// ---------------- Samples definitions ----------------
enum class type_identifier_t : uint32_t
{
    track_sample     = 0,
    process_sample   = 1,
    fragmented_space = 0xFFFF
};

struct track_sample : public cacheable_t
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
serialize(uint8_t* buffer, const track_sample& item)
{
    size_t position = 0;
    store_value(item.track_name.c_str(), buffer, position);
    store_value(item.node_id, buffer, position);
    store_value(item.process_id, buffer, position);
    store_value(item.thread_id, buffer, position);
    store_value(item.extdata.c_str(), buffer, position);
}

template <>
track_sample
deserialize(uint8_t*& buffer)
{
    track_sample result;
    process_arg(buffer, result.track_name);
    process_arg(buffer, result.node_id);
    process_arg(buffer, result.process_id);
    process_arg(buffer, result.thread_id);
    process_arg(buffer, result.extdata);
    return result;
}

auto
get_size(const track_sample& item)
{
    return get_size_helper(item.track_name.c_str()) + get_size_helper(item.node_id) +
           get_size_helper(item.process_id) + get_size_helper(item.thread_id) +
           get_size_helper(item.extdata.c_str());
}

struct process_sample : public cacheable_t
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
serialize(uint8_t* buffer, const process_sample& item)
{
    size_t position = 0;
    store_value(item.guid.c_str(), buffer, position);
    store_value(item.node_id, buffer, position);
    store_value(item.parent_process_id, buffer, position);
    store_value(item.process_id, buffer, position);
    store_value(item.init, buffer, position);
    store_value(item.fini, buffer, position);
    store_value(item.start, buffer, position);
    store_value(item.end, buffer, position);
    store_value(item.command.c_str(), buffer, position);
    store_value(item.env.c_str(), buffer, position);
    store_value(item.extdata.c_str(), buffer, position);
}

template <>
process_sample
deserialize(uint8_t*& buffer)
{
    process_sample result;
    process_arg(buffer, result.guid);
    process_arg(buffer, result.node_id);
    process_arg(buffer, result.parent_process_id);
    process_arg(buffer, result.process_id);
    process_arg(buffer, result.init);
    process_arg(buffer, result.fini);
    process_arg(buffer, result.start);
    process_arg(buffer, result.end);
    process_arg(buffer, result.command);
    process_arg(buffer, result.env);
    process_arg(buffer, result.extdata);
    return result;
}

auto
get_size(const process_sample& item)
{
    return get_size_helper(item.guid.c_str()) + get_size_helper(item.node_id) +
           get_size_helper(item.parent_process_id) + get_size_helper(item.process_id) +
           get_size_helper(item.init) + get_size_helper(item.fini) +
           get_size_helper(item.start) + get_size_helper(item.end) +
           get_size_helper(item.command.c_str()) + get_size_helper(item.env.c_str()) +
           get_size_helper(item.extdata.c_str());
}

// ---------------- rocpd post processing ----------------

struct rocpd_post_processing
: public storage_post_processing<track_sample, process_sample>
{
    template <typename T>
    postprocessing_callback_list get_callbacks() const;
};

template <>
postprocessing_callback_list
rocpd_post_processing::get_callbacks<track_sample>() const
{
    return { [](const cacheable_t& t) {
        auto track = static_cast<const track_sample&>(t);
        std::cout << "track sample:" << track.track_name << std::endl;
    } };
}

template <>
postprocessing_callback_list
rocpd_post_processing::get_callbacks<process_sample>() const
{
    return { [](const cacheable_t& t) {
        auto process = static_cast<const process_sample&>(t);
        std::cout << "process sample:" << process.command << std::endl;
    } };
}

// ---------------- Example ----------------

void
run_multithread_example()
{
    ::buffered_storage<flush_worker, type_identifier_t> buffered_storage;
    const auto                                          number_of_iterations = 1000;

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
            process_sample ps;
            ps.command = std::to_string(count);
            buffered_storage.store(ps);
            count++;
        } while(count != number_of_iterations);
    }));

    for(auto& thread : threads)
    {
        thread.join();
    }

    buffered_storage.shutdown();

    storage_parser<type_identifier_t, rocpd_post_processing> parser(
        get_buffered_storage_filename(0, 0), {});

    parser.load();
}

int
main()
{
    run_multithread_example();
}
