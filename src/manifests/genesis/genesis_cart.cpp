#include "genesis_cart.hpp"

namespace mnemos::manifests::genesis {

    std::size_t cart_sram::byte_count() const noexcept {
        if (end < start) {
            return 0;
        }
        const std::size_t span = static_cast<std::size_t>(end - start) + 1U;
        // Byte-wide SRAM stores one byte per two addresses (only the odd or even
        // lane is populated); 16-bit SRAM stores every byte.
        return map == mapping::word ? span : (span + 1U) / 2U;
    }

    std::optional<cart_sram> parse_cart_sram(std::span<const std::uint8_t> rom) noexcept {
        // The external-RAM header block spans $1B0-$1BB; require it in full.
        if (rom.size() < 0x1BCU) {
            return std::nullopt;
        }
        if (rom[0x1B0U] != static_cast<std::uint8_t>('R') ||
            rom[0x1B1U] != static_cast<std::uint8_t>('A')) {
            return std::nullopt; // no external-RAM signature
        }
        const auto be32 = [rom](std::size_t off) -> std::uint32_t {
            return (static_cast<std::uint32_t>(rom[off]) << 24U) |
                   (static_cast<std::uint32_t>(rom[off + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(rom[off + 2U]) << 8U) |
                   static_cast<std::uint32_t>(rom[off + 3U]);
        };
        cart_sram s;
        s.start = be32(0x1B4U);
        s.end = be32(0x1B8U);
        if (s.end < s.start) {
            return std::nullopt; // degenerate / absent range
        }
        // Byte lane from $1B2 bits [4:3]: 11 = odd byte (D0-D7, the common 8-bit
        // case), 10 = even byte (D8-D15), else a full 16-bit RAM. An odd start
        // address independently implies odd-byte mapping, so honour either signal.
        const unsigned lane = (rom[0x1B2U] >> 3U) & 3U;
        if (lane == 3U || (s.start & 1U) != 0U) {
            s.map = cart_sram::mapping::odd_byte;
        } else if (lane == 2U) {
            s.map = cart_sram::mapping::even_byte;
        } else {
            s.map = cart_sram::mapping::word;
        }
        return s;
    }

} // namespace mnemos::manifests::genesis
