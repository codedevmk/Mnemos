#include <mnemos/foundation/crc32.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

using mnemos::foundation::crc32;

TEST_CASE("crc32 matches the standard check vector") {
    // The canonical CRC-32 check value for "123456789" is 0xCBF43926.
    CHECK(crc32(std::string_view("123456789")) == 0xCBF43926U);
}

TEST_CASE("crc32 of empty input is zero") {
    CHECK(crc32(std::span<const std::uint8_t>{}) == 0x00000000U);
}

TEST_CASE("crc32 is incremental and order-sensitive") {
    const std::vector<std::uint8_t> whole = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const std::uint32_t one_shot = crc32(whole);

    // Splitting the input and chaining the running CRC reproduces the one-shot value.
    const std::uint32_t part1 = crc32(std::span<const std::uint8_t>(whole.data(), 4U));
    const std::uint32_t chained =
        crc32(std::span<const std::uint8_t>(whole.data() + 4U, whole.size() - 4U), part1);
    CHECK(chained == one_shot);

    // Different content -> different checksum.
    CHECK(crc32(std::string_view("123456789")) != crc32(std::string_view("12345678X")));
}
