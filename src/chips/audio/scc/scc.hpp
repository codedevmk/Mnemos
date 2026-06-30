#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Konami 051649 / Sound Creative Chip. The original SCC exposes five
    // wavetable voices through a cartridge register aperture: 32 signed 8-bit
    // samples per voice, 12-bit frequency, 4-bit volume and one enable bit per
    // channel. Channels 4 and 5 share the last waveform RAM on the original SCC
    // silicon; SCC+ later split them, but the Konami SCC mapper used by MSX2 carts
    // maps this five-voice shared-waveform variant.
    class scc final : public iaudio_synth {
      public:
        static constexpr int channel_count = 5;
        static constexpr int waveform_count = 4;
        static constexpr int waveform_size = 32;
        static constexpr int register_count = 0x100;
        static constexpr int default_clock_divider = 74; // ~48.4 kHz at 3.58 MHz

        scc() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] std::uint8_t read(std::uint16_t address) const noexcept;
        void write(std::uint16_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint16_t frequency(int channel) const noexcept;
        [[nodiscard]] std::uint8_t volume(int channel) const noexcept;
        [[nodiscard]] bool channel_enabled(int channel) const noexcept;
        [[nodiscard]] std::uint8_t wave_sample(int channel, int offset) const noexcept;
        [[nodiscard]] std::int32_t channel_output(int channel) const noexcept;

        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        static constexpr int k_output_gain = 3;

        [[nodiscard]] static std::uint8_t canonical_register(std::uint8_t address) noexcept;
        [[nodiscard]] static std::size_t waveform_for_channel(int channel) noexcept;
        void decode_register(std::uint8_t reg) noexcept;
        void advance_oscillators() noexcept;
        [[nodiscard]] std::int16_t mix_output() noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::array<std::uint16_t, channel_count> frequency_{};
        std::array<std::uint8_t, channel_count> volume_{};
        std::uint8_t enable_mask_{};
        std::array<std::uint16_t, channel_count> phase_counter_{};
        std::array<std::uint8_t, channel_count> wave_index_{};
        std::array<std::int32_t, channel_count> channel_output_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        int clock_divider_{default_clock_divider};
        int sample_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
