#pragma once

#include "bus.hpp" // topology bus
#include "sh2.hpp" // master + slave CPUs

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::sega32x {

    inline constexpr std::size_t sdram_size = 256U * 1024U;       // $06000000 shared work RAM
    inline constexpr std::size_t framebuffer_size = 256U * 1024U; // $04000000 32X frame buffer
    inline constexpr std::size_t m_bios_size = 2U * 1024U;        // master SH-2 boot ROM
    inline constexpr std::size_t s_bios_size = 1U * 1024U;        // slave SH-2 boot ROM
    inline constexpr std::size_t comm_words = 8U;                 // 8-word COMM bank

    // SH-2 address map (both CPUs see the same layout, with per-CPU BIOS at $0).
    inline constexpr std::uint32_t bios_base = 0x00000000U;
    inline constexpr std::uint32_t framebuffer_base = 0x04000000U;
    inline constexpr std::uint32_t sdram_base = 0x06000000U;
    inline constexpr std::uint32_t comm_base = 0x40000020U; // SH-2-side COMM window

    // Heap-allocated, never-moved 32X board (the "Mars" hardware). The two SH-2s
    // run on their own CPU-local buses that share the SDRAM, frame buffer, and the
    // COMM bank but see their own boot ROM at $0; the on-chip SH7604 peripherals
    // live inside each `sh2` (the $FFFFFE00 window). Built like segacd_system: the
    // buses hold spans into the member arrays and the MMIO handlers capture `this`.
    //
    // Phase B increment 2 wires the two CPUs, their buses (BIOS/framebuffer/SDRAM/
    // COMM), and the reset hold/release. The SH-2-side system/interrupt registers
    // and IRQ latches arrive in increment 3; the INTC/FRT/WDT/DMAC behaviour in
    // increment 4-5; the 32X VDP + PWM in phase C; the Genesis-bus bridge +
    // scheduling in the sega32x_machine layer (phase D).
    struct sega32x_system final {
        chips::cpu::sh2 master_cpu;
        chips::cpu::sh2 slave_cpu;
        topology::bus master_bus{32U, topology::endianness::big};
        topology::bus slave_bus{32U, topology::endianness::big};

        std::array<std::uint8_t, sdram_size> sdram{};
        std::array<std::uint8_t, framebuffer_size> framebuffer{};
        std::array<std::uint8_t, m_bios_size> m_bios{};
        std::array<std::uint8_t, s_bios_size> s_bios{};
        std::array<std::uint16_t, comm_words> comm{}; // shared 8-word COMM bank

        std::uint16_t adapter_ctrl{};  // RV / ADEN / reset / bank-select bits
        bool sh2_reset_asserted{true}; // SH-2s held in reset until the adapter releases them

        // COMM bank byte access (big-endian: even byte = high half of the word).
        // Shared by both SH-2s; the Genesis 68000 joins it in the bridge phase.
        [[nodiscard]] std::uint8_t comm_read(std::uint32_t offset) const noexcept;
        void comm_write(std::uint32_t offset, std::uint8_t value) noexcept;

        // Power-on: clear board state and (re)load both SH-2 reset vectors from
        // their BIOS. The CPUs stay held until set_sh2_reset(false).
        void reset();
        // Hold (true) or release (false) the two SH-2s.
        void set_sh2_reset(bool asserted) noexcept { sh2_reset_asserted = asserted; }
        // Advance both SH-2s when not held in reset (lockstep; real interleaving
        // is a phase-D scheduling concern).
        void run_cycles(std::uint64_t cycles);
    };

    std::unique_ptr<sega32x_system> assemble_sega32x();

} // namespace mnemos::manifests::sega32x
