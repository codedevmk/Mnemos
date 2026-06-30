#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha YMZ280B sample player.
    //
    // The model follows the documented eight-channel register map: pitch/control
    // blocks at $00-$1f, address triplets at $20-$7f, utility/status registers
    // at $80+, and external 24-bit sample-ROM addressing. It decodes the three
    // advertised voice formats (4-bit Yamaha ADPCM preview, 8-bit PCM, 16-bit
    // PCM) so late Irem boards can validate real key-on/sample routes while the
    // remaining analogue and bus-timing details stay explicitly first-pass.
    class ymz280b final : public iaudio_synth {
      public:
        static constexpr int channel_count = 8;
        static constexpr std::size_t channel_register_count = 0x04U;
        static constexpr std::size_t register_count = 0x100U;
        static constexpr std::uint32_t default_input_clock_hz = 16'934'400U;
        static constexpr std::uint32_t clocks_per_sample = 384U;
        static constexpr std::uint8_t control_key_on = 0x80U;
        static constexpr std::uint8_t control_mode_mask = 0x60U;
        static constexpr std::uint8_t control_mode_adpcm4 = 0x20U;
        static constexpr std::uint8_t control_mode_pcm8 = 0x40U;
        static constexpr std::uint8_t control_mode_pcm16 = 0x60U;
        static constexpr std::uint8_t control_loop = 0x10U;
        static constexpr std::uint8_t control_fn8 = 0x01U;
        static constexpr std::uint8_t status_active = 0x01U;
        static constexpr std::uint8_t utility_output_register = 0x80U;
        static constexpr std::uint8_t utility_dsp_enable_register = 0x81U;
        static constexpr std::uint8_t utility_dsp_data_register = 0x82U;
        static constexpr std::uint8_t utility_ram_address_high = 0x84U;
        static constexpr std::uint8_t utility_ram_address_mid = 0x85U;
        static constexpr std::uint8_t utility_ram_address_low = 0x86U;
        static constexpr std::uint8_t utility_ram_data = 0x87U;
        static constexpr std::uint8_t irq_enable_register = 0xFEU;
        static constexpr std::uint8_t global_status_register = irq_enable_register;
        static constexpr std::uint8_t utility_control_register = 0xFFU;
        static constexpr std::uint8_t utility_control_key_enable = 0x80U;
        static constexpr std::uint8_t utility_control_memory_enable = 0x40U;
        static constexpr std::uint8_t utility_control_irq_enable = 0x10U;

        enum reg : std::uint8_t {
            reg_pitch = 0x00,
            reg_rate = reg_pitch,
            reg_control = 0x01,
            reg_total_level = 0x02,
            reg_volume = reg_total_level,
            reg_pan = 0x03,
            reg_start_high = 0x20,
            reg_loop_start_high = 0x21,
            reg_loop_end_high = 0x22,
            reg_end_high = 0x23,
            reg_start_mid = 0x40,
            reg_loop_start_mid = 0x41,
            reg_loop_end_mid = 0x42,
            reg_end_mid = 0x43,
            reg_start_low = 0x60,
            reg_loop_start_low = 0x61,
            reg_loop_end_low = 0x62,
            reg_end_low = 0x63,
            reg_status = global_status_register,
        };

        enum class address_kind : std::uint8_t {
            start = 0,
            loop_start = 1,
            loop_end = 2,
            end = 3,
        };

        [[nodiscard]] static constexpr bool is_channel_function_register(
            std::uint8_t offset) noexcept {
            return offset < 0x20U;
        }

        [[nodiscard]] static constexpr bool is_channel_control_register(
            std::uint8_t offset) noexcept {
            return is_channel_function_register(offset) &&
                   (offset % channel_register_count) == reg_control;
        }

        ymz280b();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        [[nodiscard]] std::uint8_t read_register(std::uint8_t offset) noexcept;
        void write_register(std::uint8_t offset, std::uint8_t value) noexcept;
        void key_channel(std::size_t channel_index, std::uint32_t start, std::uint32_t end,
                         std::uint8_t volume, bool loop = false) noexcept;
        [[nodiscard]] bool channel_active(std::size_t index) const noexcept;

        [[nodiscard]] std::int16_t step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }
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
        enum class sample_mode : std::uint8_t {
            disabled = 0,
            adpcm4,
            pcm8,
            pcm16,
        };

        struct channel {
            std::uint32_t start{};
            std::uint32_t pos{};
            std::uint32_t loop_start{};
            std::uint32_t loop_end{};
            std::uint32_t end{};
            std::uint32_t accumulator{};
            std::uint16_t pitch{0x0100U};
            std::uint8_t total_level{};
            std::uint8_t pan{0x08U};
            sample_mode mode{sample_mode::disabled};
            std::int32_t adpcm_accumulator{};
            std::int32_t adpcm_step{127};
            bool high_nibble{true};
            bool loop{};
            bool active{};
        };

        [[nodiscard]] std::uint32_t channel_address(std::size_t channel,
                                                    address_kind kind) const noexcept;
        [[nodiscard]] sample_mode mode_from_control(std::uint8_t control) const noexcept;
        void key_channel_from_registers(std::size_t channel_index) noexcept;
        void refresh_channel_from_registers(std::size_t channel_index) noexcept;
        [[nodiscard]] std::uint8_t active_mask() const noexcept;
        [[nodiscard]] std::int16_t decode_current_sample(channel& ch) noexcept;
        void advance_channel(channel& ch) noexcept;
        void mark_channel_end(std::size_t channel_index) noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::array<channel, channel_count> channels_{};
        std::span<const std::uint8_t> rom_{};
        std::uint8_t status_flags_{};
        std::uint32_t input_clock_hz_{default_input_clock_hz};
        std::uint32_t sample_clock_{};
        std::int16_t last_sample_{};
        std::int16_t last_left_{};
        std::int16_t last_right_{};
        bool audio_capture_{};
        std::uint32_t capture_divider_{1U};
        std::uint32_t capture_counter_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
