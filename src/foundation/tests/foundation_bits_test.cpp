#include "bits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

static_assert(!mnemos::foundation::unsigned_integer<bool>);
static_assert(!mnemos::foundation::unsigned_integer<std::int32_t>);
static_assert(mnemos::foundation::unsigned_integer<std::uint32_t>);
static_assert(mnemos::foundation::bit_count_v<std::uint8_t> == 8U);
static_assert(mnemos::foundation::popcount(std::uint8_t{0b1011'0001U}) == 4);
static_assert(mnemos::foundation::countl_zero(std::uint8_t{0b0001'0000U}) == 3);
static_assert(mnemos::foundation::byte_swap(std::uint16_t{0x1234U}) == 0x3412U);
static_assert(mnemos::foundation::low_bit_mask<std::uint8_t, 0U>() == 0x00U);
static_assert(mnemos::foundation::low_bit_mask<std::uint8_t, 4U>() == 0x0FU);
static_assert(mnemos::foundation::low_bit_mask<std::uint8_t, 8U>() == 0xFFU);
static_assert(mnemos::foundation::bit_mask<std::uint8_t, 5U>() == 0x20U);
static_assert(mnemos::foundation::bit_field_mask<std::uint8_t, 2U, 3U>() == 0x1CU);
static_assert(mnemos::foundation::extract_bits<std::uint8_t, 2U, 3U>(0b1011'1100U) == 0b111U);
// Boundary fields: top bit and full width must extract correctly.
static_assert(mnemos::foundation::extract_bits<std::uint8_t, 7U, 1U>(0x80U) == 0x01U);
static_assert(mnemos::foundation::extract_bits<std::uint8_t, 0U, 8U>(0xABU) == 0xABU);

TEST_CASE("bit wrappers expose standard unsigned bit operations") {
    CHECK(mnemos::foundation::popcount(std::uint32_t{0xF0F0'0001U}) == 9);
    CHECK(mnemos::foundation::countl_zero(std::uint32_t{0x0000'8000U}) == 16);
    CHECK(mnemos::foundation::byte_swap(std::uint32_t{0x1234'ABCDU}) == 0xCDAB'3412U);
}

TEST_CASE("byte order helpers convert between native and fixed orders") {
    constexpr auto native = mnemos::foundation::native_byte_order();
    CHECK((native == mnemos::foundation::byte_order::little ||
           native == mnemos::foundation::byte_order::big ||
           native == mnemos::foundation::byte_order::mixed));

    if constexpr (native == mnemos::foundation::byte_order::little) {
        CHECK(mnemos::foundation::native_to_little(std::uint16_t{0x1234U}) == 0x1234U);
        CHECK(mnemos::foundation::native_to_big(std::uint16_t{0x1234U}) == 0x3412U);
    } else if constexpr (native == mnemos::foundation::byte_order::big) {
        CHECK(mnemos::foundation::native_to_little(std::uint16_t{0x1234U}) == 0x3412U);
        CHECK(mnemos::foundation::native_to_big(std::uint16_t{0x1234U}) == 0x1234U);
    } else {
        CHECK(native == mnemos::foundation::byte_order::mixed);
    }

    CHECK(mnemos::foundation::little_to_native(mnemos::foundation::native_to_little(
              std::uint32_t{0x89AB'CDEFU})) == std::uint32_t{0x89AB'CDEFU});
    CHECK(mnemos::foundation::big_to_native(mnemos::foundation::native_to_big(
              std::uint32_t{0x89AB'CDEFU})) == std::uint32_t{0x89AB'CDEFU});
}

TEST_CASE("single bit helpers read and modify stable masks") {
    CHECK(mnemos::foundation::test_bit<std::uint8_t, 3U>(0b0000'1000U));
    CHECK_FALSE(mnemos::foundation::test_bit<std::uint8_t, 3U>(0b0000'0100U));
    CHECK(mnemos::foundation::set_bit<std::uint8_t, 7U>(0x01U) == 0x81U);
    CHECK(mnemos::foundation::clear_bit<std::uint8_t, 4U>(0xFFU) == 0xEFU);
    CHECK(mnemos::foundation::write_bit<std::uint8_t, 1U>(0x00U, true) == 0x02U);
    CHECK(mnemos::foundation::write_bit<std::uint8_t, 1U>(0xFFU, false) == 0xFDU);
}

TEST_CASE("bitfield helpers extract and replace bounded fields") {
    CHECK(mnemos::foundation::extract_bits<std::uint16_t, 4U, 4U>(0x12F0U) == 0x0FU);
    CHECK(mnemos::foundation::extract_bits<std::uint16_t, 12U, 4U>(0xF000U) == 0x0FU);
    CHECK(mnemos::foundation::replace_bits<std::uint16_t, 4U, 4U>(0x1200U, 0x000BU) == 0x12B0U);
    CHECK(mnemos::foundation::replace_bits<std::uint8_t, 1U, 3U>(0xFFU, 0x00U) == 0xF1U);
    CHECK(mnemos::foundation::replace_bits<std::uint8_t, 1U, 3U>(0x00U, 0x0FU) == 0x0EU);
}
