#pragma once

#include "z80.hpp" // chips::cpu::z80::registers

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// ZX Spectrum 48K snapshot loaders (.z80 v1/v2/v3 and .sna). A snapshot is a
// frozen machine image -- CPU registers + the 48 KiB RAM + the border -- so
// applying one onto an assembled spectrum_system resumes a game mid-run, which is
// how the player runs games without tape loading.
//
// 128K snapshots are rejected (this is a 48K core). Clean-room per the public
// .z80 / .sna format descriptions.
namespace mnemos::manifests::spectrum {

    struct spectrum_snapshot final {
        chips::cpu::z80::registers regs{};
        std::uint8_t border{};
        std::array<std::uint8_t, 0xC000> ram{}; // $4000-$FFFF
    };

    // Parse a .z80 (v1/v2/v3) 48K snapshot. nullopt on a malformed or 128K image.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_z80_snapshot(std::span<const std::uint8_t> data);

    // Parse a .sna 48K snapshot (exactly 49179 bytes). nullopt otherwise.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_sna_snapshot(std::span<const std::uint8_t> data);

    // Dispatch by content: a 49179-byte image is a .sna, otherwise a .z80.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_snapshot(std::span<const std::uint8_t> data);

} // namespace mnemos::manifests::spectrum
