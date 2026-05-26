#include "sms_adapter.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

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

        constexpr int kMixerGainShift = 12;
        constexpr int kMixerGainOne = 1 << kMixerGainShift;
        constexpr int kGainPsg = kMixerGainOne;
        constexpr std::uint32_t kOutputRate = 48000U;

        [[nodiscard]] inline std::int16_t clip_i16(int v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        [[nodiscard]] inline int scale_q12(int sample, int gain_q12) noexcept {
            int scaled = sample * gain_q12;
            scaled += scaled >= 0 ? (kMixerGainOne / 2) : -(kMixerGainOne / 2);
            return scaled / kMixerGainOne;
        }

        // Box-average [start, end) of src into one output sample.
        [[nodiscard]] inline int sample_box(const std::int16_t* src, int src_count, double start,
                                            double end) noexcept {
            if (!src || src_count <= 0) {
                return 0;
            }
            if (start < 0.0) {
                start = 0.0;
            }
            if (end > static_cast<double>(src_count)) {
                end = static_cast<double>(src_count);
            }
            if (end <= start) {
                int idx = static_cast<int>(start);
                if (idx >= src_count) {
                    idx = src_count - 1;
                }
                if (idx < 0) {
                    idx = 0;
                }
                return src[idx];
            }
            int first = static_cast<int>(start);
            int last = static_cast<int>(end);
            if (last >= src_count) {
                last = src_count - 1;
            }
            double accum = 0.0;
            double total = 0.0;
            for (int i = first; i <= last; ++i) {
                double seg_start = start > static_cast<double>(i) ? start : static_cast<double>(i);
                double seg_end =
                    end < static_cast<double>(i + 1) ? end : static_cast<double>(i + 1);
                double w = seg_end - seg_start;
                if (w <= 0.0) {
                    continue;
                }
                accum += static_cast<double>(src[i]) * w;
                total += w;
            }
            if (total <= 0.0) {
                return src[first];
            }
            return static_cast<int>(accum / total);
        }

    } // namespace

    sms_adapter::sms_adapter(std::vector<std::uint8_t> rom,
                             const manifests::sms::sms_config& config,
                             std::string display_name)
        : sys_(manifests::sms::assemble_sms(std::move(rom), config)),
          scheduler_(build_schedule(*sys_), &sys_->vdp),
          region_(config.video_region),
          target_fps_(mnemos::fps_x1000[static_cast<std::size_t>(config.video_region)] /
                      1000.0) {
        sys_->psg.enable_audio_capture(true);

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
            const int s = sample_box(psg_buf_.data(), static_cast<int>(psg_count),
                                     psg_scale * i, psg_scale * (i + 1));
            const std::int16_t out = clip_i16(scale_q12(s, kGainPsg));
            mix_buf_[i * 2 + 0] = out;
            mix_buf_[i * 2 + 1] = out; // duplicate mono into both stereo lanes
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

} // namespace mnemos::apps::player::adapters::sms
