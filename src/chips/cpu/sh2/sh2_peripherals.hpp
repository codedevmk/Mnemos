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

        [[nodiscard]] std::uint8_t read8(std::uint32_t addr) const noexcept;
        void write8(std::uint32_t addr, std::uint8_t value) noexcept;
        // Advance the time-driven peripherals (currently the FRT) by `cycles`
        // SH-2 clocks. Called every instruction step, including during SLEEP.
        void tick(std::uint64_t cycles) noexcept;
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
    };

} // namespace mnemos::chips::cpu
