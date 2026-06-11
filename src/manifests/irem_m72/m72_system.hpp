#pragma once

#include "bus.hpp"            // topology bus
#include "dac8.hpp"           // sample-playback DAC
#include "irem_m72_video.hpp" // tilemap video unit
#include "mcs51.hpp"          // optional protection MCU
#include "rom_set.hpp"        // arcade ROM-set image
#include "v30.hpp"            // main CPU
#include "ym2151.hpp"         // FM synth on the Z80 ports
#include "z80.hpp"            // sound CPU

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mnemos::manifests::irem_m72 {

    // Canonical ROM-set region names/sizes shared by every M72 game. Graphics
    // and sample region sizes vary per game, so game declarations append those;
    // the board only fixes the two program regions. Irem sets mirror the boot
    // chunk at the top of the 1 MiB "maincpu" region (a reload in the game
    // declaration) so the V30's FFFF0 reset vector lands in ROM.
    inline constexpr std::size_t main_rom_size = 0x100000U; // V30 program, full 20-bit space
    inline constexpr std::size_t sound_rom_size = 0x10000U; // Z80 program

    // First-cut M72 memory map (board constants; refined against R-Type during
    // the video phase -- see docs/plans/2026-06-10-irem-m72-port.md phase C).
    // The program ROM backs the whole space at low priority; the RAM blocks
    // overlay it at higher priority.
    inline constexpr std::uint32_t sprite_ram_base = 0xC0000U;
    inline constexpr std::size_t sprite_ram_size = 0x400U;
    inline constexpr std::uint32_t palette_a_base = 0xC8000U;
    inline constexpr std::uint32_t palette_b_base = 0xCC000U;
    inline constexpr std::size_t palette_size = 0xC00U;
    inline constexpr std::uint32_t vram_a_base = 0xD0000U;
    inline constexpr std::uint32_t vram_b_base = 0xD8000U;
    inline constexpr std::size_t vram_size = 0x4000U;
    inline constexpr std::uint32_t work_ram_base = 0xE0000U;
    inline constexpr std::size_t work_ram_size = 0x4000U;
    inline constexpr std::uint32_t sound_ram_base = 0xF000U; // Z80 bus
    inline constexpr std::size_t sound_ram_size = 0x1000U;

    // V30 I/O ports (first-cut). Reads are active-low input bytes; the write
    // side carries the sound latch and the flip-screen/coin-counter register.
    inline constexpr std::uint16_t port_in_p1 = 0x00U;
    inline constexpr std::uint16_t port_in_p2 = 0x01U;
    inline constexpr std::uint16_t port_in_system = 0x02U; // coins/start/service
    inline constexpr std::uint16_t port_in_dsw_lo = 0x04U;
    inline constexpr std::uint16_t port_in_dsw_hi = 0x05U;
    inline constexpr std::uint16_t port_out_sound_latch = 0x00U;
    inline constexpr std::uint16_t port_out_flip_coin = 0x02U;
    // INT vector the board's interrupt controller drives during the V30
    // acknowledge cycle; games program it at boot.
    inline constexpr std::uint16_t port_out_irq_base = 0x40U;
    // V30 <-> protection-MCU latch pair (first-cut port; present only when
    // the set carries an "mcu" program region). An OUT writes the
    // main-to-MCU latch and pulses the MCU's INT1; an IN reads the MCU's
    // reply latch.
    inline constexpr std::uint16_t port_mcu_latch = 0xC0U;
    // Scroll registers as four little-endian words: +0/+2 = playfield A Y/X,
    // +4/+6 = playfield B Y/X (first-cut layout).
    inline constexpr std::uint16_t port_out_scroll_base = 0x80U;

    // Protection-MCU external (MOVX) map: the sample ROM low, the latch
    // pair at the top (first-cut layout).
    inline constexpr std::uint32_t mcu_latch_in = 0xE000U;  // main -> MCU
    inline constexpr std::uint32_t mcu_latch_out = 0xE001U; // MCU -> main

    // Z80-side ports (first-cut): YM2151 address/data (status readable at
    // the data port), the sound latch readable at 2, the sample path at
    // 0x80-0x84 (a 16-bit address latch, the DAC level, and the auto-
    // incrementing sample-ROM read used by the M84-style sound program;
    // R-Type itself plays no samples).
    inline constexpr std::uint16_t z80_port_ym2151_addr = 0x00U;
    inline constexpr std::uint16_t z80_port_ym2151_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;
    inline constexpr std::uint16_t z80_port_sample_addr_lo = 0x80U;
    inline constexpr std::uint16_t z80_port_sample_addr_hi = 0x81U;
    inline constexpr std::uint16_t z80_port_dac = 0x82U;
    inline constexpr std::uint16_t z80_port_sample_read = 0x84U;

    // Heap-allocated, never-moved M72 board. Built like sega32x_system: chips
    // and RAM blocks as value members, the buses holding spans into them, the
    // port handlers capturing `this`.
    //
    // Built so far: V30 + Z80 on their buses, the program ROM regions out of a
    // loaded rom_set_image, the RAM overlays, the main->sound latch (write
    // asserts the Z80 INT line, the Z80's latch read clears it), the input/DIP
    // port reads off plain state bytes, the tilemap/sprite video unit (VRAM/
    // palette/tile-ROM spans, scroll ports, vblank INT into the V30 through
    // the programmable vector), the YM2151 on the Z80 ports with its IRQ
    // OR'd with the sound latch onto the Z80 INT line, and the DAC + sample-
    // ROM read path on Z80 ports 0x80-0x84. Still to come: the raster-compare
    // port wiring and real input/DIP devices.
    struct m72_system final {
        chips::cpu::v30 main_cpu;
        chips::cpu::z80 sound_cpu;
        chips::video::irem_m72_video video;
        chips::audio::ym2151 fm;
        chips::audio::dac8 dac;
        chips::cpu::mcs51 mcu; // live only when the set carries an "mcu" region
        topology::bus main_bus{20U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};
        topology::bus mcu_bus{16U, topology::endianness::little};

        // The loaded ROM set; the buses map spans into its region vectors, so
        // it must not be mutated after construction. Missing program regions
        // are padded to full size with 0xFF so the maps stay valid.
        common::rom_set_image roms;

        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, palette_size> palette_a{};
        std::array<std::uint8_t, palette_size> palette_b{};
        std::array<std::uint8_t, vram_size> vram_a{};
        std::array<std::uint8_t, vram_size> vram_b{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sound_ram_size> sound_ram{};

        std::uint8_t sound_latch{};
        std::uint8_t input_p1{0xFFU};     // active low
        std::uint8_t input_p2{0xFFU};     // active low
        std::uint8_t input_system{0xFFU}; // coins/start/service, active low
        std::uint16_t dip_switches{0xFFFFU};
        std::uint8_t flip_coin_register{}; // state only until phase C/D
        std::uint8_t irq_vector_base{};    // INT vector served on V30 acknowledge
        std::array<std::uint8_t, 8> scroll_regs{};
        // The Z80's INT line is the OR of the pending sound latch and the
        // YM2151 IRQ; update_sound_irq() recomputes it on either change.
        bool sound_latch_irq{};
        // Sample-ROM read cursor (Z80 ports 0x80/0x81 set it, reads at 0x84
        // auto-increment it through the "samples" region).
        std::uint16_t sample_address{};
        // V30 <-> protection-MCU latch pair.
        std::uint8_t main_to_mcu{};
        std::uint8_t mcu_to_main{};
        bool mcu_present{};

        void update_sound_irq() noexcept {
            sound_cpu.set_irq_line(sound_latch_irq || fm.irq_asserted());
        }

        explicit m72_system(common::rom_set_image image);
    };

    // The board's canonical ROM-set declaration skeleton: the two program
    // regions with their fixed sizes and no files. A game declaration copies
    // it, fills in the dump files (and reloads), and appends its graphics /
    // sample regions.
    [[nodiscard]] common::rom_set_decl m72_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image);

} // namespace mnemos::manifests::irem_m72
