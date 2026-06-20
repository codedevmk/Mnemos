#include "nes_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp" // mnemos::dsp::kOutputRate

#include <memory>
#include <utility>

namespace mnemos::apps::player::adapters::nes {

    namespace {
        // The 2C02 dot clock is the master: the PPU ticks every cycle and is the
        // frame source. NTSC clocks the CPU + APU once per three dots (3:1). PAL
        // (2A07) runs the CPU at master/16 and the PPU at master/5, so the CPU ticks
        // once per 3.2 dots -- a rational rate (5 chip cycles per 16 master dots).
        std::vector<runtime::scheduled_chip> build_schedule(manifests::nes::nes_system& sys,
                                                            bool pal) {
            if (pal) {
                return {
                    {&sys.ppu, 1U},
                    {&sys.cpu, 1U, 16U, 5U},
                    {&sys.apu, 1U, 16U, 5U},
                };
            }
            return {
                {&sys.ppu, 1U},
                {&sys.cpu, 3U},
                {&sys.apu, 3U},
            };
        }
    } // namespace

    nes_adapter::nes_adapter(std::vector<std::uint8_t> rom,
                             const manifests::nes::nes_config& config, std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::nes::assemble_nes(rom, config)),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory,
              build_schedule(*sys_, config.video_region == mnemos::video_region::pal), &sys_->ppu)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        chip_view_[0] = &sys_->ppu;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->apu;
        sys_->apu.enable_audio_capture(true);

        spec_.push_back({.label = "System", .value = "Nintendo Entertainment System"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Program", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region nes_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void nes_adapter::step_one_frame() { scheduler_.run_frame(); }

    frontend_sdk::audio_chunk nes_adapter::drain_audio() noexcept {
        const std::size_t pairs = sys_->apu.pending_samples();
        if (pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }
        apu_buf_.resize(pairs * 2U);
        sys_->apu.drain_samples(apu_buf_.data(), pairs);

        // Output samples for this frame, carrying the fractional remainder so the
        // long-term output rate is exact.
        const double exact =
            (static_cast<double>(mnemos::dsp::kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        const double scale = static_cast<double>(pairs) / static_cast<double>(dst_pairs);
        for (int i = 0; i < dst_pairs; ++i) {
            // The APU mix is a full-range int16 duplicated to both lanes; resample
            // lane 0 (stride 2) and write it to both output lanes.
            const int s = mnemos::dsp::sample_channel_box(
                apu_buf_.data(), 2, 0, static_cast<int>(pairs), scale * i, scale * (i + 1));
            const std::int16_t out = mnemos::dsp::clip_i16(s);
            mix_buf_[static_cast<std::size_t>(i) * 2U] = out;
            mix_buf_[static_cast<std::size_t>(i) * 2U + 1U] = out;
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = mnemos::dsp::kOutputRate};
    }

    void nes_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two standard pads
        }
        using sys = manifests::nes::nes_system;
        std::uint8_t buttons = 0U;
        if (state.a) {
            buttons |= sys::btn_a;
        }
        if (state.b) {
            buttons |= sys::btn_b;
        }
        if (state.select) {
            buttons |= sys::btn_select;
        }
        if (state.start) {
            buttons |= sys::btn_start;
        }
        if (state.up) {
            buttons |= sys::btn_up;
        }
        if (state.down) {
            buttons |= sys::btn_down;
        }
        if (state.left) {
            buttons |= sys::btn_left;
        }
        if (state.right) {
            buttons |= sys::btn_right;
        }
        sys_->set_pad(port, buttons);
    }

    void force_link() noexcept {}

    namespace {
        const auto register_nes = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "nes",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<nes_adapter>(
                        std::move(opts.rom),
                        manifests::nes::nes_config{.video_region = opts.video_region},
                        std::move(opts.display_name), opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::nes
