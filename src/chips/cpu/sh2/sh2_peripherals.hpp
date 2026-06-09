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
        void reset() noexcept;

        void save_state(state_writer& writer) const;
        void load_state(state_reader& reader);

      private:
        std::array<std::uint8_t, window_size> regs_{};
    };

} // namespace mnemos::chips::cpu
