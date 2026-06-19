#pragma once

#include "player_system.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"
#include "spectrum_system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::spectrum {

    // Force-link hook (see genesis_adapter.hpp): referenced from main so the
    // static registry self-registration in the .cpp is pulled in.
    void force_link() noexcept;

    // ZX Spectrum 48K player adapter. Drives a hand-wired spectrum_system through
    // the runtime scheduler ({ULA, CPU} -- the ULA is the frame source). inc 1
    // scope: boots + displays. Audio (beeper) and keyboard input are follow-ups.
    class spectrum_adapter final : public frontend_sdk::player_system {
      public:
        explicit spectrum_adapter(std::vector<std::uint8_t> rom,
                                  const manifests::spectrum::spectrum_config& config = {},
                                  std::string display_name = {},
                                  frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                  std::span<const std::uint8_t> snapshot = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->ula.framebuffer();
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return {chip_view_.data(), chip_count_};
        }

        [[nodiscard]] manifests::spectrum::spectrum_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::spectrum::spectrum_system> sys_;
        std::array<chips::ichip*, 4> chip_view_{}; // ULA, CPU, beeper, (128K) AY
        std::size_t chip_count_{};
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        double target_fps_;
        std::vector<frontend_sdk::spec_field> spec_{};
        // drain_audio scratch: the beeper queues mono samples and the 128K AY queues
        // interleaved stereo; both are resampled to the output frame and summed.
        std::vector<std::int16_t> beeper_buf_{};
        std::vector<std::int16_t> ay_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::spectrum
