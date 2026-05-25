#pragma once

#include <mnemos/chips/storage/c1541/d64_image.hpp>

#include <cstdint>
#include <vector>

// Bind a logical .d64 into the GCR bit-stream the 1541 head actually reads: per
// sector a header-sync, GCR-encoded header block, gap, data-sync, GCR-encoded
// data block, and tail gap. This is the disk surface the full cycle-accurate
// drive spins under its head. Ported from Emu's c1541_disk_bind (ADR 0006).
namespace mnemos::chips::storage::c1541 {

    struct gcr_track final {
        std::vector<std::uint8_t> bytes; // circular GCR stream for the whole track
        std::uint8_t density_zone{};     // 3 (fastest, tracks 1-17) .. 0 (tracks 31+)
    };

    // GCR cycles-per-byte by density zone (3..0): the platter advances one GCR byte
    // every this-many drive cycles.
    inline constexpr std::array<std::uint8_t, 4> gcr_zone_byte_period = {32U, 30U, 28U, 26U};

    [[nodiscard]] std::uint8_t gcr_density_zone(std::uint8_t track) noexcept;

    // Encode every track of `disk` into its GCR stream. Empty if no disk.
    [[nodiscard]] std::vector<gcr_track> bind_gcr(const d64_image& disk);

} // namespace mnemos::chips::storage::c1541
