#pragma once

#include "peripheral.hpp"

#include <cstdint>

namespace mnemos::peripheral::input {

    // Sega MK-1653 -- the 6-button Mega Drive / Genesis Arcade Pad (a.k.a.
    // "Fighting Pad"). Adds Z, Y, X, and Mode to the original 3-button
    // layout, multiplexed onto the same port via a TH-pulse extended-read
    // protocol: each TH transition advances an internal 3-bit phase counter,
    // and phases 6/7 expose the extended bank. The phase resets to 0 on
    // ~1.5ms of inactivity on real hardware; we model that as a per-V-blank
    // reset, which matches the typical poll-once-per-frame access pattern.
    class mk1653 final : public device {
      public:
        [[nodiscard]] info describe() const noexcept override;

        void write_data(std::uint8_t value) noexcept override;
        [[nodiscard]] std::uint8_t read_data() const noexcept override;
        void on_vblank() noexcept override { phase_ = 0U; }
        void apply_state(const controller_state& state) noexcept override;

        // For tests / introspection.
        [[nodiscard]] std::uint8_t phase() const noexcept { return phase_; }

      private:
        // Internal active-high mask. Bit assignment is a model-private layout
        // -- callers pass a controller_state to apply_state() and the pad
        // owns the mapping into its protocol.
        std::uint16_t buttons_{0U};
        bool th_{false}; // matches the post-reset I/O data-reg state (= 0); this
                         // parity puts the 6-button id at phase 6 with TH=0 and
                         // the extended bank at phase 7 with TH=1 once a game
                         // starts the standard pulse sequence with TH=1.
        std::uint8_t phase_{0U};
    };

} // namespace mnemos::peripheral::input
