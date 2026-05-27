#pragma once

#include "peripheral.hpp"

#include <cstdint>

namespace mnemos::peripheral::input {

    // Sega MK-1650 -- the original 3-button Mega Drive / Genesis Control Pad.
    // The pad multiplexes its 8 buttons across two banks selected by the TH
    // line driven by the host: TH=1 reveals C/B/Right/Left/Down/Up, TH=0
    // reveals Start/A and locks Left/Right to 0 so software can distinguish
    // the pad model from the 6-button variant. Electrically compatible with
    // the SMS port too (SMS reads only buttons 1/2 and the dpad).
    class mk1650 final : public device {
      public:
        [[nodiscard]] info describe() const noexcept override;

        void write_data(std::uint8_t value) noexcept override;
        [[nodiscard]] std::uint8_t read_data() const noexcept override;
        void apply_state(const controller_state& state) noexcept override;

      private:
        // Internal active-high mask. Bit assignment is a model-private layout
        // -- callers don't reference these positions, they pass a
        // controller_state to apply_state() and the pad owns the mapping.
        std::uint8_t buttons_{0U};
        bool th_{false}; // matches the post-reset I/O data-reg state (= 0)
    };

} // namespace mnemos::peripheral::input
