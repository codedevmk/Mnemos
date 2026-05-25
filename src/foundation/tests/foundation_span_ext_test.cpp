#include "span_ext.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

TEST_CASE("span range check rejects overflow-free invalid ranges") {
    CHECK(mnemos::foundation::span_contains_range(8U, 0U, 8U));
    CHECK(mnemos::foundation::span_contains_range(8U, 8U, 0U));
    CHECK_FALSE(mnemos::foundation::span_contains_range(8U, 9U, 0U));
    CHECK_FALSE(mnemos::foundation::span_contains_range(8U, 4U, 5U));
}

TEST_CASE("try subspan returns requested span on valid ranges") {
    std::array<std::uint8_t, 5U> bytes{1U, 2U, 3U, 4U, 5U};
    auto result = mnemos::foundation::try_subspan(std::span{bytes}, 1U, 3U);
    auto bad_offset = mnemos::foundation::try_subspan(std::span{bytes}, 6U, 0U);
    auto bad_count = mnemos::foundation::try_subspan(std::span{bytes}, 3U, 3U);

    REQUIRE(result.has_value());
    CHECK(result->size() == 3U);
    CHECK((*result)[0] == 2U);
    CHECK((*result)[2] == 4U);

    (*result)[1] = 9U;
    CHECK(bytes[2] == 9U);

    REQUIRE_FALSE(bad_offset.has_value());
    CHECK(bad_offset.error() == mnemos::foundation::span_error::offset_out_of_range);
    REQUIRE_FALSE(bad_count.has_value());
    CHECK(bad_count.error() == mnemos::foundation::span_error::count_out_of_range);
}

TEST_CASE("try subspan reports offset and count failures separately") {
    std::array<std::uint8_t, 4U> bytes{1U, 2U, 3U, 4U};

    auto bad_offset = mnemos::foundation::try_subspan(std::span{bytes}, 5U, 0U);
    REQUIRE_FALSE(bad_offset.has_value());
    CHECK(bad_offset.error() == mnemos::foundation::span_error::offset_out_of_range);

    auto bad_count = mnemos::foundation::try_subspan(std::span{bytes}, 2U, 3U);
    REQUIRE_FALSE(bad_count.has_value());
    CHECK(bad_count.error() == mnemos::foundation::span_error::count_out_of_range);
}

TEST_CASE("try first and last preserve const spans") {
    constexpr std::array<std::uint16_t, 4U> words{10U, 20U, 30U, 40U};
    const std::span<const std::uint16_t> view{words};

    auto first = mnemos::foundation::try_first(view, 2U);
    auto last = mnemos::foundation::try_last(view, 2U);
    auto too_many = mnemos::foundation::try_last(view, 5U);

    REQUIRE(first.has_value());
    CHECK((*first)[0] == 10U);
    CHECK((*first)[1] == 20U);

    REQUIRE(last.has_value());
    CHECK((*last)[0] == 30U);
    CHECK((*last)[1] == 40U);

    REQUIRE_FALSE(too_many.has_value());
    CHECK(too_many.error() == mnemos::foundation::span_error::count_out_of_range);
}
