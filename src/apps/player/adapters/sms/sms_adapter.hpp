#pragma once

#include "player_system.hpp"
#include "region.hpp"
#include "scheduler.hpp"
#include "sms_system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::apps::player::adapters::sms {

    class sms_adapter final : public frontend_sdk::player_system {
      public:
        sms_adapter(std::vector<std::uint8_t> rom,
                    const manifests::sms::sms_config& config = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override;
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::sms::sms_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::sms::sms_system> sys_;
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        std::uint32_t fps_x1000_;
        std::uint64_t frames_stepped_{};

        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::sms
