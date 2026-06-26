#pragma once

#include "audio_views.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnemos::chips::audio {

    // Irem/Nanao GA20 PCM chip used by late Irem boards such as M92 and M107.
    //
    // The chip exposes four channels, each with an 8-byte register group:
    // start low/high, end low/high, rate, volume, control, and status. Start/end
    // addresses are 16-byte units. Control bit 1 keys the channel on; status bit
    // 0 reports active playback. PCM bytes are unsigned 8-bit samples centered at
    // 0x80, and a zero byte terminates playback.
    class irem_ga20 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 4;
        static constexpr std::size_t channel_register_count = 8U;
        static constexpr std::size_t register_count = channel_count * channel_register_count;
        static constexpr std::uint32_t default_input_clock_hz = 3'579'545U;
        static constexpr std::uint32_t clocks_per_sample = 4U;

        enum reg : std::uint8_t {
            reg_start_low = 0x00,
            reg_start_high = 0x01,
            reg_end_low = 0x02,
            reg_end_high = 0x03,
            reg_rate = 0x04,
            reg_volume = 0x05,
            reg_control = 0x06,
            reg_status = 0x07,
        };
        static constexpr std::uint8_t control_key_on = 0x02U;
        static constexpr std::uint8_t status_active = 0x01U;

        irem_ga20() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_audio(&audio_);
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        [[nodiscard]] std::uint8_t read_register(std::uint8_t offset) const noexcept;
        void write_register(std::uint8_t offset, std::uint8_t value) noexcept;
        [[nodiscard]] bool channel_active(std::size_t index) const noexcept;

        [[nodiscard]] std::int16_t step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        void set_input_clock(std::uint32_t hz) noexcept {
            input_clock_hz_ = hz != 0U ? hz : default_input_clock_hz;
        }
        [[nodiscard]] std::uint32_t native_sample_rate() const noexcept {
            return input_clock_hz_ / clocks_per_sample;
        }

        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct channel {
            std::uint32_t pos{};
            std::uint32_t end{};
            std::int32_t counter{};
            std::uint8_t rate{};
            std::uint16_t volume{};
            bool active{};
        };

        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(irem_ga20& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            irem_ga20* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        [[nodiscard]] std::uint32_t channel_address(std::size_t channel,
                                                    std::uint8_t lo_reg) const noexcept;
        void key_channel(std::size_t channel_index) noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::array<channel, channel_count> channels_{};
        std::span<const std::uint8_t> rom_{};
        std::uint32_t input_clock_hz_{default_input_clock_hz};
        std::uint32_t sample_clock_{};
        std::int16_t last_sample_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 10> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
