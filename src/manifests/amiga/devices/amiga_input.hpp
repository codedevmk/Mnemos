#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::amiga {

    inline constexpr std::size_t amiga_controller_port_count = 2U;

    inline constexpr std::uint8_t amiga_joy_up = 1U << 0U;
    inline constexpr std::uint8_t amiga_joy_down = 1U << 1U;
    inline constexpr std::uint8_t amiga_joy_left = 1U << 2U;
    inline constexpr std::uint8_t amiga_joy_right = 1U << 3U;
    inline constexpr std::uint8_t amiga_joy_fire = 1U << 4U;
    inline constexpr std::uint8_t amiga_joy_secondary_fire = 1U << 5U;
    inline constexpr std::uint8_t amiga_joy_middle_fire = 1U << 6U;
    inline constexpr std::uint8_t amiga_controller_button_mask =
        amiga_joy_up | amiga_joy_down | amiga_joy_left | amiga_joy_right | amiga_joy_fire |
        amiga_joy_secondary_fire | amiga_joy_middle_fire;

    inline constexpr std::uint64_t amiga_pot_reset_scanlines = 7U;
    inline constexpr std::uint64_t amiga_pot_full_scale_scanlines = 255U;

    [[nodiscard]] std::uint8_t amiga_sanitize_controller_mask(std::uint8_t mask) noexcept;

    [[nodiscard]] std::uint16_t amiga_encode_joystick(std::uint8_t mask) noexcept;

    [[nodiscard]] std::uint8_t amiga_wrap_mouse_counter(std::uint8_t value,
                                                        std::int16_t delta) noexcept;

    [[nodiscard]] std::uint8_t amiga_mouse_button_mask(bool left_button, bool right_button,
                                                       bool middle_button) noexcept;

    [[nodiscard]] std::uint16_t amiga_pack_pot_target(std::uint8_t x, std::uint8_t y) noexcept;

    [[nodiscard]] std::uint8_t amiga_pot_axis_value(std::uint8_t resistance,
                                                    std::uint64_t elapsed_lines) noexcept;

    [[nodiscard]] std::uint16_t amiga_pot_counter_value(std::uint16_t target,
                                                        std::uint64_t elapsed_lines) noexcept;

    [[nodiscard]] std::uint16_t amiga_potinp_value(
        std::uint16_t potgo,
        const std::array<std::uint8_t, amiga_controller_port_count>& joystick_state) noexcept;

} // namespace mnemos::manifests::amiga
