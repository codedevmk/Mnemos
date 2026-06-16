#pragma once

#include "bus.hpp"            // topology bus
#include "cps_a_b.hpp"        // CPS-A/CPS-B custom video
#include "cps_b_profiles.hpp" // hardware-keyed CPS-B profile census
#include "eeprom_93c46.hpp"   // serial NVRAM (QSound 3/4-player boards)
#include "kabuki.hpp"         // encrypted-Z80 (QSound sound CPU) key + decode
#include "m68000.hpp"         // main CPU
#include "okim6295.hpp"       // ADPCM sample voice (sound board)
#include "qsound.hpp"         // DL-1425 16-voice PCM mixer (QSound sound board)
#include "rom_set.hpp"        // arcade ROM-set image
#include "ym2151.hpp"         // FM synth (sound board)
#include "z80.hpp"            // sound CPU

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::manifests::capcom_cps1 {

    // CPS1 board, increment 4: the 68000 main CPU + the cps_a_b video chip (the
    // increment-2 per-frame CPS-A -> video decode, reg5 palette DMA, and vblank
    // IRQ) plus the Z80 sound subsystem -- a sound Z80 driving a YM2151 + an
    // OKIM6295 on its own little-endian bus, fed sound commands the 68K writes to
    // $800180/$800188 and read back by the Z80 at $F008/$F00A. Increment 4 adds
    // the controls / DIP read windows ($800000 player word, $800018 system+DIP),
    // the coin-control write stub ($800030), the per-frame sprite-latch DMA, and
    // the CPS-B protection read-back (board ID + 16x16 multiplier) decoded through
    // the active profile. The romset TOML key and the player adapter land in later
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

    // The CP1B1F board wires a serial 93C46 EEPROM to the CPS-B chip at $80017A
    // (inside the CPS-B register window). Only boards whose active profile sets
    // cps_b_eeprom route this word to the NVRAM device; the low (odd) byte
    // carries the EEPROM control lines, the read returns DO in bit0.
    inline constexpr std::uint32_t cps_b_eeprom_addr = 0x80017AU;

    // Controls / DIP read windows. The player-input word (P2 high byte, P1 low
    // byte) mirrors across $800000-$800007; the system word + the three DIP-switch
    // words sit at $800018-$80001F (word 0 = system inputs, words 1-3 = DIP A/B/C,
    // each as `value << 8 | 0xFF`). Coin counters/lockout latch at $800030-$800037
    // (a write-only stub here; counters are not modelled in v1).
    inline constexpr std::uint32_t player_input_base = 0x800000U;
    inline constexpr std::uint32_t player_input_window = 0x08U;
    inline constexpr std::uint32_t system_dsw_base = 0x800018U;
    inline constexpr std::uint32_t system_dsw_window = 0x08U;
    inline constexpr std::uint32_t coin_control_base = 0x800030U;
    inline constexpr std::uint32_t coin_control_window = 0x08U;

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
    // two boundary. The object table aligns to 0x800; the scroll name-table bases
    // to 0x4000; the row-scroll table base to 0x800; the palette source to a page.
    inline constexpr std::uint32_t object_base_align = 0x0800U;
    inline constexpr std::uint32_t scroll_base_align = 0x4000U;
    inline constexpr std::uint32_t other_base_align = 0x0800U;

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

    // --- QSound sound subsystem (DL-1425 path; replaces YM2151 + OKIM6295) ---
    // The QSound sound board is a Z80 (often Kabuki-encrypted) on a 16-bit
    // little-endian bus: program ROM ($0000-$7FFF, Kabuki opcode/data split when
    // encrypted) + a 16-bank 16 KiB window ($8000-$BFFF) from $10000 in the sound
    // ROM + two 4 KiB shared-RAM banks the 68K and Z80 use to pass commands +
    // the DL-1425 port window. The DSP is HLE-mixed; the board drains it at the
    // chip's ~24 kHz stereo output rate in the adapter.
    inline constexpr std::uint16_t qsound_shared0_base = 0xC000U; // Z80 shared bank 0
    inline constexpr std::uint16_t qsound_shared1_base = 0xF000U; // Z80 shared bank 1
    inline constexpr std::size_t qsound_shared_size = 0x1000U;    // 4 KiB per bank
    inline constexpr std::uint16_t qsound_port_base = 0xD000U;    // DSP / bank reg window
    inline constexpr std::uint32_t qsound_port_window = 0x800U;   // $D000-$D7FF
    inline constexpr std::uint16_t qsound_bank_reg = 0xD003U;     // banked-window select (W)
    inline constexpr std::uint16_t qsound_ready_reg = 0xD007U;    // DSP ready flag (R)
    inline constexpr std::uint8_t qsound_bank_mask = 0x0FU;       // 16 banks
    inline constexpr std::uint32_t qsound_bank_base = 0x10000U;   // first banked slice
    inline constexpr std::uint32_t qsound_comm0_base = 0xF18000U; // 68K -> shared bank 0
    inline constexpr std::uint32_t qsound_comm1_base = 0xF1E000U; // 68K -> shared bank 1
    inline constexpr std::uint32_t qsound_comm_window = 0x2000U;  // 0x1000 words each
    inline constexpr std::uint32_t qsound_z80_clock_hz = 8'000'000U;
    inline constexpr std::uint32_t qsound_irq_hz = 250U; // periodic sound-CPU /INT

    // C-board I/O on the 3/4-player QSound boards (slammast / mbombrd): player 3/4
    // inputs + a serial 93C46 EEPROM, at $F1C000-$F1C007. The EEPROM lines sit in
    // the low (odd) byte of the $F1C006 word: DI bit0, CLK bit6, CS bit7; the read
    // returns the device DO in bit0. 2-player QSound sets never touch this window.
    inline constexpr std::uint32_t qsound_io_base = 0xF1C000U;
    inline constexpr std::uint32_t qsound_io_window = 0x08U;
    inline constexpr std::uint32_t qsound_in2_addr = 0xF1C000U;    // player 3
    inline constexpr std::uint32_t qsound_in3_addr = 0xF1C002U;    // player 4
    inline constexpr std::uint32_t qsound_coin2_addr = 0xF1C004U;  // coin control 2 (stub)
    inline constexpr std::uint32_t qsound_eeprom_addr = 0xF1C006U; // serial 93C46

    // Serial 93C46 pin mapping in the EEPROM control byte, shared by the QSound
    // C-board port ($F1C006) and the CP1B1F board's CPS-B port ($80017A): DI
    // bit0, CLK bit6, CS bit7; the read returns the device DO in bit0.
    inline constexpr std::uint8_t eeprom_di = 0x01U;
    inline constexpr std::uint8_t eeprom_clk = 0x40U;
    inline constexpr std::uint8_t eeprom_cs = 0x80U;

    // Which sound board the set is wired for: the YM2151 + OKIM6295 path (the
    // CPS1 default) or the QSound DL-1425 path (later CPS1 + all of CPS2).
    enum class sound_system : std::uint8_t { okim6295, qsound };

    // Per-board wiring the ROM-set declaration's name selects. Increment 1 carries
    // only the CPS-B profile id (a board / PAL identity, never a game name; 0 =
    // the chip's legacy default profile). Later increments add DIP defaults etc.
    struct cps1_board_params final {
        std::uint16_t cps_b_profile_id{0U};
        // Vertical (TATE) cabinet: the frontend rotates the framebuffer upright.
        // Presentation only -- the board renders the same 384x224 field either way.
        bool vertical{false};
        // The sound board (default = the YM2151 + OKIM6295 path).
        sound_system sound{sound_system::okim6295};
        // When the QSound sound CPU is Kabuki-encrypted, which game's keys decrypt
        // its program ($0000-$7FFF); absent => the program is plain.
        std::optional<kabuki_game> kabuki{};
    };

    // The board parameters for a declared set name; the default params when the
    // name is unknown.
    [[nodiscard]] cps1_board_params board_params_for(std::string_view set_name);

    // The board parameters carried by a parsed ROM-set declaration: the optional
    // `[set] cps_b_profile` id selects the CPS-B board / PAL profile (absent => 0,
    // the chip's legacy default). The single source of truth the player adapter
    // uses to thread the TOML profile id into assemble_cps1.
    [[nodiscard]] cps1_board_params board_params_from_decl(const common::rom_set_decl& decl);

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
        chips::audio::qsound qdsp; // QSound DL-1425 (idle unless params.sound == qsound)
        // Serial 93C46 NVRAM at $F1C006 on the QSound 3/4-player boards (slammast /
        // mbombrd); idle on every other set. The board wires the ORG pin low (8-bit
        // org: 128 x 8). Init 0xFF (blank) -> the game writes its defaults on boot.
        chips::storage::eeprom_93c46 eeprom{chips::storage::eeprom_93c46::organization::byte8};
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

        // Controls / DIP state the read windows expose. Active-low (all-released =
        // 0xFF per byte); the Phase-6 player adapter drives these. `input_p` packs
        // P2 in the high byte and P1 in the low byte; `input_sys` is the system
        // word; `dip_a/b/c` are the three DIP-switch bytes.
        std::uint16_t input_p{0xFFFFU};
        std::uint16_t input_sys{0xFFFFU};
        std::uint8_t dip_a{0xFFU};
        std::uint8_t dip_b{0xFFU};
        std::uint8_t dip_c{0xFFU};
        // Coin-control latch ($800030-$800037 high byte). Stub: counters/lockout
        // are not modelled in v1, so writes only land here for traceability.
        std::uint8_t coin_control{};

        // Z80 sound work RAM ($D000-$D7FF).
        std::array<std::uint8_t, z80_ram_size> z80_ram{};

        // Sound-command latches. The 68K writes $800180 (primary) / $800188
        // (secondary); the Z80 reads them at $F008 / $F00A. `sound_latch_pending`
        // mirrors the hardware's latch-armed flag (cleared on the Z80's read). The
        // primary latch powers on to 0xFF (its hardware reset value).
        std::uint8_t sound_latch{0xFFU};
        std::uint8_t sound_latch2{};
        bool sound_latch_pending{};
        // The $F004 bank register (one bit selects the banked $8000 window). On the
        // QSound path the $D003 register reuses this with a 16-bank (0x0F) mask.
        std::uint8_t sound_bank{};
        // The size of the loaded sound program ROM; selects the bank-window math.
        std::uint32_t sound_rom_size{};

        // --- QSound-only state (unused on the YM2151 + OKIM6295 path) ---
        // The two 4 KiB shared-RAM banks the 68K and the sound Z80 pass commands
        // through ($C000 / $F000 on the Z80, $F18000 / $F1E000 on the 68K).
        std::array<std::array<std::uint8_t, qsound_shared_size>, 2> qsound_shared{};
        // Kabuki decode buffers: the decrypted opcode (M1-fetch) and data (read)
        // streams over the encrypted low $0000-$7FFF program window. The sound bus
        // serves the opcode stream via fetch_opcode8 and the data stream via read8;
        // empty (and unused) when the program is plain. Heap-backed so the bus
        // spans into them stay valid for the never-moved board's lifetime.
        std::vector<std::uint8_t> sound_opcode_rom;
        std::vector<std::uint8_t> sound_data_rom;

        [[nodiscard]] bool uses_qsound() const noexcept {
            return params.sound == sound_system::qsound;
        }

        // Vblank IRQ bookkeeping (observable, not architectural). The video's
        // vblank callback raises the 68K level-2 autovector ($68) IRQ and bumps the
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

        // Drive the controls / DIP state the read windows expose (active-low). The
        // Phase-6 player adapter calls this; tests may also set the members.
        void set_inputs(std::uint16_t player, std::uint16_t system, std::uint8_t dsw_a,
                        std::uint8_t dsw_b, std::uint8_t dsw_c) noexcept {
            input_p = player;
            input_sys = system;
            dip_a = dsw_a;
            dip_b = dsw_b;
            dip_c = dsw_c;
        }

      private:
        // The banked-window base into the sound ROM for the current bank.
        [[nodiscard]] std::uint32_t z80_bank_rom_base() const noexcept;
        // Run the 68K + sound Z80 + sound chips for `cpu_cycles` of 68K time,
        // interleaved in slices at the 68K:Z80 clock ratio.
        void run_cpus(std::uint64_t cpu_cycles);

        // Build the QSound sound subsystem (the Kabuki-decoded sound-Z80 program
        // bus, the DL-1425 port window, the two shared-RAM comm banks + the 68K
        // comm windows). Called from the ctor in place of the YM2151 + OKIM6295
        // path when params.sound == qsound.
        void setup_qsound();
        // The QSound run path: the 68K + the 8 MHz sound Z80 (with its 250 Hz
        // periodic /INT), no YM2151 / OKIM6295. Interleaved like run_cpus.
        void run_qsound_cpus(std::uint64_t cpu_cycles);

        // Fractional-cycle accumulators that keep the long-run clock ratios exact
        // across frames: 68K cycles -> Z80 cycles, and Z80 cycles -> OKI cycles.
        std::uint64_t cpu_cycle_accum_{};
        std::uint64_t oki_cycle_accum_{};
        std::uint64_t qsound_irq_accum_{}; // 250 Hz periodic sound-CPU /INT pacing
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

        // CPS-B window word read decoded through the active profile: the board-ID
        // value, the 16x16 multiplier result (lo/hi), else the raw register. The
        // 68K can only read the board-specific ID / protection ports; raster
        // read-back is out of v1 (returns the raw register at those offsets).
        [[nodiscard]] std::uint16_t cps_b_read_word(std::uint8_t offset) const noexcept;
        // The system / DIP word at a given word offset (0 = system inputs, 1-3 =
        // DIP A/B/C), as `byte << 8 | 0xFF` to match the hardware lane layout.
        [[nodiscard]] std::uint16_t system_dsw_word(std::uint32_t word_offset) const noexcept;
    };

    // The board's canonical ROM-set declaration skeleton: the program region with
    // its fixed size and no files. A game declaration copies it, fills in the dump
    // files, and appends its graphics region.
    [[nodiscard]] common::rom_set_decl cps1_rom_skeleton(std::string set_name);

    [[nodiscard]] std::unique_ptr<cps1_system> assemble_cps1(common::rom_set_image image,
                                                             cps1_board_params board_params = {});

} // namespace mnemos::manifests::capcom_cps1
