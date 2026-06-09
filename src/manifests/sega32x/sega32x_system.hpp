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

        // 32X interrupt sources: a per-CPU enable mask + latch, each source with a
        // fixed SH-2 IRL level + vector. An edge latches on both CPUs regardless of
        // the mask; the mask only gates CPU-visible delivery, so a mask 0->1 write
        // or an IRQ-accept rescan re-delivers a still-latched edge (the Mars
        // interrupt-controller flip-flop semantics, from the Emu reference).
        static constexpr std::uint8_t irq_vint = 0x01U; // V-blank: level 12, vector 0x44
        static constexpr std::uint8_t irq_hint = 0x02U; // H-blank: level 10, vector 0x46
        static constexpr std::uint8_t irq_cmd = 0x04U;  // 68K cmd: level 8,  vector 0x48
        static constexpr std::uint8_t irq_pwm = 0x08U;  // PWM:     level 6,  vector 0x4A
        std::uint8_t master_irq_mask{};
        std::uint8_t slave_irq_mask{};
        std::uint8_t master_irq_latch{};
        std::uint8_t slave_irq_latch{};

        // COMM bank byte access (big-endian: even byte = high half of the word).
        // Shared by both SH-2s; the Genesis 68000 joins it in the bridge phase.
        [[nodiscard]] std::uint8_t comm_read(std::uint32_t offset) const noexcept;
        void comm_write(std::uint32_t offset, std::uint8_t value) noexcept;

        // Raise a 32X interrupt source (latches on both CPUs; delivered to each
        // whose mask enables it). No-op while the SH-2s are held in reset.
        void raise_vint();
        void raise_hint();
        void raise_cmd();
        void raise_pwm();
        // Set a CPU's interrupt-enable mask, then re-deliver any latched edge.
        void set_master_irq_mask(std::uint8_t mask);
        void set_slave_irq_mask(std::uint8_t mask);

        // IRQ-accept rescan entry points (wired to each SH-2's accept callback):
        // re-deliver a latched lower-priority edge once the CPU takes a higher one.
        void master_irq_accept() { fire_latched(master_cpu, master_irq_mask, master_irq_latch); }
        void slave_irq_accept() { fire_latched(slave_cpu, slave_irq_mask, slave_irq_latch); }

        // Power-on: clear board state and (re)load both SH-2 reset vectors from
        // their BIOS. The CPUs stay held until set_sh2_reset(false).
        void reset();
        // Hold (true) or release (false) the two SH-2s.
        void set_sh2_reset(bool asserted) noexcept { sh2_reset_asserted = asserted; }
        // Advance both SH-2s when not held in reset (lockstep; real interleaving
        // is a phase-D scheduling concern).
        void run_cycles(std::uint64_t cycles);

      private:
        // Latch a source on both CPUs and deliver it to each whose mask enables it
        // and whose pending level it outranks.
        void deliver_irq(std::uint8_t bit, int level, std::uint8_t vector);
        // Re-scan a CPU's latched sources (priority order) and present the highest
        // unmasked one that outranks its pending slot. Run after a mask write or an
        // IRQ accept.
        void fire_latched(chips::cpu::sh2& cpu, std::uint8_t mask, std::uint8_t& latch);
    };

    std::unique_ptr<sega32x_system> assemble_sega32x();

} // namespace mnemos::manifests::sega32x
