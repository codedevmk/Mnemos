#pragma once

#include "introspection_views.hpp"
#include "m72_system.hpp"
#include "player_system.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::irem_m72 {

    // Force-link hook (see genesis_adapter.hpp for the rationale).
    void force_link() noexcept;

    // Player adapter for the Irem M72 arcade board family.
    //
    // Accepts either a development-format ROM set -- a .zip whose entries are
    // named by board region ("maincpu.bin", "soundcpu.bin", "tiles_a.bin",
    // "tiles_b.bin", "sprites.bin"), each loaded whole -- or a bare binary
    // treated as the V30 program image. Real per-game set declarations (dump
    // file lists, interleave, CRCs) arrive with the TOML game manifests and
    // supersede the development format.
    //
    // Input mapping (first-cut, active low): joystick bytes carry up/down/
    // left/right in bits 0-3 and buttons A/B in bits 4-5; the system byte
    // carries coin 1/2 in bits 0-1 (the pads' `select`) and start 1/2 in
    // bits 2-3. Audio: the YM2151 runs on its own 3.579545 MHz crystal via a
    // rational-rate scheduler entry and is drained at one stereo frame per 64
    // chip clocks (~55.93 kHz); samples are silence until the OPM synthesis
    // core lands, but the timing/IRQ path is live.
    class irem_m72_adapter final : public frontend_sdk::player_system {
      public:
        explicit irem_m72_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                  frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                  std::optional<std::uint16_t> dip_override = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            // 8 MHz pixel clock over a 512x284 raster: 55.0176... Hz.
            return {.frames_per_second_x1000 = 55018U, .orientation = orientation_};
        }
        // Vertical (TATE) games set this from driver metadata; default
        // horizontal (R-Type, Mr. Heli).
        void set_orientation(frontend_sdk::display_orientation orientation) noexcept {
            orientation_ = orientation;
        }
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->video.framebuffer();
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] const frontend_sdk::session_capability_info&
        session_capabilities() const noexcept override {
            return session_;
        }
        [[nodiscard]] const frontend_sdk::media_capability_info&
        media_capabilities() const noexcept override {
            return media_;
        }
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return chip_view_;
        }
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::irem_m72::m72_system& machine() noexcept { return *sys_; }

      private:
        void publish_memory_views();

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::irem_m72::m72_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::array<std::unique_ptr<instrumentation::span_memory_view>, 7> memory_view_storage_{};
        std::array<instrumentation::memory_view*, 7> system_mem_view_{};
        std::optional<runtime::scheduler> scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<std::int16_t> audio_buf_{};
        std::uint64_t samples_drained_{};
        frontend_sdk::display_orientation orientation_{
            frontend_sdk::display_orientation::horizontal};
    };

} // namespace mnemos::apps::player::adapters::irem_m72
