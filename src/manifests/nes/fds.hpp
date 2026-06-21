#pragma once

#include "nes_mapper.hpp" // nes_mapper (the FDS RAM adapter is modelled as one)

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::nes {

    // One Famicom Disk System disk side is exactly 65500 bytes in the .fds format
    // (the raw block stream, with the on-disk gaps + CRC stripped).
    inline constexpr std::size_t k_fds_side_size = 65500U;

    // True when `data` is a Famicom Disk System disk image rather than an iNES cart:
    // either it carries the "FDS\x1A" header, or it is a headerless multiple of the
    // 65500-byte side size that does not start with the iNES "NES\x1A" magic.
    [[nodiscard]] bool looks_like_fds(std::span<const std::uint8_t> data) noexcept;

    // Return the raw disk sides (side_count * 65500 bytes) with the optional 16-byte
    // "FDS\x1A" header stripped. Empty when the data does not parse as FDS.
    [[nodiscard]] std::vector<std::uint8_t> parse_fds_sides(std::span<const std::uint8_t> data);

    // Build the RP2C33 disk-system "cartridge". It maps $6000-$DFFF (the 32 KiB
    // PRG-RAM the BIOS loads disk files into), $E000-$FFFF (the 8 KiB BIOS), and the
    // $4020-$409F disk + sound register window; it attaches the 8 KiB CHR-RAM, drives
    // the disk byte-transfer + timer IRQs (via the board's per-scanline cpu-timer
    // hook), and owns the FDS sound chip (exposed through expansion_audio()). All
    // spans must outlive the returned mapper (they live in the nes_system).
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_fds(topology::bus& bus, chips::video::ppu2c02& ppu, std::span<const std::uint8_t> disk,
             std::span<const std::uint8_t> bios, std::span<std::uint8_t> ram,
             std::span<std::uint8_t> chr_ram);

} // namespace mnemos::manifests::nes
