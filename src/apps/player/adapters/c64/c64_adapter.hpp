#pragma once

// player_system adapter for the Commodore 64.
//
// Lives in the apps tier (8) and bridges the C64 manifest (tier 4) to the
// frontend_sdk::player_system interface (tier 7) -- the same role the Genesis
// and SMS adapters play for their systems. The player tool depends on whichever
// adapters a build wants supported; this one self-registers under "c64".
//
// The C64 is a home computer, not a cartridge console, so the bridge it builds
// differs from the console adapters even though the outward player_system
// contract is identical -- which is exactly what the multi-system architecture
// is for:
//
//   * Boot ROMs vs cart: there is no game cartridge in the common case. The
//     machine boots from three system images (BASIC 8K, KERNAL 8K, CHARGEN 4K).
//     The factory sources them from $MNEMOS_C64_ROM_DIR (matching the manifest
//     parity test), falling back to correctly-sized zero images so the player
//     still launches without a ROM set (boots to a blank machine, like the
//     parity test's synthetic path).
//   * Audio: the SID is sampled per φ2 cycle through its opt-in capture queue,
//     then downsampled to 48 kHz -- the same shape as the SMS PSG path.
//   * Input: the abstract controller_state maps onto a digital joystick on a
//     C64 control port (player 0 -> port 2, the usual game port), driven into
//     the keyboard/joystick matrix the CIA1 read callbacks resolve.
//
// One step_one_frame() advances exactly one VIC-II video frame.

#include "c64_runtime.hpp" // manifest-path build (also pulls c64_config + c64_input)
#include "player_system.hpp"
#include "region.hpp" // chips/shared: video_region
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::c64 {

    // Force-link hook so the adapter_registry self-registration in
    // c64_adapter.cpp survives the linker (see genesis_adapter.hpp for the
    // rationale). main.cpp calls this once at startup.
    void force_link() noexcept;

    class c64_adapter final : public frontend_sdk::player_system {
      public:
        // Build an adapter around the three moved-in system ROM images. The
        // 6510 boots from the KERNAL reset vector. `config.video_region`
        // selects 50 Hz PAL vs 60 Hz NTSC pacing (and the VIC revision +
        // φ2 clock). `display_name` is a presentation-layer label the player
        // surfaces in its status overlay (typically the media filename,
        // cleaned). Empty = no media row in the spec.
        explicit c64_adapter(std::vector<std::uint8_t> basic_rom,
                             std::vector<std::uint8_t> kernal_rom,
                             std::vector<std::uint8_t> chargen_rom,
                             const manifests::c64::c64_config& config = {},
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
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return chip_view_;
        }

        // For tests / instrumentation.
        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::c64::c64_runtime& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::c64::c64_runtime> sys_;
        // Non-owning chip pointers in scheduler order (VIC, CPU, CIA1, CIA2,
        // SID). Exposed via player_system::chips() so generic debug tooling can
        // enumerate them without depending on c64_runtime.
        std::array<chips::ichip*, 5> chip_view_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        // Video standard the adapter was built for. region() looks the milli-Hz
        // value up from the SDK's fps_x1000 table; no float round-trip.
        mnemos::video_region region_;
        // Same value in natural Hz, cached for the audio resampler.
        double target_fps_;
        std::uint64_t frames_stepped_{};

        // Pull-once status spec (System / Region / Media) the player reads back
        // through system_spec(). Populated by the constructor.
        std::vector<frontend_sdk::spec_field> spec_{};

        // Reusable scratch for drain_audio() so we don't reallocate per frame.
        // sid_buf_ holds the SID's drained mono samples at its φ2 capture rate;
        // mix_buf_ is the interleaved-stereo 48 kHz output the host queues.
        // audio_frac_ accumulates the fractional samples 48 kHz / (50|60) Hz
        // produces so the long-term output rate is exact.
        std::vector<std::int16_t> sid_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::c64
