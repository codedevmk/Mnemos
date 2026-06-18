#pragma once

#include "capcom_cps1_system.hpp"
#include "player_system.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::capcom_cps1 {

    // Force-link hook (see genesis_adapter.hpp for the rationale).
    void force_link() noexcept;

    // Player adapter for the Capcom CPS1 arcade board family.
    //
    // Accepts a development-format ROM set -- a .zip carrying a "game.toml"
    // declaration (schema mnemos-romset/1) that names the board regions
    // ("maincpu", "gfx", "audiocpu", "oki") and selects the board's CPS-B
    // profile via `[set] cps_b_profile = <id>` -- or a bare binary treated as
    // the 68000 program image. The declared profile id threads into the board
    // through cps1_board_params, so the board's CPS-B protection read-back
    // matches the real PAL.
    //
    // Input mapping (first-cut, active low): the joystick word packs P1 in the
    // low byte and P2 in the high byte (up/down/left/right in bits 0-3, buttons
    // 1-3 in bits 4-6); the system word carries coin 1/2 (the pads' `select`)
    // and start 1/2. Audio: the YM2151 + the OKIM6295 share the sound board;
    // the OKIM6295's queued ADPCM is drained per frame at its native rate.
    class capcom_cps1_adapter final : public frontend_sdk::player_system {
      public:
        explicit capcom_cps1_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                     frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                     std::optional<std::uint16_t> dip_override = {},
                                     std::string rom_path = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            // ~59.6 Hz progressive raster. Most CPS1 monitors are horizontal; a
            // vertical (TATE) set declares it in its game.toml and the frontend
            // rotates the framebuffer upright.
            return {.frames_per_second_x1000 = 59600U,
                    .orientation = sys_->params.vertical
                                       ? frontend_sdk::display_orientation::vertical
                                       : frontend_sdk::display_orientation::horizontal};
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

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::capcom_cps1::cps1_system& machine() noexcept { return *sys_; }

      private:
        // Re-pack the latched pad state onto the board's active-low input words.
        void refresh_inputs() noexcept;

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::capcom_cps1::cps1_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<std::int16_t> audio_buf_{};
        std::uint64_t samples_drained_{};
    };

} // namespace mnemos::apps::player::adapters::capcom_cps1
