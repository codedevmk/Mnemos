#include "input_pack.hpp"

namespace mnemos::apps::player::adapters {

    namespace {
        // Active-low clear: pull `bit` low on `value` when `pressed`.
        constexpr void clear_if(std::uint8_t& value, bool pressed, std::uint8_t bit) noexcept {
            if (pressed) {
                value &= static_cast<std::uint8_t>(~bit);
            }
        }
    } // namespace

    std::uint8_t pack_active_low_pad(const peripheral::controller_state& state,
                                     const dpad_layout& dpad,
                                     std::initializer_list<button_bit> buttons) noexcept {
        std::uint8_t value = 0xFFU;
        clear_if(value, state.right, dpad.right);
        clear_if(value, state.left, dpad.left);
        clear_if(value, state.down, dpad.down);
        clear_if(value, state.up, dpad.up);
        for (const button_bit& b : buttons) {
            clear_if(value, b.pressed, b.bit);
        }
        return value;
    }

    std::uint8_t pack_active_low_buttons(std::initializer_list<button_bit> buttons) noexcept {
        std::uint8_t value = 0xFFU;
        for (const button_bit& b : buttons) {
            clear_if(value, b.pressed, b.bit);
        }
        return value;
    }

} // namespace mnemos::apps::player::adapters
