#include "taito_f2_adapter.hpp"

#include "adapter_registry.hpp"
#include "input_pack.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::taito_f2 {

    namespace {
        namespace taito = mnemos::manifests::taito_f2;
        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            taito::taito_f2_board_params params;
        };

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "taito_f2.panel.p1",
                 .label = "Player 1 Panel"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "taito_f2.panel.p2",
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
                .provider_id = "taito_f2.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});
            return media;
        }

        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path) {
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[taito_f2] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return clone;
            }
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[taito_f2] refusing to resolve parent '%s': not a plain set id\n",
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
                std::fprintf(stderr, "[taito_f2] parent set %s: %s\n",
                             unreadable_zip ? "is not a readable zip" : "not found",
                             parent_path.c_str());
                return clone;
            }
            return mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
        }

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
                                std::fprintf(stderr, "[taito_f2] %s:%u:%u: %s\n",
                                             error.source.c_str(), error.line, error.column,
                                             error.message.c_str());
                            }
                            return result;
                        }
                        if (parsed.value->board != "taito_f2") {
                            std::fprintf(stderr,
                                         "[taito_f2] game.toml declares board '%s', expected "
                                         "'taito_f2'\n",
                                         parsed.value->board.c_str());
                            return result;
                        }
                        const auto effective =
                            parsed.value->parent.has_value()
                                ? with_parent_fallback(*provider, *parsed.value->parent, rom_path)
                                : *provider;
                        result.image =
                            mnemos::manifests::common::load_rom_set(*parsed.value, effective);
                        result.params = taito::board_params_from_decl(*parsed.value);
                        for (const auto& issue : result.image.issues) {
                            std::fprintf(stderr, "[taito_f2] %s: %s\n", issue.file.c_str(),
                                         issue.message.c_str());
                        }
                        return result;
                    }
                    for (const char* region : {"maincpu", "audiocpu", "tiles",
                                               "tiles_secondary", "sprites", "adpcma",
                                               "adpcmb"}) {
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

        [[nodiscard]] std::unique_ptr<taito::taito_f2_system> assemble_from(loaded_set set) {
            return taito::assemble_taito_f2(std::move(set.image), set.params);
        }

    } // namespace

    taito_f2_adapter::taito_f2_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* /*scheduler_factory*/,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path)
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(display_name, rom.size())),
          sys_(assemble_from(load_set(std::move(rom), rom_path))) {
        if (dip_override.has_value()) {
            sys_->dip_a = static_cast<std::uint8_t>(*dip_override & 0xFFU);
            sys_->dip_b = static_cast<std::uint8_t>((*dip_override >> 8U) & 0xFFU);
        }
        sys_->opnb.enable_audio_capture(true);
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu, &sys_->opnb};
        publish_memory_views();
        spec_ = {{"System", "Arcade"},
                 {"Board", "Taito F2"},
                 {"Game", display_name.empty() ? std::string{"unknown"} : std::move(display_name)}};
    }

    void taito_f2_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };
        publish(0U, "work_ram", sys_->work_ram);
        publish(1U, "palette_ram", sys_->palette_ram);
        publish(2U, "tile_ram", sys_->tile_ram);
        publish(3U, "tile_ram_secondary", sys_->tile_ram_secondary);
        publish(4U, "sprite_ram", sys_->sprite_ram);
        publish(5U, "roz_ram", sys_->roz_ram);
        publish(6U, "z80_ram", sys_->z80_ram);
    }

    void taito_f2_adapter::step_one_frame() {
        sys_->run_frame();
        ++frames_stepped_;
    }

    frontend_sdk::audio_chunk taito_f2_adapter::drain_audio() noexcept {
        const std::size_t pending = sys_->opnb.pending_samples();
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(pending * 2U, 0);
        const std::size_t drained = sys_->opnb.drain_samples(audio_buf_.data(), pending);
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(drained),
                .sample_rate = manifests::taito_f2::ym2610_clock_hz /
                               chips::audio::ym2610::default_clock_divider};
    }

    void taito_f2_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        refresh_inputs();
    }

    void taito_f2_adapter::refresh_inputs() noexcept {
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c, dpad_layout{},
                                       {{c.a, 0x10U}, {c.b, 0x20U}, {c.c, 0x40U}});
        };
        std::uint8_t system = 0xFFU;
        if (ports_[0].start) {
            system &= static_cast<std::uint8_t>(~0x01U);
        }
        if (ports_[1].start) {
            system &= static_cast<std::uint8_t>(~0x02U);
        }
        if (ports_[0].select) {
            system &= static_cast<std::uint8_t>(~0x10U);
        }
        if (ports_[1].select) {
            system &= static_cast<std::uint8_t>(~0x20U);
        }
        sys_->set_inputs(pack(ports_[0]), pack(ports_[1]), system, sys_->dip_a, sys_->dip_b);
    }

    void force_link() noexcept {}

    namespace {
        const auto register_taito_f2 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "taito_f2",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<taito_f2_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::taito_f2
