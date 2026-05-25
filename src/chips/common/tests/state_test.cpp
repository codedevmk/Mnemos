#include <mnemos/chips/common/state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using mnemos::chips::state_reader;
using mnemos::chips::state_writer;

TEST_CASE("state primitives round-trip little-endian") {
    std::vector<std::uint8_t> buffer;
    state_writer w(buffer);
    w.u8(0x12U);
    w.u16(0x3456U);
    w.u32(0x789ABCDEU);
    w.u64(0x0123456789ABCDEFULL);
    w.boolean(true);
    w.boolean(false);

    // u16 0x3456 is stored low byte first.
    REQUIRE(buffer.size() == 1U + 2U + 4U + 8U + 2U);
    CHECK(buffer[1] == 0x56U);
    CHECK(buffer[2] == 0x34U);

    state_reader r(buffer);
    CHECK(r.u8() == 0x12U);
    CHECK(r.u16() == 0x3456U);
    CHECK(r.u32() == 0x789ABCDEU);
    CHECK(r.u64() == 0x0123456789ABCDEFULL);
    CHECK(r.boolean() == true);
    CHECK(r.boolean() == false);
    CHECK(r.ok());
    CHECK(r.remaining() == 0U);
}

TEST_CASE("state blobs are length-prefixed and raw bytes are positional") {
    std::vector<std::uint8_t> buffer;
    state_writer w(buffer);
    const std::vector<std::uint8_t> payload = {0xDEU, 0xADU, 0xBEU, 0xEFU};
    w.blob(payload);
    const std::array<std::uint8_t, 3> raw = {1U, 2U, 3U};
    w.bytes(raw);

    state_reader r(buffer);
    CHECK(r.blob() == payload);
    std::array<std::uint8_t, 3> out{};
    r.bytes(out);
    CHECK(out[0] == 1U);
    CHECK(out[1] == 2U);
    CHECK(out[2] == 3U);
    CHECK(r.ok());
}

TEST_CASE("state_reader flags underrun instead of reading out of bounds") {
    std::vector<std::uint8_t> buffer = {0x01U, 0x02U}; // only two bytes
    state_reader r(buffer);
    CHECK(r.u8() == 0x01U);
    CHECK(r.u32() == 0U); // not enough bytes -> zero
    CHECK_FALSE(r.ok());  // and the reader is marked bad
}
