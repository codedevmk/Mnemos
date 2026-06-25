#pragma once

#include "capcom_cps2_system.hpp"
#include "chip.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"
#include "save_state.hpp" // runtime::save_target
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::capcom_cps2 {

    // Force-link hook (see genesis_adapter.hpp for the rationale).
    void force_link() noexcept;

    enum class cps2_input_profile : std::uint8_t {
        six_button = 0,
        four_player = 1, // legacy alias: 4P/3B layout
        two_player = 2,  // legacy alias: 2P/3B layout
        two_player_one_button = 3,
        two_player_two_button = 4,
        two_player_four_button = 5,
        three_player_three_button = 6,
        four_player_two_button = 7,
        four_player_four_button = 8,
        one_player_three_button = 9,
        cybots_four_button = 10,
        ecofighters_spinner = 11,
        puzz_loop_2_paddle = 12,
        six_button_ticket_dispenser = 13,
    };

    class capcom_cps2_adapter;

    [[nodiscard]] runtime::save_target build_save_target(capcom_cps2_adapter& adapter);

    // Player adapter for the Capcom CPS2 arcade board family.
    //
    // Accepts an authentic CPS2 set .zip and resolves its checked-in
    // games/<set>.toml declaration from the zip path stem; development zips may
    // still carry an explicit "game.toml" declaration (schema mnemos-romset/1).
    // Declarations name the board regions ("maincpu" = the encrypted 68000
    // program, "gfx", "audiocpu", "qsound"). CPS2 boards are encrypted: the
    // 20-byte board key must be present, either as a "key" region in the
    // declaration or as a `.key` entry in the zip / parent zip, or as a sidecar
    // file beside the zip (`<dir>/keys/<set>.key` or `<dir>/<set>.key`). Without
    // the key the board is a non-executable blocker (it still renders the
    // backdrop). A bare binary is treated as the encrypted program image.
    //
    // Input mapping (active low): the first input word packs P1 in the low byte
    // and P2 in the high byte (right/left/down/up in bits 0-3, buttons 1-4 in
    // bits 4-7 when the cabinet exposes them). Multiplayer cabinets repurpose
    // IN1 as P3/P4 main controls; two-row fighters use IN1 for P1 buttons 4-6
    // and P2 buttons 4-5, with P2 button 6 on IN2 bit 14. SFA3 Hispanic/Brazil
    // ticket-dispenser variants additionally drive IN1 bit 13 as the ticket-empty
    // line. Eco Fighters and Puzz Loop 2 install CPS2 analog read profiles that
    // multiplex IN0 with spinner/paddle values. The system word carries
    // service/test plus start/coin bits for the declared player count.
    // Audio is the QSound DL-1425 stereo stream, drained per frame.
    class capcom_cps2_adapter final : public frontend_sdk::player_system {
      public:
        explicit capcom_cps2_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                     frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                     std::optional<std::uint16_t> dip_override = {},
                                     std::string rom_path = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            // CPS-2's native 59.637405 Hz progressive raster, 384x224.
            return {.frames_per_second_x1000 = manifests::capcom_cps2::frame_rate_millihz,
                    .orientation = orientation_};
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
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            return sys_->eeprom().bytes();
        }
        [[nodiscard]] std::vector<std::uint8_t> save_state() override;
        [[nodiscard]] runtime::load_result load_state(std::span<const std::uint8_t> data) override;

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::capcom_cps2::cps2_system& machine() noexcept { return *sys_; }

      private:
        // Re-pack the latched pad state onto the board's active-low input words.
        void refresh_inputs() noexcept;
        void publish_memory_views();

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::capcom_cps2::cps2_system> sys_;
        std::unique_ptr<chips::ichip> board_debug_chip_{};
        std::vector<chips::ichip*> chip_view_{};
        std::array<std::unique_ptr<instrumentation::span_memory_view>, 11> memory_view_storage_{};
        std::array<instrumentation::memory_view*, 11> system_mem_view_{};
        std::array<frontend_sdk::controller_state, 4> ports_{};
        std::uint8_t player_count_{2U};
        cps2_input_profile input_profile_{cps2_input_profile::six_button};
        std::uint64_t frames_stepped_{};
        std::string resident_media_hash_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<std::int16_t> audio_buf_{};
        std::uint64_t samples_drained_{};
        frontend_sdk::display_orientation orientation_{
            frontend_sdk::display_orientation::horizontal};

        friend runtime::save_target build_save_target(capcom_cps2_adapter& adapter);
    };

} // namespace mnemos::apps::player::adapters::capcom_cps2
