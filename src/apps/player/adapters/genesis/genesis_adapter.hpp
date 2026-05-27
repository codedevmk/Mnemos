#pragma once

// player_system adapter for the Sega Genesis / Mega Drive.
//
// Lives in the apps tier (8) because it bridges the manifest (tier 4) and
// the frontend_sdk (tier 7) -- the same shape every system adapter takes
// (one per system; SMS, C64, 32X, Sega CD adapters will sit alongside this
// one). The player tool depends on whichever adapters you want supported in
// that build.
//
// Owns a heap-allocated genesis_system and a scheduler wired against it
// (VDP /1, 68000 /7, Z80 /15, YM2612 /7, PSG /15 -- the VDP drives frame
// boundaries). One step_one_frame() advances exactly one video frame.
//
// Commit 3 keeps audio and input as no-ops: drain_audio() returns an empty
// chunk, and apply_input() records the state but the controller MMIO at
// $A10003 / $A10005 still reads the hardware-idle 0x7F until Commit 5 wires
// it through.

#include "genesis_system.hpp"
#include "player_system.hpp"
#include "region.hpp" // chips/shared: video_region
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::genesis {

    // Force-link hook so the adapter_registry self-registration in
    // genesis_adapter.cpp survives the linker. main.cpp calls this once at
    // startup; without it the entire genesis_adapter.cpp can be discarded
    // because main.cpp no longer references the genesis_adapter type by
    // name (it constructs via adapter_registry::create("genesis", ...)).
    void force_link() noexcept;

    class genesis_adapter final : public frontend_sdk::player_system {
      public:
        // Build an adapter around a moved-in cartridge image. The 68000 boots
        // from the ROM's reset vectors. `config.video_region` selects 60 Hz
        // NTSC vs 50 Hz PAL pacing. `display_name` is a presentation-layer
        // label the player surfaces in its status overlay (typically the
        // cart filename, cleaned). Empty = no cart row in the spec.
        explicit genesis_adapter(std::vector<std::uint8_t> rom,
                                 const manifests::genesis::genesis_config& config = {},
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
        [[nodiscard]] manifests::genesis::genesis_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::genesis::genesis_system> sys_;
        // Non-owning chip pointers in scheduler order. Populated by the ctor;
        // exposed via the player_system::chips() debug enumerator so generic
        // tools can walk the chip list without depending on genesis_system.
        std::array<chips::ichip*, 5> chip_view_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        // Video standard the adapter was built for. region() looks the
        // milli-Hz value up from the SDK's constant fps_x1000 table; no
        // float round-trip.
        mnemos::video_region region_;
        // Same value as a double in natural Hz, cached for the audio
        // resampler so it doesn't redo the lookup per drain.
        double target_fps_;
        std::uint64_t frames_stepped_{};

        // Pull-once status spec the player surfaces in its overlay. Filled
        // by the constructor at end of init (after the system is assembled,
        // before any frame steps) and never mutated thereafter.
        std::vector<frontend_sdk::spec_field> spec_{};

        // Reusable scratch buffers for drain_audio() so we don't reallocate
        // every frame. fm_buf_ holds the FM chip's drained interleaved-
        // stereo samples at the FM native rate; psg_buf_ holds the PSG's
        // mono samples at the much-higher PSG rate; mix_buf_ is the output
        // (interleaved stereo @ the fixed 48 kHz rate) the host queues to
        // its audio device. audio_frac_ accumulates the fractional samples
        // that 48 kHz / (50|60) Hz produces so the long-term rate is exact.
        std::vector<std::int16_t> fm_buf_{};
        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::genesis
