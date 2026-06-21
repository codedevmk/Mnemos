#include "input_pack.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
    using mnemos::apps::player::adapters::button_bit;
    using mnemos::apps::player::adapters::dpad_layout;
    using mnemos::apps::player::adapters::pack_active_low_buttons;
    using mnemos::apps::player::adapters::pack_active_low_pad;
    using mnemos::peripheral::controller_state;
} // namespace

TEST_CASE("input_pack: idle pad reads all-released (0xFF)") {
    const controller_state idle{};
    CHECK(pack_active_low_pad(idle, dpad_layout{}, {}) == 0xFFU);
    CHECK(pack_active_low_buttons({}) == 0xFFU);
}

TEST_CASE("input_pack: standard nibble clears the pressed direction's bit") {
    controller_state s{};
    s.right = true;
    CHECK(pack_active_low_pad(s, dpad_layout{}, {}) == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    s = {};
    s.up = true;
    CHECK(pack_active_low_pad(s, dpad_layout{}, {}) == static_cast<std::uint8_t>(0xFFU & ~0x08U));
}

TEST_CASE("input_pack: directions and buttons compose into one active-low byte") {
    controller_state s{};
    s.up = true;
    s.a = true;
    // CPS layout: up = bit3, button1 (a) = bit4.
    const std::uint8_t packed =
        pack_active_low_pad(s, dpad_layout{}, {{s.a, 0x10U}, {s.b, 0x20U}, {s.c, 0x40U}});
    CHECK(packed == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x10U));
}

TEST_CASE("input_pack: a custom layout reassigns the direction bits") {
    controller_state s{};
    s.up = true;
    // M72 layout: button1 (a) = bit7, button2 (b) = bit6; up = bit3 still.
    const dpad_layout m72{.right = 0x01U, .left = 0x02U, .down = 0x04U, .up = 0x08U};
    const std::uint8_t packed = pack_active_low_pad(s, m72, {{s.a, 0x80U}, {s.b, 0x40U}});
    CHECK(packed == static_cast<std::uint8_t>(0xFFU & ~0x08U));
}

TEST_CASE("input_pack: dpad_layout::none packs buttons only, ignoring directions") {
    controller_state s{};
    s.up = true; // must NOT clear any bit with a none() layout
    s.x = true;
    const std::uint8_t via_pad =
        pack_active_low_pad(s, dpad_layout::none(), {{s.x, 0x01U}, {s.y, 0x02U}, {s.z, 0x04U}});
    const std::uint8_t via_buttons =
        pack_active_low_buttons({{s.x, 0x01U}, {s.y, 0x02U}, {s.z, 0x04U}});
    CHECK(via_pad == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    CHECK(via_pad == via_buttons);
}
