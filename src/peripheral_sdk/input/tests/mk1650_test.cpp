#include "mk1650.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {
    using mnemos::peripheral::input::mk1650;
}

TEST_CASE("mk1650 reports the documented Sega manifest") {
    const mk1650 pad;
    const auto i = pad.describe();
    CHECK(std::string{i.manufacturer} == "Sega");
    CHECK(std::string{i.part_number} == "MK-1650");
    CHECK(i.category == mnemos::peripheral::kind::input_pad);
    CHECK((i.compatible & mnemos::peripheral::host::genesis) != 0U);
    CHECK((i.compatible & mnemos::peripheral::host::sms) != 0U);
}

TEST_CASE("mk1650 reads idle as 0x7F with TH=1, 0x33 with TH=0") {
    mk1650 pad;
    pad.write_data(0x40U); // TH=1
    CHECK(pad.read_data() == 0x7FU);
    pad.write_data(0x00U); // TH=0
    CHECK(pad.read_data() == 0x33U);
}

TEST_CASE("mk1650 wires Start + A to the TH=0 bank, B + C to the TH=1 bank") {
    mk1650 pad;
    mnemos::peripheral::controller_state state{};

    // Start + A: only visible with TH=0.
    state.start = true;
    state.a = true;
    pad.apply_state(state);
    pad.write_data(0x00U);
    // TH=0 byte: Start clears bit 5, A clears bit 4; U/D idle = 1; LR locked 0.
    // Result = 0b00000011 = 0x03.
    CHECK(pad.read_data() == 0x03U);

    // B + C: only visible with TH=1.
    state = {};
    state.b = true;
    state.c = true;
    pad.apply_state(state);
    pad.write_data(0x40U);
    // TH=1 byte: bit 6 = TH=1, C clears bit 5, B clears bit 4, dpad idle =
    // bits 3..0 all 1. Result = 0b01001111 = 0x4F.
    CHECK(pad.read_data() == 0x4FU);
}

TEST_CASE("mk1650 LR-locked-low signature differs from the 6-button id") {
    mk1650 pad;
    mnemos::peripheral::controller_state state{};
    state.up = true;
    state.down = true;
    state.left = true;
    state.right = true;
    pad.apply_state(state);
    pad.write_data(0x00U); // TH=0
    // With dpad pressed, bits 0/1 = 0 (U/D), bits 2/3 still 0 (locked).
    // Bits 5/4 = S/A idle = 1. So byte = 0b00110000 = 0x30.
    CHECK(pad.read_data() == 0x30U);
}
