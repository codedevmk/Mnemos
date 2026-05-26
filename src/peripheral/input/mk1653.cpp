#include "mk1653.hpp"

namespace mnemos::peripheral::input {

    info mk1653::describe() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "MK-1653",
            .family = "Mega Drive 6-Button Arcade Pad",
            .category = kind::input_pad,
            // Falls back to 3-button compatibility on SMS (TH pulses are
            // ignored; the pad reports the standard bank on every read).
            .compatible = host::genesis | host::sms | host::mega_cd | host::sega_32x,
        };
    }

    void mk1653::write_data(std::uint8_t value) noexcept {
        const bool new_th = (value & 0x40U) != 0U;
        if (new_th != th_) {
            th_ = new_th;
            phase_ = static_cast<std::uint8_t>((phase_ + 1U) & 7U);
        }
    }

    void mk1653::apply_state(const controller_state& s) noexcept {
        // 12-button layout owned by this model. The frontend pushes a
        // controller_state; we pick the subset the MK-1653 exposes and lay
        // it out into our internal bit positions.
        constexpr std::uint16_t up_bit    = 0x0001U;
        constexpr std::uint16_t down_bit  = 0x0002U;
        constexpr std::uint16_t left_bit  = 0x0004U;
        constexpr std::uint16_t right_bit = 0x0008U;
        constexpr std::uint16_t a_bit     = 0x0010U;
        constexpr std::uint16_t b_bit     = 0x0020U;
        constexpr std::uint16_t c_bit     = 0x0040U;
        constexpr std::uint16_t start_bit = 0x0080U;
        constexpr std::uint16_t z_bit     = 0x0100U;
        constexpr std::uint16_t y_bit     = 0x0200U;
        constexpr std::uint16_t x_bit     = 0x0400U;
        constexpr std::uint16_t mode_bit  = 0x0800U;
        buttons_ = static_cast<std::uint16_t>(
            (s.up    ? up_bit    : 0U) | (s.down  ? down_bit  : 0U) |
            (s.left  ? left_bit  : 0U) | (s.right ? right_bit : 0U) |
            (s.a     ? a_bit     : 0U) | (s.b     ? b_bit     : 0U) |
            (s.c     ? c_bit     : 0U) | (s.start ? start_bit : 0U) |
            (s.z     ? z_bit     : 0U) | (s.y     ? y_bit     : 0U) |
            (s.x     ? x_bit     : 0U) | (s.mode  ? mode_bit  : 0U));
    }

    std::uint8_t mk1653::read_data() const noexcept {
        const auto inv = [&](std::uint16_t mask) -> std::uint8_t {
            return (buttons_ & mask) ? 0U : 1U;
        };
        std::uint8_t out = th_ ? 0x40U : 0x00U;
        if (th_) {
            if (phase_ == 7U) {
                // Extended bank: C B Mode X Y Z.
                out |= inv(0x0100U) << 0; // Z
                out |= inv(0x0200U) << 1; // Y
                out |= inv(0x0400U) << 2; // X
                out |= inv(0x0800U) << 3; // Mode
                out |= inv(0x0020U) << 4; // B
                out |= inv(0x0040U) << 5; // C
            } else {
                out |= inv(0x0001U) << 0; // Up
                out |= inv(0x0002U) << 1; // Down
                out |= inv(0x0004U) << 2; // Left
                out |= inv(0x0008U) << 3; // Right
                out |= inv(0x0020U) << 4; // B
                out |= inv(0x0040U) << 5; // C
            }
        } else {
            out |= inv(0x0010U) << 4; // A
            out |= inv(0x0080U) << 5; // Start
            if (phase_ == 6U) {
                // 6-button id bank: bits 3..0 = 0 (no dpad echo) -- distinguishes
                // an MK-1653 from an MK-1650 which would echo dpad bits here.
            } else {
                out |= inv(0x0001U) << 0; // Up
                out |= inv(0x0002U) << 1; // Down
                // bits 2,3 stay 0 (3-button compat signature)
            }
        }
        return out;
    }

} // namespace mnemos::peripheral::input
