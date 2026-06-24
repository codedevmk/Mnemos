#pragma once

#include "introspection_views.hpp"
#include "msx2_system.hpp"
#include "player_system.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::msx2 {

    void force_link() noexcept;

    class msx2_adapter final : public frontend_sdk::player_system {
      public:
        explicit msx2_adapter(std::vector<std::uint8_t> bios, std::vector<std::uint8_t> cart,
                              const manifests::msx2::msx2_config& config = {},
                              std::string display_name = {},
                              frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->vdp.framebuffer();
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] const frontend_sdk::session_capability_info&
        session_capabilities() const noexcept override {
            return session_;
        }
        [[nodiscard]] const frontend_sdk::media_capability_info&
        media_capabilities() const noexcept override {
            return media_;
        }
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return {chip_view_.data(), chip_count_};
        }
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }

        [[nodiscard]] manifests::msx2::msx2_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::msx2::msx2_system> sys_;
        instrumentation::span_memory_view ram_view_;
        std::array<instrumentation::memory_view*, 1> system_mem_view_{};
        std::array<chips::ichip*, 7> chip_view_{};
        std::size_t chip_count_{};
        runtime::scheduler scheduler_;
        mnemos::video_region video_region_;
        double target_fps_{};
        std::vector<frontend_sdk::spec_field> spec_{};

        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> scc_buf_{};
        std::vector<std::int16_t> music_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::msx2
