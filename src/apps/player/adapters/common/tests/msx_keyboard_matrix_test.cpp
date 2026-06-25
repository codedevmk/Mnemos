#include "msx_keyboard_matrix.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
    using mnemos::apps::player::adapters::msx_keyboard_matrix_from_input;
    using mnemos::peripheral::controller_state;
} // namespace

TEST_CASE("msx_keyboard_matrix maps USB HID usages to active-low MSX rows") {
    controller_state state{};
    state.set_key(0x04U, true); // A
    state.set_key(0x1EU, true); // 1
    state.set_key(0x28U, true); // Return
    state.set_key(0x50U, true); // Left arrow

    const auto matrix = msx_keyboard_matrix_from_input(state);
    CHECK(matrix[0] == 0xFDU);
    CHECK(matrix[2] == 0xBFU);
    CHECK(matrix[7] == 0x7FU);
    CHECK(matrix[8] == 0xEFU);
}

TEST_CASE("msx_keyboard_matrix keeps generic player controls on the keyboard port") {
    controller_state state{};
    state.right = true;
    state.a = true;
    state.start = true;
    state.select = true;

    const auto matrix = msx_keyboard_matrix_from_input(state);
    CHECK(matrix[6] == 0xFEU);
    CHECK(matrix[7] == 0x7FU);
    CHECK(matrix[8] == 0x7EU);
}
