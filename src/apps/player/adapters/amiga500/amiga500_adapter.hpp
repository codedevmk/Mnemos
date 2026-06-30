#pragma once

#include "amiga500_system.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"
#include "save_state.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::amiga500 {

    class amiga500_adapter;
    void force_link() noexcept;
    [[nodiscard]] runtime::save_target build_save_target(amiga500_adapter& adapter);

    class amiga500_adapter final : public frontend_sdk::player_system {
      public:
        explicit amiga500_adapter(std::vector<std::uint8_t> kickstart_rom,
                                  const manifests::amiga500::amiga500_config& config = {},
                                  std::string display_name = {},
                                  frontend_sdk::scheduler_factory* scheduler_factory = nullptr);
        explicit amiga500_adapter(std::vector<std::uint8_t> kickstart_rom,
                                  const manifests::amiga500::amiga500_config& config,
                                  std::string display_name,
                                  std::vector<std::vector<std::uint8_t>> disks,
                                  frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->agnus.framebuffer();
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
        [[nodiscard]] std::size_t media_count() const noexcept override { return disks_.size(); }
        [[nodiscard]] std::size_t current_media_index() const noexcept override {
            return disk_index_;
        }
        bool insert_media(std::size_t index) noexcept override;
        [[nodiscard]] std::vector<std::uint8_t> save_state() override;
        [[nodiscard]] runtime::load_result load_state(std::span<const std::uint8_t> data) override;

        [[nodiscard]] manifests::amiga500::amiga500_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        friend runtime::save_target build_save_target(amiga500_adapter& adapter);

        void save_adapter_state(chips::state_writer& writer) const;
        void load_adapter_state(chips::state_reader& reader);

        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::amiga500::amiga500_system> sys_;
        std::unique_ptr<chips::ichip> board_debug_chip_;
        instrumentation::span_memory_view chip_ram_view_;
        std::array<instrumentation::memory_view*, 1> system_mem_view_{};
        std::array<chips::ichip*, 7> chip_view_{};
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        manifests::amiga500::amiga500_keyboard_layout keyboard_layout_;
        manifests::amiga500::amiga500_model model_;
        double target_fps_;
        std::vector<frontend_sdk::spec_field> spec_{};
        std::array<frontend_sdk::controller_state, 6> ports_{};
        std::array<bool, manifests::amiga500::amiga500_system::keyboard_raw_key_count>
            reported_keyboard_keys_{};
        std::vector<std::vector<std::uint8_t>> disks_{};
        std::size_t disk_index_{};
        bool mouse_position_valid_{};
        std::int16_t mouse_x_{-1};
        std::int16_t mouse_y_{-1};

        std::vector<std::int16_t> paula_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::amiga500
