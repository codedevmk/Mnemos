#include <mnemos/manifests/c64/c64_input.hpp>

namespace mnemos::manifests::c64 {

    void c64_input::set_key(key k, bool pressed) noexcept {
        const std::uint8_t code = static_cast<std::uint8_t>(k);
        const std::uint8_t column = static_cast<std::uint8_t>((code >> 3U) & 0x07U);
        const std::uint8_t row_bit = static_cast<std::uint8_t>(1U << (code & 0x07U));
        if (pressed) {
            matrix_[column] = static_cast<std::uint8_t>(matrix_[column] | row_bit);
        } else {
            matrix_[column] = static_cast<std::uint8_t>(matrix_[column] & ~row_bit);
        }
    }

    void c64_input::set_joystick(std::uint8_t port, std::uint8_t mask) noexcept {
        const std::uint8_t bits = static_cast<std::uint8_t>(mask & 0x1FU);
        if (port == 1U) {
            joy1_ = bits;
        } else if (port == 2U) {
            joy2_ = bits;
        }
    }

    void c64_input::set_paddle(std::uint8_t port, std::uint8_t x, std::uint8_t y) noexcept {
        if (port == 1U || port == 2U) {
            paddle_x_[port - 1U] = x;
            paddle_y_[port - 1U] = y;
        }
    }

    std::uint8_t c64_input::paddle_x(std::uint8_t port) const noexcept {
        return (port == 1U || port == 2U) ? paddle_x_[port - 1U] : 0U;
    }

    std::uint8_t c64_input::paddle_y(std::uint8_t port) const noexcept {
        return (port == 1U || port == 2U) ? paddle_y_[port - 1U] : 0U;
    }

    std::uint8_t c64_input::read_rows(std::uint8_t column_strobe) const noexcept {
        std::uint8_t rows = 0xFFU;
        for (std::uint8_t column = 0; column < 8U; ++column) {
            if ((column_strobe & (1U << column)) == 0U) { // column driven low (selected)
                rows = static_cast<std::uint8_t>(rows & ~matrix_[column]);
            }
        }
        return static_cast<std::uint8_t>(rows & ~joy1_); // joystick 1 overlays PRB 0-4
    }

    std::uint8_t c64_input::read_columns(std::uint8_t row_strobe) const noexcept {
        std::uint8_t columns = 0xFFU;
        for (std::uint8_t column = 0; column < 8U; ++column) {
            for (std::uint8_t row = 0; row < 8U; ++row) {
                const bool pressed = (matrix_[column] & (1U << row)) != 0U;
                const bool row_selected = (row_strobe & (1U << row)) == 0U;
                if (pressed && row_selected) {
                    columns = static_cast<std::uint8_t>(columns & ~(1U << column));
                }
            }
        }
        return static_cast<std::uint8_t>(columns & ~joy2_); // joystick 2 overlays PRA 0-4
    }

} // namespace mnemos::manifests::c64
