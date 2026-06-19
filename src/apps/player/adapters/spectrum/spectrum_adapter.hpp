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
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = 44100U};
        }
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return {chip_view_.data(), chip_view_.size()};
        }

        [[nodiscard]] manifests::spectrum::spectrum_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::spectrum::spectrum_system> sys_;
        std::array<chips::ichip*, 2> chip_view_{};
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        std::vector<frontend_sdk::spec_field> spec_{};
    };

} // namespace mnemos::apps::player::adapters::spectrum
