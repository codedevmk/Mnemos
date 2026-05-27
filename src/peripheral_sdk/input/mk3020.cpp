#include "mk3020.hpp"

namespace mnemos::peripheral::input {

    info mk3020::describe() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "MK-3020",
            .family = "Sega Master System Control Pad",
            .category = kind::input_pad,
            // Electrically + protocol-compatible across the Sega 8/16-bit
            // family: a Master System pad plugs into a Mega Drive port and
            // works as a 2-button device.
            .compatible = host::sms | host::genesis | host::mega_cd | host::sega_32x,
        };
    }

    std::uint8_t mk3020::read_data() const noexcept {
        const auto inv = [&](std::uint8_t mask) -> std::uint8_t {
            return (buttons_ & mask) ? 0U : 1U;
        };
        // Pad-private layout into the wire byte. Bits 6-7 idle high because
        // the SMS pad doesn't drive those lines.
        constexpr std::uint8_t up_bit = 0x01U;
        constexpr std::uint8_t down_bit = 0x02U;
        constexpr std::uint8_t left_bit = 0x04U;
        constexpr std::uint8_t right_bit = 0x08U;
        constexpr std::uint8_t b1_bit = 0x10U;
        constexpr std::uint8_t b2_bit = 0x20U;
        std::uint8_t out = static_cast<std::uint8_t>(
            (inv(up_bit) << 0) | (inv(down_bit) << 1) | (inv(left_bit) << 2) |
            (inv(right_bit) << 3) | (inv(b1_bit) << 4) | (inv(b2_bit) << 5));
        out |= 0xC0U;
        return out;
    }

    void mk3020::apply_state(const controller_state& s) noexcept {
        // 2-button + dpad layout owned by this model; controller_state's
        // a/b map to B1/B2, the rest is ignored.
        constexpr std::uint8_t up_bit = 0x01U;
        constexpr std::uint8_t down_bit = 0x02U;
        constexpr std::uint8_t left_bit = 0x04U;
        constexpr std::uint8_t right_bit = 0x08U;
        constexpr std::uint8_t b1_bit = 0x10U;
        constexpr std::uint8_t b2_bit = 0x20U;
        buttons_ = static_cast<std::uint8_t>(
            (s.up ? up_bit : 0U) | (s.down ? down_bit : 0U) |
            (s.left ? left_bit : 0U) | (s.right ? right_bit : 0U) |
            (s.a ? b1_bit : 0U) | (s.b ? b2_bit : 0U));
    }

} // namespace mnemos::peripheral::input
