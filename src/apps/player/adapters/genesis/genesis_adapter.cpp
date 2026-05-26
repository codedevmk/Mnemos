#include "genesis_adapter.hpp"

#include "region.hpp" // shared sega16 region detector

#include <utility>

namespace mnemos::apps::player::adapters::genesis {

    manifests::genesis::genesis_config::region
    detect_region(std::span<const std::uint8_t> rom) noexcept {
        using genesis_region = manifests::genesis::genesis_config::region;
        return detect_sega16_region(rom) == video_region::pal ? genesis_region::pal
                                                              : genesis_region::ntsc;
    }

    namespace {

        // The Genesis chip dividers, in scheduler dispatch order. The VDP comes
        // first because it advances the raster the 68000 then samples; the
        // gated Z80 runs only while it owns its bus (see manifests/genesis).
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::genesis::genesis_system& sys) {
            return {
                {&sys.vdp, 1U},
                {&sys.cpu, 7U},
                {&sys.z80_gate, 15U},
                {&sys.fm, 7U},
                {&sys.psg, 15U},
            };
        }

    } // namespace

    genesis_adapter::genesis_adapter(std::vector<std::uint8_t> rom,
                                     const manifests::genesis::genesis_config& config)
        : sys_(manifests::genesis::assemble_genesis(std::move(rom), config)),
          scheduler_(build_schedule(*sys_), &sys_->vdp),
          region_(config.video_region) {}

    frontend_sdk::video_region genesis_adapter::region() const noexcept {
        // NTSC Genesis runs at ~59.922 Hz; PAL at 49.701 Hz. We pace against
        // the standard whole-fps for now; tighter pinning is follow-up work.
        return {region_ == manifests::genesis::genesis_config::region::pal ? 50000U : 60000U};
    }

    chips::frame_buffer_view genesis_adapter::current_frame() const noexcept {
        return sys_->vdp.framebuffer();
    }

    void genesis_adapter::step_one_frame() {
        scheduler_.run_frame();
        ++frames_stepped_;
    }

    void genesis_adapter::apply_input(int port,
                                      const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        // Translate the system-agnostic controller_state into the Genesis pad
        // bitmask the system's $A10003/$A10005 read handler consumes. The
        // adapter is the right place for this translation: the genesis_system
        // doesn't know about frontend_sdk types (tier-4 manifest can't depend
        // on tier-7 frontend_sdk), and the frontend_sdk doesn't know about
        // system-specific button layouts.
        const std::uint8_t pad =
            (state.up ? 0x01U : 0U) | (state.down ? 0x02U : 0U) |
            (state.left ? 0x04U : 0U) | (state.right ? 0x08U : 0U) |
            (state.a ? 0x10U : 0U) | (state.b ? 0x20U : 0U) | (state.c ? 0x40U : 0U) |
            (state.start ? 0x80U : 0U);
        sys_->set_pad(port, pad);
    }

    frontend_sdk::audio_chunk genesis_adapter::drain_audio() noexcept {
        // Commit 7 wires the YM2612 + PSG audio sinks through here. Until
        // then the player runs silent.
        return {.samples = nullptr, .frame_count = 0U, .sample_rate = 0U};
    }

} // namespace mnemos::apps::player::adapters::genesis
