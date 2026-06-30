#pragma once

#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::amiga {

    struct zorro2_expansion_board final {
        std::uint8_t product{};
        std::uint16_t manufacturer{};
        std::uint32_t serial{};
        std::size_t memory_size{};
        std::uint32_t assigned_base{};
        bool memory{};
        bool configured{};
        bool shut_up{};
    };

    [[nodiscard]] std::uint8_t zorro2_autoconfig_read(
        const zorro2_expansion_board& board, std::uint32_t autoconfig_base,
        std::uint32_t address) noexcept;
    [[nodiscard]] std::uint8_t zorro2_write_nibble(std::uint8_t value) noexcept;

} // namespace mnemos::manifests::amiga
