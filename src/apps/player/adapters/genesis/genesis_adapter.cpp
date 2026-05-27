#include "genesis_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>

using mnemos::apps::player::adapters::clip_i16;
using mnemos::apps::player::adapters::kMixerGainOne;
using mnemos::apps::player::adapters::kMixerGainShift;
using mnemos::apps::player::adapters::kOutputRate;
using mnemos::apps::player::adapters::sample_channel_box;
using mnemos::apps::player::adapters::sample_channel_linear;
using mnemos::apps::player::adapters::scale_q12;

namespace mnemos::apps::player::adapters::genesis {

    namespace {

        // VDP first: it drives the raster the 68000 then samples. cpu_gate /
        // z80_gate let the 68K and Z80 stall on DMA / BUSREQ.
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::genesis::genesis_system& sys) {
            return {
                {&sys.vdp, 1U},
                {&sys.cpu_gate, 7U},
                {&sys.z80_gate, 15U},
                {&sys.fm, 7U},
                {&sys.psg, 15U},
            };
        }

        // Genesis-specific mixer gains. The system-agnostic DSP helpers
        // (clip_i16, scale_q12, sample_channel_*) live in adapters/common.
        // 3:1 (~9.5 dB) FM bias.
        constexpr int kGainFm = 3072;
        constexpr int kGainPsg = 1024;

        // Mix FM (stereo) + PSG (mono) into dst at dst_count samples, both
        // resampled from their respective source rates to the same target.
        void mix_genesis(const std::int16_t* fm_stereo, int fm_count,
                         const std::int16_t* psg_mono, int psg_count, std::int16_t* dst_stereo,
                         int dst_count) noexcept {
            if (!dst_stereo || dst_count <= 0) {
                return;
            }
            if ((!fm_stereo || fm_count <= 0) && (!psg_mono || psg_count <= 0)) {
                std::fill_n(dst_stereo, dst_count * 2, std::int16_t{0});
                return;
            }
            const double fm_scale =
                fm_count > 0 ? static_cast<double>(fm_count) / static_cast<double>(dst_count) : 0.0;
            const double psg_scale = psg_count > 0
                                       ? static_cast<double>(psg_count) / static_cast<double>(dst_count)
                                       : 0.0;
            for (int i = 0; i < dst_count; ++i) {
                int left = 0;
                int right = 0;
                int psg = 0;
                if (fm_count > 0) {
                    if (fm_scale > 1.0) {
                        left = sample_channel_box(fm_stereo, 2, 0, fm_count, fm_scale * i,
                                                  fm_scale * (i + 1));
                        right = sample_channel_box(fm_stereo, 2, 1, fm_count, fm_scale * i,
                                                   fm_scale * (i + 1));
                    } else {
                        left = sample_channel_linear(fm_stereo, 2, 0, fm_count, fm_scale * i);
                        right = sample_channel_linear(fm_stereo, 2, 1, fm_count, fm_scale * i);
                    }
                }
                if (psg_count > 0) {
                    if (psg_scale > 1.0) {
                        psg = sample_channel_box(psg_mono, 1, 0, psg_count, psg_scale * i,
                                                 psg_scale * (i + 1));
                    } else {
                        psg = sample_channel_linear(psg_mono, 1, 0, psg_count, psg_scale * i);
                    }
                }
                dst_stereo[i * 2 + 0] = clip_i16(scale_q12(left, kGainFm) + scale_q12(psg, kGainPsg));
                dst_stereo[i * 2 + 1] =
                    clip_i16(scale_q12(right, kGainFm) + scale_q12(psg, kGainPsg));
            }
        }

    } // namespace

    namespace {
        runtime::scheduler make_scheduler(frontend_sdk::scheduler_factory* factory,
                                          std::vector<runtime::scheduled_chip> chips,
                                          chips::ivideo* frame_source) {
            if (factory != nullptr) {
                return factory->create(std::move(chips), frame_source);
            }
            return runtime::scheduler(std::move(chips), frame_source);
        }
    } // namespace

    genesis_adapter::genesis_adapter(std::vector<std::uint8_t> rom,
                                     const manifests::genesis::genesis_config& config,
                                     std::string display_name,
                                     frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::genesis::assemble_genesis(std::move(rom), config)),
          scheduler_(make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->vdp)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        sys_->fm.enable_audio_capture(true);
        sys_->psg.enable_audio_capture(true);

        // Non-owning chip enumeration in scheduler order; matches build_schedule().
        // Note: cpu and z80 are exposed directly (not the *_gate wrappers) because
        // the gates are scheduling-side stalls, not the architectural chips
        // generic debug tools want to inspect.
        chip_view_[0] = &sys_->vdp;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->z80;
        chip_view_[3] = &sys_->fm;
        chip_view_[4] = &sys_->psg;

        // Publish the static description once, post-init.
        spec_.push_back({.label = "System", .value = "Genesis"});
        spec_.push_back({.label = "Region",
                         .value = config.video_region == mnemos::video_region::pal ? "PAL"
                                                                                   : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Cart", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region genesis_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
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
        // Whichever device is plugged into this port (3-button pad, 6-button
        // pad, lightgun, ...) picks the controller_state fields it cares
        // about; the bit-layout into its own protocol is the device's job.
        if (auto* dev = sys_->port_device(port)) {
            dev->apply_state(state);
        }
    }

    frontend_sdk::audio_chunk genesis_adapter::drain_audio() noexcept {
        // Drain both chip queues, resample each from its native rate into a
        // fixed 48 kHz stereo output (SDL_AudioStream's built-in resampler from
        // the raw chip rates produced audible artifacts + a growing queue).
        const std::size_t fm_count = sys_->fm.pending_samples();
        const std::size_t psg_count = sys_->psg.pending_samples();
        if (fm_count == 0U && psg_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        if (fm_count > 0U) {
            fm_buf_.resize(fm_count * 2U);
            sys_->fm.drain_samples(fm_buf_.data(), fm_count);
        } else {
            fm_buf_.clear();
        }
        if (psg_count > 0U) {
            psg_buf_.resize(psg_count);
            sys_->psg.drain_samples(psg_buf_.data(), psg_count);
        } else {
            psg_buf_.clear();
        }
        // Accumulate the fractional sample so the long-term output rate is
        // exact even when (kOutputRate / target_fps_) is not an integer.
        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        mix_genesis(fm_count > 0U ? fm_buf_.data() : nullptr, static_cast<int>(fm_count),
                    psg_count > 0U ? psg_buf_.data() : nullptr, static_cast<int>(psg_count),
                    mix_buf_.data(), dst_pairs);
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        const auto register_genesis = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "genesis",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    auto* sched_factory = opts.scheduler_factory;
                    return std::make_unique<genesis_adapter>(
                        std::move(opts.rom),
                        manifests::genesis::genesis_config{.video_region = opts.video_region},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::genesis
