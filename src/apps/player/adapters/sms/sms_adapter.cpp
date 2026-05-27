#include "sms_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

using mnemos::apps::player::adapters::clip_i16;
using mnemos::apps::player::adapters::kMixerGainOne;
using mnemos::apps::player::adapters::kMixerGainShift;
using mnemos::apps::player::adapters::kOutputRate;
using mnemos::apps::player::adapters::sample_channel_box;
using mnemos::apps::player::adapters::scale_q12;

namespace mnemos::apps::player::adapters::sms {

    namespace {

        // Scheduler ticks count Z80 cycles. VDP first: it drives the raster
        // the Z80 then samples. PSG ticks at the Z80 rate and applies its own
        // /16 internal divider.
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::sms::sms_system& sys) {
            return {
                {&sys.vdp, 1U},
                {&sys.cpu, 1U},
                {&sys.psg, 1U},
            };
        }

        // SMS-only PSG gain. The system-agnostic DSP helpers (clip_i16,
        // scale_q12, sample_channel_*) live in adapters/common.
        constexpr int kGainPsg = kMixerGainOne;

        runtime::scheduler make_scheduler(frontend_sdk::scheduler_factory* factory,
                                          std::vector<runtime::scheduled_chip> chips,
                                          chips::ivideo* frame_source) {
            if (factory != nullptr) {
                return factory->create(std::move(chips), frame_source);
            }
            return runtime::scheduler(std::move(chips), frame_source);
        }

    } // namespace

    sms_adapter::sms_adapter(std::vector<std::uint8_t> rom,
                             const manifests::sms::sms_config& config,
                             std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::sms::assemble_sms(std::move(rom), config)),
          scheduler_(make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->vdp)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        sys_->psg.enable_audio_capture(true);

        // Non-owning chip enumeration in scheduler order; matches build_schedule().
        chip_view_[0] = &sys_->vdp;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->psg;

        // Publish the static description once, post-init.
        spec_.push_back({.label = "System", .value = "Master System"});
        spec_.push_back({.label = "Region",
                         .value = config.video_region == mnemos::video_region::pal ? "PAL"
                                                                                   : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Cart", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region sms_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view sms_adapter::current_frame() const noexcept {
        return sys_->vdp.framebuffer();
    }

    void sms_adapter::step_one_frame() {
        scheduler_.run_frame();
        ++frames_stepped_;
    }

    void sms_adapter::apply_input(int port,
                                  const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        // Push the system-agnostic input to whichever device is plugged
        // into this port (default: SMS Control Pad); the device picks the
        // controller_state fields its hardware exposes.
        if (auto* dev = sys_->port_device(port)) {
            dev->apply_state(state);
        }
    }

    frontend_sdk::audio_chunk sms_adapter::drain_audio() noexcept {
        const std::size_t psg_count = sys_->psg.pending_samples();
        if (psg_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        psg_buf_.resize(psg_count);
        sys_->psg.drain_samples(psg_buf_.data(), psg_count);

        // Accumulate the fractional sample so the long-term output rate is
        // exact even when (kOutputRate / target_fps_) is not an integer.
        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        const double psg_scale =
            static_cast<double>(psg_count) / static_cast<double>(dst_pairs);
        for (int i = 0; i < dst_pairs; ++i) {
            const int s = sample_channel_box(psg_buf_.data(), 1, 0,
                                             static_cast<int>(psg_count),
                                             psg_scale * i, psg_scale * (i + 1));
            const std::int16_t out = clip_i16(scale_q12(s, kGainPsg));
            mix_buf_[i * 2 + 0] = out;
            mix_buf_[i * 2 + 1] = out; // duplicate mono into both stereo lanes
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        const auto register_sms = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "sms",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    auto* sched_factory = opts.scheduler_factory;
                    return std::make_unique<sms_adapter>(
                        std::move(opts.rom),
                        manifests::sms::sms_config{.video_region = opts.video_region},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::sms
