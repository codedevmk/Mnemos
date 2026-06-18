#pragma once

#include "player_system.hpp"
#include "region.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"
#include "sms_runtime.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::sms {

    // Force-link hook (see genesis_adapter.hpp for the rationale).
    void force_link() noexcept;

    class sms_adapter final : public frontend_sdk::player_system {
      public:
        explicit sms_adapter(std::vector<std::uint8_t> rom,
                             const manifests::sms::sms_config& config = {},
                             std::string display_name = {},
                             frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override;
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
        // Cartridge battery store (the 93C46 EEPROM's 128 bytes when present, else
        // empty) the frontend persists to .srm.
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            return sys_->battery_ram();
        }

        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::sms::sms_runtime& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        frontend_sdk::session_capability_info session_{};
        frontend_sdk::media_capability_info media_{};
        std::unique_ptr<manifests::sms::sms_runtime> sys_;
        std::array<chips::ichip*, 4> chip_view_{};
        std::size_t chip_count_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        // Video standard the adapter was built for. region() returns the
        // SDK's constant milli-Hz value for that standard; no float math.
        mnemos::video_region region_;
        // Same value in natural Hz, precomputed for the audio resampler.
        double target_fps_;
        // Game Gear hardware: the PSG queues interleaved L/R stereo (drained as
        // two channels) and port 0's START feeds the GG $00 mode register.
        bool game_gear_;
        bool fm_unit_;
        std::uint64_t frames_stepped_{};

        // Pull-once status spec (System / Region / Cart) the player reads
        // back through system_spec(). Populated by the constructor.
        std::vector<frontend_sdk::spec_field> spec_{};

        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> fm_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::sms
