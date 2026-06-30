#include "rp5c01.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::chip_class;
    using mnemos::chips::create_chip;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::peripheral::rp5c01;

    void select_block(rp5c01& rtc, std::uint8_t block) {
        rtc.select(0x0DU);
        rtc.data_write(static_cast<std::uint8_t>(0x08U | (block & 0x03U)));
    }
} // namespace

TEST_CASE("rp5c01 registers in the chip registry as a peripheral", "[rp5c01][rtc]") {
    auto chip = create_chip("ricoh.rp5c01");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == chip_class::peripheral);
}

TEST_CASE("rp5c01 maps index and 4-bit data through block-select mode", "[rp5c01][rtc]") {
    rp5c01 rtc;
    rtc.select(0x8DU);
    CHECK(rtc.selected() == 0x0DU);
    CHECK(rtc.data_read() == 0x08U);

    select_block(rtc, 3U);
    rtc.select(1U);
    rtc.data_write(0x3CU);
    CHECK(rtc.data_read() == 0x0CU);
    CHECK(rtc.raw_block_register(3, 1) == 0x0CU);

    select_block(rtc, 2U);
    rtc.select(0U);
    CHECK(rtc.data_read() == 0x0AU);
}

TEST_CASE("rp5c01 advances BCD time from emulated cycles", "[rp5c01][rtc]") {
    rp5c01 rtc;
    rtc.set_cycles_per_second(4U);
    rtc.set_time_24h(24U, 12U, 31U, 2U, 23U, 59U, 58U, 1U);

    rtc.tick(4U);
    CHECK(rtc.raw_block_register(0, 0) == 9U);
    CHECK(rtc.raw_block_register(0, 1) == 5U);

    rtc.tick(4U);
    CHECK(rtc.raw_block_register(0, 0) == 0U);
    CHECK(rtc.raw_block_register(0, 1) == 0U);
    CHECK(rtc.raw_block_register(0, 2) == 0U);
    CHECK(rtc.raw_block_register(0, 3) == 0U);
    CHECK(rtc.raw_block_register(0, 4) == 0U);
    CHECK(rtc.raw_block_register(0, 5) == 0U);
    CHECK(rtc.raw_block_register(0, 7) == 1U);
    CHECK(rtc.raw_block_register(0, 8) == 0U);
    CHECK(rtc.raw_block_register(0, 9) == 1U);
    CHECK(rtc.raw_block_register(0, 10) == 0U);
    CHECK(rtc.raw_block_register(0, 11) == 5U);
    CHECK(rtc.raw_block_register(0, 12) == 2U);
}

TEST_CASE("rp5c01 timer enable gates time progression", "[rp5c01][rtc]") {
    rp5c01 rtc;
    rtc.set_cycles_per_second(2U);
    rtc.set_time_24h(0U, 1U, 1U, 0U, 0U, 0U, 0U);
    rtc.select(0x0DU);
    rtc.data_write(0x00U);
    rtc.tick(10U);
    CHECK(rtc.raw_block_register(0, 0) == 0U);

    rtc.data_write(0x08U);
    rtc.tick(2U);
    CHECK(rtc.raw_block_register(0, 0) == 1U);
}

TEST_CASE("rp5c01 save_state/load_state round-trips CMOS and subsecond phase", "[rp5c01][rtc]") {
    rp5c01 a;
    a.set_cycles_per_second(8U);
    a.set_time_24h(26U, 2U, 28U, 5U, 1U, 2U, 3U);
    select_block(a, 3U);
    a.select(5U);
    a.data_write(0x07U);
    a.tick(3U);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    rp5c01 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    a.tick(5U);
    b.tick(5U);
    CHECK(b.raw_block_register(3, 5) == 0x07U);
    CHECK(a.raw_block_register(0, 0) == b.raw_block_register(0, 0));
}
