#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha YMZ280B first-pass sample player.
    //
    // The real part supports eight ADPCM/PCM channels with external sample ROM.
    // This model exposes the board-facing register/control shape and a
    // deterministic unsigned-8-bit PCM preview path so systems can validate ROM
    // routing, audio capture, and state restore before the native ADPCM decoder
    // is implemented.
    class ymz280b final : public iaudio_synth {
      public:
        static constexpr int channel_count = 8;
        static constexpr std::size_t channel_register_count = 0x10U;
        static constexpr std::size_t register_count = 0x100U;
        static constexpr std::uint32_t default_input_clock_hz = 16'934'400U;
        static constexpr std::uint32_t clocks_per_sample = 384U;
        static constexpr std::uint8_t control_key_on = 0x80U;
        static constexpr std::uint8_t control_loop = 0x40U;
        static constexpr std::uint8_t status_active = 0x01U;
        static constexpr std::uint8_t global_status_register = 0xFFU;

        enum reg : std::uint8_t {
            reg_start_low = 0x00,
            reg_start_mid = 0x01,
            reg_start_high = 0x02,
            reg_end_low = 0x03,
            reg_end_mid = 0x04,
            reg_end_high = 0x05,
            reg_rate = 0x06,
            reg_volume = 0x07,
            reg_control = 0x08,
            reg_status = 0x09,
        };

        ymz280b();

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
        void key_channel(std::size_t channel_index, std::uint32_t start, std::uint32_t end,
                         std::uint8_t volume, bool loop = false) noexcept;
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
        void set_capture_divider(std::uint32_t divider) noexcept {
            capture_divider_ = divider != 0U ? divider : 1U;
            capture_counter_ %= capture_divider_;
        }
        [[nodiscard]] std::uint32_t capture_divider() const noexcept { return capture_divider_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct channel {
            std::uint32_t start{};
            std::uint32_t pos{};
            std::uint32_t end{};
            std::uint32_t accumulator{};
            std::uint8_t rate{1U};
            std::uint8_t volume{};
            bool loop{};
            bool active{};
        };

        [[nodiscard]] std::uint32_t channel_address(std::size_t channel,
                                                    std::uint8_t lo_reg) const noexcept;
        void key_channel_from_registers(std::size_t channel_index) noexcept;
        [[nodiscard]] std::uint8_t active_mask() const noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::array<channel, channel_count> channels_{};
        std::span<const std::uint8_t> rom_{};
        std::uint32_t input_clock_hz_{default_input_clock_hz};
        std::uint32_t sample_clock_{};
        std::int16_t last_sample_{};
        bool audio_capture_{};
        std::uint32_t capture_divider_{1U};
        std::uint32_t capture_counter_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
