#pragma once

#include "z80.hpp" // chips::cpu::z80::registers

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// ZX Spectrum snapshot loaders (.z80 v1/v2/v3 and .sna). A snapshot is a frozen
// machine image -- CPU registers + RAM (as 16 KiB banks) + the border -- so
// applying one onto an assembled spectrum_system resumes a game mid-run, which is
// how the player runs games without tape loading.
//
// RAM is held as eight 16 KiB banks (the 128K layout). A 48K snapshot fills the
// three banks the 48K machine maps -- 5 ($4000), 2 ($8000), 0 ($C000); a 128K
// snapshot fills all eight and carries the $7FFD paging latch. Clean-room per the
// public .z80 / .sna format descriptions.
namespace mnemos::manifests::spectrum {

    struct spectrum_snapshot final {
        chips::cpu::z80::registers regs{};
        std::uint8_t border{};
        bool is_128k{};
        std::uint8_t port_7ffd{};                               // 128K paging latch (snap-only)
        std::array<std::array<std::uint8_t, 0x4000>, 8> bank{}; // RAM banks
    };

    // Parse a .z80 (v1/v2/v3) 48K or 128K snapshot. nullopt on a malformed image.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_z80_snapshot(std::span<const std::uint8_t> data);

    // Parse a .sna 48K snapshot (exactly 49179 bytes). nullopt otherwise.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_sna_snapshot(std::span<const std::uint8_t> data);

    // Dispatch by content: a 49179-byte image is a 48K .sna, otherwise a .z80.
    [[nodiscard]] std::optional<spectrum_snapshot>
    load_snapshot(std::span<const std::uint8_t> data);

} // namespace mnemos::manifests::spectrum
