#pragma once

#include "chip.hpp"
#include "introspection_views.hpp"

#include <cstdint>
#include <deque>

namespace mnemos::chips::audio {

    // 1-bit speaker ("beeper"), as used by the ZX Spectrum (port $FE bit 4) and
    // other machines whose only sound is a CPU-toggled speaker line. There is no
    // oscillator: the host sets the speaker level, and the chip -- ticked once per
    // CPU cycle -- box-averages the level over each output-sample window into
    // signed 16-bit samples. A held level is DC (silent); the audible signal is the
    // toggling. Drained through the same capture API as the other audio chips.
    class beeper final : public iaudio_synth {
      public:
        // Default to the 48K Spectrum's 3.5 MHz CPU clock and the engine's output
        // rate; both are adjustable for other hosts.
        static constexpr std::uint32_t default_cpu_clock_hz = 3'500'000U;
        static constexpr std::uint32_t default_output_rate_hz = 48'000U;
        static constexpr std::int32_t default_amplitude = 9'000;

        beeper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void set_clock(std::uint32_t cpu_clock_hz, std::uint32_t output_rate_hz) noexcept {
            cpu_clock_ = cpu_clock_hz != 0U ? cpu_clock_hz : default_cpu_clock_hz;
            output_rate_ = output_rate_hz != 0U ? output_rate_hz : default_output_rate_hz;
        }
        // The speaker line level (port $FE bit 4 on the Spectrum).
        void set_speaker(bool high) noexcept { speaker_high_ = high; }
        [[nodiscard]] std::uint32_t output_rate() const noexcept { return output_rate_; }

        // Capture API (mirrors sn76489 / ym2612): when enabled, tick() queues mono
        // samples the host drains with drain_samples().
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] std::size_t pending_samples() const noexcept { return queue_.size(); }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_samples) noexcept;

      private:
        struct introspection_surface final : public instrumentation::ichip_introspection {};

        std::uint32_t cpu_clock_{default_cpu_clock_hz};
        std::uint32_t output_rate_{default_output_rate_hz};
        std::int32_t amplitude_{default_amplitude};

        bool speaker_high_{};
        bool audio_capture_{};
        std::int64_t level_sum_{}; // CPU cycles the speaker was high in this window
        std::int64_t window_{};    // CPU cycles accumulated in this window
        std::int64_t phase_{};     // output-rate accumulator vs cpu_clock_
        std::deque<std::int16_t> queue_;

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::audio
