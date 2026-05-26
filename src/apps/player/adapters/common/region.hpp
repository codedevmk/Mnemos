#pragma once

// Cross-system ROM region detection.
//
// Every emulated system needs to decide NTSC vs PAL (and similar regional
// variants) before it can pace video and audio correctly. The mechanism is
// system-specific -- each platform encodes region differently in its cartridge
// header -- but the *output* (the categorical region) is the same. This header
// pins that shared output type and exposes one detector per platform family,
// so the player tool can use a uniform pattern when constructing any
// adapter:
//
//     const auto region = adapters::detect_<family>_region(rom_bytes);
//     auto adapter = std::make_unique<adapters::<family>::<family>_adapter>(
//         std::move(rom_bytes), region);
//
// Adapter families sharing a header format (Sega 16-bit: Genesis / 32X /
// Sega CD all use the Mega Drive ROM header) share one detector.

#include <cstdint>
#include <span>

namespace mnemos::apps::player::adapters {

    enum class video_region : std::uint8_t {
        ntsc,
        pal,
    };

    // Sega 16-bit family (Mega Drive / Genesis, 32X, Sega CD). The Mega Drive
    // ROM header at $1F0..$1F2 carries the region field, encoded either as
    // 1-3 ASCII letters (`J`=Japan, `U`=USA, `E`=Europe) or as a hex bitfield
    // byte where bit 0=Japan, bit 1=USA, bit 2=Europe (newer carts). PAL is
    // preferred when Europe is among the supported markets, since PAL-only
    // display features (V30 mode + full vertical border budget) are then
    // honoured the way the cart's PAL-aware screens were authored for.
    [[nodiscard]] video_region detect_sega16_region(std::span<const std::uint8_t> rom) noexcept;

    // Sega Master System / Mark III. Region is typically inferred from the
    // header at $7FF0..$7FFF: byte $7FFF's high nibble encodes the target
    // country (3=SMS Japan, 4=SMS Export, 5=GG Japan, ...). Most US/EU SMS
    // carts run on either console; default NTSC for export carts.
    // (Stub for the future SMS adapter; currently always returns NTSC.)
    [[nodiscard]] video_region detect_sms_region(std::span<const std::uint8_t> rom) noexcept;

    // Future detectors land here as the corresponding adapters arrive:
    //   detect_snes_region(span)  -- SNES header (LoROM / HiROM / ExHiROM)
    //   detect_nes_region(span)   -- iNES + NES 2.0 flags
    //   detect_segacd_region(span) reuses detect_sega16_region for the BIOS;
    //                              CD-ROM ISO header carries its own region byte

} // namespace mnemos::apps::player::adapters
