#ifndef MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
#define MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP

// CPS-2 board assembler. Loads the encrypted 68000 program, builds the decrypted
// opcode image with the phase-1 cipher, maps the encrypted ROM for data reads +
// the decrypted image as the opcode overlay (the phase-3 m68000 split), wires the
// full 68000 memory map (RAM / CPS-A-B registers / control / I-O / 93C46 EEPROM),
// and the QSound sound subsystem (Z80 + the 68K<->Z80 comm RAM + the DL-1425 DSP).
// Boots the 68000 from the decrypted reset vector and drives the shared CPS-A/B
// video chip at frame-vblank. Remaining: precise QSound /INT cadence and audio
// mixing.

#include "cps2_crypto.hpp"
#include "cps2_video.hpp"
#include "eeprom_93c46.hpp"
#include "m68000.hpp"
#include "qsound.hpp"
#include "rom_set.hpp"
#include "z80.hpp"

#include "bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::manifests::capcom_cps2 {

    // 68000 memory map (24-bit, big-endian), transcribed from the reference core.
    inline constexpr std::uint32_t program_base = 0x000000U;     // encrypted program ROM
    inline constexpr std::uint32_t control_reg_base = 0x400000U; // 16-byte CPS-2 control regs
    inline constexpr std::size_t control_reg_size = 0x10U;
    inline constexpr std::uint32_t qsound_shared_base = 0x618000U; // 68K side of the comm RAM
    inline constexpr std::size_t qsound_shared_window = 0x2000U;   // 8 KiB (odd-byte into 4 KiB)
    inline constexpr std::size_t qsound_shared_size = 0x1000U;     // the 4 KiB comm RAM
    inline constexpr std::uint32_t sound_reset_port = 0x804041U;   // 68K: bit3 holds Z80 in reset

    // Z80 sound-CPU map (16-bit, little-endian).
    inline constexpr std::uint16_t z80_rom_base = 0x0000U;
    inline constexpr std::uint32_t z80_rom_window = 0x8000U; // fixed low 32 KiB
    inline constexpr std::uint16_t z80_bank_base = 0x8000U;
    inline constexpr std::uint32_t z80_bank_window = 0x4000U;    // 16 KiB banked window
    inline constexpr std::uint32_t z80_bank_rom_base = 0x10000U; // banks start here in the ROM
    inline constexpr std::uint8_t z80_bank_mask = 0x0FU;         // 16 banks ($D003)
    inline constexpr std::uint16_t z80_shared_base = 0xC000U;    // 68K<->Z80 comm RAM (4 KiB)
    inline constexpr std::uint16_t z80_ram_base = 0xD000U;       // 2 KiB scratch RAM
    inline constexpr std::uint32_t z80_ram_window = 0x800U;
    inline constexpr std::uint16_t z80_port_base = 0xD000U; // $D000-$D002 = DL-1425 ports
    inline constexpr std::uint16_t z80_bank_reg = 0xD003U;  // banked-window select (W)
    inline constexpr std::uint16_t z80_ready_reg = 0xD007U; // DSP ready flag (R)
    inline constexpr std::uint16_t z80_work_base = 0xF000U; // 4 KiB work RAM
    inline constexpr std::uint32_t z80_work_window = 0x1000U;
    inline constexpr std::uint32_t extra_ram_base = 0x660000U;
    inline constexpr std::size_t extra_ram_size = 0x4000U; // 16 KiB
    inline constexpr std::uint32_t extra_ctrl_base = 0x664000U;
    inline constexpr std::size_t extra_ctrl_size = 0x2U;
    inline constexpr std::uint32_t object_ram_base = 0x700000U; // object/sprite RAM
    inline constexpr std::size_t object_ram_size = 0x10000U;    // 64 KiB (banks fold here)
    inline constexpr std::uint32_t cps_a_base = 0x804100U;      // CPS-A register window
    inline constexpr std::uint32_t cps_b_base = 0x804140U;      // CPS-B register window
    inline constexpr std::uint32_t cps_a_mirror_base = 0x800100U;
    inline constexpr std::uint32_t cps_b_mirror_base = 0x800140U;
    inline constexpr std::size_t cps_reg_block = 0x40U;     // bytes per CPS-A / CPS-B window
    inline constexpr std::size_t cps_reg_size = 0x200U;     // the backing register file
    inline constexpr std::uint32_t cps_io_base = 0x804000U; // inputs / EEPROM / volume / control
    inline constexpr std::size_t cps_io_size = 0x100U;
    inline constexpr std::uint32_t video_ram_base = 0x900000U; // tile/attribute RAM
    inline constexpr std::size_t video_ram_size = 0x30000U;    // 192 KiB
    inline constexpr std::uint32_t main_ram_base = 0xFF0000U;  // 64 KiB work RAM
    inline constexpr std::size_t main_ram_size = 0x10000U;     // (0xFF0000-0xFFFFFF)

    // CPS-A output-register word indices. CPS-2 keeps tile RAM and object RAM in
    // separate 68K windows, but the scroll/row-scroll/palette register decode is
    // shared with CPS-1.
    inline constexpr std::size_t cps_a_reg_count = 32U;
    inline constexpr std::size_t cps_a_obj_base = 0U;
    inline constexpr std::size_t cps_a_scroll1_base = 1U;
    inline constexpr std::size_t cps_a_scroll2_base = 2U;
    inline constexpr std::size_t cps_a_scroll3_base = 3U;
    inline constexpr std::size_t cps_a_rowscroll_base = 4U;
    inline constexpr std::size_t cps_a_palette_base = 5U;
    inline constexpr std::size_t cps_a_scroll1_x = 6U;
    inline constexpr std::size_t cps_a_scroll1_y = 7U;
    inline constexpr std::size_t cps_a_scroll2_x = 8U;
    inline constexpr std::size_t cps_a_scroll2_y = 9U;
    inline constexpr std::size_t cps_a_scroll3_x = 10U;
    inline constexpr std::size_t cps_a_scroll3_y = 11U;
    inline constexpr std::size_t cps_a_rowscroll_offset = 16U;
    inline constexpr std::size_t cps_a_video_control = 17U;

    inline constexpr std::uint32_t object_base_align = 0x0800U;
    inline constexpr std::uint32_t scroll_base_align = 0x4000U;
    inline constexpr std::uint32_t other_base_align = 0x0800U;
    inline constexpr std::uint32_t palette_page_bytes = 0x400U;
    inline constexpr std::uint32_t palette_copy_pages = 6U;
    inline constexpr std::uint16_t palette_control_default = 0x003FU; // all 6 pages

    // EEPROM pin bits at the $804040 write port, and the data-out bit on input 2.
    inline constexpr std::uint8_t eeprom_di_bit = 0x10U;
    inline constexpr std::uint8_t eeprom_clk_bit = 0x20U;
    inline constexpr std::uint8_t eeprom_cs_bit = 0x40U;
    inline constexpr std::uint16_t qsound_volume_status = 0xE021U;
    inline constexpr std::uint32_t m68k_clock_hz = 11'800'000U;
    inline constexpr std::uint32_t frame_rate_hz = 60U;

    struct cps2_board_params final {
        // The 20-byte board key (an external asset, never committed). Without it
        // the program cannot be decrypted and the machine is not executable.
        std::optional<std::array<std::uint8_t, crypto_key_size>> key;
    };

    // Assembled CPS-2 machine. Never moved after construction: the bus and video
    // chip hold spans into owned ROM/RAM/palette storage.
    class cps2_system final {
      public:
        explicit cps2_system(common::rom_set_image image, cps2_board_params params = {});

        cps2_system(const cps2_system&) = delete;
        cps2_system& operator=(const cps2_system&) = delete;

        // True when a valid key decrypted the program -- i.e. the 68000 is running
        // real opcodes. False (a "missing key" blocker) leaves the opcode image as
        // the raw encrypted bytes; the board must not be treated as running.
        [[nodiscard]] bool executable() const noexcept { return executable_; }

        // De-scramble a run of CPS-2 gfx (a multiple of 8 bytes, a power-of-two
        // count of 8-byte units) in place: the mask ROMs load word/byte-lane
        // interleaved, then each bank is recursively unshuffled into the linear
        // tile layout the decoder reads. The board applies it per 0x200000 bank at
        // load; exposed for unit testing on a small bank.
        static void unshuffle_gfx_units(std::span<std::uint8_t> units) noexcept;

        // Run whole 68000 instructions until at least `cycles` have elapsed.
        void run_cycles(std::uint64_t cycles);
        // Tick one 60 Hz field: decode CPS-A latches, run the visible CPU slice
        // (including QSound when released), render at vblank, latch sprites for
        // the next frame, then finish vblank.
        void run_frame();

        [[nodiscard]] chips::cpu::m68000& cpu() noexcept { return main_cpu; }
        [[nodiscard]] topology::bus& bus() noexcept { return main_bus; }
        [[nodiscard]] chips::storage::eeprom_93c46& eeprom() noexcept { return eeprom_; }
        [[nodiscard]] chips::video::cps2_video& video() noexcept { return video_; }
        [[nodiscard]] const chips::video::cps2_video& video() const noexcept { return video_; }
        [[nodiscard]] chips::cpu::z80& sound_cpu() noexcept { return sound_cpu_; }
        [[nodiscard]] chips::audio::qsound& qsound_dsp() noexcept { return qdsp_; }
        [[nodiscard]] topology::bus& sound_bus() noexcept { return sound_bus_; }
        [[nodiscard]] bool has_sound() const noexcept { return sound_rom_size_ > 0U; }
        [[nodiscard]] const common::rom_set_image& rom_set() const noexcept { return roms; }
        [[nodiscard]] std::uint64_t vblank_irq_raised() const noexcept {
            return vblank_irq_raised_;
        }
        [[nodiscard]] std::uint64_t vblank_irq_acked() const noexcept { return vblank_irq_acked_; }

        // Active-low controls the player adapter drives (all-released = 0xFFFF).
        // input0 = P1(low)/P2(high), input1 = P3/P4, input_sys = start/coin bits.
        std::uint16_t input0{0xFFFFU};
        std::uint16_t input1{0xFFFFU};
        std::uint16_t input_sys{0xFFFFU};

      private:
        topology::bus main_bus{24U, topology::endianness::big};
        topology::bus sound_bus_{16U, topology::endianness::little};
        chips::cpu::m68000 main_cpu;
        chips::cpu::z80 sound_cpu_;
        chips::audio::qsound qdsp_;
        chips::video::cps2_video video_;
        // CPS-2 NVRAM: a serial 93C46 in 16-bit organisation (64 x 16).
        chips::storage::eeprom_93c46 eeprom_{chips::storage::eeprom_93c46::organization::word16};

        common::rom_set_image roms;
        cps2_board_params params;

        // The decrypted opcode image the 68000 fetches instructions from; the
        // encrypted program ROM (in `roms`) is what data reads see. Heap-backed so
        // the bus opcode overlay span stays valid for the board's lifetime.
        std::vector<std::uint8_t> opcode_image;
        // RAM regions. The large ones are heap-backed so the never-moved board does
        // not put hundreds of KiB on the stack; the bus spans into them stay valid.
        std::vector<std::uint8_t> work_ram_ = std::vector<std::uint8_t>(main_ram_size, 0U);
        std::vector<std::uint8_t> video_ram_ = std::vector<std::uint8_t>(video_ram_size, 0U);
        std::vector<std::uint8_t> object_ram_ = std::vector<std::uint8_t>(object_ram_size, 0U);
        std::vector<std::uint8_t> extra_ram_ = std::vector<std::uint8_t>(extra_ram_size, 0U);
        std::array<std::uint8_t, control_reg_size> control_regs_{};
        std::array<std::uint8_t, extra_ctrl_size> extra_control_{};
        // The CPS-A + CPS-B register file, indexed by the reference layout (CPS-A
        // at 0x100, CPS-B at 0x140) and reachable through both primary and legacy
        // mirror windows. CPS-A words are mirrored into cps_a_regs_ for per-frame
        // video decode; CPS-B writes are forwarded to the shared video chip.
        std::array<std::uint8_t, cps_reg_size> cps_regs_{};
        std::array<std::uint16_t, cps_a_reg_count> cps_a_regs_{};
        std::uint8_t object_bank_{0U};
        std::uint64_t vblank_irq_raised_{};
        std::uint64_t vblank_irq_acked_{};
        bool executable_{false};

        // --- QSound sound subsystem ---
        // The 4 KiB comm RAM shared by the 68K ($618000, odd-byte) and the Z80
        // ($C000); 2 KiB Z80 scratch ($D000); 4 KiB Z80 work RAM ($F000).
        std::array<std::uint8_t, qsound_shared_size> qsound_shared_ram_{};
        std::array<std::uint8_t, z80_ram_window> z80_ram_{};
        std::array<std::uint8_t, z80_work_window> qsound_work_ram_{};
        std::uint32_t sound_rom_size_{0U};
        std::uint8_t sound_bank_{0U};
        bool sound_reset_asserted_{true};  // the Z80 powers up held in reset
        std::int64_t sound_cycle_debt_{0}; // 68K cycles owed to the Z80 (clock-ratio scaled)

        // Map the CPS register file at one window (primary or mirror) onto cps_regs_
        // starting at file_offset (0x100 for CPS-A, 0x140 for CPS-B).
        void map_cps_reg_window(std::uint32_t base, std::size_t file_offset);
        [[nodiscard]] std::uint16_t cps_reg_word(std::size_t file_offset,
                                                 std::size_t word_index) const noexcept;
        [[nodiscard]] std::uint32_t video_ram_base_from_reg(std::uint16_t reg) const noexcept;
        [[nodiscard]] std::uint32_t video_ram_base_aligned(std::uint16_t reg,
                                                           std::uint32_t boundary) const noexcept;
        void push_cps_a_to_video() noexcept;
        // The current CPS-A reg5 palette source (page-aligned video-RAM offset) the
        // video chip DMAs from at render time.
        [[nodiscard]] std::uint32_t palette_source() const noexcept;
        // Wire the Z80 sound CPU + the 68K<->Z80 comm RAM + the DL-1425 QSound DSP.
        void setup_sound();
    };

} // namespace mnemos::manifests::capcom_cps2

#endif // MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
