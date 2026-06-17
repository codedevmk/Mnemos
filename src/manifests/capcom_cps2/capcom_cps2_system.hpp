#ifndef MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
#define MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP

// CPS-2 board assembler (skeleton). Phase 4 of the CPS-2 bring-up: load the
// encrypted 68000 program, build the decrypted opcode image with the phase-1
// cipher, map the encrypted ROM for data reads + the decrypted image as the
// opcode overlay (the phase-3 m68000 split), wire the work RAM, and boot the
// 68000 from the decrypted reset vector. Video, the full I/O / CPS register map,
// EEPROM, and QSound land in later phases.

#include "cps2_crypto.hpp"
#include "eeprom_93c46.hpp"
#include "m68000.hpp"
#include "rom_set.hpp"

#include "bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mnemos::manifests::capcom_cps2 {

    // 68000 memory map (24-bit, big-endian), transcribed from the reference core.
    inline constexpr std::uint32_t program_base = 0x000000U;     // encrypted program ROM
    inline constexpr std::uint32_t control_reg_base = 0x400000U; // 16-byte CPS-2 control regs
    inline constexpr std::size_t control_reg_size = 0x10U;
    inline constexpr std::uint32_t qsound_shared_base = 0x618000U; // 68K<->Z80 shared (phase 6)
    inline constexpr std::size_t qsound_shared_window = 0x2000U;   // 2x 4 KiB, 68K side
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

    // EEPROM pin bits at the $804040 write port, and the data-out bit on input 2.
    inline constexpr std::uint8_t eeprom_di_bit = 0x10U;
    inline constexpr std::uint8_t eeprom_clk_bit = 0x20U;
    inline constexpr std::uint8_t eeprom_cs_bit = 0x40U;
    inline constexpr std::uint16_t qsound_volume_status = 0xE021U;

    struct cps2_board_params final {
        // The 20-byte board key (an external asset, never committed). Without it
        // the program cannot be decrypted and the machine is not executable.
        std::optional<std::array<std::uint8_t, crypto_key_size>> key;
    };

    // Assembled CPS-2 machine (skeleton): a 68000 over the encrypted-data /
    // decrypted-opcode split, plus work RAM. Never moved after construction (bus
    // spans point into its owned storage).
    class cps2_system final {
      public:
        explicit cps2_system(common::rom_set_image image, cps2_board_params params = {});

        cps2_system(const cps2_system&) = delete;
        cps2_system& operator=(const cps2_system&) = delete;

        // True when a valid key decrypted the program -- i.e. the 68000 is running
        // real opcodes. False (a "missing key" blocker) leaves the opcode image as
        // the raw encrypted bytes; the board must not be treated as running.
        [[nodiscard]] bool executable() const noexcept { return executable_; }

        // Run whole 68000 instructions until at least `cycles` have elapsed.
        void run_cycles(std::uint64_t cycles);

        [[nodiscard]] chips::cpu::m68000& cpu() noexcept { return main_cpu; }
        [[nodiscard]] topology::bus& bus() noexcept { return main_bus; }
        [[nodiscard]] chips::storage::eeprom_93c46& eeprom() noexcept { return eeprom_; }

        // Active-low controls the player adapter drives (all-released = 0xFFFF).
        // input0 = P1(low)/P2(high), input1 = P3/P4, input_sys = start/coin bits.
        std::uint16_t input0{0xFFFFU};
        std::uint16_t input1{0xFFFFU};
        std::uint16_t input_sys{0xFFFFU};

      private:
        topology::bus main_bus{24U, topology::endianness::big};
        chips::cpu::m68000 main_cpu;
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
        std::vector<std::uint8_t> qsound_shared_ =
            std::vector<std::uint8_t>(qsound_shared_window, 0U);
        std::array<std::uint8_t, control_reg_size> control_regs_{};
        std::array<std::uint8_t, extra_ctrl_size> extra_control_{};
        // The CPS-A + CPS-B register file (latches; the video decode reads these in
        // a later phase). Indexed by the reference layout (CPS-A at 0x100, CPS-B at
        // 0x140), reachable through both the primary and the legacy mirror windows.
        std::array<std::uint8_t, cps_reg_size> cps_regs_{};
        std::uint8_t object_bank_{0U};
        bool executable_{false};

        // Map the CPS register file at one window (primary or mirror) onto cps_regs_
        // starting at file_offset (0x100 for CPS-A, 0x140 for CPS-B).
        void map_cps_reg_window(std::uint32_t base, std::size_t file_offset);
    };

} // namespace mnemos::manifests::capcom_cps2

#endif // MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
