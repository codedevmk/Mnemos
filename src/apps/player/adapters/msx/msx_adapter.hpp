#pragma once

#include "introspection_views.hpp"
#include "msx_system.hpp"
#include "player_system.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::msx {

    void force_link() noexcept;

    class msx_adapter final : public frontend_sdk::player_system {
      public:
        enum class media_kind : std::uint8_t { none, cartridge, tape, disk };

        explicit msx_adapter(std::vector<std::uint8_t> bios,
                             std::vector<std::uint8_t> cartridge = {},
                             const manifests::msx::msx_config& config = {},
                             std::string display_name = {},
                             frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                             std::vector<std::uint8_t> disk_rom = {},
                             std::vector<std::uint8_t> kanji_rom = {},
                             std::vector<std::uint8_t> cartridge2 = {},
                             std::vector<std::vector<std::uint8_t>> additional_disks = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->framebuffer();
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
            return {chip_view_.data(), chip_count_};
        }
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }
        [[nodiscard]] std::size_t media_count() const noexcept override { return disks_.size(); }
        [[nodiscard]] std::size_t current_media_index() const noexcept override {
            return disk_index_;
        }
        bool insert_media(std::size_t index) noexcept override;
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            return sys_->battery_ram();
        }

        [[nodiscard]] manifests::msx::msx_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::msx::msx_system> sys_;
        instrumentation::span_memory_view ram_view_;
        std::array<instrumentation::memory_view*, 1> system_mem_view_{};
        std::array<chips::ichip*, 8> chip_view_{};
        std::size_t chip_count_{};
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        double target_fps_;
        media_kind media_kind_{media_kind::none};
        std::vector<std::vector<std::uint8_t>> disks_{};
        std::size_t disk_index_{};
        std::vector<frontend_sdk::spec_field> spec_{};

        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> scc_buf_{};
        std::vector<std::int16_t> fm_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::msx
