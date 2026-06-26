#pragma once

#include "introspection_views.hpp"
#include "m107_system.hpp"
#include "player_system.hpp"
#include "save_state.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::irem_m107 {

    void force_link() noexcept;

    class irem_m107_adapter;

    [[nodiscard]] runtime::save_target build_save_target(manifests::irem_m107::m107_system& sys);
    [[nodiscard]] runtime::save_target build_save_target(irem_m107_adapter& adapter);

    class irem_m107_adapter final : public frontend_sdk::player_system {
      public:
        explicit irem_m107_adapter(std::vector<std::uint8_t> rom, std::string display_name = {},
                                   frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                   std::optional<std::uint16_t> dip_override = {},
                                   std::string rom_path = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override {
            return {.frames_per_second_x1000 = manifests::irem_m107::frame_rate_x1000,
                    .orientation = frontend_sdk::display_orientation::horizontal};
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
        [[nodiscard]] manifests::irem_m107::m107_system& machine() noexcept { return *sys_; }
        [[nodiscard]] const std::string& set_name() const noexcept { return loaded_set_name_; }

      private:
        friend runtime::save_target build_save_target(irem_m107_adapter& adapter);

        void publish_memory_views();
        void sync_inputs_from_ports() noexcept;
        void save_adapter_state(chips::state_writer& writer) const;
        void load_adapter_state(chips::state_reader& reader);

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::irem_m107::m107_system> sys_;
        std::vector<chips::ichip*> chip_view_{};
        std::array<std::unique_ptr<instrumentation::span_memory_view>, 5> memory_view_storage_{};
        std::array<instrumentation::memory_view*, 5> system_mem_view_{};
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};
        std::string loaded_set_name_{};
        std::vector<std::int16_t> audio_buf_{};
        std::vector<std::int16_t> pcm_audio_buf_{};
        std::uint64_t samples_drained_{};
    };

} // namespace mnemos::apps::player::adapters::irem_m107
