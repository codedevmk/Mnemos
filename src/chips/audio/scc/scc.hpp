#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Konami 051649 SCC: five mono wavetable voices used by Konami MSX
    // cartridges. Each voice has a 12-bit period divider and 4-bit volume; voices
    // 4 and 5 share the fourth 32-byte signed waveform RAM in the original SCC
    // compatibility mode. Registers are memory-mapped by the cartridge in the
    // $9800-$9FFF area after the mapper enables the SCC window.
    class scc final : public iaudio_synth {
      public:
        static constexpr int channel_count = 5;
        static constexpr int waveform_count = 4;
        static constexpr int waveform_size = 32;
        static constexpr int default_clock_divider = 32;

        scc();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        [[nodiscard]] std::uint8_t read(std::uint16_t address) noexcept;
        void write(std::uint16_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t waveform(int waveform, int index) const noexcept;
        [[nodiscard]] std::uint16_t period(int channel) const noexcept;
        [[nodiscard]] std::uint8_t volume(int channel) const noexcept;
        [[nodiscard]] std::uint8_t enable_mask() const noexcept { return enable_mask_; }
        [[nodiscard]] std::uint8_t deformation() const noexcept { return deformation_; }
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
        struct channel_state final {
            std::uint16_t period{};
            std::uint8_t volume{};
            std::uint16_t counter{};
            std::uint8_t phase{};
        };

        [[nodiscard]] static constexpr std::size_t waveform_for_channel(int channel) noexcept {
            return channel < 3 ? static_cast<std::size_t>(channel) : 3U;
        }

        [[nodiscard]] std::uint16_t reload_period(const channel_state& channel) const noexcept;
        void step() noexcept;
        void note_write(std::uint16_t port, std::uint8_t value);

        std::array<std::array<std::uint8_t, waveform_size>, waveform_count> waveform_{};
        std::array<channel_state, channel_count> channels_{};
        std::uint8_t enable_mask_{};
        std::uint8_t deformation_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
        instrumentation::reg_write_trace::callback reg_write_callback_{};
    };

} // namespace mnemos::chips::audio
