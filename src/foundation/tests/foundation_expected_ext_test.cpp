#include "expected_ext.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

    enum class test_error : std::uint8_t {
        none,
        invalid,
        missing,
    };

} // namespace

TEST_CASE("expected aliases preserve std expected values") {
    mnemos::foundation::expected<int, test_error> result{42};

    CHECK(result.has_value());
    CHECK_FALSE(mnemos::foundation::has_error(result));
    REQUIRE(mnemos::foundation::value_if(result) != nullptr);
    CHECK(*mnemos::foundation::value_if(result) == 42);
    CHECK(mnemos::foundation::error_if(result) == nullptr);
}

TEST_CASE("expected helpers expose errors without throwing") {
    mnemos::foundation::expected<int, test_error> result{
        mnemos::foundation::unexpected(test_error::invalid)};

    CHECK_FALSE(result.has_value());
    CHECK(mnemos::foundation::has_error(result));
    CHECK(mnemos::foundation::value_if(result) == nullptr);
    REQUIRE(mnemos::foundation::error_if(result) != nullptr);
    CHECK(*mnemos::foundation::error_if(result) == test_error::invalid);
}

TEST_CASE("const expected helpers preserve pointer constness") {
    const mnemos::foundation::expected<int, test_error> result{7};
    const mnemos::foundation::expected<int, test_error> failure{
        mnemos::foundation::unexpected(test_error::missing)};

    const int* value = mnemos::foundation::value_if(result);
    REQUIRE(value != nullptr);
    CHECK(*value == 7);
    CHECK(mnemos::foundation::error_if(result) == nullptr);
    CHECK(mnemos::foundation::value_if(failure) == nullptr);
    REQUIRE(mnemos::foundation::error_if(failure) != nullptr);
    CHECK(*mnemos::foundation::error_if(failure) == test_error::missing);
}

TEST_CASE("status alias supports void success and failure") {
    mnemos::foundation::status<test_error> success{};
    mnemos::foundation::status<test_error> failure{
        mnemos::foundation::unexpected(test_error::missing)};

    CHECK(success.has_value());
    CHECK_FALSE(mnemos::foundation::has_error(success));
    CHECK(mnemos::foundation::error_if(success) == nullptr);

    CHECK_FALSE(failure.has_value());
    REQUIRE(mnemos::foundation::error_if(failure) != nullptr);
    CHECK(*mnemos::foundation::error_if(failure) == test_error::missing);
}

TEST_CASE("const status helper exposes failure without mutation") {
    const mnemos::foundation::status<test_error> success{};
    const mnemos::foundation::status<test_error> failure{
        mnemos::foundation::unexpected(test_error::invalid)};

    CHECK(mnemos::foundation::error_if(success) == nullptr);
    REQUIRE(mnemos::foundation::error_if(failure) != nullptr);
    CHECK(*mnemos::foundation::error_if(failure) == test_error::invalid);
}
