#include "segacd_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

using mnemos::dsp::clip_i16;
using mnemos::dsp::kOutputRate;
using mnemos::dsp::sample_channel_box;
using mnemos::dsp::sample_channel_linear;
using mnemos::dsp::scale_q12;

namespace mnemos::apps::player::adapters::segacd {

    namespace {

        // The sub-CPU runs at 12.5 MHz; the CD drive ticks at 75 Hz. Neither is a
        // Genesis master divider, so the adapter paces them per video frame.
        constexpr double kSubCpuHz = 12'500'000.0;
        constexpr double kCdFrameHz = 75.0;

        // Genesis FM bias (3:1, ~9.5 dB) over PSG -- matches the Genesis adapter.
        constexpr int kGainFm = 3072;
        constexpr int kGainPsg = 1024;

        // Scheduler order: VDP first (it drives the raster the 68000 samples),
        // then the gated 68000 (DMA stall) + gated Z80 (BUSREQ), FM, PSG.
        std::vector<runtime::scheduled_chip> build_schedule(manifests::genesis::genesis_system& g) {
            return {
                {&g.vdp, 1U}, {&g.cpu_gate, 7U}, {&g.z80_gate, 15U}, {&g.fm, 7U}, {&g.psg, 15U},
            };
        }

        // Mix FM (stereo) + PSG (mono) into dst, each resampled from its native
        // rate to the output rate. Mirrors the Genesis adapter's mixer.
        void mix_genesis(const std::int16_t* fm_stereo, int fm_count, const std::int16_t* psg_mono,
                         int psg_count, std::int16_t* dst_stereo, int dst_count) noexcept {
            if (!dst_stereo || dst_count <= 0) {
                return;
            }
            if ((!fm_stereo || fm_count <= 0) && (!psg_mono || psg_count <= 0)) {
                std::fill_n(dst_stereo, dst_count * 2, std::int16_t{0});
                return;
            }
            const double fm_scale =
                fm_count > 0 ? static_cast<double>(fm_count) / static_cast<double>(dst_count) : 0.0;
            const double psg_scale =
                psg_count > 0 ? static_cast<double>(psg_count) / static_cast<double>(dst_count)
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
                dst_stereo[i * 2 + 0] =
                    clip_i16(scale_q12(left, kGainFm) + scale_q12(psg, kGainPsg));
                dst_stereo[i * 2 + 1] =
                    clip_i16(scale_q12(right, kGainFm) + scale_q12(psg, kGainPsg));
            }
        }

    } // namespace

    segacd_adapter::segacd_adapter(std::vector<std::uint8_t> bios,
                                   const manifests::genesis::genesis_config& config,
                                   std::string display_name,
                                   frontend_sdk::scheduler_factory* scheduler_factory)
        : machine_(manifests::segacd::assemble_segacd_machine(std::move(bios), config)),
          work_ram_view_("work_ram", machine_->genesis->work_ram),
          prg_ram_view_("prg_ram", machine_->sub->prg_ram),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory, build_schedule(*machine_->genesis), &machine_->genesis->vdp)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        machine_->genesis->fm.enable_audio_capture(true);
        machine_->genesis->psg.enable_audio_capture(true);

        chip_view_[0] = &machine_->genesis->vdp;
        chip_view_[1] = &machine_->genesis->cpu;
        chip_view_[2] = &machine_->genesis->z80;
        chip_view_[3] = &machine_->genesis->fm;
        chip_view_[4] = &machine_->genesis->psg;

        system_mem_view_[0] = &work_ram_view_;
        system_mem_view_[1] = &prg_ram_view_;

        spec_.push_back({.label = "System", .value = "Sega CD"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Disc", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region segacd_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view segacd_adapter::current_frame() const noexcept {
        return machine_->genesis->vdp.framebuffer();
    }

    void segacd_adapter::step_one_frame() {
        // Advance the Genesis a full video frame on the scheduler.
        scheduler_.run_frame();

        // Tick the CD drive at 75 Hz (raises CDC/CDD IRQs the sub-CPU services).
        const double cd_exact = (kCdFrameHz / target_fps_) + cd_frame_frac_;
        int cd_updates = static_cast<int>(cd_exact);
        cd_frame_frac_ = cd_exact - static_cast<double>(cd_updates);
        for (int i = 0; i < cd_updates; ++i) {
            machine_->sub->cdd_update();
        }

        // Run the sub-CPU for its share of this frame (a no-op until the BIOS
        // releases it via gate $01).
        const double sub_exact = (kSubCpuHz / target_fps_) + sub_cycle_frac_;
        const auto sub_cycles = static_cast<std::uint64_t>(sub_exact);
        sub_cycle_frac_ = sub_exact - static_cast<double>(sub_cycles);
        machine_->sub->run_cycles(sub_cycles);

        ++frames_stepped_;
    }

    void segacd_adapter::apply_input(int port,
                                     const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        if (auto* dev = machine_->genesis->port_device(port)) {
            dev->apply_state(state);
        }
    }

    frontend_sdk::audio_chunk segacd_adapter::drain_audio() noexcept {
        // D2: the Genesis FM + PSG mix. The CD-DA + RF5C164 PCM mix joins in D3.
        const std::size_t fm_count = machine_->genesis->fm.pending_samples();
        const std::size_t psg_count = machine_->genesis->psg.pending_samples();
        if (fm_count == 0U && psg_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        if (fm_count > 0U) {
            fm_buf_.resize(fm_count * 2U);
            machine_->genesis->fm.drain_samples(fm_buf_.data(), fm_count);
        } else {
            fm_buf_.clear();
        }
        if (psg_count > 0U) {
            psg_buf_.resize(psg_count);
            machine_->genesis->psg.drain_samples(psg_buf_.data(), psg_count);
        } else {
            psg_buf_.clear();
        }
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
        // Provisional registration: D2 boots the BIOS image directly. The real
        // .cue/.iso routing (find the BIOS + attach the disc) lands in D3.
        const auto register_segacd = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "segacd",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<segacd_adapter>(
                        std::move(opts.rom),
                        manifests::genesis::genesis_config{.video_region = opts.video_region},
                        std::move(opts.display_name), opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::segacd
