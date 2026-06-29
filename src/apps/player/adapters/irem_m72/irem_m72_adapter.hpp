#pragma once

#include "introspection_views.hpp"
#include "m72_system.hpp"
#include "player_system.hpp"
#include "rom_set.hpp"
#include "save_state.hpp" // runtime::save_target
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

    class irem_m72_adapter;

    // Build a whole-board save target for tooling/rewind tests. The M72 board
    // serializes as a single component because m72_system owns the chip state,
    // writable board RAM, and glue latches as one compatibility-checked unit.
    [[nodiscard]] runtime::save_target build_save_target(manifests::irem_m72::m72_system& sys);
    [[nodiscard]] runtime::save_target build_save_target(irem_m72_adapter& adapter);

    // Player adapter for the Irem M72 arcade board family.
    //
    // Accepts standard set zips or unpacked set directories by resolving the
    // source basename against the checked-in Irem M72 game manifests (e.g.
    // rtype.zip or rtype/ -> rtype.toml). A source may still carry a game.toml
    // override for development. If neither path resolves, the development-format
    // ROM set applies to zips: entries named by board region ("maincpu.bin",
    // "soundcpu.bin", "tiles_a.bin", "tiles_b.bin", "sprites.bin",
    // "samples.bin", "mcu.bin") are loaded whole. A bare binary is treated as
    // the V30 program image.
    //
    // Input mapping (first-cut, active low): joystick bytes carry up/down/
    // left/right in bits 0-3 and buttons A/B in bits 4-5; the system byte
    // carries start 1/2 in bits 0-1, coin 1/2 in bits 2-3 (the pads'
    // `select`), service 1/2 in bits 4-5 (`service`, with `mode` retained as
    // a legacy alias), and operator test in bit 6. Audio: the
    // YM2151 runs on its own 3.579545 MHz crystal via a rational-rate scheduler
    // entry and is drained at one stereo frame per 64 chip clocks (~55.93 kHz);
    // the board's DAC latch is mixed into the same stereo stream.
    class irem_m72_adapter final : public frontend_sdk::player_system {
      public:
        explicit irem_m72_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                  frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                  std::optional<std::uint16_t> dip_override = {},
                                  std::string rom_path = {},
                                  std::vector<std::vector<std::uint8_t>> supplemental_roms = {},
                                  std::vector<std::string> supplemental_rom_paths = {});

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
        [[nodiscard]] std::vector<std::uint8_t> save_state() override;
        [[nodiscard]] runtime::load_result load_state(std::span<const std::uint8_t> data) override;

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::irem_m72::m72_system& machine() noexcept { return *sys_; }
        [[nodiscard]] std::span<const manifests::common::rom_set_dip_switch>
        dip_switches() const noexcept {
            return dip_switches_;
        }
        [[nodiscard]] const std::string& set_name() const noexcept { return loaded_set_name_; }

      private:
        friend runtime::save_target build_save_target(irem_m72_adapter& adapter);

        void publish_memory_views();
        void sync_inputs_from_ports() noexcept;
        void save_adapter_state(chips::state_writer& writer) const;
        void load_adapter_state(chips::state_reader& reader);

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::irem_m72::m72_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::array<std::unique_ptr<instrumentation::span_memory_view>, 8> memory_view_storage_{};
        std::array<instrumentation::memory_view*, 8> system_mem_view_{};
        std::optional<runtime::scheduler> scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<manifests::common::rom_set_dip_switch> dip_switches_{};
        std::string loaded_set_name_{};
        std::vector<std::int16_t> audio_buf_{};
        std::uint64_t samples_drained_{};
        std::int16_t dac_mix_output_{};
        frontend_sdk::display_orientation orientation_{
            frontend_sdk::display_orientation::horizontal};
    };

} // namespace mnemos::apps::player::adapters::irem_m72
