#pragma once
#include "cacheable.hpp"
#include <functional>
#include <map>
#include <optional>

template <typename TupleOfSupportedTypes, typename TypeIdentifierEnum>
class type_registry
{
    static_assert(is_enum_class_v<TypeIdentifierEnum>,
                  "TypeIdentifierEnum must be an enum class");

public:
    using variant_t = typename tuple_to_variant<TupleOfSupportedTypes>::type;

    type_registry() { register_all_types(); }

    std::optional<variant_t> get_type(TypeIdentifierEnum id, uint8_t*& data)
    {
        auto it = deserializers.find(id);
        if(it != deserializers.end())
        {
            return it->second(data);
        }
        return std::nullopt;
    }

private:
    std::map<TypeIdentifierEnum, std::function<variant_t(uint8_t*&)>> deserializers;

    template <typename T>
    inline void register_type()
    {
        static_assert(has_type_identifier<T, TypeIdentifierEnum>::value,
                      "Type must have type_identifier");
        static_assert(has_deserialize<T>::value, "Type must have deserialize function");
        deserializers[T::type_identifier] = [](uint8_t*& data) -> variant_t {
            return deserialize<T>(data);
        };
    }

    template <std::size_t Index = 0>
    void register_all_types()
    {
        if constexpr(Index < std::tuple_size_v<TupleOfSupportedTypes>)
        {
            using Type = std::tuple_element_t<Index, TupleOfSupportedTypes>;
            register_type<Type>();
            register_all_types<Index + 1>();
        }
    }
};