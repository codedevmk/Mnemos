#pragma once

#include "bus.hpp"            // topology bus
#include "dac8.hpp"           // sample-playback DAC
#include "irem_m72_video.hpp" // tilemap video unit
#include "mcs51.hpp"          // optional protection MCU
#include "pic_8259.hpp"       // main-CPU interrupt controller (uPD71059)
#include "rom_set.hpp"        // arcade ROM-set image
#include "v30.hpp"            // main CPU
#include "ym2151.hpp"         // FM synth on the Z80 ports
#include "z80.hpp"            // sound CPU

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mnemos::manifests::irem_m72 {

    // Canonical ROM-set region names/sizes shared by every M72 game. Graphics
    // and sample region sizes vary per game, so game declarations append those;
    // the board only fixes the program region. Irem sets mirror the boot chunk
    // at the top of the 1 MiB "maincpu" region (a reload in the game
    // declaration) so the V30's FFFF0 reset vector lands in ROM.
    inline constexpr std::size_t main_rom_size = 0x100000U; // V30 program, full 20-bit space

    // M72 memory map. The program ROM backs the whole space at low priority;
    // the RAM blocks overlay it at higher priority. The sound CPU has no ROM:
    // its 64K program RAM is shared with the V30 through the byte window at
    // 0xE0000, the V30 uploads the sound program there and then releases the
    // Z80 from reset (port 0x02 bit 4). Work RAM sits at a per-game base
    // (0x40000 or 0xA0000 wiring variants), carried in m72_board_params.
    inline constexpr std::uint32_t sprite_ram_base = 0xC0000U;
    inline constexpr std::size_t sprite_ram_size = 0x400U;
    inline constexpr std::uint32_t palette_a_base = 0xC8000U; // sprite palette
    inline constexpr std::uint32_t palette_b_base = 0xCC000U; // tile palette
    inline constexpr std::size_t palette_size = 0xC00U;
    inline constexpr std::uint32_t vram_a_base = 0xD0000U;
    inline constexpr std::uint32_t vram_b_base = 0xD8000U;
    inline constexpr std::size_t vram_size = 0x4000U;
    inline constexpr std::size_t work_ram_size = 0x4000U;
    inline constexpr std::uint32_t sound_ram_window = 0xE0000U; // V30 view
    inline constexpr std::size_t sound_ram_size = 0x10000U;     // Z80 program RAM

    // V30 I/O ports. Reads are active-low input bytes; the write side carries
    // the sound latch, the board-control register, the sprite-DMA trigger,
    // the raster-compare line, the interrupt controller, and the scroll
    // registers.
    inline constexpr std::uint16_t port_in_p1 = 0x00U;
    inline constexpr std::uint16_t port_in_p2 = 0x01U;
    inline constexpr std::uint16_t port_in_system = 0x02U; // start/coin/service
    inline constexpr std::uint16_t port_in_dsw_lo = 0x04U;
    inline constexpr std::uint16_t port_in_dsw_hi = 0x05U;
    inline constexpr std::uint16_t port_out_sound_latch = 0x00U;
    // Board control: bits 0/1 coin counters, bit 2 flip screen, bit 3 display
    // disable, bit 4 sound-CPU /RESET (active low: set = run).
    inline constexpr std::uint16_t port_out_control = 0x02U;
    // Writing any value copies sprite RAM into the video unit's holding
    // buffer (the board's sprite DMA).
    inline constexpr std::uint16_t port_out_sprite_dma = 0x04U;
    // 16-bit raster-compare line as two byte lanes; position = (word & 0x1ff)
    // - 128. The write also withdraws a pending, unserviced raster request.
    inline constexpr std::uint16_t port_out_raster_lo = 0x06U;
    inline constexpr std::uint16_t port_out_raster_hi = 0x07U;
    // uPD71059 interrupt controller, word-spaced (A0 = address bit 1): even
    // byte lanes 0x40 / 0x42 are the chip's two ports. IR0 = vblank, IR2 =
    // raster compare, both pulsed for one scanline.
    inline constexpr std::uint16_t port_pic_a0 = 0x40U;
    inline constexpr std::uint16_t port_pic_a1 = 0x42U;
    // V30 <-> protection-MCU latch pair (first-cut port; present only when
    // the set carries an "mcu" program region). An OUT writes the
    // main-to-MCU latch and pulses the MCU's INT1; an IN reads the MCU's
    // reply latch.
    inline constexpr std::uint16_t port_mcu_latch = 0xC0U;
    // Scroll registers as four little-endian words: +0/+2 = playfield A Y/X,
    // +4/+6 = playfield B Y/X.
    inline constexpr std::uint16_t port_out_scroll_base = 0x80U;

    // Protection-MCU external (MOVX) map: the sample ROM low, the latch
    // pair at the top (first-cut layout).
    inline constexpr std::uint32_t mcu_latch_in = 0xE000U;  // main -> MCU
    inline constexpr std::uint32_t mcu_latch_out = 0xE001U; // MCU -> main

    // Z80-side ports: YM2151 address/data (status readable at the data port),
    // the sound latch readable at 2 with its interrupt acknowledged by the
    // separate write to 6, the sample path at 0x80-0x84 (a 16-bit address
    // latch, the DAC level, and the auto-incrementing sample-ROM read used by
    // sample-carrying sets; sets without a "samples" region never touch it).
    inline constexpr std::uint16_t z80_port_ym2151_addr = 0x00U;
    inline constexpr std::uint16_t z80_port_ym2151_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;
    inline constexpr std::uint16_t z80_port_latch_ack = 0x06U;
    inline constexpr std::uint16_t z80_port_sample_addr_lo = 0x80U;
    inline constexpr std::uint16_t z80_port_sample_addr_hi = 0x81U;
    inline constexpr std::uint16_t z80_port_dac = 0x82U;
    inline constexpr std::uint16_t z80_port_sample_read = 0x84U;

    // The Z80's IM 0 vectors, jammed during IACK by the board's negative RST
    // buffer: the sound latch drives RST 18h, the YM2151 drives RST 28h; the
    // open-collector lines AND together when both are pending.
    inline constexpr std::uint8_t z80_rst_idle = 0xFFU;  // RST 38h (floating)
    inline constexpr std::uint8_t z80_rst_latch = 0xDFU; // RST 18h
    inline constexpr std::uint8_t z80_rst_ym = 0xEFU;    // RST 28h

    // Per-game board wiring the ROM-set declaration's name selects: the work
    // RAM base differs between M72 wiring variants, and each game carries its
    // factory DIP defaults.
    struct m72_board_params final {
        std::uint32_t work_ram_base{0xA0000U};
        std::uint16_t dip_default{0xFFFFU};
    };

    // The board parameters for a declared set name; the base-map defaults
    // when the name is unknown.
    [[nodiscard]] m72_board_params board_params_for(std::string_view set_name);

    // Heap-allocated, never-moved M72 board. Built like sega32x_system: chips
    // and RAM blocks as value members, the buses holding spans into them, the
    // port handlers capturing `this`.
    struct m72_system final {
        chips::cpu::v30 main_cpu;
        chips::cpu::z80 sound_cpu;
        chips::video::irem_m72_video video;
        chips::audio::ym2151 fm;
        chips::audio::dac8 dac;
        chips::bus_controller::pic_8259 pic;
        chips::cpu::mcs51 mcu; // live only when the set carries an "mcu" region
        topology::bus main_bus{20U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};
        topology::bus mcu_bus{16U, topology::endianness::little};

        // The loaded ROM set; the buses map spans into its region vectors, so
        // it must not be mutated after construction. A missing program region
        // is padded to full size with 0xFF so the maps stay valid.
        common::rom_set_image roms;

        m72_board_params params;

        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, palette_size> palette_a{};
        std::array<std::uint8_t, palette_size> palette_b{};
        std::array<std::uint8_t, vram_size> vram_a{};
        std::array<std::uint8_t, vram_size> vram_b{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        // The Z80's whole program/work RAM, shared with the V30 at 0xE0000.
        std::array<std::uint8_t, sound_ram_size> sound_ram{};

        std::uint8_t sound_latch{};
        std::uint8_t input_p1{0xFFU};     // active low
        std::uint8_t input_p2{0xFFU};     // active low
        std::uint8_t input_system{0xFFU}; // start/coin/service, active low
        std::uint16_t dip_switches{0xFFFFU};
        std::uint8_t control_register{}; // port 0x02 latch (coin/flip/blank/reset)
        std::array<std::uint8_t, 8> scroll_regs{};
        std::array<std::uint8_t, 2> raster_regs{};
        // The Z80's INT line is the OR of the pending sound latch and the
        // YM2151 IRQ; update_sound_irq() recomputes line + IM0 vector on
        // either change.
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

        explicit m72_system(common::rom_set_image image, m72_board_params board_params = {});
    };

    // The board's canonical ROM-set declaration skeleton: the program region
    // with its fixed size and no files. A game declaration copies it, fills
    // in the dump files (and reloads), and appends its graphics / sample
    // regions. There is no sound-program region -- the Z80 runs from the
    // shared RAM the V30 fills at boot.
    [[nodiscard]] common::rom_set_decl m72_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image,
                                                           m72_board_params board_params = {});

} // namespace mnemos::manifests::irem_m72
