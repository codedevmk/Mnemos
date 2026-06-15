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

    // CPS1 board, increment 2: the 68000 main CPU + the cps_a_b video chip, now
    // with the per-frame CPS-A -> video decode, the reg5 palette DMA, and the
    // vblank IRQ. The Z80 sound bus, YM2151/OKIM6295, IO/DIP inputs, sprite-latch
    // DMA, CPS-B protection read-back, and the player adapter all land in later
    // increments. The board maps the program ROM low across the 8 MiB program
    // window with the work / GFX RAM overlaid above it, and exposes the
    // CPS-A/CPS-B register windows so the video chip's logical state is reachable
    // from the 68K.

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

    // CPS-A output-register word indices (the chip latches one 16-bit value per
    // index). Each *_base word is a GFX-RAM name-table pointer the board decodes;
    // the scroll X/Y words are signed pixel offsets; reg17 is the video-control
    // latch (flip-screen + per-layer enables + row-scroll enable).
    inline constexpr std::size_t cps_a_obj_base = 0U;       // object/sprite table base
    inline constexpr std::size_t cps_a_scroll1_base = 1U;   // scroll1 name-table base
    inline constexpr std::size_t cps_a_scroll2_base = 2U;   // scroll2 name-table base
    inline constexpr std::size_t cps_a_scroll3_base = 3U;   // scroll3 name-table base
    inline constexpr std::size_t cps_a_rowscroll_base = 4U; // scroll2 row-scroll table base
    inline constexpr std::size_t cps_a_palette_base = 5U;   // palette source (reg5 DMA)
    inline constexpr std::size_t cps_a_scroll1_x = 6U;
    inline constexpr std::size_t cps_a_scroll1_y = 7U;
    inline constexpr std::size_t cps_a_scroll2_x = 8U;
    inline constexpr std::size_t cps_a_scroll2_y = 9U;
    inline constexpr std::size_t cps_a_scroll3_x = 10U;
    inline constexpr std::size_t cps_a_scroll3_y = 11U;
    inline constexpr std::size_t cps_a_rowscroll_offset = 16U; // row-scroll line bias
    inline constexpr std::size_t cps_a_video_control = 17U;    // flip/enables/row-scroll

    // GFX-RAM base decode: a CPS-A base word addresses GFX RAM as `reg << 8`; if
    // that lands inside the GFX-RAM window it is rebased to a GFX-RAM offset, else
    // it is taken as a raw `reg * 256` offset. Aligned variants mask to a power-of-
    // two boundary. The object table aligns to 0x800; the palette source to a page.
    inline constexpr std::uint32_t object_base_align = 0x0800U;

    // reg5 palette DMA: the GFX-RAM palette source is copied into the board's
    // palette buffer one page at a time, gated by the CPS-B palette-control mask.
    inline constexpr std::uint32_t palette_page_bytes = 0x400U;
    inline constexpr std::uint32_t palette_copy_pages = 6U;
    inline constexpr std::uint16_t palette_control_default = 0x003FU; // all 6 pages

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
        // The resolved CPS-B profile (the chip's legacy default when the id is not
        // in the census). Held so the palette DMA can read its page-control offset.
        chips::video::cps_a_b::cps_b_profile profile{};

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, gfx_ram_size> gfx_ram{};
        // The DMA-filled palette RAM the video mixer decodes. Filled by the reg5
        // palette DMA from GFX RAM; zero (black backdrop) until the first DMA.
        std::array<std::uint8_t, 0x4000> palette{};

        // The raw CPS-A register file ($800100-$80013F). run_frame() decodes these
        // into the video chip's scroll/object/control state each frame.
        std::array<std::uint16_t, cps_a_reg_count> cps_a_regs{};

        // Vblank IRQ bookkeeping (observable, not architectural). The video's
        // vblank callback raises the 68K level-6 autovector IRQ and bumps the
        // raise count; the CPU's IACK clears the line and bumps the ack count.
        std::uint64_t vblank_irq_raised{};
        std::uint64_t vblank_irq_acked{};

        explicit cps1_system(common::rom_set_image image, cps1_board_params board_params = {});

        // Tick the 68K and the video chip for one frame; video.framebuffer() then
        // holds the rendered frame. The CPS-A latch is decoded to the video at
        // frame start, the video renders + raises the vblank IRQ on entering
        // vblank, and the CPU runs across the frame so the IRQ is serviced. Exact
        // CPU<->beam cycle sync is a later increment.
        void run_frame();

      private:
        // Decode the raw CPS-A latch into the video chip's logical state (scroll/
        // object bases, scroll offsets, row-scroll, video-control, display enable).
        void push_cps_a_to_video() noexcept;
        // Copy the palette from GFX RAM into `palette`, page-gated by the active
        // CPS-B profile's palette-control register. Triggered by a reg5 write.
        void copy_palette_from_gfx_ram() noexcept;
        // GFX-RAM base decode shared by the CPS-A pointer registers.
        [[nodiscard]] std::uint32_t gfx_ram_base_from_reg(std::uint16_t reg) const noexcept;
        [[nodiscard]] std::uint32_t gfx_ram_base_aligned(std::uint16_t reg,
                                                         std::uint32_t boundary) const noexcept;
    };

    // The board's canonical ROM-set declaration skeleton: the program region with
    // its fixed size and no files. A game declaration copies it, fills in the dump
    // files, and appends its graphics region.
    [[nodiscard]] common::rom_set_decl cps1_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<cps1_system> assemble_cps1(common::rom_set_image image,
                                                             cps1_board_params board_params = {});

} // namespace mnemos::manifests::capcom_cps1
