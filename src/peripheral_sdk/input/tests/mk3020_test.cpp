#include "mk3020.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
    using mnemos::peripheral::input::mk3020;
}

TEST_CASE("mk3020 reports the documented Sega manifest") {
    const mk3020 pad;
    const auto i = pad.describe();
    CHECK(std::string{i.manufacturer} == "Sega");
    CHECK(std::string{i.part_number} == "MK-3020");
    CHECK(i.category == mnemos::peripheral::kind::input_pad);
    CHECK((i.compatible & mnemos::peripheral::host::sms) != 0U);
    // Electrically compatible with the Mega Drive port too.
    CHECK((i.compatible & mnemos::peripheral::host::genesis) != 0U);
}

TEST_CASE("mk3020 reads idle as all-ones on its 6 input pins") {
    mk3020 pad;
    // No buttons pressed -> bits 0..5 all 1 (active-low), bits 6..7 idle high.
    CHECK(pad.read_data() == 0xFFU);
}

TEST_CASE("mk3020 reflects pressed buttons into the active-low byte") {
    mk3020 pad;
    mnemos::peripheral::controller_state state{};
    state.up = true;
    state.a = true; // -> Button 1
    pad.apply_state(state);
    // Up clears bit 0; Button 1 clears bit 4. Result = 0b11101110 = 0xEE.
    CHECK(pad.read_data() == 0xEEU);

    state = {};
    state.right = true;
    state.b = true; // -> Button 2
    pad.apply_state(state);
    // Right clears bit 3; Button 2 clears bit 5. Result = 0b11010111 = 0xD7.
    CHECK(pad.read_data() == 0xD7U);
}
