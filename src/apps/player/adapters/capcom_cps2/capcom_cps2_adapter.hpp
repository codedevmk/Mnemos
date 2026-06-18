#pragma once

#include "capcom_cps2_system.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::capcom_cps2 {

    // Force-link hook (see genesis_adapter.hpp for the rationale).
    void force_link() noexcept;

    // Player adapter for the Capcom CPS2 arcade board family.
    //
    // Accepts a .zip carrying a "game.toml" declaration (schema mnemos-romset/1)
    // that names the board regions ("maincpu" = the encrypted 68000 program,
    // "gfx", "audiocpu", "qsound"). CPS2 boards are encrypted: the 20-byte board
    // key must be present, either as a "key" region in the declaration or as a
    // sidecar file beside the zip (`<dir>/keys/<set>.key` or `<dir>/<set>.key`).
    // Without the key the board is a non-executable blocker (it still renders the
    // backdrop). A bare binary is treated as the encrypted program image.
    //
    // Input mapping (active low): the joystick word packs P1 in the low byte and
    // P2 in the high byte (up/down/left/right in bits 0-3, buttons 1-3 in bits
    // 4-6); the extra-button word carries buttons 4-6 in bits 0-2. The system word
    // carries coin 1/2 (the pads' `select`) and start 1/2. Audio is the QSound
    // DL-1425 stereo stream, drained per frame.
    class capcom_cps2_adapter final : public frontend_sdk::player_system {
      public:
        explicit capcom_cps2_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                     frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                     std::optional<std::uint16_t> dip_override = {},
                                     std::string rom_path = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            // ~59.6 Hz progressive raster, 384x224.
            return {.frames_per_second_x1000 = 59600U, .orientation = orientation_};
        }
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->video().framebuffer();
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return chip_view_;
        }
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::capcom_cps2::cps2_system& machine() noexcept { return *sys_; }

      private:
        // Re-pack the latched pad state onto the board's active-low input words.
        void refresh_inputs() noexcept;
        void publish_memory_views();

        std::unique_ptr<manifests::capcom_cps2::cps2_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::array<std::unique_ptr<instrumentation::span_memory_view>, 10>
            memory_view_storage_{};
        std::array<instrumentation::memory_view*, 10> system_mem_view_{};
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<std::int16_t> audio_buf_{};
        std::uint64_t samples_drained_{};
        frontend_sdk::display_orientation orientation_{
            frontend_sdk::display_orientation::horizontal};
    };

} // namespace mnemos::apps::player::adapters::capcom_cps2
