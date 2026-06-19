#include "spectrum_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "spectrum_snapshot.hpp"

#include <memory>
#include <utility>

namespace mnemos::apps::player::adapters::spectrum {

    namespace {
        using manifests::spectrum::spectrum_model;

        // Scheduler ticks count Z80 T-states. ULA first: it advances the frame /
        // raises the /INT the CPU then samples, then the CPU + beeper run; the 128K
        // AY ticks too (its /32 divider yields the 1.7734 MHz/16 sample rate).
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::spectrum::spectrum_system& sys) {
            std::vector<runtime::scheduled_chip> chips{
                {&sys.ula, 1U},
                {&sys.cpu, 1U},
                {&sys.beeper, 1U},
            };
            if (sys.model == spectrum_model::k128) {
                chips.push_back({&sys.ay, 1U});
            }
            return chips;
        }

        // Resample one mono/stereo source into the output-frame accumulators
        // (mirrors the SMS adapter's mixer).
        void add_source(std::int32_t* acc_l, std::int32_t* acc_r, const std::int16_t* src,
                        int chans, int count, int gain, int dst_count) noexcept {
            if (src == nullptr || count <= 0 || dst_count <= 0) {
                return;
            }
            const double scale = static_cast<double>(count) / static_cast<double>(dst_count);
            for (int i = 0; i < dst_count; ++i) {
                int l = 0;
                int r = 0;
                if (scale > 1.0) {
                    l = mnemos::dsp::sample_channel_box(src, chans, 0, count, scale * i,
                                                        scale * (i + 1));
                    r = chans == 2 ? mnemos::dsp::sample_channel_box(src, chans, 1, count,
                                                                     scale * i, scale * (i + 1))
                                   : l;
                } else {
                    l = mnemos::dsp::sample_channel_linear(src, chans, 0, count, scale * i);
                    r = chans == 2
                            ? mnemos::dsp::sample_channel_linear(src, chans, 1, count, scale * i)
                            : l;
                }
                acc_l[i] += mnemos::dsp::scale_q12(l, gain);
                acc_r[i] += mnemos::dsp::scale_q12(r, gain);
            }
        }
    } // namespace

    spectrum_adapter::spectrum_adapter(std::vector<std::uint8_t> rom,
                                       const manifests::spectrum::spectrum_config& config,
                                       std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::span<const std::uint8_t> snapshot)
        : sys_(manifests::spectrum::assemble_spectrum(rom, config)),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->ula)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        const bool is_128k = sys_->model == spectrum_model::k128;
        chip_view_[chip_count_++] = &sys_->ula;
        chip_view_[chip_count_++] = &sys_->cpu;
        chip_view_[chip_count_++] = &sys_->beeper;
        sys_->beeper.enable_audio_capture(true);
        if (is_128k) {
            chip_view_[chip_count_++] = &sys_->ay;
            sys_->ay.enable_audio_capture(true);
        }

        // A snapshot (.z80/.sna) resumes a game mid-run on top of the system ROM.
        if (!snapshot.empty()) {
            if (const auto snap = manifests::spectrum::load_snapshot(snapshot)) {
                sys_->apply_snapshot(*snap);
            }
        }

        spec_.push_back(
            {.label = "System", .value = is_128k ? "ZX Spectrum 128K" : "ZX Spectrum 48K"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Program", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region spectrum_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void spectrum_adapter::step_one_frame() { scheduler_.run_frame(); }

    frontend_sdk::audio_chunk spectrum_adapter::drain_audio() noexcept {
        // Drain the beeper (mono) and, on the 128K, the AY (interleaved stereo).
        const std::size_t beeper_count = sys_->beeper.pending_samples();
        beeper_buf_.resize(beeper_count);
        const int beeper_got =
            static_cast<int>(sys_->beeper.drain_samples(beeper_buf_.data(), beeper_count));

        int ay_got = 0;
        if (sys_->model == spectrum_model::k128) {
            const std::size_t ay_pairs = sys_->ay.pending_samples();
            ay_buf_.resize(ay_pairs * 2U);
            ay_got = static_cast<int>(sys_->ay.drain_samples(ay_buf_.data(), ay_pairs));
        }
        if (beeper_got == 0 && ay_got == 0) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }

        // Output samples for this frame, carrying the fractional remainder so the
        // long-term rate is exact.
        const double exact =
            (static_cast<double>(mnemos::dsp::kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        // Beeper is already centred near full headroom; the AY's three channels get
        // half-scale so the sum stays clear of clipping.
        add_source(acc_l_.data(), acc_r_.data(), beeper_buf_.data(), 1, beeper_got,
                   mnemos::dsp::kMixerGainOne, dst_pairs);
        add_source(acc_l_.data(), acc_r_.data(), ay_buf_.data(), 2, ay_got,
                   mnemos::dsp::kMixerGainOne / 2, dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U] = mnemos::dsp::clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = mnemos::dsp::clip_i16(acc_r_[i]);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = mnemos::dsp::kOutputRate};
    }

    void spectrum_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port != 0) {
            return; // one player for now (keyboard + Kempston)
        }
        auto& sys = *sys_;
        const bool fire = state.a || state.b;

        // Kempston joystick at port $1F (active-HIGH): right/left/down/up/fire.
        std::uint8_t k = 0;
        if (state.right) {
            k |= 0x01U;
        }
        if (state.left) {
            k |= 0x02U;
        }
        if (state.down) {
            k |= 0x04U;
        }
        if (state.up) {
            k |= 0x08U;
        }
        if (fire) {
            k |= 0x10U;
        }
        sys.kempston = k;

        // Keyboard: rebuild from released, then press the mapped keys. The dpad
        // drives the Sinclair cursor keys (5/6/7/8) so cursor-control games respond
        // too; fire -> 0, Enter, Space and CAPS SHIFT are also exposed.
        sys.keyboard_rows.fill(0xFFU);
        sys.set_key(3, 4, state.left);   // 5
        sys.set_key(4, 4, state.down);   // 6
        sys.set_key(4, 3, state.up);     // 7
        sys.set_key(4, 2, state.right);  // 8
        sys.set_key(4, 0, fire);         // 0 (fire)
        sys.set_key(6, 0, state.start);  // ENTER
        sys.set_key(7, 0, state.select); // SPACE
        sys.set_key(0, 0, state.mode);   // CAPS SHIFT
    }

    void force_link() noexcept {}

    namespace {
        const auto register_spectrum = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "spectrum",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    // main routes a .z80/.sna snapshot in as additional_media[0]
                    // (the system ROM is opts.rom).
                    const std::span<const std::uint8_t> snapshot =
                        opts.additional_media.empty()
                            ? std::span<const std::uint8_t>{}
                            : std::span<const std::uint8_t>(opts.additional_media.front());
                    return std::make_unique<spectrum_adapter>(
                        std::move(opts.rom),
                        manifests::spectrum::spectrum_config{.video_region = opts.video_region},
                        std::move(opts.display_name), opts.scheduler_factory_override, snapshot);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::spectrum
