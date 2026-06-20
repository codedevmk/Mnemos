#include "nes_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp" // mnemos::dsp::kOutputRate

#include <memory>
#include <utility>

namespace mnemos::apps::player::adapters::nes {

    namespace {
        // The 2C02 dot clock is the master: the PPU ticks every cycle and is the
        // frame source; the CPU and APU tick once per three (NTSC 3:1).
        std::vector<runtime::scheduled_chip> build_schedule(manifests::nes::nes_system& sys) {
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
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->ppu)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        chip_view_[0] = &sys_->ppu;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->apu;

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
        // APU audio is the next increment; the NROM boot is video-only.
        return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
    }

    void nes_adapter::apply_input(int /*port*/,
                                  const frontend_sdk::controller_state& /*state*/) noexcept {
        // Controllers ($4016/$4017 shift registers) arrive in a later increment.
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
