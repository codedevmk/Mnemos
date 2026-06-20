#pragma once

#include "bus.hpp"            // topology::bus
#include "m6510.hpp"          // chips::cpu::m6510 (the 2A03 is a 6502 core)
#include "ppu2c02.hpp"        // chips::video::ppu2c02
#include "region.hpp"         // mnemos::video_region
#include "ricoh_2a03_apu.hpp" // chips::audio::ricoh_2a03_apu

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::nes {

    struct nes_config final {
        // The NES is principally a 60 Hz (NTSC) machine; PAL timing is a later
        // increment, so the host frame cadence defaults to NTSC.
        mnemos::video_region video_region{mnemos::video_region::ntsc};
    };

    // A parsed iNES (.nes) image: the PRG/CHR banks plus the cartridge wiring the
    // 16-byte header declares.
    struct ines_image final {
        std::vector<std::uint8_t> prg; // PRG-ROM (16 or 32 KiB for NROM)
        std::vector<std::uint8_t> chr; // CHR-ROM (8 KiB); empty => CHR-RAM
        chips::video::ppu2c02::mirroring mirroring{chips::video::ppu2c02::mirroring::horizontal};
        int mapper{};      // iNES mapper number (0 = NROM)
        bool chr_is_ram{}; // header CHR count 0 => 8 KiB CHR-RAM
        bool valid{};      // false if the header magic / sizes don't parse
    };

    // Parse an iNES image. `valid` is false (and the banks empty) when the magic
    // or the declared sizes don't fit the data.
    [[nodiscard]] ines_image parse_ines(std::span<const std::uint8_t> data);

    // A hand-wired NES (the NROM increment): the 2A03 (a 6502 with the on-chip
    // I/O port disabled) + the 2C02 PPU + the APU on a 16-bit little-endian bus.
    // The PPU is the frame source; it raises the CPU /NMI at vblank through an
    // injected callback. Heap-allocated and never moved -- the bus holds spans
    // into the member arrays and the MMIO / NMI handlers capture `this`.
    struct nes_system final {
        chips::cpu::m6510 cpu;
        chips::video::ppu2c02 ppu;
        chips::audio::ricoh_2a03_apu apu;
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x800> wram{}; // 2 KiB work RAM ($0000-$07FF, x4 mirror)
        std::vector<std::uint8_t> prg;          // PRG-ROM, mapped read-only at $8000
        std::vector<std::uint8_t> chr;          // CHR (ROM image), the PPU's $0000-$1FFF

        // (controllers, OAM-DMA stall accuracy and save-state arrive in later
        // increments; CHR-RAM games are out of scope for the NROM boot.)
    };

    // Build a runnable NES from a .nes image. An image that doesn't parse (or a
    // mapper this increment doesn't implement) still yields a machine -- it boots
    // a blank PRG -- so the player never crashes on an unsupported cart.
    [[nodiscard]] std::unique_ptr<nes_system> assemble_nes(std::span<const std::uint8_t> rom,
                                                           const nes_config& config = {});

} // namespace mnemos::manifests::nes
