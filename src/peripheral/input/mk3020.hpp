#pragma once

#include "peripheral.hpp"

#include <cstdint>

namespace mnemos::peripheral::input {

    // Sega MK-3020 -- the Master System Control Pad. 4-way d-pad plus two
    // fire buttons (Button 1, Button 2). Unlike the Mega Drive pad it has no
    // on-pad Start; pause is a separate console button. The host can drive
    // the TR pin as an output (overriding Button 2) via the I/O sub-
    // controller's direction bits at $3F, but that's a system-level concern
    // -- the pad itself just reports its 6 pin lines (U, D, L, R, B1, B2).
    //
    // The CPU never reads this byte directly. The SMS multiplexes input pins
    // across $DC and $DD by port, so the host system queries read_data() on
    // each attached pad and recombines bits into the final port byte.
    class mk3020 final : public device {
      public:
        [[nodiscard]] info describe() const noexcept override;

        // Pads are pure-input on the SMS data lines; the I/O sub-controller
        // handles output direction at $3F, not the pad.
        void write_data(std::uint8_t /*value*/) noexcept override {}

        // Active-low byte representing the pad's 6 input pin lines:
        //   bit 0 = Up,    bit 1 = Down,  bit 2 = Left,  bit 3 = Right
        //   bit 4 = Button 1,             bit 5 = Button 2
        //   bits 6-7 = 1 (pad does not drive them)
        [[nodiscard]] std::uint8_t read_data() const noexcept override;

        void apply_state(const controller_state& state) noexcept override;

      private:
        std::uint8_t buttons_{0U};
    };

} // namespace mnemos::peripheral::input
