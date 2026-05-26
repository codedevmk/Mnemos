#include "genesis_adapter.hpp"

#include <cctype>
#include <utility>

namespace mnemos::apps::player::adapters::genesis {

    manifests::genesis::genesis_config::region
    detect_region(std::span<const std::uint8_t> rom) noexcept {
        using region = manifests::genesis::genesis_config::region;
        if (rom.size() < 0x200) {
            return region::ntsc;
        }
        bool has_e = false;
        bool has_non_e = false;
        for (std::size_t i = 0x1F0; i < 0x1F3; ++i) {
            const auto raw = rom[i];
            const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            // ASCII region letters.
            if (c == 'E') {
                has_e = true;
            } else if (c == 'J' || c == 'U') {
                has_non_e = true;
            }
            // Hex-bitfield region byte (newer carts): bit 2 = Europe, bit 0 = Japan,
            // bit 1 = USA. Accept 0-9 / A-F as either char or raw byte.
            int hex = -1;
            if (c >= '0' && c <= '9') {
                hex = c - '0';
            } else if (c >= 'A' && c <= 'F') {
                hex = 10 + (c - 'A');
            } else if (raw <= 0x0FU) {
                hex = static_cast<int>(raw);
            }
            if (hex >= 0) {
                if ((hex & 0x04) != 0) {
                    has_e = true;
                }
                if ((hex & 0x03) != 0) {
                    has_non_e = true;
                }
            }
        }
        // Favor PAL when Europe is supported -- PAL-only display features
        // (V30, full vertical border budget) are then rendered the way the
        // cart's PAL-aware screens were authored for. Pure J/U carts stay
        // NTSC.
        return has_e ? region::pal : (has_non_e ? region::ntsc : region::ntsc);
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
        // Commit 5 wires this into the genesis_system controller MMIO; for now
        // the state is recorded but not surfaced on the bus.
    }

    frontend_sdk::audio_chunk genesis_adapter::drain_audio() noexcept {
        // Commit 7 wires the YM2612 + PSG audio sinks through here. Until
        // then the player runs silent.
        return {.samples = nullptr, .frame_count = 0U, .sample_rate = 0U};
    }

} // namespace mnemos::apps::player::adapters::genesis
