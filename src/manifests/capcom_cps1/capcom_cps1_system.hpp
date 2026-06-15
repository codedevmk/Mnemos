#pragma once

#include "bus.hpp"            // topology bus
#include "cps_a_b.hpp"        // CPS-A/CPS-B custom video
#include "cps_b_profiles.hpp" // hardware-keyed CPS-B profile census
#include "m68000.hpp"         // main CPU
#include "okim6295.hpp"       // ADPCM sample voice (sound board)
#include "rom_set.hpp"        // arcade ROM-set image
#include "ym2151.hpp"         // FM synth (sound board)
#include "z80.hpp"            // sound CPU

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mnemos::manifests::capcom_cps1 {

    // CPS1 board, increment 3: the 68000 main CPU + the cps_a_b video chip (the
    // increment-2 per-frame CPS-A -> video decode, reg5 palette DMA, and vblank
    // IRQ) plus the Z80 sound subsystem -- a sound Z80 driving a YM2151 + an
    // OKIM6295 on its own little-endian bus, fed sound commands the 68K writes to
    // $800180/$800188 and read back by the Z80 at $F008/$F00A. IO/DIP inputs,
    // sprite-latch DMA, CPS-B protection read-back, the romset TOML key, and the
    // player adapter all land in later increments. The board maps the program ROM
    // low across the 8 MiB program window with the work / GFX RAM overlaid above
    // it, and exposes the CPS-A/CPS-B register windows so the video chip's
    // logical state is reachable from the 68K.

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

    // --- Z80 sound subsystem (YM2151 + OKIM6295 path) ---
    // The sound board is a Z80 on a 16-bit little-endian bus: program ROM
    // ($0000-$7FFF) + a banked 16 KiB window ($8000-$BFFF) into the upper sound
    // ROM + 2 KiB work RAM ($D000-$D7FF) + the memory-mapped sound I/O at $F000+.
    inline constexpr std::uint16_t z80_rom_base = 0x0000U;
    inline constexpr std::uint32_t z80_rom_window = 0x8000U; // fixed low 32 KiB
    inline constexpr std::uint16_t z80_bank_base = 0x8000U;
    inline constexpr std::uint32_t z80_bank_window = 0x4000U; // 16 KiB banked
    inline constexpr std::uint16_t z80_ram_base = 0xD000U;
    inline constexpr std::size_t z80_ram_size = 0x800U; // 2 KiB
    inline constexpr std::uint16_t z80_io_base = 0xF000U;
    inline constexpr std::uint32_t z80_io_window = 0x1000U; // $F000-$FFFF

    // Sound I/O port offsets within the $F000 window.
    inline constexpr std::uint16_t z80_io_ym_addr = 0xF000U;  // YM2151 address (W) / status (R)
    inline constexpr std::uint16_t z80_io_ym_data = 0xF001U;  // YM2151 data (W) / status (R)
    inline constexpr std::uint16_t z80_io_oki = 0xF002U;      // OKIM6295 command (W) / status (R)
    inline constexpr std::uint16_t z80_io_bank = 0xF004U;     // sound ROM bank select (W)
    inline constexpr std::uint16_t z80_io_oki_pin7 = 0xF006U; // OKIM6295 pin-7 clock select (W)
    inline constexpr std::uint16_t z80_io_latch = 0xF008U;    // primary sound latch (R)
    inline constexpr std::uint16_t z80_io_latch2 = 0xF00AU;   // secondary sound latch (R)

    // The non-QSound bank register is a single bit (two banks). Banked window
    // base into the sound ROM: real ZIP-loaded sets carry two 16 KiB banks from
    // $10000; smaller compact program ROMs are laid out linearly from $8000.
    inline constexpr std::uint8_t z80_bank_mask = 0x01U;
    inline constexpr std::uint32_t z80_bank_split_threshold = 0x18000U;
    inline constexpr std::uint32_t z80_bank_base_large = 0x10000U;
    inline constexpr std::uint32_t z80_bank_base_small = 0x8000U;

    // 68K-side sound-command latches the Z80 reads at $F008/$F00A. A byte/word
    // write to $800180 sets the primary latch (and arms it pending); a write to
    // $800188 (low byte) sets the secondary timer/fade latch.
    inline constexpr std::uint32_t sound_latch_addr = 0x800180U;
    inline constexpr std::uint32_t sound_latch2_addr = 0x800188U;
    inline constexpr std::uint32_t sound_latch_window = 0x10U; // $800180-$80018F

    // Clocks: 68K ~10 MHz, sound Z80 ~3.579545 MHz. The OKIM6295 runs from a
    // 1 MHz input clock (its chip default); pin-7 high = input/132.
    inline constexpr std::uint32_t m68k_clock_hz = 10'000'000U;
    inline constexpr std::uint32_t z80_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t oki_clock_hz = 1'000'000U;
    inline constexpr std::uint32_t frame_rate_hz = 60U;

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
        chips::cpu::z80 sound_cpu;
        chips::video::cps_a_b video;
        chips::audio::ym2151 fm;
        chips::audio::okim6295 oki;
        topology::bus main_bus{24U, topology::endianness::big};
        topology::bus sound_bus{16U, topology::endianness::little};

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

        // Z80 sound work RAM ($D000-$D7FF).
        std::array<std::uint8_t, z80_ram_size> z80_ram{};

        // Sound-command latches. The 68K writes $800180 (primary) / $800188
        // (secondary); the Z80 reads them at $F008 / $F00A. `sound_latch_pending`
        // mirrors the hardware's latch-armed flag (cleared on the Z80's read).
        std::uint8_t sound_latch{};
        std::uint8_t sound_latch2{};
        bool sound_latch_pending{};
        // The $F004 bank register (one bit selects the banked $8000 window).
        std::uint8_t sound_bank{};
        // The size of the loaded sound program ROM; selects the bank-window math.
        std::uint32_t sound_rom_size{};

        // Vblank IRQ bookkeeping (observable, not architectural). The video's
        // vblank callback raises the 68K level-6 autovector IRQ and bumps the
        // raise count; the CPU's IACK clears the line and bumps the ack count.
        std::uint64_t vblank_irq_raised{};
        std::uint64_t vblank_irq_acked{};

        explicit cps1_system(common::rom_set_image image, cps1_board_params board_params = {});

        // Tick the 68K, the sound Z80, the YM2151 + OKIM6295, and the video chip
        // for one frame; video.framebuffer() then holds the rendered frame. The
        // CPS-A latch is decoded to the video at frame start; the two CPUs and the
        // sound chips advance together in interleaved slices at the 68K:Z80 clock
        // ratio; the video renders + raises the vblank IRQ on entering vblank and
        // the CPU runs across the frame so the IRQ is serviced. Exact CPU<->beam
        // cycle sync is a later increment.
        void run_frame();

        // Recompute the Z80 sound INT from the YM2151's timer-IRQ line (the chip
        // owns the (timerA & enA) | (timerB & enB) edge; this routes it to /INT).
        void sync_sound_irq() noexcept;

      private:
        // The banked-window base into the sound ROM for the current bank.
        [[nodiscard]] std::uint32_t z80_bank_rom_base() const noexcept;
        // Run the 68K + sound Z80 + sound chips for `cpu_cycles` of 68K time,
        // interleaved in slices at the 68K:Z80 clock ratio.
        void run_cpus(std::uint64_t cpu_cycles);

        // Fractional-cycle accumulators that keep the long-run clock ratios exact
        // across frames: 68K cycles -> Z80 cycles, and Z80 cycles -> OKI cycles.
        std::uint64_t cpu_cycle_accum_{};
        std::uint64_t oki_cycle_accum_{};
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
