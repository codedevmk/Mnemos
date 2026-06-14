#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // General Instrument AY-3-8910 / Yamaha YM2149 SSG (Software-controlled Sound
    // Generator): a 3-voice square-wave PSG with a shared 17-bit noise LFSR and a
    // shared 16-bit volume envelope. Each tone channel has a 12-bit period divider;
    // amplitude is either a 4-bit fixed level or the envelope output (the M bit).
    // The chip is programmed through 16 registers selected via an ADDRESS port and
    // accessed via a DATA port. Yamaha re-uses the SSG block inside its YM2203 /
    // YM2608 / YM2610, which is why so many arcade boards depend on it.
    //
    // The chip is mono; like the RF5C68 it surfaces one (L,R) stereo frame per
    // native sample by duplicating the mono mix onto both lanes. Native output is
    // the input clock divided by 16 (the internal master prescaler).
    //
    // Ported from the Emu reference (chips/ssg); clean-room per the AY-3-8910 /
    // YM2149 SSG datasheet. The Emu reference ships only the register surface +
    // decode; the tone divider, 17-bit noise LFSR, and envelope shape machine are
    // implemented here directly from the datasheet (the reference names them as
    // follow-up). No emulator source consulted.
    class ssg final : public iaudio_synth {
      public:
        static constexpr int channel_count = 3;
        static constexpr int register_count = 16;
        // The internal master clock prescaler: the analog tone/noise dividers run
        // at the input clock / 16 (datasheet). step() is one such native sample.
        static constexpr int default_clock_divider = 16;

        // Register offsets ($00..$0F).
        enum reg : std::uint8_t {
            reg_a_freq_lo = 0x00, // channel A tone period low byte
            reg_a_freq_hi = 0x01, // channel A tone period high nibble (bits 3:0)
            reg_b_freq_lo = 0x02,
            reg_b_freq_hi = 0x03,
            reg_c_freq_lo = 0x04,
            reg_c_freq_hi = 0x05,
            reg_noise = 0x06,   // noise period (bits 4:0)
            reg_mixer = 0x07,   // tone/noise enables (active-low) + port direction
            reg_a_level = 0x08, // channel A amplitude (bit 4 = M, bits 3:0 = volume)
            reg_b_level = 0x09,
            reg_c_level = 0x0A,
            reg_env_lo = 0x0B,    // envelope period low byte
            reg_env_hi = 0x0C,    // envelope period high byte
            reg_env_shape = 0x0D, // envelope shape (CONT/ATT/ALT/HOLD, bits 3:0)
            reg_port_a = 0x0E,    // I/O port A data
            reg_port_b = 0x0F,    // I/O port B data
        };

        // Mixer bits (active-LOW: 0 enables the path).
        static constexpr std::uint8_t mixer_tone_a = 1U << 0U;
        static constexpr std::uint8_t mixer_noise_a = 1U << 3U;

        // Amplitude-register bits.
        static constexpr std::uint8_t level_env_mode = 1U << 4U; // M: envelope drives level
        static constexpr std::uint8_t level_vol_mask = 0x0FU;

        // Envelope-shape bits ($0D low nibble).
        static constexpr std::uint8_t env_hold = 1U << 0U;
        static constexpr std::uint8_t env_alt = 1U << 1U;
        static constexpr std::uint8_t env_att = 1U << 2U; // attack direction: 1 = up
        static constexpr std::uint8_t env_cont = 1U << 3U;

        ssg() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_reg_writes([this](instrumentation::reg_write_trace::callback cb) {
                    reg_write_callback_ = std::move(cb);
                });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // ADDRESS port: latch the register selected for the next data access.
        // Upper 4 bits are ignored per chip spec.
        void address(std::uint8_t reg) noexcept {
            selected_reg_ = static_cast<std::uint8_t>(reg & 0x0FU);
        }
        [[nodiscard]] std::uint8_t selected_register() const noexcept { return selected_reg_; }

        // DATA port: read/write the latched register.
        [[nodiscard]] std::uint8_t read() const noexcept { return read_reg(selected_reg_); }
        void write(std::uint8_t value) noexcept { write_reg(selected_reg_, value); }

        // Direct register access (skips the address latch) for integrators that map
        // the SSG block straight into their own bus (e.g. the YM2610 family).
        [[nodiscard]] std::uint8_t read_reg(std::uint8_t index) const noexcept;
        void write_reg(std::uint8_t index, std::uint8_t value) noexcept;

        // Generate one native sample, updating last_left()/last_right() and
        // advancing the tone dividers, the noise LFSR, and the envelope.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even).
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Introspection / test accessors for the decoded state.
        [[nodiscard]] std::uint16_t tone_period(int ch) const noexcept {
            return (ch >= 0 && ch < channel_count)
                       ? channels_[static_cast<std::size_t>(ch)].tone_period
                       : 0U;
        }
        [[nodiscard]] std::uint8_t volume(int ch) const noexcept {
            return (ch >= 0 && ch < channel_count) ? channels_[static_cast<std::size_t>(ch)].volume
                                                   : 0U;
        }
        [[nodiscard]] std::uint8_t noise_period() const noexcept { return noise_period_; }
        [[nodiscard]] std::uint16_t envelope_period() const noexcept { return envelope_period_; }
        [[nodiscard]] std::uint8_t envelope_shape() const noexcept { return envelope_shape_; }
        [[nodiscard]] std::uint8_t envelope_volume() const noexcept { return envelope_volume_; }
        [[nodiscard]] std::uint32_t noise_lfsr() const noexcept { return noise_lfsr_; }

        // Real-time capture sink (mirrors rf5c68/ym2612): when enabled, tick()
        // queues one interleaved (L,R) stereo frame per native sample step. Counts
        // below are in STEREO FRAMES (pairs) -- matching the player's add_source()
        // contract -- NOT raw int16 samples.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        // Copies up to `max_pairs` (L,R) pairs into `out` (2*max_pairs int16) and
        // removes them from the queue; returns the number of pairs copied.
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        // Input cycles per native sample step.
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct channel {
            std::uint16_t tone_period{}; // 12-bit divider reload value
            std::uint8_t level{};        // raw amplitude register (incl. M bit)
            std::uint8_t volume{};       // 4-bit fixed volume (when envelope_mode = false)
            bool envelope_mode{};        // M bit: envelope drives the level
            bool tone_enabled{};         // decoded from mixer (active-low)
            bool noise_enabled{};
            std::uint16_t tone_counter{}; // live countdown
            std::uint8_t tone_output{};   // square-wave phase (0/1)
        };

        // Fire a live register write to the installed reg_write_trace subscriber.
        void note_write(std::uint16_t port, std::uint8_t value) {
            if (reg_write_callback_) {
                reg_write_callback_({.port = port, .value = value});
            }
        }

        void decode_channel(int idx) noexcept;
        void decode_all() noexcept;
        void restart_envelope() noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::uint8_t selected_reg_{};

        std::array<channel, channel_count> channels_{};
        std::uint8_t noise_period_{};     // 5-bit
        std::uint16_t envelope_period_{}; // 16-bit
        std::uint8_t envelope_shape_{};   // low nibble of $0D
        std::uint8_t mixer_{0xFFU};

        // Live synthesis state.
        std::uint16_t noise_counter_{};    // noise divider countdown
        std::uint8_t noise_output_{};      // current noise bit (0/1)
        std::uint32_t noise_lfsr_{1U};     // 17-bit LFSR (taps bit 0 ^ bit 3)
        std::uint32_t envelope_counter_{}; // envelope divider countdown
        std::uint8_t envelope_step_{};     // 0..15 position in the current ramp
        std::uint8_t envelope_volume_{};   // 0..15 envelope amplitude output
        bool envelope_holding_{};          // shape reached its terminal HOLD state
        std::uint8_t envelope_attack_{};   // 0x00 or 0x0F: XOR applied to step->level

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 13> register_view_{};
        instrumentation::introspection_builder introspection_;
        instrumentation::reg_write_trace::callback reg_write_callback_{};
    };

} // namespace mnemos::chips::audio
