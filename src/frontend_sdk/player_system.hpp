#pragma once

// The system-agnostic interface a windowed (or otherwise interactive) frontend
// uses to drive an emulated system. Concrete implementations live in the
// `manifests/<system>/` tiers (Genesis, SMS, C64, 32X, Sega CD, ...) so the
// same player binary can boot any of them without per-system glue in the
// frontend itself.
//
// The contract is intentionally small:
//
//   * one frame of pixels can be read at any time (the most recently
//     completed frame);
//   * the system advances exactly one video frame on demand (the frontend
//     paces these against vsync);
//   * input is a pre-mixed `controller_state` per port (the adapter maps
//     it onto its system's controller protocol);
//   * audio is drained as interleaved stereo s16 samples whose rate the
//     adapter declares (the frontend hands them to SDL_AudioStream which
//     resamples to the device rate).

#include "chip.hpp"       // for mnemos::chips::frame_buffer_view
#include "peripheral.hpp" // for mnemos::peripheral::controller_state

#include <cstdint>

namespace mnemos::frontend_sdk {

    // Video timing of the booted system. `frames_per_second_x1000` is the
    // refresh rate scaled by 1000 so NTSC (~59.94) and PAL (50.00) both fit
    // an integer without losing the meaningful decimal places.
    struct video_region final {
        std::uint32_t frames_per_second_x1000{60000U};
    };

    // System-agnostic controller state; the canonical definition lives in
    // peripheral/common so devices (tier 2) and frontends (tier 7) can both
    // reference it without a tier inversion. Re-exported here so existing
    // frontend_sdk callers don't have to change.
    using controller_state = mnemos::peripheral::controller_state;

    // A borrowed view of a stereo s16 audio chunk. `samples` is interleaved
    // L,R,L,R,... and points at `frame_count` (L,R) pairs at `sample_rate`
    // Hz. The pointer is valid until the next drain_audio() or
    // step_one_frame() on the same player_system, whichever comes first.
    struct audio_chunk final {
        const std::int16_t* samples{};
        std::uint32_t frame_count{};
        std::uint32_t sample_rate{};
    };

    // A bootable, frame-steppable, presentable, input-consuming system. The
    // windowed player owns one of these and drives it; everything system-
    // specific lives behind this interface.
    class player_system {
      public:
        virtual ~player_system() = default;

        // Video timing for vsync pacing in the frontend.
        [[nodiscard]] virtual video_region region() const noexcept = 0;

        // The most-recently-completed framebuffer. Pixel format and lifetime
        // follow chips::frame_buffer_view (0x00RRGGBB, valid until the next
        // frame-completing tick).
        [[nodiscard]] virtual chips::frame_buffer_view current_frame() const noexcept = 0;

        // Advance the system by exactly one video frame on its master clock.
        virtual void step_one_frame() = 0;

        // Latch `state` as the live input for `port` (0 = controller 1,
        // 1 = controller 2, ...). The adapter surfaces it on the next bus
        // read of the controller MMIO. Adapters MAY ignore ports past their
        // hardware's controller count.
        virtual void apply_input(int port, const controller_state& state) noexcept = 0;

        // Drain audio produced since the last drain (or since boot).
        // Returns an empty chunk (frame_count == 0) when the adapter has no
        // audio path wired yet -- the frontend handles silence cleanly.
        [[nodiscard]] virtual audio_chunk drain_audio() noexcept = 0;
    };

} // namespace mnemos::frontend_sdk
