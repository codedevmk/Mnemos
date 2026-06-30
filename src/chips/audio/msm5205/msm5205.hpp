#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace mnemos::chips::audio {

    // OKI MSM5205: single-channel 4-bit ADPCM decoder driven by an external VCLK.
    // The host presents one nibble on the data pins; each VCLK edge consumes that
    // latched nibble and advances the 12-bit predictor. Boards usually pair this
    // part with a small sound CPU/MCU that owns the nibble stream timing.
    class msm5205 final : public iaudio_synth {
      public:
        static constexpr int max_step_index = 48;

        msm5205() {
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

        void data_w(std::uint8_t nibble) noexcept;
        void reset_w(bool asserted) noexcept;
        void vclk_tick() noexcept;

        [[nodiscard]] std::uint8_t data_latch() const noexcept { return data_latch_; }
        [[nodiscard]] bool reset_asserted() const noexcept { return reset_asserted_; }
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }
        [[nodiscard]] std::int32_t predictor() const noexcept { return predictor_; }
        [[nodiscard]] int step_index() const noexcept { return step_index_; }
        [[nodiscard]] std::uint64_t vclk_count() const noexcept { return vclk_count_; }

        void generate(std::span<std::int16_t> buf_lr) noexcept;

        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        void set_clock_divider(int divider) noexcept { clock_divider_ = divider > 0 ? divider : 1; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        void note_write(std::uint16_t port, std::uint8_t value) {
            if (reg_write_callback_) {
                reg_write_callback_({.port = port, .value = value});
            }
        }

        std::uint8_t data_latch_{};
        bool reset_asserted_{};
        std::int32_t predictor_{};
        int step_index_{};
        std::int16_t last_sample_{};
        std::uint64_t vclk_count_{};

        int clock_divider_{1};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        instrumentation::reg_write_trace::callback reg_write_callback_{};
        std::array<register_descriptor, 7> register_view_{};
        instrumentation::introspection_builder introspection_{};
    };

} // namespace mnemos::chips::audio
