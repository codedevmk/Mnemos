#include "capcom_cps1_adapter.hpp"

#include "adapter_registry.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace mnemos::apps::player::adapters::capcom_cps1 {

    namespace {

        using mnemos::manifests::common::rom_set_image;
        namespace cps1 = mnemos::manifests::capcom_cps1;

        struct loaded_set final {
            rom_set_image image;
            cps1::cps1_board_params params; // CPS-B profile etc. from the declaration
        };

        // Resolve a clone set's parent zip beside the clone on disk and compose
        // a fallback provider (clone first, then parent) so the parent's shared
        // dumps fill in. `rom_path` is the clone zip's own path; the parent zip
        // is `<dir>/<parent>.zip`. On any failure the clone provider is returned
        // unchanged -- load_rom_set then reports the missing shared files.
        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path) {
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[capcom_cps1] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return clone;
            }
            // Defence in depth: the loader already constrains `parent` to a plain
            // set id, but never build a path from one carrying a separator / "..".
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[capcom_cps1] refusing to resolve parent '%s': not a plain "
                             "set id\n",
                             parent.c_str());
                return clone;
            }
            const auto slash = rom_path.find_last_of("/\\");
            const std::string dir =
                slash == std::string::npos ? std::string{} : rom_path.substr(0, slash + 1);
            const std::string parent_path = dir + parent + ".zip";
            bool unreadable_zip = false;
            auto parent_provider = mnemos::manifests::common::make_zip_rom_provider_from_path(
                parent_path, &unreadable_zip);
            if (!parent_provider.has_value()) {
                std::fprintf(stderr, "[capcom_cps1] parent set %s: %s\n",
                             unreadable_zip ? "is not a readable zip" : "not found",
                             parent_path.c_str());
                return clone;
            }
            return mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
        }

        // Set loader. A .zip carrying a "game.toml" declaration (schema
        // mnemos-romset/1) loads declaratively -- per-file placement, interleave,
        // CRC verification -- with loader issues reported to stderr; the declared
        // `cps_b_profile` selects the board's hardware profile. A clone set names
        // a `parent`, whose zip (beside the clone, via `rom_path`) supplies the
        // shared dumps. Without a manifest the development format applies:
        // region-named entries ("maincpu", "gfx", "audiocpu", "oki") loaded
        // whole. A bare binary is the 68000 program.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
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
                                std::fprintf(stderr, "[capcom_cps1] %s:%u:%u: %s\n",
                                             error.source.c_str(), error.line, error.column,
                                             error.message.c_str());
                            }
                            return result; // declared but invalid: boot an empty board
                        }
                        const auto effective =
                            parsed.value->parent.has_value()
                                ? with_parent_fallback(*provider, *parsed.value->parent, rom_path)
                                : *provider;
                        result.image =
                            mnemos::manifests::common::load_rom_set(*parsed.value, effective);
                        // Thread the declared CPS-B profile id from the TOML into the
                        // board params (absent => the chip's legacy default profile).
                        result.params = cps1::board_params_from_decl(*parsed.value);
                        for (const auto& issue : result.image.issues) {
                            std::fprintf(stderr, "[capcom_cps1] %s: %s\n", issue.file.c_str(),
                                         issue.message.c_str());
                        }
                        return result;
                    }
                    for (const char* region : {"maincpu", "gfx", "audiocpu", "oki", "qsound"}) {
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

        [[nodiscard]] std::unique_ptr<cps1::cps1_system> assemble_from(loaded_set set) {
            return cps1::assemble_cps1(std::move(set.image), set.params);
        }

    } // namespace

    capcom_cps1_adapter::capcom_cps1_adapter(std::vector<std::uint8_t> rom,
                                             std::string display_name,
                                             frontend_sdk::scheduler_factory* /*scheduler_factory*/,
                                             std::optional<std::uint16_t> dip_override,
                                             std::string rom_path)
        // The CPS1 board integrates both CPUs + the sound chips + the beam in its
        // own run_frame(), so there is no per-chip master-clock schedule to build;
        // the scheduler_factory override (a multi-clock pacing hook) does not apply.
        : sys_(assemble_from(load_set(std::move(rom), rom_path))) {
        if (dip_override.has_value()) {
            // The arcade DIP override packs DSW A/B/C into the low three bytes.
            sys_->dip_a = static_cast<std::uint8_t>(*dip_override & 0xFFU);
            sys_->dip_b = static_cast<std::uint8_t>((*dip_override >> 8U) & 0xFFU);
        }
        if (sys_->uses_qsound()) {
            chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu, &sys_->qdsp};
        } else {
            chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu, &sys_->fm, &sys_->oki};
        }
        spec_ = {{"System", "Arcade"},
                 {"Board", "Capcom CPS1"},
                 {"Game", display_name.empty() ? std::string{"unknown"} : std::move(display_name)}};
    }

    void capcom_cps1_adapter::step_one_frame() {
        // The board's run_frame() integrates both CPUs, the sound chips, and the
        // beam (incl. the vblank IRQ tail) for exactly one frame.
        sys_->run_frame();
        ++frames_stepped_;
    }

    frontend_sdk::audio_chunk capcom_cps1_adapter::drain_audio() noexcept {
        if (sys_->uses_qsound()) {
            // QSound emits a fixed ~24 kHz stereo stream independent of the CPU
            // clock; pace the drain off the frame clock (native rate / fps) and
            // step the HLE mixer once per output frame via generate().
            constexpr std::uint32_t rate = chips::audio::qsound::native_sample_rate;
            const std::uint64_t due =
                frames_stepped_ * rate / manifests::capcom_cps1::frame_rate_hz;
            const std::uint64_t pending = due - samples_drained_;
            samples_drained_ = due;
            if (pending == 0U) {
                return {};
            }
            audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
            sys_->qdsp.generate(audio_buf_);
            return {.samples = audio_buf_.data(),
                    .frame_count = static_cast<std::uint32_t>(pending),
                    .sample_rate = rate};
        }
        // The YM2151 is the musical voice (one stereo frame per 64 chip clocks);
        // its elapsed-clock counter is the sample clock, so drains never drift.
        // The OKIM6295's held sample (DAC level) is mixed into both lanes -- its
        // own ~7.6 kHz queue resamples to the device rate in the frontend, but at
        // single-source granularity here the held level is the audible voice.
        const std::uint64_t due =
            sys_->fm.elapsed_clocks() / chips::audio::ym2151::clocks_per_sample;
        const std::uint64_t pending = due - samples_drained_;
        samples_drained_ = due;
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
        sys_->fm.update(audio_buf_);
        if (const std::int32_t voice = sys_->oki.last_sample(); voice != 0) {
            for (std::int16_t& sample : audio_buf_) {
                const std::int32_t mixed = sample + voice;
                sample = static_cast<std::int16_t>(
                    mixed > 32767 ? 32767 : (mixed < -32768 ? -32768 : mixed));
            }
        }
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(pending),
                .sample_rate = 55930U}; // 3579545 / 64
    }

    void capcom_cps1_adapter::apply_input(int port,
                                          const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two-player hardware
        }
        ports_[static_cast<std::size_t>(port)] = state;
        refresh_inputs();
    }

    void capcom_cps1_adapter::refresh_inputs() noexcept {
        // Player byte (active low): right/left/down/up in bits 0-3, buttons 1/2/3
        // in bits 4-6 (jab/strong/fierce on a six-button cab is the next refine).
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            std::uint8_t value = 0xFFU;
            const auto clear = [&value](std::uint8_t bit) {
                value &= static_cast<std::uint8_t>(~bit);
            };
            if (c.right) {
                clear(0x01U);
            }
            if (c.left) {
                clear(0x02U);
            }
            if (c.down) {
                clear(0x04U);
            }
            if (c.up) {
                clear(0x08U);
            }
            if (c.a) {
                clear(0x10U); // button 1
            }
            if (c.b) {
                clear(0x20U); // button 2
            }
            if (c.c) {
                clear(0x40U); // button 3
            }
            return value;
        };
        // P2 high byte, P1 low byte (the board's player-input word layout).
        const std::uint16_t player = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(pack(ports_[1])) << 8U) | pack(ports_[0]));

        // System byte (active low): start1/start2 in bits 0-1, coin1/coin2 (the
        // pads' `select`) in bits 4-5.
        std::uint8_t system_byte = 0xFFU;
        if (ports_[0].start) {
            system_byte &= static_cast<std::uint8_t>(~0x01U);
        }
        if (ports_[1].start) {
            system_byte &= static_cast<std::uint8_t>(~0x02U);
        }
        if (ports_[0].select) {
            system_byte &= static_cast<std::uint8_t>(~0x10U);
        }
        if (ports_[1].select) {
            system_byte &= static_cast<std::uint8_t>(~0x20U);
        }
        const std::uint16_t system = static_cast<std::uint16_t>(0xFF00U | system_byte);
        sys_->set_inputs(player, system, sys_->dip_a, sys_->dip_b, sys_->dip_c);
    }

    void force_link() noexcept {}

    namespace {
        const auto register_capcom_cps1 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "cps1",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<capcom_cps1_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::capcom_cps1
