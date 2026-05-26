#pragma once

// player_system adapter for the Sega Master System.
//
// Mirrors the shape of the Genesis adapter: owns a heap-allocated sms_system
// and a scheduler wired against it (VDP /1, Z80 /1, PSG /1 -- the SMS counts
// scheduler ticks in Z80 cycles, the VDP drives frame boundaries at 228 Z80
// cycles per scanline). One step_one_frame() advances exactly one video
// frame. Audio is the PSG only (mono); the adapter duplicates it into stereo
// and resamples to the host-facing 48 kHz output rate.

#include "player_system.hpp"
#include "region.hpp" // chips/shared: video_region
#include "scheduler.hpp"
#include "sms_system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::apps::player::adapters::sms {

    class sms_adapter final : public frontend_sdk::player_system {
      public:
        // Build an adapter around a moved-in cartridge image. `config.video_region`
        // selects 60 Hz NTSC vs 50 Hz PAL pacing; cartridge_mapper auto-detects
        // Sega vs Codemasters by default.
        sms_adapter(std::vector<std::uint8_t> rom,
                    const manifests::sms::sms_config& config = {});

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override;
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;

        // For tests / instrumentation.
        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::sms::sms_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::sms::sms_system> sys_;
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        mnemos::video_region region_{mnemos::video_region::ntsc};
        std::uint64_t frames_stepped_{};

        // Scratch buffers for drain_audio (PSG mono -> resampled stereo @ 48 kHz).
        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::sms
