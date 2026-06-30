#include "devices/amiga_input.hpp"

#include <algorithm>

namespace mnemos::manifests::amiga {

    std::uint8_t amiga_sanitize_controller_mask(std::uint8_t mask) noexcept {
        return static_cast<std::uint8_t>(mask & amiga_controller_button_mask);
    }

    std::uint16_t amiga_encode_joystick(std::uint8_t mask) noexcept {
        const bool right = (mask & amiga_joy_right) != 0U;
        const bool left = (mask & amiga_joy_left) != 0U;
        const bool down = (mask & amiga_joy_down) != 0U;
        const bool up = (mask & amiga_joy_up) != 0U;

        std::uint16_t value = 0U;
        if (right) {
            value = static_cast<std::uint16_t>(value | 0x0002U);
        }
        if (left) {
            value = static_cast<std::uint16_t>(value | 0x0200U);
        }
        if (down != right) {
            value = static_cast<std::uint16_t>(value | 0x0001U);
        }
        if (up != left) {
            value = static_cast<std::uint16_t>(value | 0x0100U);
        }
        return value;
    }

    std::uint8_t amiga_wrap_mouse_counter(std::uint8_t value, std::int16_t delta) noexcept {
        return static_cast<std::uint8_t>((static_cast<int>(value) + static_cast<int>(delta)) &
                                         0xFF);
    }

    std::uint8_t amiga_mouse_button_mask(bool left_button, bool right_button,
                                         bool middle_button) noexcept {
        std::uint8_t buttons = 0U;
        if (left_button) {
            buttons = static_cast<std::uint8_t>(buttons | amiga_joy_fire);
        }
        if (right_button) {
            buttons = static_cast<std::uint8_t>(buttons | amiga_joy_secondary_fire);
        }
        if (middle_button) {
            buttons = static_cast<std::uint8_t>(buttons | amiga_joy_middle_fire);
        }
        return buttons;
    }

    std::uint16_t amiga_pack_pot_target(std::uint8_t x, std::uint8_t y) noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(y) << 8U) | x);
    }

    std::uint8_t amiga_pot_axis_value(std::uint8_t resistance,
                                      std::uint64_t elapsed_lines) noexcept {
        if (elapsed_lines <= amiga_pot_reset_scanlines) {
            return 0U;
        }

        // The controller port ADC holds the counters reset for the first
        // scanlines, then advances once per horizontal line until the RC charge
        // crosses the comparator threshold. The documented 528K/47nF
        // full-scale path maps to the full 8-bit counter range.
        const std::uint64_t charge_lines = elapsed_lines - amiga_pot_reset_scanlines;
        const std::uint64_t target =
            (static_cast<std::uint64_t>(resistance) * amiga_pot_full_scale_scanlines + 127U) /
            255U;
        const std::uint64_t clamped = std::min(charge_lines, target);
        return static_cast<std::uint8_t>(clamped);
    }

    std::uint16_t amiga_pot_counter_value(std::uint16_t target,
                                          std::uint64_t elapsed_lines) noexcept {
        const auto x = amiga_pot_axis_value(static_cast<std::uint8_t>(target), elapsed_lines);
        const auto y =
            amiga_pot_axis_value(static_cast<std::uint8_t>(target >> 8U), elapsed_lines);
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(y) << 8U) | x);
    }

    std::uint16_t amiga_potinp_value(
        std::uint16_t potgo,
        const std::array<std::uint8_t, amiga_controller_port_count>& joystick_state) noexcept {
        constexpr std::uint16_t out_lx = 0x0200U;
        constexpr std::uint16_t dat_lx = 0x0100U;
        constexpr std::uint16_t out_ly = 0x0800U;
        constexpr std::uint16_t dat_ly = 0x0400U;
        constexpr std::uint16_t out_rx = 0x2000U;
        constexpr std::uint16_t dat_rx = 0x1000U;
        constexpr std::uint16_t out_ry = 0x8000U;
        constexpr std::uint16_t dat_ry = 0x4000U;

        const auto pin = [potgo](std::uint16_t out_bit, std::uint16_t data_bit,
                                 bool external_low) noexcept -> std::uint16_t {
            if ((potgo & out_bit) != 0U) {
                return (potgo & data_bit) != 0U ? data_bit : 0U;
            }
            return external_low ? 0U : data_bit;
        };

        std::uint16_t value =
            static_cast<std::uint16_t>(potgo & (out_lx | out_ly | out_rx | out_ry));
        value = static_cast<std::uint16_t>(
            value | pin(out_lx, dat_lx, (joystick_state[0] & amiga_joy_middle_fire) != 0U) |
            pin(out_ly, dat_ly, (joystick_state[0] & amiga_joy_secondary_fire) != 0U) |
            pin(out_rx, dat_rx, (joystick_state[1] & amiga_joy_middle_fire) != 0U) |
            pin(out_ry, dat_ry, (joystick_state[1] & amiga_joy_secondary_fire) != 0U));
        return value;
    }

} // namespace mnemos::manifests::amiga
