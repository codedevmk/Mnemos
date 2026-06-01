#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // MOS 6581 / 8580 Sound Interface Device (the Commodore 64 audio chip).
    //
    // Three voices, each with a 16-bit oscillator (triangle/sawtooth/pulse/noise
    // with combined-waveform AND, ring modulation, and hard sync), an ADSR
    // envelope generator, a multi-mode state-variable filter, OSC3/ENV3 readback,
    // and a mixed signed-16-bit sample output. tick() advances one φ2 cycle.
    class sid_6581 final : public iaudio_synth, public immio {
      public:
        enum class variant : std::uint8_t { mos_6581, mos_8580 };

        enum class env_phase : std::uint8_t { idle, attack, decay, sustain, release };

        static constexpr std::uint8_t voice_count = 3U;
        static constexpr std::uint8_t register_count = 32U;
        static constexpr std::int32_t default_sample_rate_hz = 985'248; // PAL φ2

        sid_6581() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Machine configuration (survives reset).
        void set_variant(variant v) noexcept { variant_ = v; }
        [[nodiscard]] variant chip_variant() const noexcept { return variant_; }
        void set_sample_rate(std::int32_t rate_hz) noexcept;

        // Register access; addr masked to 5 bits (the 32-byte alias in $D400-$D7FF).
        [[nodiscard]] std::uint8_t read(std::uint8_t address) const noexcept;
        void write(std::uint8_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read(static_cast<std::uint8_t>(offset));
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write(static_cast<std::uint8_t>(offset), value);
        }

        void set_paddle_x(std::uint8_t value) noexcept;
        void set_paddle_y(std::uint8_t value) noexcept;

        // One mixed signed-16-bit sample (advances the filter integrators).
        [[nodiscard]] std::int16_t sample() noexcept;

        [[nodiscard]] std::uint8_t envelope_value(std::uint8_t voice) const noexcept;
        [[nodiscard]] env_phase envelope_phase(std::uint8_t voice) const noexcept;
        [[nodiscard]] std::uint16_t waveform_output(std::uint8_t voice) const noexcept;
        [[nodiscard]] std::uint32_t voice_phase(std::uint8_t voice) const noexcept;
        [[nodiscard]] std::int32_t filter_cutoff_hz() const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

        // Real-time audio sink (mirror of the SN76489 / YM2612 capture API).
        // When enabled, tick() appends one mixed sample per φ2 cycle to an
        // internal queue; the host drains it with drain_samples(). Samples come
        // out at the φ2 rate the chip is clocked at (set_sample_rate, ~1 MHz),
        // so the adapter downsamples to its output rate before mixing -- the
        // same arrangement the Genesis/SMS PSG uses. Off by default: a SID
        // nobody captures from behaves exactly as before, because tick() then
        // never calls sample() and the queue stays empty.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept { return sample_queue_.size(); }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_samples) noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        struct voice_state final {
            std::uint16_t frequency{};
            std::uint16_t pulse_width{};
            std::uint8_t control{};
            std::uint8_t attack{};
            std::uint8_t decay{};
            std::uint8_t sustain{};
            std::uint8_t release{};

            env_phase phase{env_phase::idle};
            std::uint8_t envelope{};
            bool gate_prev{};
            std::uint16_t rate_counter{};
            std::uint8_t exp_counter{};
            std::uint8_t exp_period{};

            std::uint32_t accumulator{};
            std::uint32_t accumulator_prev{};
            std::uint32_t noise_lfsr{0x007FFFFFU};
        };

        void decode_voice(std::uint8_t voice_index) noexcept;
        void decode_filter_and_volume() noexcept;
        void envelope_step(voice_state& v) noexcept;
        void waveform_step(std::uint8_t voice_index) noexcept;
        void apply_noise_corruption(voice_state& v) const noexcept;
        [[nodiscard]] std::int32_t filter_frequency_coeff() const noexcept;
        [[nodiscard]] std::int32_t filter_damping_coeff() const noexcept;

        variant variant_{variant::mos_6581};
        std::array<std::uint8_t, register_count> regs_{};
        std::array<voice_state, voice_count> voices_{};

        std::uint16_t filter_cutoff_{};
        std::uint8_t filter_resonance_{};
        std::uint8_t filter_route_{};
        std::uint8_t filter_mode_{};
        std::uint8_t volume_{};

        std::int32_t filter_lp_{};
        std::int32_t filter_bp_{};
        std::int32_t sample_rate_hz_{default_sample_rate_hz};

        std::uint8_t potx_{};
        std::uint8_t poty_{};
        std::uint8_t osc3_{};
        std::uint8_t env3_{};

        std::array<register_descriptor, 4> register_view_{};
        introspection_surface introspection_{};

        // Host-side audio capture buffer (see enable_audio_capture). Transient
        // playback state: not part of save/load and not cleared by reset(), so
        // the host owns drain cadence independently of machine resets.
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};
    };

} // namespace mnemos::chips::audio
