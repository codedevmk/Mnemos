#include "string.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using mnemos::common::ends_with_ci;
using mnemos::common::to_lower;

TEST_CASE("to_lower folds ASCII upper-case and leaves other bytes intact") {
    CHECK(to_lower(std::string("AbCdEf 123_.")) == "abcdef 123_.");
    CHECK(to_lower(std::string("")).empty());
}

TEST_CASE("ends_with_ci matches suffixes case-insensitively") {
    CHECK(ends_with_ci("game.SMS", "sms"));
    CHECK(ends_with_ci("game.sms", "SMS"));
    CHECK(ends_with_ci("anything", ""));

    CHECK_FALSE(ends_with_ci("game.bin", "sms"));
    CHECK_FALSE(ends_with_ci("ms", "sms")); // suffix longer than text
}
