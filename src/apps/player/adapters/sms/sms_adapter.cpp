#include "sms_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

using mnemos::dsp::clip_i16;
using mnemos::dsp::kMixerGainOne;
using mnemos::dsp::kOutputRate;
using mnemos::dsp::sample_channel_box;
using mnemos::dsp::sample_channel_linear;
using mnemos::dsp::scale_q12;

namespace mnemos::apps::player::adapters::sms {

    namespace {

        // Scheduler ticks count Z80 cycles. VDP first: it drives the raster
        // the Z80 then samples. PSG ticks at the Z80 rate and applies its own
        // /16 internal divider.
        std::vector<runtime::scheduled_chip> build_schedule(manifests::sms::sms_runtime& sys) {
            std::vector<runtime::scheduled_chip> chips{
                {sys.vdp(), 1U},
                {sys.cpu(), 1U},
                {sys.psg(), 1U},
            };
            if (sys.fm_unit_enabled()) {
                chips.push_back({sys.fm(), 1U});
            }
            return chips;
        }

        // SMS mixer gains. Base SMS/GG PSG keeps the historical full-scale
        // output; FM-enabled SMS gives the two sources headroom before summing.
        constexpr int kGainPsg = kMixerGainOne;
        constexpr int kGainPsgWithFm = kMixerGainOne / 2;
        constexpr int kGainFm = kMixerGainOne / 2;

        // Resample one mono or stereo source into the output-frame accumulators.
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

    sms_adapter::sms_adapter(std::vector<std::uint8_t> rom,
                             const manifests::sms::sms_config& config, std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::sms::build_sms_runtime(std::move(rom), config)),
          work_ram_view_("work_ram", sys_->graph.region_span("work_ram")),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), sys_->vdp())),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]),
          game_gear_(config.game_gear), fm_unit_(sys_->fm_unit_enabled()) {
        sys_->psg()->enable_audio_capture(true);
        if (fm_unit_) {
            sys_->fm()->enable_audio_capture(true);
        }

        // Non-owning chip enumeration in scheduler order; matches build_schedule().
        chip_view_[chip_count_++] = sys_->vdp();
        chip_view_[chip_count_++] = sys_->cpu();
        chip_view_[chip_count_++] = sys_->psg();
        if (fm_unit_) {
            chip_view_[chip_count_++] = sys_->fm();
        }
        system_mem_view_[0] = &work_ram_view_;

        // Publish the static description once, post-init.
        spec_.push_back({.label = "System", .value = game_gear_ ? "Game Gear" : "Master System"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (fm_unit_) {
            spec_.push_back({.label = "Audio", .value = "PSG+FM"});
        }
        if (!display_name.empty()) {
            spec_.push_back({.label = "Cart", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region sms_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view sms_adapter::current_frame() const noexcept {
        return sys_->vdp()->framebuffer();
    }

    void sms_adapter::step_one_frame() {
        scheduler_.run_frame();
        ++frames_stepped_;
    }

    void sms_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
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
        // The Game Gear START button is a console key (not a pad pin); it reads
        // back through the $00 mode register. Drive it from player 1's START.
        if (game_gear_ && port == 0) {
            sys_->set_gg_start(state.start);
        }
    }

    frontend_sdk::audio_chunk sms_adapter::drain_audio() noexcept {
        const std::size_t psg_count = sys_->psg()->pending_samples();
        const std::size_t fm_count = fm_unit_ ? sys_->fm()->pending_samples() : 0U;
        if (psg_count == 0U && fm_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        if (psg_count > 0U) {
            psg_buf_.resize(psg_count);
            sys_->psg()->drain_samples(psg_buf_.data(), psg_count);
        } else {
            psg_buf_.clear();
        }
        if (fm_count > 0U) {
            fm_buf_.resize(fm_count * 2U);
            sys_->fm()->drain_samples(fm_buf_.data(), fm_count);
        } else {
            fm_buf_.clear();
        }

        // The Game Gear PSG queues interleaved L/R (2 samples per step); the SMS
        // queues mono. Resample each source channel with the box filter, reading
        // the queue at the matching stride.
        const int stride = game_gear_ ? 2 : 1;
        const int src_frames = static_cast<int>(psg_count) / stride;

        // Accumulate the fractional sample so the long-term output rate is
        // exact even when (kOutputRate / target_fps_) is not an integer.
        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        if (src_frames > 0) {
            add_source(acc_l_.data(), acc_r_.data(), psg_buf_.data(), stride, src_frames,
                       fm_unit_ ? kGainPsgWithFm : kGainPsg, dst_pairs);
        }
        if (fm_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), fm_buf_.data(), 2, static_cast<int>(fm_count),
                       kGainFm, dst_pairs);
        }

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U + 0U] = clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = clip_i16(acc_r_[i]);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        // Map the CLI `--mapper` override string to the SMS mapper selection.
        // Unrecognised / empty -> automatic (cart-header detection). Korean is
        // force-only, reachable solely through this explicit selection.
        [[nodiscard]] manifests::sms::sms_config::mapper
        mapper_from_override(const std::string& name) noexcept {
            using mapper = manifests::sms::sms_config::mapper;
            if (name == "sega") {
                return mapper::sega;
            }
            if (name == "codemasters") {
                return mapper::codemasters;
            }
            if (name == "korean") {
                return mapper::korean;
            }
            if (name == "korean-msx") {
                return mapper::korean_msx;
            }
            if (name == "korean-msx-nemesis") {
                return mapper::korean_msx_nemesis;
            }
            if (name == "korean-hicom") {
                return mapper::korean_hicom;
            }
            if (name == "korean-janggun") {
                return mapper::korean_janggun;
            }
            if (name == "korean-multi-4x8k") {
                return mapper::korean_multi_4x8k;
            }
            if (name == "korean-multi-16k") {
                return mapper::korean_multi_16k;
            }
            return mapper::automatic;
        }

        const auto register_sms = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "sms",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    auto* sched_factory = opts.scheduler_factory_override;
                    return std::make_unique<sms_adapter>(
                        std::move(opts.rom),
                        manifests::sms::sms_config{.video_region = opts.video_region,
                                                   .cartridge_mapper =
                                                       mapper_from_override(opts.mapper_override),
                                                   .fm_unit = opts.fm_unit},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();

        // The Game Gear reuses the SMS adapter (same Z80/VDP/PSG/mapper stack)
        // with game_gear enabled: GG VDP mode, PSG $06 stereo, and the $00-$06
        // handset. The handset is 60 Hz NTSC only, so the region is forced.
        const auto register_gg = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "gg",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    auto* sched_factory = opts.scheduler_factory_override;
                    return std::make_unique<sms_adapter>(
                        std::move(opts.rom),
                        manifests::sms::sms_config{.video_region = mnemos::video_region::ntsc,
                                                   .cartridge_mapper =
                                                       mapper_from_override(opts.mapper_override),
                                                   .game_gear = true},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::sms
