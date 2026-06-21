#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // MMC5 expansion sound: the two pulse channels are identical to the 2A03's
    // (11-bit timer, 4-bit duty, length counter, volume envelope) EXCEPT they have
    // no sweep unit, plus a raw 8-bit PCM channel ($5011) that is a direct DAC the
    // CPU writes. There is no $4017-style frame sequencer: the envelopes and length
    // counters are clocked at a fixed 240 Hz (the MMC5 length counter runs at twice
    // the 2A03's 120 Hz half-frame rate, i.e. also 240 Hz).
    //
    // Registers (the cartridge maps $5000-$5015 and forwards them here): $5000-$5003
    // pulse 1 (duty/halt/const-vol/vol, sweep byte ignored, timer low, timer-high +
    // length-load), $5004-$5007 pulse 2, $5010 PCM control, $5011 PCM data, $5015
    // pulse enables / length status.
    //
    // The chip is mono; like the other synth chips it duplicates the mono mix onto
    // both stereo lanes. The dividers run at the CPU clock; set_clock_divider()
    // picks how many input cycles make one captured output sample. (The hardware's
    // reversed output polarity vs the 2A03 is a phase detail left unmodelled -- it
    // is inaudible for the independent voices games actually play.)
    //
    // Clean-room from the public MMC5 audio documentation; the pulse oscillator +
    // envelope/length math mirror the in-tree 2A03 APU. No emulator source.
    class mmc5 final : public iaudio_synth {
      public:
        static constexpr int default_clock_divider = 37; // ~48.4 kHz at 1.79 MHz

        mmc5() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // CPU register access ($5000-$5015). The cartridge forwards the writes here;
        // read_status() serves $5015.
        void write_reg(std::uint16_t addr, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept;

        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Real-time capture sink (mirrors the other synth chips); counts are STEREO
        // FRAMES (pairs).
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        // Test/introspection accessors.
        [[nodiscard]] std::uint16_t pulse_timer(int ch) const noexcept {
            return (ch >= 0 && ch < 2) ? pulse_[static_cast<std::size_t>(ch)].timer : 0U;
        }
        [[nodiscard]] std::uint8_t pulse_length(int ch) const noexcept {
            return (ch >= 0 && ch < 2) ? pulse_[static_cast<std::size_t>(ch)].length_counter : 0U;
        }
        [[nodiscard]] std::uint8_t pcm_level() const noexcept { return pcm_level_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // A pulse channel: like the 2A03's, minus the sweep unit.
        struct pulse_channel {
            std::uint8_t r0{}, r2{}, r3{};
            std::uint8_t duty{};
            bool length_halt{};
            bool constant_volume{};
            std::uint8_t volume_env{};
            std::uint16_t timer{}; // 11-bit
            std::uint8_t length_load{};
            bool enabled{};
            std::uint8_t length_counter{};
            std::uint16_t timer_counter{};
            std::uint8_t sequence_step{}; // 0..7
            bool env_start{};
            std::uint8_t env_divider{};
            std::uint8_t env_decay{};
        };

        void decode_pulse(pulse_channel& p) noexcept;
        void clock_frame() noexcept; // 240 Hz: envelopes + length counters
        void advance_oscillators(int cpu_cycles) noexcept;
        [[nodiscard]] std::int16_t mix() const noexcept;

        std::array<pulse_channel, 2> pulse_{};
        std::uint8_t pcm_level_{};   // $5011 8-bit DAC
        std::uint8_t pcm_control_{}; // $5010
        bool apu_half_{};            // pulse timers tick every other CPU cycle
        int frame_accum_{};          // CPU-cycle countup to the 240 Hz frame tick

        std::int16_t last_left_{};
        std::int16_t last_right_{};
        double dc_{}; // DC-blocker running average

        int clock_divider_{default_clock_divider};
        int sample_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 6> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
