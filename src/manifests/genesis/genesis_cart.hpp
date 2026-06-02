#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace mnemos::manifests::genesis {

    // External cartridge RAM (battery-backed SRAM) as declared in the ROM header.
    // The 8-bit SRAM chip is usually wired to one byte lane of the 16-bit bus, so
    // only odd (D0-D7) or even (D8-D15) addresses hold data; some carts use a full
    // 16-bit SRAM (every byte).
    struct cart_sram {
        enum class mapping { word, odd_byte, even_byte };

        std::uint32_t start{}; // first byte address (e.g. $200000 or $200001)
        std::uint32_t end{};   // last byte address, inclusive
        mapping map{mapping::word};

        // Number of stored bytes: one per address for `word`, one per two
        // addresses for the byte-wide mappings.
        [[nodiscard]] std::size_t byte_count() const noexcept;
    };

    // Parse the Sega cartridge header's external-RAM fields:
    //   $1B0-$1B1 = "RA" signature, $1B2 = RAM type, $1B4/$1B8 = start/end (BE32).
    // Returns nullopt when no SRAM is declared (signature absent or header too
    // short, or the declared range is degenerate).
    [[nodiscard]] std::optional<cart_sram>
    parse_cart_sram(std::span<const std::uint8_t> rom) noexcept;

} // namespace mnemos::manifests::genesis
