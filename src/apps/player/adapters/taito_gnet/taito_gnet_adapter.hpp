#pragma once

#include "introspection_views.hpp"
#include "player_system.hpp"
#include "save_state.hpp"
#include "taito_gnet_system.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::taito_gnet {

    void force_link() noexcept;

    class taito_gnet_adapter;

    [[nodiscard]] runtime::save_target build_save_target(taito_gnet_adapter& adapter);

    // Player-facing bridge for the first Taito G-NET / Sony ZN-2 board shell.
    // This is a launchable board-smoke adapter, not a gameplay claim: the manifest
    // still lacks a GPU renderer, SPU, real GTE command math, full DMA timing,
    // exact root-timer sync, JVS/I/O, and the locked-card command/security path needed for commercial compatibility.
    class taito_gnet_adapter final : public frontend_sdk::player_system {
      public:
        explicit taito_gnet_adapter(std::vector<std::uint8_t> bios,
                                    std::vector<std::uint8_t> package,
                                    std::string display_name = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            return {.frames_per_second_x1000 = 59826U,
                    .orientation = frontend_sdk::display_orientation::horizontal};
        }
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return {.pixels = framebuffer_.data(),
                    .width = frame_width,
                    .height = frame_height,
                    .stride = frame_width};
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] const frontend_sdk::session_capability_info&
        session_capabilities() const noexcept override {
            return session_;
        }
        [[nodiscard]] const frontend_sdk::media_capability_info&
        media_capabilities() const noexcept override {
            return media_;
        }
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
        [[nodiscard]] bool valid() const noexcept { return sys_ != nullptr; }
        [[nodiscard]] manifests::taito_gnet::taito_gnet_system& machine() noexcept {
            return *sys_;
        }
        [[nodiscard]] const manifests::taito_gnet::taito_gnet_system& machine() const noexcept {
            return *sys_;
        }

      private:
        friend runtime::save_target build_save_target(taito_gnet_adapter& adapter);

        static constexpr std::uint32_t frame_width = 320U;
        static constexpr std::uint32_t frame_height = 240U;
        static constexpr std::uint32_t instructions_per_frame = 20000U;

        void publish_memory_views();
        void draw_placeholder_frame() noexcept;

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::taito_gnet::taito_gnet_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::vector<std::unique_ptr<instrumentation::span_memory_view>> memory_view_storage_{};
        std::vector<std::string> memory_view_names_{};
        std::vector<instrumentation::memory_view*> system_mem_view_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::vector<std::uint32_t> framebuffer_{};
        std::uint64_t frames_stepped_{};
    };

} // namespace mnemos::apps::player::adapters::taito_gnet
