#include "mk1650.hpp"

namespace mnemos::peripheral::input {

    info mk1650::describe() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "MK-1650",
            .family = "Mega Drive Control Pad",
            .category = kind::input_pad,
            .compatible = host::genesis | host::sms | host::mega_cd | host::sega_32x,
        };
    }

    void mk1650::write_data(std::uint8_t value) noexcept {
        // Bit 6 of the CPU-written byte is the TH select line; the other bits
        // are output-only pad lines the host can drive but the pad ignores.
        th_ = (value & 0x40U) != 0U;
    }

    void mk1650::apply_state(const controller_state& s) noexcept {
        // 8-button layout owned by this model. The frontend pushes a
        // controller_state; we pick the subset the MK-1650 exposes.
        constexpr std::uint8_t up_bit = 0x01U;
        constexpr std::uint8_t down_bit = 0x02U;
        constexpr std::uint8_t left_bit = 0x04U;
        constexpr std::uint8_t right_bit = 0x08U;
        constexpr std::uint8_t a_bit = 0x10U;
        constexpr std::uint8_t b_bit = 0x20U;
        constexpr std::uint8_t c_bit = 0x40U;
        constexpr std::uint8_t start_bit = 0x80U;
        buttons_ = static_cast<std::uint8_t>((s.up ? up_bit : 0U) | (s.down ? down_bit : 0U) |
                                             (s.left ? left_bit : 0U) | (s.right ? right_bit : 0U) |
                                             (s.a ? a_bit : 0U) | (s.b ? b_bit : 0U) |
                                             (s.c ? c_bit : 0U) | (s.start ? start_bit : 0U));
    }

    std::uint8_t mk1650::read_data() const noexcept {
        const auto inv = [&](std::uint8_t mask) -> std::uint8_t {
            return (buttons_ & mask) ? 0U : 1U; // active-high -> active-low wire
        };
        std::uint8_t out = th_ ? 0x40U : 0x00U;
        if (th_) {
            out |= inv(0x01U) << 0; // Up
            out |= inv(0x02U) << 1; // Down
            out |= inv(0x04U) << 2; // Left
            out |= inv(0x08U) << 3; // Right
            out |= inv(0x20U) << 4; // B
            out |= inv(0x40U) << 5; // C
        } else {
            out |= inv(0x01U) << 0; // Up
            out |= inv(0x02U) << 1; // Down
            // bits 2,3 always 0 -- locks Left/Right to identify the 3-button pad.
            out |= inv(0x10U) << 4; // A
            out |= inv(0x80U) << 5; // Start
        }
        return out;
    }

} // namespace mnemos::peripheral::input
