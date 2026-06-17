#ifndef MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
#define MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP

// CPS-2 board assembler (skeleton). Phase 4 of the CPS-2 bring-up: load the
// encrypted 68000 program, build the decrypted opcode image with the phase-1
// cipher, map the encrypted ROM for data reads + the decrypted image as the
// opcode overlay (the phase-3 m68000 split), wire the work RAM, and boot the
// 68000 from the decrypted reset vector. Video, the full I/O / CPS register map,
// EEPROM, and QSound land in later phases.

#include "cps2_crypto.hpp"
#include "m68000.hpp"
#include "rom_set.hpp"

#include "bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mnemos::manifests::capcom_cps2 {

    // 68000 memory map (24-bit, big-endian). The skeleton wires only the program
    // ROM + work RAM; the remaining windows are documented for the later phases.
    inline constexpr std::uint32_t program_base = 0x000000U;    // encrypted program ROM
    inline constexpr std::uint32_t main_ram_base = 0xFF0000U;   // 64 KiB work RAM
    inline constexpr std::size_t main_ram_size = 0x10000U;      // (0xFF0000-0xFFFFFF)
    inline constexpr std::uint32_t video_ram_base = 0x900000U;  // (phase 5+)
    inline constexpr std::uint32_t object_ram_base = 0x700000U; // (phase 5+)
    inline constexpr std::uint32_t cps_io_base = 0x804000U;     // (phase 5+)

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

      private:
        topology::bus main_bus{24U, topology::endianness::big};
        chips::cpu::m68000 main_cpu;

        common::rom_set_image roms;
        cps2_board_params params;

        // The decrypted opcode image the 68000 fetches instructions from; the
        // encrypted program ROM (in `roms`) is what data reads see. Heap-backed so
        // the bus opcode overlay span stays valid for the board's lifetime.
        std::vector<std::uint8_t> opcode_image;
        std::array<std::uint8_t, main_ram_size> work_ram{};
        bool executable_{false};
    };

} // namespace mnemos::manifests::capcom_cps2

#endif // MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_SYSTEM_HPP
