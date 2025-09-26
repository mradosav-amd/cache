#pragma once
#include "cacheable.hpp"
#include <functional>
#include <optional>
#include <unordered_map>
#include <variant>

template <typename... Types>
class type_registry
{
public:
    using variant_t = std::variant<Types...>;

public:
    type_registry() { (register_type<Types>(), ...); }

    std::optional<variant_t> get_type(type_identifier_t id, uint8_t*& data)
    {
        auto it = deserializers.find(id);
        if(it != deserializers.end())
        {
            return it->second(data);
        }
        return std::nullopt;
    }

    bool has_type(type_identifier_t id) const
    {
        return deserializers.find(id) != deserializers.end();
    }

private:
    std::unordered_map<type_identifier_t, std::function<variant_t(uint8_t*&)>>
        deserializers;

    template <typename T>
    void register_type()
    {
        static_assert(has_type_id<T>::value, "Type must have type_identifier");
        deserializers[T::type_identifier] = [](uint8_t*& data) -> variant_t {
            return deserialize<T>(data);
        };
    }
};