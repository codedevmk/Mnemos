#pragma once

#include "chip.hpp"

#include <cstdint>

namespace mnemos::chips::audio {

    // Generic 8-bit unsigned latch DAC -- the sample-playback output stage on
    // boards like the Irem M72 family, where the sound CPU writes raw PCM
    // bytes and the analog stage holds the level between writes.
    //
    // The CPU writes unsigned bytes (0x80 = midpoint/silence); output() is
    // the held level as a signed 16-bit sample scaled for mixing alongside an
    // FM chip's channels (one DAC step = 64 output units, full scale ~ +/-8k).
    // tick() only counts elapsed clocks; the board's audio drain samples the
    // held level at its own cadence.
    class dac8 final : public iaudio_synth {
      public:
        dac8() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override { elapsed_ += cycles; }
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void write(std::uint8_t value) noexcept { level_ = value; }
        [[nodiscard]] std::uint8_t level() const noexcept { return level_; }
        [[nodiscard]] std::int16_t output() const noexcept {
            return static_cast<std::int16_t>((static_cast<std::int32_t>(level_) - 0x80) * 64);
        }
        [[nodiscard]] std::uint64_t elapsed_clocks() const noexcept { return elapsed_; }

      private:
        std::uint8_t level_{0x80U}; // unsigned midpoint = silence
        std::uint64_t elapsed_{};
        instrumentation::ichip_introspection introspection_{};
    };

} // namespace mnemos::chips::audio
