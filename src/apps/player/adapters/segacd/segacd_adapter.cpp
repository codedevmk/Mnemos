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
        // Genesis master divider, so the adapter paces them against the Genesis
        // master clock (NTSC; the small PAL delta is immaterial to sub timing).
        constexpr double kSubCpuHz = 12'500'000.0;
        constexpr double kGenesisMasterHz = 53'693'175.0;
        constexpr double kCdFrameHz = 75.0;
        // Interleave granularity: ~one NTSC scanline of master cycles. Small
        // enough that the main<->sub gate handshake makes progress every poll.
        constexpr std::uint64_t kSliceMasterCycles = 3420;

        // Mixer gains (Q12). FM keeps its 3:1 (~9.5 dB) bias over PSG, matching
        // the Genesis adapter; PCM + CD-DA join the sub side.
        constexpr int kGainFm = 3072;
        constexpr int kGainPsg = 1024;
        constexpr int kGainPcm = 2048;
        constexpr int kGainCdda = 4096; // CD audio is full-scale Red Book

        // CD-DA sample rate (Red Book).
        constexpr double kCddaHz = 44'100.0;

        // Scheduler order: VDP first (it drives the raster the 68000 samples),
        // then the gated 68000 (DMA stall) + gated Z80 (BUSREQ), FM, PSG.
        std::vector<runtime::scheduled_chip> build_schedule(manifests::genesis::genesis_system& g) {
            return {
                {&g.vdp, 1U}, {&g.cpu_gate, 7U}, {&g.z80_gate, 15U}, {&g.fm, 7U}, {&g.psg, 15U},
            };
        }

        // Resample one source (mono or interleaved stereo, `count` frames at its
        // native rate) to `dst_count` output frames and accumulate it, scaled by
        // `gain` (Q12), into the L/R sum buffers. A mono source feeds both sides.
        void add_source(std::int32_t* acc_l, std::int32_t* acc_r, const std::int16_t* src,
                        int chans, int count, int gain, int dst_count) noexcept {
            if (src == nullptr || count <= 0 || dst_count <= 0) {
                return;
            }
            const int stride = chans;
            const double scale = static_cast<double>(count) / static_cast<double>(dst_count);
            for (int i = 0; i < dst_count; ++i) {
                int l = 0;
                int r = 0;
                if (scale > 1.0) {
                    l = sample_channel_box(src, stride, 0, count, scale * i, scale * (i + 1));
                    r = chans == 2
                            ? sample_channel_box(src, stride, 1, count, scale * i, scale * (i + 1))
                            : l;
                } else {
                    l = sample_channel_linear(src, stride, 0, count, scale * i);
                    r = chans == 2 ? sample_channel_linear(src, stride, 1, count, scale * i) : l;
                }
                acc_l[i] += scale_q12(l, gain);
                acc_r[i] += scale_q12(r, gain);
            }
        }

    } // namespace

    segacd_adapter::segacd_adapter(std::vector<std::uint8_t> bios,
                                   const manifests::genesis::genesis_config& config,
                                   std::string display_name,
                                   frontend_sdk::scheduler_factory* scheduler_factory,
                                   const std::string& disc_path)
        : machine_(manifests::segacd::assemble_segacd_machine(std::move(bios), config)),
          work_ram_view_("work_ram", machine_->genesis->work_ram),
          prg_ram_view_("prg_ram", machine_->sub->prg_ram),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory, build_schedule(*machine_->genesis), &machine_->genesis->vdp)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        machine_->genesis->fm.enable_audio_capture(true);
        machine_->genesis->psg.enable_audio_capture(true);
        machine_->sub->pcm.enable_audio_capture(true);

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

        // Mount the CD image (if any) and hand it to the sub side. A failed open
        // leaves the drive empty -- the BIOS boots to its no-disc screen.
        if (!disc_path.empty()) {
            if (auto image = mnemos::disc::disc_image::open(disc_path)) {
                disc_ = std::make_unique<mnemos::disc::disc_image>(std::move(*image));
                machine_->sub->attach_disc(disc_.get());
            }
        }
    }

    frontend_sdk::video_region segacd_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view segacd_adapter::current_frame() const noexcept {
        return machine_->genesis->vdp.framebuffer();
    }

    void segacd_adapter::step_one_frame() {
        // Interleave the two 68000s at sub-frame granularity. The main and sub
        // CPUs hand-shake through the gate array + word RAM continuously (the
        // BIOS polls every few instructions), so running a whole main frame
        // before the sub gets any cycles deadlocks the handshake. Advance ~a
        // scanline of master cycles, then the sub's proportional share, until the
        // VDP completes a frame.
        const std::uint64_t start_frame = scheduler_.frame_index();
        while (scheduler_.frame_index() == start_frame) {
            scheduler_.run_master_cycles(kSliceMasterCycles);
            const double sub_exact =
                (static_cast<double>(kSliceMasterCycles) * kSubCpuHz / kGenesisMasterHz) +
                sub_cycle_frac_;
            const auto sub_cycles = static_cast<std::uint64_t>(sub_exact);
            sub_cycle_frac_ = sub_exact - static_cast<double>(sub_cycles);
            machine_->sub->run_cycles(sub_cycles); // no-op until the BIOS releases it
            machine_->sub->pcm.tick(sub_cycles);   // PCM runs regardless of sub-CPU reset
        }

        // Tick the CD drive at 75 Hz (raises CDC/CDD IRQs the sub-CPU services).
        const double cd_exact = (kCdFrameHz / target_fps_) + cd_frame_frac_;
        int cd_updates = static_cast<int>(cd_exact);
        cd_frame_frac_ = cd_exact - static_cast<double>(cd_updates);
        for (int i = 0; i < cd_updates; ++i) {
            machine_->sub->cdd_update();
        }

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
        // Mix all four sources: Genesis FM (stereo) + PSG (mono) + the sub side's
        // RF5C164 PCM (stereo) + CD-DA (stereo), each resampled to the output rate.
        const std::size_t fm_count = machine_->genesis->fm.pending_samples();
        const std::size_t psg_count = machine_->genesis->psg.pending_samples();
        const std::size_t pcm_count = machine_->sub->pcm.pending_samples();

        // Pull this frame's CD-DA at 44.1 kHz (yields nothing unless an audio
        // track is playing).
        const auto cdda_want = static_cast<std::size_t>(kCddaHz / target_fps_) + 1U;
        cdda_buf_.resize(cdda_want * 2U);
        std::size_t cdda_count = 0;
        for (std::size_t i = 0; i < cdda_want; ++i) {
            std::int16_t l = 0;
            std::int16_t r = 0;
            if (!machine_->sub->cdda_next_sample(l, r)) {
                break;
            }
            cdda_buf_[cdda_count * 2U] = l;
            cdda_buf_[cdda_count * 2U + 1U] = r;
            ++cdda_count;
        }

        if (fm_count == 0U && psg_count == 0U && pcm_count == 0U && cdda_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }

        if (fm_count > 0U) {
            fm_buf_.resize(fm_count * 2U);
            machine_->genesis->fm.drain_samples(fm_buf_.data(), fm_count);
        }
        if (psg_count > 0U) {
            psg_buf_.resize(psg_count);
            machine_->genesis->psg.drain_samples(psg_buf_.data(), psg_count);
        }
        if (pcm_count > 0U) {
            pcm_buf_.resize(pcm_count * 2U);
            machine_->sub->pcm.drain_samples(pcm_buf_.data(), pcm_count);
        }

        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        if (fm_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), fm_buf_.data(), 2, static_cast<int>(fm_count),
                       kGainFm, dst_pairs);
        }
        if (psg_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), psg_buf_.data(), 1,
                       static_cast<int>(psg_count), kGainPsg, dst_pairs);
        }
        if (pcm_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), pcm_buf_.data(), 2,
                       static_cast<int>(pcm_count), kGainPcm, dst_pairs);
        }
        if (cdda_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), cdda_buf_.data(), 2,
                       static_cast<int>(cdda_count), kGainCdda, dst_pairs);
        }

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U] = clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = clip_i16(acc_r_[i]);
        }
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
                        std::move(opts.display_name), opts.scheduler_factory_override,
                        opts.disc_path);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::segacd
