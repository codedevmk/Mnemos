#pragma once

#include <mnemos/chips/common/chip.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::storage {

    // Commodore 1530/C2N datasette: plays a `.tap` pulse stream to the C64.
    //
    // Ported from the Emu reference core (ADR 0006). The tape is a sequence of
    // pulse lengths (in cycles); while the motor runs and PLAY is held, the device
    // counts down each pulse and, at its end, asserts the CIA1 /FLAG line (one
    // negative edge). It also drives the cassette-sense line (low while a key is
    // held). Read-only playback: write (.tap recording) is out of scope.
    class datasette final : public i_storage {
      public:
        struct config final {
            std::function<bool()> motor_on;      // C64 $01 bit 5 low -> motor running
            std::function<void()> flag_pulse;    // assert one CIA1 /FLAG negative edge
            std::function<void(bool)> set_sense; // drive cassette sense (true = key held)
        };

        // A .tap v0 overflow byte ($00) encodes a pulse longer than 255*8 cycles
        // whose exact length is not recorded; modelled as this many cycles.
        static constexpr std::uint32_t v0_overflow_cycles = 256U * 8U;

        datasette() = default;

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        void configure(config cfg);
        [[nodiscard]] bool load_tap(std::span<const std::uint8_t> tap);
        void eject() noexcept;
        [[nodiscard]] bool loaded() const noexcept { return !data_.empty(); }

        void set_play(bool pressed) noexcept; // PLAY button
        void rewind() noexcept;               // back to the start of the tape
        [[nodiscard]] bool playing() const noexcept { return play_; }
        [[nodiscard]] bool at_end() const noexcept { return pos_ >= data_.size(); }

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        [[nodiscard]] std::uint32_t next_pulse_cycles();
        void update_sense();

        config cfg_{};
        std::uint8_t version_{};
        std::vector<std::uint8_t> data_; // raw pulse stream (after the 20-byte header)
        std::size_t pos_{};
        std::uint32_t countdown_{};
        bool play_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::storage
