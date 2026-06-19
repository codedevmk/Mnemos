#include "spectrum_adapter.hpp"

#include "adapter_registry.hpp"

#include <memory>
#include <utility>

namespace mnemos::apps::player::adapters::spectrum {

    namespace {
        // Scheduler ticks count Z80 T-states. ULA first: it advances the frame /
        // raises the /INT the CPU then samples, then the CPU runs.
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::spectrum::spectrum_system& sys) {
            return {
                {&sys.ula, 1U},
                {&sys.cpu, 1U},
            };
        }
    } // namespace

    spectrum_adapter::spectrum_adapter(std::vector<std::uint8_t> rom,
                                       const manifests::spectrum::spectrum_config& config,
                                       std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::spectrum::assemble_spectrum(rom, config)),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->ula)),
          region_(config.video_region) {
        chip_view_[0] = &sys_->ula;
        chip_view_[1] = &sys_->cpu;

        spec_.push_back({.label = "System", .value = "ZX Spectrum 48K"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Program", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region spectrum_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void spectrum_adapter::step_one_frame() { scheduler_.run_frame(); }

    void spectrum_adapter::apply_input(int /*port*/,
                                       const frontend_sdk::controller_state& /*state*/) noexcept {
        // Keyboard input is a follow-up increment: the Spectrum's input is the full
        // 40-key matrix (port $FE half-rows), not a pad, so it needs a dedicated
        // host-key -> matrix mapping rather than the controller_state pad fields.
    }

    void force_link() noexcept {}

    namespace {
        const auto register_spectrum = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "spectrum",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<spectrum_adapter>(
                        std::move(opts.rom),
                        manifests::spectrum::spectrum_config{.video_region = opts.video_region},
                        std::move(opts.display_name), opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::spectrum
