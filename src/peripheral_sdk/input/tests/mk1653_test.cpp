#include "mk1653.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace {
    using mnemos::peripheral::input::mk1653;
}

TEST_CASE("mk1653 reports the documented Sega manifest") {
    const mk1653 pad;
    const auto i = pad.describe();
    CHECK(std::string{i.manufacturer} == "Sega");
    CHECK(std::string{i.part_number} == "MK-1653");
    CHECK(i.category == mnemos::peripheral::kind::input_pad);
    CHECK((i.compatible & mnemos::peripheral::host::genesis) != 0U);
}

TEST_CASE("mk1653 falls back to 3-button-compat when TH never pulses") {
    mk1653 pad;
    pad.write_data(0x40U); // TH=1 once -> phase 1
    CHECK(pad.read_data() == 0x7FU);
    // Re-read at phase 1 keeps returning the standard CBRLDU bank.
    CHECK(pad.read_data() == 0x7FU);
}

TEST_CASE("mk1653 exposes the 6-button id at phase 6 and Z/Y/X/Mode at phase 7") {
    mk1653 pad;
    mnemos::peripheral::controller_state state{};
    state.x = true;
    state.y = true;
    state.z = true;
    state.mode = true;
    pad.apply_state(state);

    // Default state: TH=0, phase=0. The standard 6-button polling sequence
    // starts with TH=1 (advancing to phase 1) then alternates; phase 6 lands
    // at TH=0 and phase 7 at TH=1.
    auto pulse = [&](std::uint8_t th_byte) {
        pad.write_data(th_byte);
        return pad.read_data();
    };

    CHECK(pulse(0x40U) == 0x7FU); // phase 1 (TH=1): standard CBRLDU
    CHECK(pulse(0x00U) == 0x33U); // phase 2 (TH=0): standard SA00DU
    CHECK(pulse(0x40U) == 0x7FU); // phase 3
    CHECK(pulse(0x00U) == 0x33U); // phase 4
    CHECK(pulse(0x40U) == 0x7FU); // phase 5
    CHECK(pulse(0x00U) == 0x30U); // phase 6 (TH=0): 6-button id (bits 3..0 = 0)
    CHECK(pad.phase() == 6U);
    CHECK(pulse(0x40U) == 0x70U); // phase 7 (TH=1): C B Mode X Y Z (all pressed)
    CHECK(pad.phase() == 7U);
    CHECK(pulse(0x00U) == 0x33U); // phase 0 (wrap): back to standard
}

TEST_CASE("mk1653 V-blank resets the phase counter") {
    mk1653 pad;
    pad.write_data(0x40U); // TH 0->1: phase 1
    pad.write_data(0x00U); // 1->0: phase 2
    pad.write_data(0x40U); // 0->1: phase 3
    CHECK(pad.phase() == 3U);
    pad.on_vblank();
    CHECK(pad.phase() == 0U);
}
