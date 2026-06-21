#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Ricoh RP2C33 expansion sound -- the wavetable + modulator unit inside the
    // Famicom Disk System RAM adapter. A single channel plays a 64-step, 6-bit
    // waveform whose pitch is perturbed by a frequency modulator (its own
    // accumulator walks a 32-entry, 3-bit modulation table that nudges a signed
    // "mod counter", which scales the carrier pitch). Two ramp envelopes drive the
    // wave volume and the modulation depth, and a 2-bit master attenuator sits on
    // the output. The host programmes it through registers $4040-$409F.
    //
    // The chip is mono; like the other synth chips it surfaces one (L,R) stereo
    // frame per native sample by duplicating the mono level onto both lanes. It is
    // clocked at the CPU rate; the internal wave/mod accumulators advance once per
    // 16 input cycles, while set_clock_divider() picks how many input cycles make
    // one captured output sample.
    //
    // Clean-room from the public RP2C33 / FDS-audio documentation; no emulator
    // source consulted.
    class rp2c33 final : public iaudio_synth {
      public:
        static constexpr std::size_t wave_size = 64U;      // 64-step waveform RAM
        static constexpr std::size_t mod_table_size = 32U; // 32-step modulation table
        // The wave/mod accumulators tick once per this many input (CPU) cycles.
        static constexpr int internal_divider = 16;
        static constexpr int default_clock_divider = 37; // ~48.4 kHz native at 1.79 MHz

        // Register window offsets (absolute CPU addresses; the FDS maps them at
        // $4040-$409F and forwards reads/writes here).
        static constexpr std::uint16_t reg_wave_base = 0x4040U; // $4040-$407F wave RAM
        static constexpr std::uint16_t reg_vol_env = 0x4080U;
        static constexpr std::uint16_t reg_freq_lo = 0x4082U;
        static constexpr std::uint16_t reg_freq_hi = 0x4083U;
        static constexpr std::uint16_t reg_mod_env = 0x4084U;
        static constexpr std::uint16_t reg_mod_counter = 0x4085U;
        static constexpr std::uint16_t reg_mod_freq_lo = 0x4086U;
        static constexpr std::uint16_t reg_mod_freq_hi = 0x4087U;
        static constexpr std::uint16_t reg_mod_table = 0x4088U;
        static constexpr std::uint16_t reg_master = 0x4089U;
        static constexpr std::uint16_t reg_env_speed = 0x408AU;

        rp2c33() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // CPU register access ($4040-$409F). The FDS forwards its sound-register
        // reads/writes here; addresses outside the window are ignored / open bus.
        void write_reg(std::uint16_t addr, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_reg(std::uint16_t addr) const noexcept;

        // Generate one native mono sample (advancing nothing extra; tick() drives
        // the accumulators). last_left()/last_right() hold the most recent level.
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Real-time capture sink (mirrors the other synth chips): when enabled,
        // tick() queues one interleaved (L,R) frame per native output sample. Counts
        // are in STEREO FRAMES (pairs).
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        // Test/introspection accessors for the decoded state.
        [[nodiscard]] std::uint16_t wave_frequency() const noexcept { return wave_freq_; }
        [[nodiscard]] std::uint8_t volume_gain() const noexcept { return vol_gain_; }
        [[nodiscard]] std::uint8_t wave_sample(std::size_t i) const noexcept {
            return wave_ram_[i % wave_size];
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        void clock_internal() noexcept; // one wave/mod accumulator step (per 16 cycles)
        void clock_envelopes() noexcept;
        [[nodiscard]] std::int16_t output_sample() const noexcept;
        [[nodiscard]] std::uint32_t modulated_pitch() const noexcept;

        std::array<std::uint8_t, wave_size> wave_ram_{};
        std::array<std::uint8_t, mod_table_size> mod_table_{};

        // Wave channel.
        std::uint16_t wave_freq_{};    // 12-bit
        std::uint32_t wave_acc_{};     // 18-bit phase accumulator (top 6 bits = position)
        bool wave_halt_{};             // $4083 bit 7: hold + reset the wave
        bool env_disabled_{};          // $4083 bit 6: freeze the volume + mod envelopes
        bool wave_write_enable_{};     // $4089 bit 7: wave RAM writable + output held
        std::uint8_t master_volume_{}; // $4089 bits 1-0

        // Volume + modulation envelopes (each: direct-or-ramp, direction, speed).
        bool vol_direct_{};
        bool vol_increase_{};
        std::uint8_t vol_speed_{};
        std::uint8_t vol_gain_{}; // 0..63 (output clamped to 32)
        int vol_timer_{};

        bool mod_direct_{};
        bool mod_increase_{};
        std::uint8_t mod_speed_{};
        std::uint8_t mod_gain_{};
        int mod_timer_{};

        std::uint8_t env_speed_{}; // $408A multiplier

        // Modulator.
        std::uint16_t mod_freq_{};     // 12-bit
        std::uint32_t mod_acc_{};      // accumulator; bit-12 carries step the table
        std::int8_t mod_counter_{};    // 7-bit signed pitch bias
        std::uint8_t mod_table_pos_{}; // 5-bit table position
        bool mod_halt_{};              // $4087 bit 7: hold mod accumulator + table-write mode

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        int clock_divider_{default_clock_divider};
        int sample_prescaler_{};
        int internal_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 8> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
