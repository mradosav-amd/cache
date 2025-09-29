#pragma once
#include "cacheable.hpp"
#include <functional>
#include <optional>
#include <unordered_map>
#include <variant>

template <typename TupleOfSupportedTypes>
class type_registry
{
    // helper to translate tuple to variant
    template <typename T>
    struct tuple_to_variant;
    template <typename... Types>
    struct tuple_to_variant<std::tuple<Types...>>
    {
        using type = std::variant<Types...>;
    };

public:
    using variant_t = typename tuple_to_variant<TupleOfSupportedTypes>::type;

    type_registry() { register_all_types(); }

    std::optional<variant_t> get_type(type_identifier_t id, uint8_t*& data)
    {
        auto it = deserializers.find(id);
        if(it != deserializers.end())
        {
            return it->second(data);
        }
        return std::nullopt;
    }

private:
    std::unordered_map<type_identifier_t, std::function<variant_t(uint8_t*&)>>
        deserializers;

    template <typename T>
    void register_type()
    {
        static_assert(has_type_identifier<T>::value, "Type must have type_identifier");
        deserializers[T::type_identifier] = [](uint8_t*& data) -> variant_t {
            return deserialize<T>(data);
        };
    }

    template <std::size_t I = 0>
    void register_all_types()
    {
        if constexpr(I < std::tuple_size_v<TupleOfSupportedTypes>)
        {
            using Type = std::tuple_element_t<I, TupleOfSupportedTypes>;
            register_type<Type>();
            register_all_types<I + 1>();
        }
    }
};