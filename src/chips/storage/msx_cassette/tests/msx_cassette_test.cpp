#include "msx_cassette.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {
    using mnemos::chips::storage::msx_cassette;

    std::vector<std::uint8_t> make_cas(std::initializer_list<std::uint8_t> bytes) {
        std::vector<std::uint8_t> cas(msx_cassette::cas_header_magic.begin(),
                                      msx_cassette::cas_header_magic.end());
        cas.insert(cas.end(), bytes.begin(), bytes.end());
        return cas;
    }
} // namespace

TEST_CASE("msx_cassette registers and detects CAS headers") {
    REQUIRE(mnemos::chips::find_factory("msx.cassette") != nullptr);
    REQUIRE(mnemos::chips::create_chip("msx.cassette") != nullptr);

    CHECK_FALSE(msx_cassette::has_cas_header(std::vector<std::uint8_t>{1U, 2U, 3U}));
    CHECK(msx_cassette::has_cas_header(make_cas({0x42U})));

    msx_cassette tape;
    CHECK_FALSE(tape.load_cas({}));
    CHECK_FALSE(tape.loaded());
}

TEST_CASE("msx_cassette advances input transitions only with PLAY and motor") {
    msx_cassette tape;
    const std::array<std::uint32_t, 2> pulses{3U, 2U};
    tape.load_half_cycles(pulses);
    tape.set_play(true);

    CHECK(tape.input_high());
    tape.tick(100U);
    CHECK(tape.input_high());
    CHECK(tape.position_half_cycle() == 0U);

    tape.set_motor_on(true);
    tape.tick(2U);
    CHECK(tape.input_high());
    tape.tick(1U);
    CHECK_FALSE(tape.input_high());
    CHECK(tape.position_half_cycle() == 1U);

    tape.tick(2U);
    CHECK(tape.input_high());
    CHECK(tape.at_end());
}

TEST_CASE("msx_cassette converts CAS bytes into BIOS FSK half-cycles") {
    msx_cassette tape;
    tape.set_cycles_per_second(msx_cassette::default_cycles_per_second);
    REQUIRE(tape.load_cas(make_cas({0x43U})));

    // The CAS header marker is a file-format sentinel. Playback replaces it with
    // the long MSX leader tone, then emits the data byte with start/data/stop bits.
    CHECK(tape.half_cycle_count() > (15'360U * 2U));
}

TEST_CASE("msx_cassette save/load preserves playback position") {
    const std::array<std::uint32_t, 3> pulses{3U, 4U, 5U};
    msx_cassette a;
    a.load_half_cycles(pulses);
    a.set_play(true);
    a.set_motor_on(true);
    a.tick(4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a.save_state(writer);

    msx_cassette b;
    b.load_half_cycles(pulses);
    mnemos::chips::state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(b.position_half_cycle() == a.position_half_cycle());
    CHECK(b.countdown() == a.countdown());
    CHECK(b.input_high() == a.input_high());
    CHECK(b.playing());
    CHECK(b.motor_on());
}
