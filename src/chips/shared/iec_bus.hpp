#pragma once

#include <array>
#include <cstdint>

namespace mnemos::chips {

    // Commodore IEC serial bus: three open-collector, wired-AND lines (ATN, CLK,
    // DATA) shared by the computer (device 0) and up to 15 peripherals (drives are
    // 8-11). Each device can pull a line low; a line is LOW (asserted) iff any
    // device pulls it, otherwise it floats HIGH (released). The bus itself models
    // only the wired-OR; all handshake timing lives in the devices.
    //
    // Header-only: it is a handful of bit operations with no clocked state, used
    // by both tier-2 storage chips and the tier-4 C64 assembly.
    class iec_bus final {
      public:
        enum class line : std::uint8_t { atn = 0, clk = 1, data = 2 };

        static constexpr std::uint8_t line_count = 3U;
        static constexpr std::uint8_t max_devices = 16U;

        // Set or clear `device`'s pull on `l`. pulled == true drives the line LOW.
        void set_driver(std::uint8_t device, line l, bool pulled) noexcept {
            const std::uint16_t bit = static_cast<std::uint16_t>(1U << (device & 0x0FU));
            std::uint16_t& mask = pulled_[static_cast<std::size_t>(l)];
            mask = pulled ? static_cast<std::uint16_t>(mask | bit)
                          : static_cast<std::uint16_t>(mask & ~bit);
        }

        // true when the line is released (HIGH) — i.e. no device is pulling it.
        [[nodiscard]] bool released(line l) const noexcept {
            return pulled_[static_cast<std::size_t>(l)] == 0U;
        }
        // true when the line is asserted (LOW) by at least one device.
        [[nodiscard]] bool asserted(line l) const noexcept { return !released(l); }

        // Drop all of `device`'s pulls (reset / hot-removal).
        void release_all(std::uint8_t device) noexcept {
            const auto bit = static_cast<std::uint16_t>(1U << (device & 0x0FU));
            for (std::uint16_t& mask : pulled_) {
                mask = static_cast<std::uint16_t>(mask & ~bit);
            }
        }

        void reset() noexcept { pulled_.fill(0U); }

      private:
        std::array<std::uint16_t, line_count> pulled_{};
    };

} // namespace mnemos::chips
