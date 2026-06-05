#pragma once

#include "bus.hpp"    // topology bus
#include "m68000.hpp" // sub-CPU
#include "rf5c68.hpp" // PCM

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::segacd {

    inline constexpr std::size_t prg_ram_size = 512U * 1024U;  // 0x80000 sub-CPU program RAM
    inline constexpr std::size_t word_ram_size = 256U * 1024U; // 0x40000 dual-port word RAM
    inline constexpr std::size_t backup_ram_size = 8U * 1024U; // 0x2000 battery backup RAM
    inline constexpr std::size_t bios_max_size = 256U * 1024U; // sub-CPU boot ROM ceiling
    inline constexpr std::size_t gate_array_size = 0x100;      // 256-byte gate-array block

    // Heap-allocated, never-moved Sega CD sub side: the sub-bus holds spans into
    // the member arrays and the MMIO handlers capture `this`. Phase B1 wires the
    // sub-CPU, its bus (PRG/word RAM + PCM + BIOS overlay), and the run/reset
    // control. The gate array, word-RAM 2M/1M banking, backup RAM, and the
    // sub-CPU IRQ controller arrive in B2/B3; the CDC/CDD and stamp ASIC in
    // phase C; Genesis main-side integration in phase D.
    struct segacd_system final {
        chips::cpu::m68000 sub_cpu;
        chips::audio::rf5c68 pcm;
        topology::bus sub_bus{24U, topology::endianness::big};

        std::array<std::uint8_t, prg_ram_size> prg_ram{};
        std::array<std::uint8_t, word_ram_size> word_ram{};
        std::array<std::uint8_t, backup_ram_size> backup_ram{};
        std::array<std::uint8_t, gate_array_size> gate_array{};

        std::vector<std::uint8_t> bios; // borrowed by the sub-bus (read overlay)
        bool sub_reset_asserted{true};  // held in reset until the main CPU releases it
        bool sub_busreq{false};         // main CPU holds the sub-CPU bus ($01 bit 1)

        // Advance the sub-CPU by `cycles` of its clock. No-op while held in reset.
        void run_cycles(std::uint64_t cycles);
        // Release the sub-CPU from reset and boot it from the $0/$4 vectors (which
        // come from the BIOS overlay, or from PRG-RAM in tests with no BIOS).
        void release_sub_reset();
        void assert_sub_reset() noexcept { sub_reset_asserted = true; }
        void reset();

        // Gate-array register access. The sub side ($FF8000 / $0FF800 on the
        // sub-bus) and the main side ($A12000, wired in phase D) differ on the
        // memory-mode register ($03): the main CPU owns the PRG-RAM bank + DMNA,
        // the sub-CPU owns RET. $01 controls the sub-CPU reset / bus request.
        [[nodiscard]] std::uint8_t gate_read(std::uint8_t offset) noexcept;
        void gate_write_main(std::uint8_t offset, std::uint8_t value);
        void gate_write_sub(std::uint8_t offset, std::uint8_t value);
    };

    // Build a Sega CD sub side and wire the sub-bus. `bios` may be empty (the
    // sub-CPU then boots from whatever is loaded into PRG-RAM, e.g. unit tests).
    // The sub-CPU starts held in reset; call release_sub_reset() to run it.
    [[nodiscard]] std::unique_ptr<segacd_system>
    assemble_segacd(std::vector<std::uint8_t> bios = {});

} // namespace mnemos::manifests::segacd
