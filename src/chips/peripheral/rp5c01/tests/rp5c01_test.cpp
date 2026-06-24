#include "rp5c01.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::peripheral::rp5c01;

    void select_register(rp5c01& rtc, std::uint8_t reg) { rtc.write_address(reg); }

    void write_register(rp5c01& rtc, std::uint8_t reg, std::uint8_t value) {
        select_register(rtc, reg);
        rtc.write_data(value);
    }

    [[nodiscard]] std::uint8_t read_register(rp5c01& rtc, std::uint8_t reg) {
        select_register(rtc, reg);
        return rtc.read_data();
    }

    void select_block(rp5c01& rtc, std::uint8_t block, bool timer_enabled = true) {
        write_register(rtc, 0x0DU,
                       static_cast<std::uint8_t>((timer_enabled ? 0x08U : 0x00U) | block));
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iperipheral, rp5c01>);

TEST_CASE("rp5c01 registers as a Ricoh peripheral", "[chips][peripheral][rp5c01]") {
    const rp5c01 rtc;
    const auto md = rtc.metadata();
    CHECK(md.manufacturer == "Ricoh");
    CHECK(md.part_number == "RP-5C01");
    CHECK(md.klass == mnemos::chips::chip_class::peripheral);

    const auto* descriptor = mnemos::chips::find_factory("ricoh.rp5c01");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::peripheral);

    auto chip = mnemos::chips::create_chip("ricoh.rp5c01");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::peripheral);
}

TEST_CASE("rp5c01 exposes 4-bit MSX register select and CMOS blocks",
          "[chips][peripheral][rp5c01]") {
    rp5c01 rtc;

    write_register(rtc, 0x0DU, 0x2AU); // high bits ignored, select block 2, timer on
    CHECK(rtc.mode() == 0x0AU);
    CHECK(rtc.selected_block() == 2U);
    CHECK(read_register(rtc, 0x00U) == 0x0AU); // MSX RTC-valid marker

    select_block(rtc, 3U);
    write_register(rtc, 0x05U, 0xBCU);
    CHECK(read_register(rtc, 0x05U) == 0x0CU);
    CHECK(rtc.block_register(3, 5) == 0x0CU);
}

TEST_CASE("rp5c01 advances deterministic BCD calendar time", "[chips][peripheral][rp5c01]") {
    rp5c01 rtc;

    write_register(rtc, 0x00U, 9U); // seconds 59
    write_register(rtc, 0x01U, 5U);
    write_register(rtc, 0x02U, 9U); // minutes 59
    write_register(rtc, 0x03U, 5U);
    write_register(rtc, 0x04U, 3U); // hours 23
    write_register(rtc, 0x05U, 2U);
    write_register(rtc, 0x06U, 4U); // weekday
    write_register(rtc, 0x07U, 1U); // day 31
    write_register(rtc, 0x08U, 3U);
    write_register(rtc, 0x09U, 2U); // month 12
    write_register(rtc, 0x0AU, 1U);
    write_register(rtc, 0x0BU, 9U); // year 99
    write_register(rtc, 0x0CU, 9U);

    rtc.tick(rp5c01::input_clock_hz);

    CHECK(read_register(rtc, 0x00U) == 0U);
    CHECK(read_register(rtc, 0x01U) == 0U);
    CHECK(read_register(rtc, 0x02U) == 0U);
    CHECK(read_register(rtc, 0x03U) == 0U);
    CHECK(read_register(rtc, 0x04U) == 0U);
    CHECK(read_register(rtc, 0x05U) == 0U);
    CHECK(read_register(rtc, 0x06U) == 5U);
    CHECK(read_register(rtc, 0x07U) == 1U);
    CHECK(read_register(rtc, 0x08U) == 0U);
    CHECK(read_register(rtc, 0x09U) == 1U);
    CHECK(read_register(rtc, 0x0AU) == 0U);
    CHECK(read_register(rtc, 0x0BU) == 0U);
    CHECK(read_register(rtc, 0x0CU) == 0U);
}

TEST_CASE("rp5c01 timer enable freezes and resumes seconds", "[chips][peripheral][rp5c01]") {
    rp5c01 rtc;

    select_block(rtc, 0U, false);
    rtc.tick(rp5c01::input_clock_hz);
    CHECK(read_register(rtc, 0x00U) == 0U);

    select_block(rtc, 0U, true);
    rtc.tick(rp5c01::input_clock_hz);
    CHECK(read_register(rtc, 0x00U) == 1U);
}

TEST_CASE("rp5c01 reset register clears sub-second state and alarm data",
          "[chips][peripheral][rp5c01]") {
    rp5c01 rtc;

    rtc.tick(rp5c01::input_clock_hz / 2U);
    write_register(rtc, 0x0FU, 0x02U); // clear fraction smaller than one second
    rtc.tick(rp5c01::input_clock_hz / 2U);
    CHECK(read_register(rtc, 0x00U) == 0U);

    select_block(rtc, 1U);
    write_register(rtc, 0x02U, 0x07U);
    write_register(rtc, 0x0FU, 0x01U);
    CHECK(read_register(rtc, 0x02U) == 0U);
}

TEST_CASE("rp5c01 save/load preserves CMOS and fractional time", "[chips][peripheral][rp5c01]") {
    rp5c01 a;
    select_block(a, 3U);
    write_register(a, 0x04U, 0x0DU);
    const std::uint32_t first_half = rp5c01::input_clock_hz / 2U;
    const std::uint32_t second_half = rp5c01::input_clock_hz - first_half;
    a.tick(first_half);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a.save_state(writer);

    rp5c01 b;
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    b.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(b.selected_block() == 3U);
    CHECK(read_register(b, 0x04U) == 0x0DU);

    a.tick(second_half);
    b.tick(second_half);
    select_block(a, 0U);
    select_block(b, 0U);
    CHECK(read_register(b, 0x00U) == read_register(a, 0x00U));
    CHECK(read_register(b, 0x00U) == 1U);
}
