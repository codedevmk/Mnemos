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
            std::vector<runtime::scheduled_chip> sched;
            if (pal) {
                sched = {
                    {&sys.ppu, 1U},
                    {&sys.cpu, 1U, 16U, 5U},
                    {&sys.apu, 1U, 16U, 5U},
                };
            } else {
                sched = {
                    {&sys.ppu, 1U},
                    {&sys.cpu, 3U},
                    {&sys.apu, 3U},
                };
            }
            // A cart with expansion audio (Sunsoft 5B) clocks its sound chip at the
            // same rate as the CPU/APU (its own /16 prescaler sets the native rate).
            if (auto* ea = sys.mapper ? sys.mapper->expansion_audio() : nullptr) {
                if (pal) {
                    sched.push_back({ea, 1U, 16U, 5U});
                } else {
                    sched.push_back({ea, 3U});
                }
            }
            return sched;
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
        chip_view_ = {&sys_->ppu, &sys_->cpu, &sys_->apu};
        sys_->apu.enable_audio_capture(true);
        // Expose the expansion sound chip (if any) for introspection too; the mapper
        // already enabled its capture in reset().
        if (auto* ea = sys_->mapper ? sys_->mapper->expansion_audio() : nullptr) {
            chip_view_.push_back(ea);
        }

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

        // Cartridge expansion audio (Sunsoft 5B, ...): drain its native queue so it
        // can be resampled and summed into this output frame. Empty for every other
        // cart -- the 2A03 path below is then byte-identical to the no-expansion case.
        std::size_t exp_pairs = 0U;
        if (sys_->mapper != nullptr) {
            exp_pairs = sys_->mapper->expansion_audio_pending();
            if (exp_pairs != 0U) {
                exp_buf_.resize(exp_pairs * 2U);
                exp_pairs = sys_->mapper->drain_expansion_audio(exp_buf_.data(), exp_pairs);
            }
        }

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
        const double exp_scale =
            exp_pairs != 0U ? static_cast<double>(exp_pairs) / static_cast<double>(dst_pairs) : 0.0;
        // When expansion audio is present, attenuate both sources so the summed mix
        // keeps headroom (each can approach full scale on its own). The balance is
        // provisional -- it needs an ear A/B against a real 5B cart -- so it is kept in
        // these two named gains. Without expansion audio the 2A03 stays at full level.
        const double apu_gain = exp_pairs != 0U ? 0.55 : 1.0;
        constexpr double exp_gain = 0.50;
        // One-pole low-pass modelling the 2A03's ~14 kHz analog output filter: it
        // rolls off the high-frequency aliasing of the raw square/triangle edges
        // (the residual "crackle"). a = 1 - exp(-2*pi*fc/fs), fc=14k, fs=48k.
        constexpr double lp_a = 0.84;
        for (int i = 0; i < dst_pairs; ++i) {
            // The APU mix is a full-range int16 duplicated to both lanes; resample
            // lane 0 (stride 2) and write it to both output lanes.
            const int a = mnemos::dsp::sample_channel_box(
                apu_buf_.data(), 2, 0, static_cast<int>(pairs), scale * i, scale * (i + 1));
            double mixed = static_cast<double>(a) * apu_gain;
            if (exp_pairs != 0U) {
                const int e = mnemos::dsp::sample_channel_box(exp_buf_.data(), 2, 0,
                                                              static_cast<int>(exp_pairs),
                                                              exp_scale * i, exp_scale * (i + 1));
                mixed += static_cast<double>(e) * exp_gain;
            }
            lp_state_ += lp_a * (mixed - lp_state_);
            const std::int16_t out = mnemos::dsp::clip_i16(static_cast<int>(lp_state_));
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
        // With the Zapper plugged into port 2, that port feeds the gun (aim + trigger)
        // rather than a pad.
        if (port == 1 && sys_->zapper_enabled) {
            sys_->set_zapper(state.aim_x, state.aim_y, state.trigger);
            return;
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

    runtime::save_target build_save_target(manifests::nes::nes_system& sys) {
        runtime::save_target target;
        target.manifest_id = "nes";
        target.manifest_rev = 1U;
        target.chips.push_back({"cpu", &sys.cpu});
        target.chips.push_back({"ppu", &sys.ppu});
        target.chips.push_back({"apu", &sys.apu});
        target.memory.push_back({"wram", std::span<std::uint8_t>(sys.wram)});
        target.memory.push_back({"prg_ram", std::span<std::uint8_t>(sys.prg_ram)});
        if (sys.chr_is_ram && !sys.chr.empty()) {
            target.memory.push_back({"chr_ram", std::span<std::uint8_t>(sys.chr)});
        }
        target.components.push_back({"mapper",
                                     [&sys](chips::state_writer& w) {
                                         if (sys.mapper) {
                                             sys.mapper->save_state(w);
                                         }
                                     },
                                     [&sys](chips::state_reader& r) {
                                         if (sys.mapper) {
                                             sys.mapper->load_state(r);
                                         }
                                     }});
        return target;
    }

    namespace {
        const auto register_nes = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "nes",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    manifests::nes::nes_config cfg{.video_region = opts.video_region};
                    cfg.zapper = opts.light_gun; // --zapper plugs a gun into port 2
                    // The player passes the FDS BIOS (when the loaded file is a .fds
                    // disk) as the first bios image; assemble_nes uses it to build the
                    // RP2C33 RAM adapter. A plain cart leaves bios_images empty.
                    if (!opts.bios_images.empty()) {
                        cfg.fds_bios = std::move(opts.bios_images.front());
                    }
                    return std::make_unique<nes_adapter>(std::move(opts.rom), std::move(cfg),
                                                         std::move(opts.display_name),
                                                         opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::nes
