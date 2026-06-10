#pragma once

#include "bus.hpp"     // topology bus
#include "rom_set.hpp" // arcade ROM-set image
#include "v30.hpp"     // main CPU
#include "z80.hpp"     // sound CPU

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

    // Z80-side ports (first-cut): YM2151 at 0/1 (lands in the audio phase),
    // the sound latch readable at 2.
    inline constexpr std::uint16_t z80_port_ym2151_addr = 0x00U;
    inline constexpr std::uint16_t z80_port_ym2151_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;

    // Heap-allocated, never-moved M72 board. Built like sega32x_system: chips
    // and RAM blocks as value members, the buses holding spans into them, the
    // port handlers capturing `this`.
    //
    // Built so far: V30 + Z80 on their buses, the program ROM regions out of a
    // loaded rom_set_image, the RAM overlays, the main->sound latch (write
    // asserts the Z80 INT line, the Z80's latch read clears it), and the
    // input/DIP port reads off plain state bytes. Still to come: the tilemap/
    // sprite video unit + raster IRQs (phase C), YM2151 + DAC + the sample
    // port (phase D), real input/DIP devices (phase D).
    struct m72_system final {
        chips::cpu::v30 main_cpu;
        chips::cpu::z80 sound_cpu;
        topology::bus main_bus{20U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};

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

        explicit m72_system(common::rom_set_image image);
    };

    // The board's canonical ROM-set declaration skeleton: the two program
    // regions with their fixed sizes and no files. A game declaration copies
    // it, fills in the dump files (and reloads), and appends its graphics /
    // sample regions.
    [[nodiscard]] common::rom_set_decl m72_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image);

} // namespace mnemos::manifests::irem_m72
