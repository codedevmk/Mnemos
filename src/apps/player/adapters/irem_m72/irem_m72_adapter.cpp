#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::irem_m72 {

    namespace {

        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            std::string set_name; // from the declaration; empty for dev formats
            frontend_sdk::display_orientation orientation{
                frontend_sdk::display_orientation::horizontal};
        };

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m72.panel.p1",
                 .label = "Player 1 Panel"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m72.panel.p2",
                 .label = "Player 2 Panel"},
            };
            session.deterministic_frame_input = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    std::uint64_t byte_count) {
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "rom_set",
                .label = display_name.empty() ? std::string{"ROM set"} : std::string{display_name},
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = byte_count,
                .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                .provider_id = "irem_m72.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});
            return media;
        }

        [[nodiscard]] frontend_sdk::display_orientation
        to_display_orientation(mnemos::manifests::common::screen_orientation orientation) noexcept {
            return orientation == mnemos::manifests::common::screen_orientation::vertical
                       ? frontend_sdk::display_orientation::vertical
                       : frontend_sdk::display_orientation::horizontal;
        }

        // Set loader. A .zip carrying a "game.toml" declaration (schema
        // mnemos-romset/1) loads declaratively -- per-file placement,
        // interleave, CRC verification -- with loader issues reported to
        // stderr; the declared set name selects the per-game board wiring.
        // Without one, the development format applies: region-named entries
        // ("maincpu.bin", ...) loaded whole. A bare binary is the V30
        // program image.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom) {
            loaded_set result;
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
                            return result; // declared but invalid: boot an empty board
                        }
                        if (parsed.value->board != "irem_m72") {
                            std::fprintf(stderr,
                                         "[irem_m72] game.toml declares board '%s', expected "
                                         "'irem_m72'\n",
                                         parsed.value->board.c_str());
                            return result;
                        }
                        result.image =
                            mnemos::manifests::common::load_rom_set(*parsed.value, *provider);
                        result.set_name = parsed.value->name;
                        result.orientation = to_display_orientation(parsed.value->orientation);
                        for (const auto& issue : result.image.issues) {
                            std::fprintf(stderr, "[irem_m72] %s: %s\n", issue.file.c_str(),
                                         issue.message.c_str());
                        }
                        return result;
                    }
                    for (const char* region :
                         {"maincpu", "soundcpu", "tiles_a", "tiles_b", "sprites"}) {
                        if (auto bytes = (*provider)(std::string{region} + ".bin")) {
                            result.image.regions.emplace(region, std::move(*bytes));
                        }
                    }
                }
                return result;
            }
            result.image.regions.emplace("maincpu", std::move(rom));
            return result;
        }

        [[nodiscard]] std::unique_ptr<manifests::irem_m72::m72_system>
        assemble_from(loaded_set set) {
            return manifests::irem_m72::assemble_m72(
                std::move(set.image), manifests::irem_m72::board_params_for(set.set_name));
        }

        [[nodiscard]] std::vector<runtime::scheduled_chip>
        build_schedule(manifests::irem_m72::m72_system& sys) {
            // 32 MHz board crystal: pixel clock /4, V30 /4 (8 MHz effective).
            // The Z80 and the YM2151 share the separate 3.579545 MHz sound
            // crystal -- not an integer divider of the master, so both run on
            // the rational rate (715909 chip cycles per 6400000 master
            // cycles). Video first so the CPUs observe the advanced beam.
            std::vector<runtime::scheduled_chip> chips{
                {.chip = &sys.video, .divider = 4U},
                {.chip = &sys.main_cpu, .divider = 4U},
                {.chip = &sys.sound_cpu, .divider = 1U, .rate_num = 6400000U, .rate_den = 715909U},
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
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(display_name, rom.size())) {
        loaded_set set = load_set(std::move(rom));
        orientation_ = set.orientation;
        sys_ = assemble_from(std::move(set));
        scheduler_.emplace(
            frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->video));
        if (dip_override.has_value()) {
            sys_->dip_switches = *dip_override;
        }
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu};
        if (sys_->mcu_present) {
            chip_view_.push_back(&sys_->mcu);
        }
        publish_memory_views();
        spec_ = {{"System", "Arcade"},
                 {"Board", "Irem M72"},
                 {"Game", display_name.empty() ? std::string{"unknown"} : std::move(display_name)}};
    }

    void irem_m72_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };

        publish(0U, "work_ram", sys_->work_ram);
        publish(1U, "sound_ram", sys_->sound_ram);
        publish(2U, "sprite_ram", sys_->sprite_ram);
        publish(3U, "palette_a", sys_->palette_a);
        publish(4U, "palette_b", sys_->palette_b);
        publish(5U, "vram_a", sys_->vram_a);
        publish(6U, "vram_b", sys_->vram_b);
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
        scheduler_->run_frame();
        ++frames_stepped_;
    }

    void irem_m72_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two-player hardware
        }
        ports_[static_cast<std::size_t>(port)] = state;

        // Hardware bit layout (active low): joystick right/left/down/up from
        // bit 0, buttons 4..1 from bit 4 -- button 1 is the MSB.
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            std::uint8_t value = 0xFFU;
            if (c.right) {
                value &= static_cast<std::uint8_t>(~0x01U);
            }
            if (c.left) {
                value &= static_cast<std::uint8_t>(~0x02U);
            }
            if (c.down) {
                value &= static_cast<std::uint8_t>(~0x04U);
            }
            if (c.up) {
                value &= static_cast<std::uint8_t>(~0x08U);
            }
            if (c.a) {
                value &= static_cast<std::uint8_t>(~0x80U); // button 1
            }
            if (c.b) {
                value &= static_cast<std::uint8_t>(~0x40U); // button 2
            }
            return value;
        };
        sys_->input_p1 = pack(ports_[0]);
        sys_->input_p2 = pack(ports_[1]);

        // System byte: start1/start2/coin1/coin2 from bit 0, service at 4/5,
        // bit 7 = sprite-DMA-complete (always inactive here).
        std::uint8_t system_byte = 0xFFU;
        if (ports_[0].start) {
            system_byte &= static_cast<std::uint8_t>(~0x01U); // start 1
        }
        if (ports_[1].start) {
            system_byte &= static_cast<std::uint8_t>(~0x02U); // start 2
        }
        if (ports_[0].select) {
            system_byte &= static_cast<std::uint8_t>(~0x04U); // coin 1
        }
        if (ports_[1].select) {
            system_byte &= static_cast<std::uint8_t>(~0x08U); // coin 2
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
