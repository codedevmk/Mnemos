#pragma once

#include "bus.hpp"         // topology bus
#include "genesis_vdp.hpp" // video
#include "m68000.hpp"      // main cpu
#include "sn76489.hpp"     // audio (PSG)
#include "ym2612.hpp"      // audio (FM)
#include "z80.hpp"         // sound cpu

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::genesis {

    // Machine configuration resolved at assembly time.
    struct genesis_config final {
        enum class region : std::uint8_t { ntsc, pal };
        region video_region{region::ntsc};
    };

    // A wired Sega Genesis / Mega Drive: the 68000 main CPU, the VDP, the YM2612 FM
    // and SN76489 PSG, the Z80 sound CPU, 64 KiB of 68K work RAM, 8 KiB of Z80 RAM,
    // and the 68000 24-bit big-endian bus. Heap-allocated and never moved after
    // assembly, because the bus regions hold spans into the members and the MMIO/IRQ
    // callbacks capture `this`.
    //
    // THIS phase wires the 68000 side: the full memory map (ROM, work RAM, the Z80
    // bank with Z80 RAM + YM2612, the I/O + Z80-control region, and the VDP), the VDP
    // V/H-blank IRQ into the 68000 IPL, VDP DMA reading from 68K space, and the PSG.
    // The Z80 sound CPU is present and 68K-accessible but is held off the schedule
    // (the 68000 owns the bus); bringing the Z80 online with bus arbitration and the
    // full audio mix is the next phase.
    struct genesis_system final {
        chips::cpu::m68000 cpu;
        chips::cpu::z80 z80; // present + 68K-accessible; not yet scheduled
        chips::video::genesis_vdp vdp;
        chips::audio::ym2612 fm;
        chips::audio::sn76489 psg;
        topology::bus bus{24U, topology::endianness::big};

        std::array<std::uint8_t, 0x10000> work_ram{}; // 64 KiB, mirrored $E00000-$FFFFFF
        std::array<std::uint8_t, 0x2000> z80_ram{};   // 8 KiB at $A00000
        std::vector<std::uint8_t> rom;                // cartridge image (borrowed by the bus)

        std::uint8_t version_register{}; // $A10001 region/version readback

        // 16-bit coalescing latches for the VDP ports: a 68000 word access arrives as
        // an even byte (high) then an odd byte (low); the VDP sees one whole word.
        std::uint8_t vdp_write_high{};
        std::uint8_t vdp_read_low{};

        // Z80 control latches (the Z80 is dormant this phase; reads report "bus
        // granted to the 68000" so wait loops fall through).
        bool z80_bus_requested{};
        bool z80_reset_released{};

        // Controller state, active-high (a set bit = pressed); the port read converts
        // to the active-low pad lines. Full 3/6-button protocol is the next phase.
        std::array<std::uint8_t, 2> pad{};

        void set_pad(int port, std::uint8_t buttons) noexcept {
            if (port >= 0 && port < 2) {
                pad[static_cast<std::size_t>(port)] = buttons;
            }
        }
    };

    // Assemble a bootable Genesis from a cartridge image (moved in). The 68000 boots
    // from the ROM's reset vectors ($000000 SSP, $000004 PC). ROM verification is the
    // caller's job; pass any image to exercise the wiring.
    [[nodiscard]] std::unique_ptr<genesis_system>
    assemble_genesis(std::vector<std::uint8_t> rom, const genesis_config& config = {});

} // namespace mnemos::manifests::genesis
