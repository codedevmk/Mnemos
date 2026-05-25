#include "datasette.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::storage::datasette;

    // A v1 .tap with the given pulse bytes (each byte = length / 8 cycles).
    std::vector<std::uint8_t> make_tap(const std::vector<std::uint8_t>& pulses) {
        std::vector<std::uint8_t> tap;
        const std::string_view magic = "C64-TAPE-RAW";
        tap.insert(tap.end(), magic.begin(), magic.end());
        tap.push_back(1U);             // version
        tap.insert(tap.end(), 3U, 0U); // reserved
        const auto size = static_cast<std::uint32_t>(pulses.size());
        tap.push_back(static_cast<std::uint8_t>(size & 0xFFU));
        tap.push_back(static_cast<std::uint8_t>((size >> 8U) & 0xFFU));
        tap.push_back(static_cast<std::uint8_t>((size >> 16U) & 0xFFU));
        tap.push_back(static_cast<std::uint8_t>((size >> 24U) & 0xFFU));
        tap.insert(tap.end(), pulses.begin(), pulses.end());
        return tap;
    }

} // namespace

TEST_CASE("datasette registers and rejects junk") {
    REQUIRE(mnemos::chips::find_factory("commodore.1530") != nullptr);
    REQUIRE(mnemos::chips::create_chip("commodore.1530") != nullptr);
    datasette tape;
    CHECK_FALSE(tape.load_tap(std::vector<std::uint8_t>(8U, 0U)));
    CHECK_FALSE(tape.loaded());
}

TEST_CASE("datasette pulses /FLAG at the encoded pulse lengths") {
    int pulses = 0;
    bool sense_held = false;
    bool motor = true;
    datasette tape;
    datasette::config cfg;
    cfg.motor_on = [&]() { return motor; };
    cfg.flag_pulse = [&]() { ++pulses; };
    cfg.set_sense = [&](bool held) { sense_held = held; };
    tape.configure(cfg);

    REQUIRE(tape.load_tap(make_tap({0x10U, 0x20U}))); // 128 then 256 cycles
    tape.set_play(true);
    CHECK(sense_held); // PLAY held -> sense low

    tape.tick(127U);
    CHECK(pulses == 0);
    tape.tick(1U);
    CHECK(pulses == 1); // 128 cycles -> first /FLAG

    tape.tick(255U);
    CHECK(pulses == 1);
    tape.tick(1U);
    CHECK(pulses == 2); // +256 cycles -> second /FLAG
}

TEST_CASE("datasette is paused without the motor or PLAY") {
    int pulses = 0;
    bool motor = false;
    datasette tape;
    datasette::config cfg;
    cfg.motor_on = [&]() { return motor; };
    cfg.flag_pulse = [&]() { ++pulses; };
    tape.configure(cfg);
    REQUIRE(tape.load_tap(make_tap({0x10U})));

    tape.set_play(true);
    tape.tick(1000U);
    CHECK(pulses == 0); // motor off -> no advance

    motor = true;
    tape.set_play(false);
    tape.tick(1000U);
    CHECK(pulses == 0); // PLAY released -> no advance
}

TEST_CASE("datasette save/load round-trips") {
    datasette a;
    REQUIRE(a.load_tap(make_tap({0x10U, 0x20U, 0x30U})));
    a.set_play(true);
    a.tick(200U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    datasette b;
    REQUIRE(b.load_tap(make_tap({0x10U, 0x20U, 0x30U})));
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
