#include "msx_keyboard_matrix.hpp"

#include <span>

namespace mnemos::apps::player::adapters {

    namespace {
        struct key_binding final {
            std::uint16_t usage;
            std::uint8_t row;
            std::uint8_t bit;
        };

        constexpr std::uint8_t bit(std::uint8_t index) noexcept {
            return static_cast<std::uint8_t>(1U << index);
        }

        void clear_key(msx_keyboard_matrix& matrix, std::uint8_t row, std::uint8_t bit_index,
                       bool pressed) noexcept {
            if (!pressed || row >= matrix.size() || bit_index >= 8U) {
                return;
            }
            matrix[row] = static_cast<std::uint8_t>(matrix[row] & ~bit(bit_index));
        }

        void apply_usage_map(msx_keyboard_matrix& matrix, const peripheral::controller_state& state,
                             std::span<const key_binding> bindings) noexcept {
            for (const key_binding& binding : bindings) {
                clear_key(matrix, binding.row, binding.bit, state.key_down(binding.usage));
            }
        }

        // USB HID usage IDs mapped onto the standard MSX keyboard matrix.
        constexpr key_binding k_usage_map[] = {
            {0x27U, 0U, 0U}, // 0
            {0x1EU, 0U, 1U}, // 1
            {0x1FU, 0U, 2U}, // 2
            {0x20U, 0U, 3U}, // 3
            {0x21U, 0U, 4U}, // 4
            {0x22U, 0U, 5U}, // 5
            {0x23U, 0U, 6U}, // 6
            {0x24U, 0U, 7U}, // 7
            {0x25U, 1U, 0U}, // 8
            {0x26U, 1U, 1U}, // 9
            {0x2DU, 1U, 2U}, // -
            {0x2EU, 1U, 3U}, // = / ^
            {0x31U, 1U, 4U}, // backslash
            {0x32U, 1U, 4U}, // non-US # / backslash
            {0x2FU, 1U, 5U}, // [
            {0x30U, 1U, 6U}, // ]
            {0x33U, 1U, 7U}, // ;
            {0x34U, 2U, 0U}, // '
            {0x35U, 2U, 1U}, // `
            {0x36U, 2U, 2U}, // ,
            {0x37U, 2U, 3U}, // .
            {0x38U, 2U, 4U}, // /
            {0x64U, 2U, 5U}, // non-US \ / _
            {0x04U, 2U, 6U}, // A
            {0x05U, 2U, 7U}, // B
            {0x06U, 3U, 0U}, // C
            {0x07U, 3U, 1U}, // D
            {0x08U, 3U, 2U}, // E
            {0x09U, 3U, 3U}, // F
            {0x0AU, 3U, 4U}, // G
            {0x0BU, 3U, 5U}, // H
            {0x0CU, 3U, 6U}, // I
            {0x0DU, 3U, 7U}, // J
            {0x0EU, 4U, 0U}, // K
            {0x0FU, 4U, 1U}, // L
            {0x10U, 4U, 2U}, // M
            {0x11U, 4U, 3U}, // N
            {0x12U, 4U, 4U}, // O
            {0x13U, 4U, 5U}, // P
            {0x14U, 4U, 6U}, // Q
            {0x15U, 4U, 7U}, // R
            {0x16U, 5U, 0U}, // S
            {0x17U, 5U, 1U}, // T
            {0x18U, 5U, 2U}, // U
            {0x19U, 5U, 3U}, // V
            {0x1AU, 5U, 4U}, // W
            {0x1BU, 5U, 5U}, // X
            {0x1CU, 5U, 6U}, // Y
            {0x1DU, 5U, 7U}, // Z
            {0xE1U, 6U, 0U}, // left shift
            {0xE5U, 6U, 0U}, // right shift
            {0xE0U, 6U, 1U}, // left control
            {0xE4U, 6U, 1U}, // right control
            {0xE2U, 6U, 2U}, // left alt / graph
            {0xE6U, 6U, 2U}, // right alt / graph
            {0x39U, 6U, 3U}, // caps lock
            {0xE3U, 6U, 4U}, // left GUI / code
            {0xE7U, 6U, 4U}, // right GUI / code
            {0x3AU, 6U, 5U}, // F1
            {0x3BU, 6U, 6U}, // F2
            {0x3CU, 6U, 7U}, // F3
            {0x3DU, 7U, 0U}, // F4
            {0x3EU, 7U, 1U}, // F5
            {0x29U, 7U, 2U}, // escape
            {0x2BU, 7U, 3U}, // tab
            {0x48U, 7U, 4U}, // pause / stop
            {0x2AU, 7U, 5U}, // backspace
            {0x4EU, 7U, 6U}, // page down / select
            {0x28U, 7U, 7U}, // return
            {0x58U, 7U, 7U}, // keypad enter
            {0x2CU, 8U, 0U}, // space
            {0x4AU, 8U, 1U}, // home
            {0x49U, 8U, 2U}, // insert
            {0x4CU, 8U, 3U}, // delete
            {0x50U, 8U, 4U}, // left
            {0x52U, 8U, 5U}, // up
            {0x51U, 8U, 6U}, // down
            {0x4FU, 8U, 7U}, // right
        };
    } // namespace

    msx_keyboard_matrix
    msx_keyboard_matrix_from_input(const peripheral::controller_state& state) noexcept {
        msx_keyboard_matrix matrix{};
        matrix.fill(0xFFU);

        apply_usage_map(matrix, state, k_usage_map);

        clear_key(matrix, 8U, 4U, state.left);
        clear_key(matrix, 8U, 5U, state.up);
        clear_key(matrix, 8U, 6U, state.down);
        clear_key(matrix, 8U, 7U, state.right);
        clear_key(matrix, 8U, 0U, state.a || state.b);
        clear_key(matrix, 7U, 7U, state.start);
        clear_key(matrix, 6U, 0U, state.select);
        clear_key(matrix, 6U, 2U, state.mode);

        return matrix;
    }

} // namespace mnemos::apps::player::adapters
