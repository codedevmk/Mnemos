#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <cstdio>
#include <utility>

namespace mnemos::apps::player::adapters::irem_m72 {

    namespace {

        using mnemos::manifests::common::rom_set_image;

        // Set loader. A .zip carrying a "game.toml" declaration (schema
        // mnemos-romset/1) loads declaratively -- per-file placement,
        // interleave, CRC verification -- with loader issues reported to
        // stderr. Without one, the development format applies: region-named
        // entries ("maincpu.bin", ...) loaded whole. A bare binary is the
        // V30 program image.
        [[nodiscard]] rom_set_image load_set(std::vector<std::uint8_t> rom) {
            rom_set_image image;
            const bool is_zip = rom.size() >= 4U && rom[0] == 'P' && rom[1] == 'K';
            if (is_zip) {
                if (auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
                    if (auto manifest_bytes = (*provider)("game.toml")) {
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        const auto parsed =
                            mnemos::manifests::common::parse_rom_set_decl(text, "game.toml");
                        if (!parsed.ok()) {
                            for (const auto& error : parsed.errors) {
                                std::fprintf(stderr, "[irem_m72] %s:%u:%u: %s\n",
                                             error.source.c_str(), error.line, error.column,
                                             error.message.c_str());
                            }
                            return image; // declared but invalid: boot an empty board
                        }
                        image = mnemos::manifests::common::load_rom_set(*parsed.value, *provider);
                        for (const auto& issue : image.issues) {
                            std::fprintf(stderr, "[irem_m72] %s: %s\n", issue.file.c_str(),
                                         issue.message.c_str());
                        }
                        return image;
                    }
                    for (const char* region :
                         {"maincpu", "soundcpu", "tiles_a", "tiles_b", "sprites"}) {
                        if (auto bytes = (*provider)(std::string{region} + ".bin")) {
                            image.regions.emplace(region, std::move(*bytes));
                        }
                    }
                }
                return image;
            }
            image.regions.emplace("maincpu", std::move(rom));
            return image;
        }

        [[nodiscard]] std::vector<runtime::scheduled_chip>
        build_schedule(manifests::irem_m72::m72_system& sys) {
            // 32 MHz board crystal: pixel clock /4, V30 /4 (8 MHz), Z80 /8
            // (4 MHz); the YM2151's own 3.579545 MHz crystal is not an integer
            // divider, so it runs on the rational rate (715909 chip cycles per
            // 6400000 master cycles). Video first so the CPUs observe the
            // advanced beam.
            std::vector<runtime::scheduled_chip> chips{
                {.chip = &sys.video, .divider = 4U},
                {.chip = &sys.main_cpu, .divider = 4U},
                {.chip = &sys.sound_cpu, .divider = 8U},
                {.chip = &sys.fm, .divider = 1U, .rate_num = 6400000U, .rate_den = 715909U}};
            if (sys.mcu_present) {
                // 8 MHz MCU crystal, 12 clocks per machine cycle: 32 MHz / 48.
                chips.push_back({.chip = &sys.mcu, .divider = 48U});
            }
            return chips;
        }

    } // namespace

    irem_m72_adapter::irem_m72_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::optional<std::uint16_t> dip_override)
        : sys_(manifests::irem_m72::assemble_m72(load_set(std::move(rom)))),
          scheduler_(frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_),
                                                  &sys_->video)) {
        if (dip_override.has_value()) {
            sys_->dip_switches = *dip_override;
        }
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu};
        if (sys_->mcu_present) {
            chip_view_.push_back(&sys_->mcu);
        }
        spec_ = {{"System", "Arcade"},
                 {"Board", "Irem M72"},
                 {"Game", display_name.empty() ? std::string{"unknown"} : std::move(display_name)}};
    }

    frontend_sdk::audio_chunk irem_m72_adapter::drain_audio() noexcept {
        // One stereo frame per 64 YM2151 clocks; the chip's elapsed-clock
        // counter is the sample clock, so drains never drift from emulation.
        const std::uint64_t due =
            sys_->fm.elapsed_clocks() / chips::audio::ym2151::clocks_per_sample;
        const std::uint64_t pending = due - samples_drained_;
        samples_drained_ = due;
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
        sys_->fm.update(audio_buf_);
        // Mix the DAC's held level into both channels (per-sample DAC timing
        // is a parity-pass refinement; the latch level changes at most once
        // per drain granule here).
        if (const std::int32_t dac = sys_->dac.output(); dac != 0) {
            for (std::int16_t& sample : audio_buf_) {
                const std::int32_t mixed = sample + dac;
                sample = static_cast<std::int16_t>(
                    mixed > 32767 ? 32767 : (mixed < -32768 ? -32768 : mixed));
            }
        }
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(pending),
                .sample_rate = 55930U}; // 3579545 / 64
    }

    void irem_m72_adapter::step_one_frame() {
        scheduler_.run_frame();
        ++frames_stepped_;
    }

    void irem_m72_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two-player hardware
        }
        ports_[static_cast<std::size_t>(port)] = state;

        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            std::uint8_t value = 0xFFU; // active low
            if (c.up) {
                value &= static_cast<std::uint8_t>(~0x01U);
            }
            if (c.down) {
                value &= static_cast<std::uint8_t>(~0x02U);
            }
            if (c.left) {
                value &= static_cast<std::uint8_t>(~0x04U);
            }
            if (c.right) {
                value &= static_cast<std::uint8_t>(~0x08U);
            }
            if (c.a) {
                value &= static_cast<std::uint8_t>(~0x10U);
            }
            if (c.b) {
                value &= static_cast<std::uint8_t>(~0x20U);
            }
            return value;
        };
        sys_->input_p1 = pack(ports_[0]);
        sys_->input_p2 = pack(ports_[1]);

        std::uint8_t system_byte = 0xFFU;
        if (ports_[0].select) {
            system_byte &= static_cast<std::uint8_t>(~0x01U); // coin 1
        }
        if (ports_[1].select) {
            system_byte &= static_cast<std::uint8_t>(~0x02U); // coin 2
        }
        if (ports_[0].start) {
            system_byte &= static_cast<std::uint8_t>(~0x04U); // start 1
        }
        if (ports_[1].start) {
            system_byte &= static_cast<std::uint8_t>(~0x08U); // start 2
        }
        sys_->input_system = system_byte;
    }

    void force_link() noexcept {}

    namespace {
        const auto register_irem_m72 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "irem_m72",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_m72_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::irem_m72
