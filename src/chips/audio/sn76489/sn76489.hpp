#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::audio {

    // Texas Instruments SN76489 PSG — 3 square-wave tone channels + 1 LFSR noise
    // channel, each with 4-bit attenuation. Ported from the Emu reference core
    // (ADR 0006); used by the Sega Master System (Z80 port $7F) and, later, the
    // Genesis PSG.
    //
    // Programmed through a single write port (latch/data byte format). The chip
    // generates one mono sample per internal step (the real chip runs at the input
    // clock / 16). step() advances one PSG step and returns the mixed sample;
    // tick(cycles) drives step() through an internal /16 prescaler so the chip can
    // be clocked at the system rate. An optional 1-pole low-pass models the analog
    // output RC (off by default).
    class sn76489 final : public i_audio_synth {
      public:
        static constexpr int channel_count = 4;
        static constexpr int default_clock_divider = 16;

        sn76489() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        // Write a byte to the PSG data port (the SMS routes Z80 OUT ($7F) here).
        void write(std::uint8_t value) noexcept;

        // Advance one PSG step and return the mixed mono sample (signed 16-bit).
        [[nodiscard]] std::int16_t step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }

        // Configure the analog-character low-pass. cutoff_hz <= 0 disables it.
        void set_lowpass_cutoff_hz(int sample_rate_hz, int cutoff_hz) noexcept;
        // Cycles per internal PSG step (the real chip divides the input clock by 16).
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        // Introspection / test accessors.
        [[nodiscard]] std::uint16_t tone(int ch) const noexcept {
            return (ch >= 0 && ch < 3) ? tone_[static_cast<std::size_t>(ch)] : 0U;
        }
        [[nodiscard]] std::uint8_t volume(int ch) const noexcept {
            return (ch >= 0 && ch < channel_count) ? volume_[static_cast<std::size_t>(ch)] : 0U;
        }
        [[nodiscard]] std::uint16_t lfsr() const noexcept { return lfsr_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        std::array<std::uint16_t, 3> tone_{};    // 10-bit tone period registers
        std::array<std::uint16_t, 4> counter_{}; // internal countdown timers
        std::array<std::int8_t, 4> polarity_{};  // +1 / -1 square-wave state
        std::array<std::uint8_t, 4> volume_{};   // 4-bit attenuation (0=loud, 15=off)
        std::uint8_t noise_mode_{};              // bits 1-0 rate, bit 2 white/periodic
        std::uint16_t lfsr_{};                   // 16-bit linear-feedback shift register

        std::uint8_t latched_ch_{};
        bool latched_vol_{};

        std::int32_t lp_alpha_q15_{}; // 0 = filter disabled (pass-through)
        std::int32_t lp_state_{};

        int clock_divider_{default_clock_divider};
        int prescaler_{};
        std::int16_t last_sample_{};

        std::array<register_descriptor, 9> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::audio
