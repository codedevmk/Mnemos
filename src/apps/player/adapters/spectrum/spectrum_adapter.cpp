#include "spectrum_adapter.hpp"

#include "adapter_registry.hpp"
#include "spectrum_snapshot.hpp"

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
                {&sys.beeper, 1U},
            };
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
          region_(config.video_region) {
        chip_view_[0] = &sys_->ula;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->beeper;
        sys_->beeper.enable_audio_capture(true);

        // A snapshot (.z80/.sna) resumes a game mid-run on top of the system ROM.
        if (!snapshot.empty()) {
            if (const auto snap = manifests::spectrum::load_snapshot(snapshot)) {
                sys_->apply_snapshot(*snap);
            }
        }

        spec_.push_back({.label = "System", .value = "ZX Spectrum 48K"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Program", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region spectrum_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void spectrum_adapter::step_one_frame() { scheduler_.run_frame(); }

    frontend_sdk::audio_chunk spectrum_adapter::drain_audio() noexcept {
        const std::size_t count = sys_->beeper.pending_samples();
        if (count == 0U) {
            return {
                .samples = nullptr, .frame_count = 0U, .sample_rate = sys_->beeper.output_rate()};
        }
        beeper_buf_.resize(count);
        const std::size_t got = sys_->beeper.drain_samples(beeper_buf_.data(), count);
        stereo_buf_.resize(got * 2U);
        for (std::size_t i = 0; i < got; ++i) {
            stereo_buf_[i * 2U] = beeper_buf_[i];      // L
            stereo_buf_[i * 2U + 1U] = beeper_buf_[i]; // R
        }
        return {.samples = stereo_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(got),
                .sample_rate = sys_->beeper.output_rate()};
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
