#pragma once

#include "bus.hpp"            // topology bus
#include "cps_a_b.hpp"        // CPS-A/CPS-B custom video
#include "cps_b_profiles.hpp" // hardware-keyed CPS-B profile census
#include "m68000.hpp"         // main CPU
#include "rom_set.hpp"        // arcade ROM-set image

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mnemos::manifests::capcom_cps1 {

    // CPS1 board, increment 1: the 68000 main CPU + the cps_a_b video chip only.
    // The Z80 sound bus, YM2151/OKIM6295, vblank IRQ, palette/sprite DMA, IO/DIP
    // inputs, and the player adapter all land in later increments. The board maps
    // the program ROM low across the 8 MiB program window with the work / GFX RAM
    // overlaid above it, and exposes the CPS-A/CPS-B register windows so the video
    // chip's logical state is reachable from the 68K.

    // Program ROM window: $000000-$7FFFFF (the 68K's lower 8 MiB; absent dumps
    // read 0xFF). Work RAM: $FF0000-$FFFFFF (64 KiB). Unified GFX RAM (tilemap
    // name tables, row-scroll, object list): $900000-$92FFFF (192 KiB).
    inline constexpr std::uint32_t program_rom_base = 0x000000U;
    inline constexpr std::size_t program_rom_window = 0x800000U;
    inline constexpr std::uint32_t work_ram_base = 0xFF0000U;
    inline constexpr std::size_t work_ram_size = 0x10000U;
    inline constexpr std::uint32_t gfx_ram_base = 0x900000U;
    inline constexpr std::size_t gfx_ram_size = 0x30000U;

    // CPS-A register file ($800100-$80013F, word-spaced) and CPS-B register file
    // ($800140-$80017F, word-spaced) write windows. The board writes the raw
    // 32-word files; the video chip's active profile interprets the CPS-B side.
    inline constexpr std::uint32_t cps_a_reg_base = 0x800100U;
    inline constexpr std::uint32_t cps_a_reg_size = 0x40U;
    inline constexpr std::uint32_t cps_b_reg_base = 0x800140U;
    inline constexpr std::uint32_t cps_b_reg_size = 0x40U;
    inline constexpr std::size_t cps_a_reg_count = 32U;

    // Canonical ROM-set region names. The board fixes only the program region's
    // size; the graphics region size varies per board, so a set declaration
    // appends it.
    inline constexpr std::size_t main_rom_size = program_rom_window;

    // Per-board wiring the ROM-set declaration's name selects. Increment 1 carries
    // only the CPS-B profile id (a board / PAL identity, never a game name; 0 =
    // the chip's legacy default profile). Later increments add DIP defaults etc.
    struct cps1_board_params final {
        std::uint16_t cps_b_profile_id{0U};
    };

    // The board parameters for a declared set name; the default params when the
    // name is unknown.
    [[nodiscard]] cps1_board_params board_params_for(std::string_view set_name);

    // Heap-allocated, never-moved CPS1 board: chips and RAM blocks as value
    // members, the bus holding spans into them, the MMIO closures capturing
    // `this`. The loaded ROM set backs the bus spans, so it must not be mutated
    // after construction.
    struct cps1_system final {
        chips::cpu::m68000 main_cpu;
        chips::video::cps_a_b video;
        topology::bus main_bus{24U, topology::endianness::big};

        common::rom_set_image roms;
        cps1_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, gfx_ram_size> gfx_ram{};
        // The DMA-filled palette RAM the video mixer decodes. Increment 1 has no
        // reg5 palette DMA, so it stays zero -> a black backdrop.
        std::array<std::uint8_t, 0x4000> palette{};

        // The raw CPS-A register file ($800100-$80013F). CPS-A scroll/object
        // decode off these registers lands in a later increment; held as data now.
        std::array<std::uint16_t, cps_a_reg_count> cps_a_regs{};

        explicit cps1_system(common::rom_set_image image, cps1_board_params board_params = {});

        // Tick the 68K and the video chip for one frame; video.framebuffer()
        // then holds the rendered frame. Exact CPU<->beam sync is a later
        // increment -- decoupled per-frame ticking is sufficient here.
        void run_frame();
    };

    // The board's canonical ROM-set declaration skeleton: the program region with
    // its fixed size and no files. A game declaration copies it, fills in the dump
    // files, and appends its graphics region.
    [[nodiscard]] common::rom_set_decl cps1_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<cps1_system> assemble_cps1(common::rom_set_image image,
                                                             cps1_board_params board_params = {});

} // namespace mnemos::manifests::capcom_cps1
