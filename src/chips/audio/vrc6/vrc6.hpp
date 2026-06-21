#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Konami VRC6 expansion sound: two pulse channels (selectable 1/16..8/16 duty,
    // 4-bit volume) plus a sawtooth (a 6-bit rate accumulated into a rising 5-bit
    // ramp). All three share a CPU-rate 12-bit period divider each; the channels'
    // 4-bit/4-bit/5-bit outputs sum into a linear 6-bit DAC. The host programmes it
    // through registers $9000-$9003 (pulse 1 + global), $A000-$A002 (pulse 2) and
    // $B000-$B002 (sawtooth) -- the cartridge maps them and forwards to write_reg.
    //
    // The chip is mono; like the other synth chips it surfaces one (L,R) stereo
    // frame per native sample by duplicating the mono mix onto both lanes. The
    // dividers run at the CPU clock; set_clock_divider() picks how many input cycles
    // make one captured output sample.
    //
    // Clean-room from the public VRC6 audio documentation; no emulator source.
    class vrc6 final : public iaudio_synth {
      public:
        static constexpr int default_clock_divider = 37; // ~48.4 kHz at 1.79 MHz

        vrc6() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // CPU register access. The cartridge forwards the audio writes here; the
        // sub-index within each $X00x group is already de-swapped for VRC6a/VRC6b.
        void write_reg(std::uint16_t addr, std::uint8_t value) noexcept;

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
        [[nodiscard]] std::uint16_t pulse_period(int ch) const noexcept {
            return (ch >= 0 && ch < 2) ? pulse_[static_cast<std::size_t>(ch)].period : 0U;
        }
        [[nodiscard]] std::uint8_t pulse_volume(int ch) const noexcept {
            return (ch >= 0 && ch < 2) ? pulse_[static_cast<std::size_t>(ch)].volume : 0U;
        }
        [[nodiscard]] std::uint16_t saw_period() const noexcept { return saw_.period; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct pulse_channel {
            std::uint16_t period{};   // 12-bit
            std::uint16_t counter{};  // divider countdown
            std::uint8_t volume{};    // 4-bit
            std::uint8_t duty{};      // 3-bit (high part length - 1)
            bool mode{};              // ignore the duty generator (output = volume)
            bool enabled{};           // $x002 bit 7
            std::uint8_t duty_step{}; // 0..15, counts down
            std::uint8_t output{};    // 0..15
        };
        struct saw_channel {
            std::uint16_t period{};  // 12-bit
            std::uint16_t counter{}; // divider countdown
            std::uint8_t rate{};     // 6-bit accumulator rate
            bool enabled{};          // $B002 bit 7
            std::uint8_t accum{};    // 8-bit accumulator
            std::uint8_t step{};     // 0..13 phase (accumulate on even steps)
            std::uint8_t output{};   // 0..31 (top 5 accumulator bits)
        };

        void clock_pulse(pulse_channel& p) noexcept;
        void clock_saw() noexcept;
        [[nodiscard]] std::int16_t mix() const noexcept;

        std::array<pulse_channel, 2> pulse_{};
        saw_channel saw_{};
        bool halt_{};               // $9003 bit 0: freeze all channels
        std::uint8_t freq_shift_{}; // 0 / 4 / 8: period >>= shift (16x / 256x modes)

        std::int16_t last_left_{};
        std::int16_t last_right_{};
        double dc_{}; // DC-blocker running average

        int clock_divider_{default_clock_divider};
        int sample_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 7> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
