#ifndef METADATA
#define METADATA

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <optional>
#include <stdint.h>
#include <string.h>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <algorithm>
#include <functional>

template<typename T>
class synced_set
{
public:
    void emplace(const T& value)
    {
       std::lock_guard guard{m_mutex};
       m_set.emplace(value);
    }

    std::optional<T> find(std::function<bool(const T&)> predicate)
    {
       std::lock_guard guard{m_mutex};
        auto it = std::find_if(
        m_set.begin(), m_set.end(), predicate);
        if(it == m_set.end())
        {
            return std::nullopt;
        }
        return *it;
    }

private:
    std::mutex m_mutex;
    std::set<T> m_set;
};

namespace metadata {

struct node_info
{
    uint32_t nid;
    std::string_view hostname;
    uint32_t hash;
};

struct process_info
{
  size_t node_id;
  uint32_t process_id;
};

struct agent
{
    uint32_t nid;
    uint32_t pid;
    uint32_t absolute_index;
    std::string_view type;
};

struct pmc_info
{
    uint32_t nid;
    uint32_t pid;
    uint32_t agent_index;
    std::string unique_name;
    std::string_view unit;

    friend bool operator< (const pmc_info& lhs, const pmc_info& rhs)
    {
        return lhs.unique_name.compare(rhs.unique_name);
    }
};

// struct thread_info
// {
//     uint32_t nid;
//     uint32_t pid;
//     uint32_t tid;
//     std::string_view name;
// };

struct storage {

    void set_current_node(const node_info &node)
    {
        m_current_node = node;
    }

    void set_current_process(const process_info& process_info)
    {
        m_current_process = process_info;
    }

    void add_agent(const agent& agent)
    {
        m_agents.push_back(agent);
    }

    void add_pmc_info(const pmc_info& pmc_info)
    {
        m_pmc_infos.emplace(pmc_info);
    }

    node_info get_current_node()
    {
        return m_current_node;
    }

    process_info get_current_process()
    {
        return m_current_process;
    }

    std::optional<agent> get_agent_for_index(uint32_t abs_index)
    {
        auto it = std::find_if(
            m_agents.begin(), m_agents.end(),
            [&](const metadata::agent &value) {
              return value.absolute_index == abs_index;
            });
        if(it == m_agents.end())
        {
          return std::nullopt;
        }
        return *it;
    }

    std::optional<pmc_info> get_pmc_info(const std::string_view& unique_name)
    {
        return m_pmc_infos.find([&](const metadata::pmc_info &value) {
              return value.unique_name.compare(unique_name) == 0;
            });
    }
private:
    node_info m_current_node;
    process_info m_current_process;
    std::vector<agent> m_agents;
    synced_set<pmc_info> m_pmc_infos;
};

} // namespace metadata


#endif