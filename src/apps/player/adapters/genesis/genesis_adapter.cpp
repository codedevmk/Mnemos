#include "genesis_adapter.hpp"

#include "region.hpp" // shared sega16 region detector

#include <algorithm>
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
          region_(config.video_region) {
        // Turn on per-chip audio capture so drain_audio() has samples to mix.
        // Headless callers that don't care about audio still pay only the
        // per-tick push cost; drain_audio is opt-in via the player.
        sys_->fm.enable_audio_capture(true);
        sys_->psg.enable_audio_capture(true);
    }

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
        // Drain whatever stereo samples the FM has queued since the last call.
        // The PSG runs much faster (master/15/16 ~ 223 kHz vs FM ~53 kHz);
        // for this first cut we mix it in by averaging its samples per-FM-
        // sample window and adding (scaled) to both channels. Fine for
        // playable audio; cleaner resampling is a polish pass.
        const std::size_t fm_pairs = sys_->fm.pending_samples();
        if (fm_pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = ym_sample_rate()};
        }
        mix_buf_.resize(fm_pairs * 2U);
        sys_->fm.drain_samples(mix_buf_.data(), fm_pairs);

        const std::size_t psg_avail = sys_->psg.pending_samples();
        if (psg_avail > 0U) {
            psg_buf_.resize(psg_avail);
            sys_->psg.drain_samples(psg_buf_.data(), psg_avail);
            // Map psg_avail psg samples uniformly onto fm_pairs windows. For
            // each FM pair, average the corresponding PSG window and add at
            // ~half gain to both channels (PSG is mono).
            const std::size_t window = std::max<std::size_t>(1U, psg_avail / fm_pairs);
            std::size_t psg_idx = 0;
            for (std::size_t i = 0; i < fm_pairs; ++i) {
                std::int32_t acc = 0;
                std::size_t cnt = 0;
                for (; cnt < window && psg_idx < psg_avail; ++cnt, ++psg_idx) {
                    acc += psg_buf_[psg_idx];
                }
                if (cnt == 0U) {
                    break;
                }
                const std::int32_t avg = (acc / static_cast<std::int32_t>(cnt)) / 2;
                auto& l = mix_buf_[i * 2U + 0U];
                auto& r = mix_buf_[i * 2U + 1U];
                const std::int32_t lm = static_cast<std::int32_t>(l) + avg;
                const std::int32_t rm = static_cast<std::int32_t>(r) + avg;
                l = static_cast<std::int16_t>(std::clamp(lm, -32768, 32767));
                r = static_cast<std::int16_t>(std::clamp(rm, -32768, 32767));
            }
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(fm_pairs),
                .sample_rate = ym_sample_rate()};
    }

    std::uint32_t genesis_adapter::ym_sample_rate() const noexcept {
        // YM2612 sample = master/7/144 = master/1008. NTSC master 53.693175 MHz
        // -> ~53267 Hz. PAL master 53.203424 MHz -> ~52781 Hz. SDL_AudioStream
        // resamples to the device rate, so this just needs to be the truth.
        return region_ == manifests::genesis::genesis_config::region::pal ? 52781U : 53267U;
    }

} // namespace mnemos::apps::player::adapters::genesis
