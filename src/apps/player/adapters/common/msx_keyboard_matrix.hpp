#pragma once

#include "peripheral.hpp" // mnemos::peripheral::controller_state

#include <array>
#include <cstddef>
#include <cstdint>

namespace mnemos::apps::player::adapters {

    inline constexpr std::size_t msx_keyboard_row_count = 16U;
    using msx_keyboard_matrix = std::array<std::uint8_t, msx_keyboard_row_count>;

    // Convert the frontend's USB/HID keyboard usages plus generic pad fields to
    // the MSX active-low keyboard rows read through the 8255 PPI.
    [[nodiscard]] msx_keyboard_matrix
    msx_keyboard_matrix_from_input(const peripheral::controller_state& state) noexcept;

} // namespace mnemos::apps::player::adapters
