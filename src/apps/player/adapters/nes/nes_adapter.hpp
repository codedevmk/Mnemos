#pragma once

#include "nes_system.hpp"
#include "player_system.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::nes {

    // Force-link hook (see genesis_adapter.hpp): referenced from main so the
    // static registry self-registration in the .cpp is pulled in.
    void force_link() noexcept;

    // NES player adapter. Drives a hand-wired nes_system through the runtime
    // scheduler. The 2C02 PPU is the frame source and the master clock; the CPU
    // and APU run at a third of its dot rate (NTSC 3:1). inc 1 scope: boots an
    // NROM cart + renders. Audio (APU) and controllers are follow-ups.
    class nes_adapter final : public frontend_sdk::player_system {
      public:
        explicit nes_adapter(std::vector<std::uint8_t> rom,
                             const manifests::nes::nes_config& config = {},
                             std::string display_name = {},
                             frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->ppu.framebuffer();
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return {chip_view_.data(), chip_view_.size()};
        }

        [[nodiscard]] manifests::nes::nes_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::nes::nes_system> sys_;
        std::array<chips::ichip*, 3> chip_view_{}; // PPU (frame source), CPU, APU
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        double target_fps_;
        std::vector<frontend_sdk::spec_field> spec_{};
    };

} // namespace mnemos::apps::player::adapters::nes
