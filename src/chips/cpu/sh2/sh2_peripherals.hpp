#pragma once

#include <array>
#include <cstdint>

namespace mnemos::chips {
    class state_writer;
    class state_reader;
} // namespace mnemos::chips

namespace mnemos::chips::cpu {

    // Hitachi SH7604 on-chip peripherals, mapped at $FFFFFE00..$FFFFFFFF: the
    // interrupt controller (INTC), free-running timer (FRT), watchdog timer
    // (WDT), bus state controller (BSC), DMA controller (DMAC), serial
    // controller (SCI), and the cache/power control registers. One block is
    // integral to each SH-2 core, so the `sh2` chip owns one by composition and
    // intercepts the window before its external bus -- mapping it on a shared bus
    // would alias the master and slave CPUs' private peripheral state.
    //
    // This is the register-window shell (phase B, increment 1): the window reads
    // and writes back as storage. The FRT/WDT/DMAC/INTC behaviour (timers,
    // transfers, on-chip interrupt resolution) lands in later phase-B increments
    // (see docs/plans/2026-06-09-sega-32x-port.md).
    class sh2_peripherals final {
      public:
        static constexpr std::uint32_t window_base = 0xFFFFFE00U; // on-chip register base
        static constexpr std::uint32_t window_size = 0x200U;      // 512-byte window

        // True for an address inside the on-chip peripheral window.
        [[nodiscard]] static constexpr bool in_window(std::uint32_t addr) noexcept {
            return addr >= window_base;
        }

        // The highest on-chip interrupt the INTC currently presents (level 0 = none).
        // Derived from live register state with no side effects: the on-chip
        // sources are level-triggered and stay asserted until the handler clears
        // them, so the CPU may poll this every boundary.
        struct onchip_irq final {
            int level{};
            std::uint8_t vector{};
        };

        [[nodiscard]] std::uint8_t read8(std::uint32_t addr) const noexcept;
        void write8(std::uint32_t addr, std::uint8_t value) noexcept;
        // Advance the time-driven peripherals (currently the FRT) by `cycles`
        // SH-2 clocks. Called every instruction step, including during SLEEP.
        void tick(std::uint64_t cycles) noexcept;
        [[nodiscard]] onchip_irq pending_onchip_irq() const noexcept;
        void reset() noexcept;

        void save_state(state_writer& writer) const;
        void load_state(state_reader& reader);

      private:
        // The output-compare register the TOCR.OCRS bit currently selects.
        [[nodiscard]] std::uint16_t selected_ocr() const noexcept {
            return (tocr_ & 0x10U) != 0U ? ocrb_ : ocra_;
        }

        std::array<std::uint8_t, window_size> regs_{};

        // FRT -- free-running timer ($FE10-$FE17). A 16-bit counter clocked off a
        // TCR-selected prescale of the SH-2 clock; matches against OCRA/OCRB and
        // wraps, latching status flags in FTCSR. The timer interrupts (gated by
        // TIER) are delivered once the on-chip INTC lands; for now the flags are
        // pollable. Input capture (FICR) is not modelled.
        std::uint16_t frc_{};                     // FE12/13 free-running counter
        std::uint16_t ocra_{0xFFFFU};             // FE14/15 output compare A
        std::uint16_t ocrb_{0xFFFFU};             // FE14/15 output compare B
        std::uint8_t tier_{0x01U};                // FE10 timer interrupt enable
        std::uint8_t ftcsr_{};                    // FE11 control/status flags
        std::uint8_t tcr_{};                      // FE16 timer control (clock select)
        std::uint8_t tocr_{0xE0U};                // FE17 output-compare control
        int frt_prescale_acc_{};                  // accumulated source clocks
        mutable std::uint8_t frt_temp_{};         // 16-bit access byte latch
        mutable std::uint8_t ftcsr_read_flags_{}; // flags observed by a read

        // INTC -- interrupt controller. The FRT priority lives in IPRB[11:8]; its
        // vectors in VCRC (low byte = output-compare, shared by OCFA/OCFB) and
        // VCRD (high byte = overflow). WDT/DMAC/SCI priorities + vectors stay raw
        // in the window until those interrupts land.
        std::uint16_t iprb_{}; // FE60 interrupt priority B
        std::uint16_t vcrc_{}; // FE66 FRT ICI/OCI vectors
        std::uint16_t vcrd_{}; // FE68 FRT OVI vector

        // WDT -- watchdog / interval timer ($FE80-$FE83). An 8-bit counter
        // (WTCNT) clocked off a WTCSR-selected prescale; on overflow it sets the
        // interval-timer flag (WTCSR.OVF) or, in watchdog mode, the reset flag
        // (RSTCSR.WOVF). Writes are keyed (a high-byte key selects the register).
        // The ITI interrupt and the watchdog reset are deferred; the flags are
        // pollable.
        std::uint8_t wtcsr_{0x18U};     // FE80 control/status
        std::uint8_t wtcnt_{};          // FE80/81 counter
        std::uint8_t rstcsr_{0x1FU};    // FE82/83 reset control/status
        int wdt_prescale_acc_{};        // accumulated source clocks
        std::uint8_t wdt_key_{};        // latched high-byte key of a keyed write
        mutable bool wtcsr_ovf_read_{}; // WTCSR.OVF observed by a read
    };

} // namespace mnemos::chips::cpu
