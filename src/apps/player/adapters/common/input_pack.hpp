#pragma once

#include "peripheral.hpp" // mnemos::peripheral::controller_state

#include <cstdint>
#include <initializer_list>

namespace mnemos::apps::player::adapters {

    // Shared packing of the abstract pad onto an arcade panel's active-low input
    // byte. The arcade boards (CPS1/CPS2/M72) all read a byte where 1 = released
    // and a pressed control pulls its bit low; only the per-board bit assignment
    // differs. This is the one documented place for that idiom -- each adapter
    // supplies its own bit layout, so no console's wiring is baked in.

    // The four joystick directions on an active-low panel byte. The standard
    // arcade nibble is right/left/down/up in bits 0-3; a board that wires them
    // elsewhere passes its own bits. `none()` is for a byte that carries no
    // directions (e.g. a buttons-only second input word).
    struct dpad_layout final {
        std::uint8_t right{0x01U};
        std::uint8_t left{0x02U};
        std::uint8_t down{0x04U};
        std::uint8_t up{0x08U};

        [[nodiscard]] static constexpr dpad_layout none() noexcept {
            return {.right = 0U, .left = 0U, .down = 0U, .up = 0U};
        }
    };

    // One button -> bit binding: clear `bit` (active low) when `pressed`. Adapters
    // build these from the controller_state fields their cabinet exposes.
    struct button_bit final {
        bool pressed{};
        std::uint8_t bit{};
    };

    // Pack a directional pad plus an arbitrary set of buttons into one active-low
    // byte (0xFF idle; every pressed control clears its bit). The directions come
    // from `state` via `dpad`; the buttons are the caller's own field->bit map.
    [[nodiscard]] std::uint8_t
    pack_active_low_pad(const peripheral::controller_state& state, const dpad_layout& dpad,
                        std::initializer_list<button_bit> buttons) noexcept;

    // Clear each pressed binding's bit on an active-low byte (0xFF idle). The
    // buttons-only form for input words that carry no joystick directions.
    [[nodiscard]] std::uint8_t
    pack_active_low_buttons(std::initializer_list<button_bit> buttons) noexcept;

} // namespace mnemos::apps::player::adapters
