#pragma once

// Captures a chip's register-write STREAM over time and writes it as a VGM song
// -- the temporal counterpart to asset_export / audio_export's snapshots. The
// caller advances the system one frame at a time and calls mark_frame() after
// each step_one_frame(); every frame becomes a VGM frame-wait, so the log plays
// back at the right tempo. The VGM is written on destruction.
//
// Currently logs the first SN76489 (the PSG) that exposes reg_writes(); the
// session knows nothing else system-specific. YM2612 (FM) support is additive.

#include "player_system.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace mnemos::debug {

    class reg_write_log_session final {
      public:
        // Finds the first SN76489 exposing its register-write stream and installs
        // a trace; if none is present the session is inactive (active() == false)
        // and the dtor is a no-op. `vgm_path` is written on destruction.
        reg_write_log_session(frontend_sdk::player_system& sys, const std::string& vgm_path);

        reg_write_log_session(const reg_write_log_session&) = delete;
        reg_write_log_session& operator=(const reg_write_log_session&) = delete;
        reg_write_log_session(reg_write_log_session&&) = delete;
        reg_write_log_session& operator=(reg_write_log_session&&) = delete;
        ~reg_write_log_session();

        [[nodiscard]] bool active() const noexcept { return target_ != nullptr; }

        // Mark the end of one emulated frame -- emits a VGM frame-wait command.
        void mark_frame() noexcept;

      private:
        chips::ichip* target_{};
        std::string path_;
        std::uint32_t psg_clock_{0};         // SN76489 clock for the VGM header
        std::uint32_t rate_{60};             // playback rate hint (Hz)
        std::uint8_t frame_wait_cmd_{0x62U}; // NTSC; 0x63 for PAL
        std::uint32_t samples_per_frame_{735U};
        std::uint64_t frames_{0};
        // Heap-held so the installed callback can outlive the construction stack.
        struct state;
        std::unique_ptr<state> state_;
    };

} // namespace mnemos::debug
