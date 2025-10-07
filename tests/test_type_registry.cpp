#include "mocked_types.hpp"
#include "type_registry.hpp"

#include <gtest/gtest.h>

class TypeRegistryTest : public ::testing::Test
{
protected:
    trace_cache::type_registry<test_type_identifier_t, test_sample_1, test_sample_2>
        type_registry;
};

TEST_F(TypeRegistryTest, test_get_type_sample_1)
{
    test_sample_1        test_value{ 42, "hello" };
    size_t               buffer_size = trace_cache::get_size(test_value);
    std::vector<uint8_t> buffer(buffer_size);
    trace_cache::serialize(buffer.data(), test_value);

    auto buffer_data = buffer.data();
    auto result =
        type_registry.get_type(test_type_identifier_t::sample_type_1, buffer_data);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<test_sample_1>(result.value()));

    auto sample_1 = std::get<test_sample_1>(result.value());
    EXPECT_EQ(sample_1.value, 42);
    EXPECT_EQ(sample_1.text, "hello");
}

TEST_F(TypeRegistryTest, test_get_type_sample_2)
{
    test_sample_2        test_value{ 3.14, 123 };
    size_t               buffer_size = trace_cache::get_size(test_value);
    std::vector<uint8_t> buffer(buffer_size);
    trace_cache::serialize(buffer.data(), test_value);

    auto buffer_data = buffer.data();
    auto result =
        type_registry.get_type(test_type_identifier_t::sample_type_2, buffer_data);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<test_sample_2>(result.value()));

    auto sample_2 = std::get<test_sample_2>(result.value());
    EXPECT_DOUBLE_EQ(sample_2.data, 3.14);
    EXPECT_EQ(sample_2.sample_id, 123);
}

TEST_F(TypeRegistryTest, test_get_type_unknown_id)
{
    uint8_t  dummy_data = 0;
    uint8_t* data       = &dummy_data;

    auto result = type_registry.get_type(test_type_identifier_t::fragmented_space, data);

    EXPECT_FALSE(result.has_value());
}

TEST_F(TypeRegistryTest, test_variant_type_definition)
{
    using expected_variant = std::variant<test_sample_1, test_sample_2>;
    using actual_variant =
        trace_cache::type_registry<test_type_identifier_t, test_sample_1,
                                   test_sample_2>::variant_t;

    EXPECT_TRUE((std::is_same_v<expected_variant, actual_variant>) );
}

TEST_F(TypeRegistryTest, test_multiple_calls_same_type)
{
    test_sample_1 test_value1{ 100, "first" };
    test_sample_1 test_value2{ 200, "second" };

    size_t buffer_size1 = trace_cache::get_size(test_value1);
    size_t buffer_size2 = trace_cache::get_size(test_value2);

    std::vector<uint8_t> buffer1(buffer_size1);
    std::vector<uint8_t> buffer2(buffer_size2);

    trace_cache::serialize(buffer1.data(), test_value1);
    trace_cache::serialize(buffer2.data(), test_value2);

    auto buffer1_data = buffer1.data();
    auto buffer2_data = buffer2.data();

    auto result1 =
        type_registry.get_type(test_type_identifier_t::sample_type_1, buffer1_data);
    auto result2 =
        type_registry.get_type(test_type_identifier_t::sample_type_1, buffer2_data);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());

    auto sample_1_1 = std::get<test_sample_1>(result1.value());
    auto sample_1_2 = std::get<test_sample_1>(result2.value());

    EXPECT_EQ(sample_1_1.value, 100);
    EXPECT_EQ(sample_1_1.text, "first");
    EXPECT_EQ(sample_1_2.value, 200);
    EXPECT_EQ(sample_1_2.text, "second");
}