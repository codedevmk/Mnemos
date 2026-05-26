#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Texas Instruments SN76489 PSG — 3 square-wave tone channels + 1 LFSR noise
    // channel, each with 4-bit attenuation. Used by the Sega Master System (Z80
    // port $7F) and, later, the Genesis PSG.
    //
    // Programmed through a single write port (latch/data byte format). The chip
    // generates one mono sample per internal step (the real chip runs at the input
    // clock / 16). step() advances one PSG step and returns the mixed sample;
    // tick(cycles) drives step() through an internal /16 prescaler so the chip can
    // be clocked at the system rate. An optional 1-pole low-pass models the analog
    // output RC (off by default).
    class sn76489 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 4;
        static constexpr int default_clock_divider = 16;

        sn76489() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

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

        // Real-time audio sink (mirror of the YM2612 capture API). When
        // enabled, tick() appends every step()'s mono sample to an internal
        // queue; the host drains it with drain_samples(). Sample rate is the
        // chip's input-clock / clock_divider (default 16) -- for Genesis PSG
        // driven at master/15 that's ~223 kHz, much faster than the YM rate;
        // the adapter is responsible for any downsampling before mixing.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept { return sample_queue_.size(); }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_samples) noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

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

        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 9> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::audio
