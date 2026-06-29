#include "taito_f2_adapter.hpp"

#include "adapter_registry.hpp"
#include "file.hpp"
#include "input_pack.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"
#include "taito_f2_game_manifests.hpp"
#include "zip_archive.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::taito_f2 {

    namespace {
        namespace taito = mnemos::manifests::taito_f2;
        using mnemos::manifests::common::rom_set_image;

        constexpr std::uint32_t taito_f2_adapter_state_version = 2U;

        struct loaded_set final {
            rom_set_image image;
            taito::taito_f2_board_params params;
        };

        struct parent_resolution final {
            mnemos::manifests::common::rom_file_provider provider;
            std::optional<mnemos::manifests::common::rom_set_decl> decl;
        };

        struct nested_set_zip final {
            std::string set_id;
            std::string entry_name;
            std::vector<std::uint8_t> bytes;
        };

        [[nodiscard]] std::uint8_t
        player_count_for(const taito::taito_f2_board_params& params) noexcept {
            return params.players;
        }

        [[nodiscard]] bool uses_split_panel_layout(
            const taito::taito_f2_board_params& params) noexcept {
            return params.input_profile == taito::taito_f2_input_profile::split_tmp82c265;
        }

        [[nodiscard]] bool uses_te7750_quad_layout(
            const taito::taito_f2_board_params& params) noexcept {
            return params.input_profile == taito::taito_f2_input_profile::te7750_quad;
        }

        frontend_sdk::session_capability_info make_session_capabilities(std::uint8_t players) {
            frontend_sdk::session_capability_info session{};
            session.input_ports.reserve(players);
            for (std::uint8_t i = 0U; i < players; ++i) {
                const std::uint8_t slot = static_cast<std::uint8_t>(i + 1U);
                session.input_ports.push_back(frontend_sdk::session_input_port{
                    .port_index = i,
                    .player_slot = slot,
                    .format = frontend_sdk::input_device_format::arcade_panel,
                    .device_id = "taito_f2.panel.p" + std::to_string(slot),
                    .label = "Player " + std::to_string(slot) + " Panel"});
            }
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
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

        [[nodiscard]] bool has_zip_signature(std::span<const std::uint8_t> bytes) noexcept {
            return bytes.size() >= 4U && bytes[0] == 'P' && bytes[1] == 'K';
        }

        [[nodiscard]] bool is_plain_set_id(std::string_view value) noexcept {
            if (value.empty()) {
                return false;
            }
            for (char c : value) {
                const auto u = static_cast<unsigned char>(c);
                if (std::isalnum(u) == 0 && c != '_') {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::string lowercase_ascii(std::string text) {
            for (char& c : text) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return text;
        }

        [[nodiscard]] std::string filename_from_path_like(std::string_view path_like) {
            const auto slash = path_like.find_last_of("/\\");
            return std::string(slash == std::string_view::npos ? path_like
                                                               : path_like.substr(slash + 1U));
        }

        [[nodiscard]] std::optional<std::string>
        set_id_from_path_like(std::string_view path_like) {
            std::string filename = filename_from_path_like(path_like);
            const auto dot = filename.find_last_of('.');
            if (dot != std::string::npos) {
                filename.resize(dot);
            }
            filename = lowercase_ascii(std::move(filename));
            if (!is_plain_set_id(filename)) {
                return std::nullopt;
            }
            return filename;
        }

        [[nodiscard]] bool has_zip_extension(std::string_view filename) {
            const auto dot = filename.find_last_of('.');
            if (dot == std::string_view::npos) {
                return false;
            }
            std::string extension = lowercase_ascii(std::string(filename.substr(dot)));
            return extension == ".zip";
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        parse_taito_decl(std::string_view text,
                         std::string source,
                         std::optional<std::string_view> expected_set = std::nullopt) {
            auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[taito_f2] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                }
                return std::nullopt;
            }
            if (parsed.value->board != "taito_f2") {
                std::fprintf(stderr,
                             "[taito_f2] %s declares board '%s', expected 'taito_f2'\n",
                             source.c_str(), parsed.value->board.c_str());
                return std::nullopt;
            }
            if (expected_set.has_value() && parsed.value->name != *expected_set) {
                std::fprintf(stderr, "[taito_f2] %s declares set '%s', expected '%.*s'\n",
                             source.c_str(), parsed.value->name.c_str(),
                             static_cast<int>(expected_set->size()), expected_set->data());
                return std::nullopt;
            }
            return std::move(*parsed.value);
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        embedded_decl_for_set(std::string_view set_id) {
            const std::string_view toml = taito::taito_f2_game_manifest_toml(set_id);
            if (toml.empty()) {
                return std::nullopt;
            }
            return parse_taito_decl(toml, "embedded:taito_f2/" + std::string(set_id) + ".toml",
                                    set_id);
        }

        [[nodiscard]] const mnemos::compression::zip_entry*
        find_zip_entry(const mnemos::compression::zip_archive& archive, std::string_view name) {
            for (const mnemos::compression::zip_entry& entry : archive.entries()) {
                if (entry.name == name) {
                    return &entry;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<nested_set_zip>
        extract_nested_set_zip(const mnemos::compression::zip_archive& archive,
                               std::optional<std::string_view> expected_set = std::nullopt) {
            for (const mnemos::compression::zip_entry& entry : archive.entries()) {
                const std::string filename = filename_from_path_like(entry.name);
                if (filename.empty() || !has_zip_extension(filename)) {
                    continue;
                }
                auto set_id = set_id_from_path_like(filename);
                if (!set_id.has_value()) {
                    continue;
                }
                if (expected_set.has_value() && *set_id != *expected_set) {
                    continue;
                }
                if (!embedded_decl_for_set(*set_id).has_value()) {
                    continue;
                }
                auto bytes = archive.extract(entry);
                if (!bytes.has_value()) {
                    continue;
                }
                return nested_set_zip{
                    .set_id = std::move(*set_id),
                    .entry_name = entry.name,
                    .bytes = std::move(*bytes),
                };
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string make_virtual_nested_path(const std::string& outer_rom_path,
                                                           std::string_view entry_name) {
            const std::string filename = filename_from_path_like(entry_name);
            if (outer_rom_path.empty()) {
                return filename;
            }
            const auto slash = outer_rom_path.find_last_of("/\\");
            if (slash == std::string::npos) {
                return filename;
            }
            return outer_rom_path.substr(0, slash + 1U) + filename;
        }

        [[nodiscard]] std::optional<parent_resolution>
        provider_from_archive_bytes(std::vector<std::uint8_t> bytes,
                                    const std::string& label,
                                    std::string_view expected_set,
                                    bool allow_embedded_decl) {
            auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(bytes));
            if (!provider.has_value()) {
                std::fprintf(stderr, "[taito_f2] parent set is not a readable zip: %s\n",
                             label.c_str());
                return std::nullopt;
            }

            parent_resolution resolved;
            if (auto manifest_bytes = (*provider)("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                resolved.decl = parse_taito_decl(text, label + "/game.toml", expected_set);
            } else if (allow_embedded_decl) {
                resolved.decl = embedded_decl_for_set(expected_set);
            }
            resolved.provider = std::move(*provider);
            return resolved;
        }

        [[nodiscard]] std::optional<parent_resolution>
        resolve_parent_archive(const std::string& parent,
                               const std::string& rom_path,
                               bool allow_embedded_parent_decl) {
            const auto slash = rom_path.find_last_of("/\\");
            const std::string dir =
                slash == std::string::npos ? std::string{} : rom_path.substr(0, slash + 1U);
            const std::string parent_path = dir + parent + ".zip";
            if (auto parent_bytes = mnemos::io::read_file(parent_path)) {
                return provider_from_archive_bytes(std::move(*parent_bytes), parent_path, parent,
                                                   allow_embedded_parent_decl);
            }

            if (dir.empty()) {
                std::fprintf(stderr, "[taito_f2] parent set not found: %s\n",
                             parent_path.c_str());
                return std::nullopt;
            }

            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path directory = fs::path(dir);
            for (const fs::directory_entry& sibling : fs::directory_iterator(directory, ec)) {
                if (ec) {
                    break;
                }
                if (!sibling.is_regular_file(ec)) {
                    ec.clear();
                    continue;
                }
                if (lowercase_ascii(sibling.path().extension().string()) != ".zip") {
                    continue;
                }
                auto outer = mnemos::io::read_file(sibling.path().string());
                if (!outer.has_value()) {
                    continue;
                }
                auto archive = mnemos::compression::zip_archive::open(
                    std::span<const std::uint8_t>(outer->data(), outer->size()));
                if (!archive.has_value()) {
                    continue;
                }
                auto nested = extract_nested_set_zip(*archive, std::string_view{parent});
                if (!nested.has_value()) {
                    continue;
                }
                const std::string label = sibling.path().string() + ":" + nested->entry_name;
                return provider_from_archive_bytes(std::move(nested->bytes), label, parent,
                                                   allow_embedded_parent_decl);
            }
            if (ec) {
                std::fprintf(stderr, "[taito_f2] parent set directory unreadable: %s\n",
                             dir.c_str());
            } else {
                std::fprintf(stderr, "[taito_f2] parent set not found: %s\n",
                             parent_path.c_str());
            }
            return std::nullopt;
        }

        [[nodiscard]] parent_resolution
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent,
                             const std::string& rom_path,
                             bool allow_embedded_parent_decl) {
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[taito_f2] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return {.provider = clone};
            }
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[taito_f2] refusing to resolve parent '%s': not a plain set id\n",
                             parent.c_str());
                return {.provider = clone};
            }
            auto parent_provider =
                resolve_parent_archive(parent, rom_path, allow_embedded_parent_decl);
            if (!parent_provider.has_value()) {
                return {.provider = clone};
            }
            parent_provider->provider = mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(parent_provider->provider));
            return std::move(*parent_provider);
        }

        [[nodiscard]] loaded_set
        load_declared_set(mnemos::manifests::common::rom_set_decl decl,
                          mnemos::manifests::common::rom_file_provider provider,
                          const std::string& rom_path,
                          bool allow_embedded_parent_decl) {
            loaded_set result;
            mnemos::manifests::common::rom_file_provider effective = std::move(provider);
            if (decl.parent.has_value()) {
                parent_resolution parent = with_parent_fallback(
                    effective, *decl.parent, rom_path, allow_embedded_parent_decl);
                if (parent.decl.has_value()) {
                    decl = mnemos::manifests::common::inherit_parent_regions(
                        *parent.decl, std::move(decl));
                }
                effective = std::move(parent.provider);
            }
            result.image = mnemos::manifests::common::load_rom_set(decl, effective);
            result.params = taito::board_params_from_decl(decl);
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[taito_f2] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            return result;
        }

        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
            loaded_set result;
            if (has_zip_signature(std::span<const std::uint8_t>(rom.data(), rom.size()))) {
                const auto archive = mnemos::compression::zip_archive::open(
                    std::span<const std::uint8_t>(rom.data(), rom.size()));
                if (archive.has_value()) {
                    if (const auto* manifest = find_zip_entry(*archive, "game.toml")) {
                        auto manifest_bytes = archive->extract(*manifest);
                        if (!manifest_bytes.has_value()) {
                            return result;
                        }
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        auto decl = parse_taito_decl(text, "game.toml");
                        if (!decl.has_value()) {
                            return result; // declared but invalid: boot an empty board
                        }
                        if (auto provider =
                                mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
                            return load_declared_set(std::move(*decl), *provider, rom_path, false);
                        }
                        return result;
                    }
                    if (auto set_id = set_id_from_path_like(rom_path)) {
                        if (auto decl = embedded_decl_for_set(*set_id)) {
                            if (auto provider = mnemos::manifests::common::make_zip_rom_provider(
                                    std::move(rom))) {
                                return load_declared_set(std::move(*decl), *provider, rom_path,
                                                         true);
                            }
                            return result;
                        }
                    }
                    if (auto nested = extract_nested_set_zip(*archive)) {
                        const std::string nested_path =
                            make_virtual_nested_path(rom_path, nested->entry_name);
                        return load_set(std::move(nested->bytes), nested_path);
                    }
                }

                if (auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
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

        void save_controller_state(chips::state_writer& writer,
                                   const frontend_sdk::controller_state& state) {
            writer.boolean(state.up);
            writer.boolean(state.down);
            writer.boolean(state.left);
            writer.boolean(state.right);
            writer.boolean(state.start);
            writer.boolean(state.select);
            writer.boolean(state.a);
            writer.boolean(state.b);
            writer.boolean(state.c);
            writer.boolean(state.x);
            writer.boolean(state.y);
            writer.boolean(state.z);
            writer.boolean(state.mode);
            writer.boolean(state.service);
            writer.boolean(state.test);
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_x));
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_y));
            writer.boolean(state.trigger);
            writer.u16(state.paddle);
        }

        [[nodiscard]] frontend_sdk::controller_state
        load_controller_state(chips::state_reader& reader) noexcept {
            frontend_sdk::controller_state state{};
            state.up = reader.boolean();
            state.down = reader.boolean();
            state.left = reader.boolean();
            state.right = reader.boolean();
            state.start = reader.boolean();
            state.select = reader.boolean();
            state.a = reader.boolean();
            state.b = reader.boolean();
            state.c = reader.boolean();
            state.x = reader.boolean();
            state.y = reader.boolean();
            state.z = reader.boolean();
            state.mode = reader.boolean();
            state.service = reader.boolean();
            state.test = reader.boolean();
            state.aim_x = std::bit_cast<std::int16_t>(reader.u16());
            state.aim_y = std::bit_cast<std::int16_t>(reader.u16());
            state.trigger = reader.boolean();
            state.paddle = reader.u16();
            return state;
        }

    } // namespace

    taito_f2_adapter::taito_f2_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* /*scheduler_factory*/,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path)
        : session_(make_session_capabilities(2U)),
          media_(make_media_capabilities(display_name, rom.size())),
          sys_(assemble_from(load_set(std::move(rom), rom_path))) {
        player_count_ = player_count_for(sys_->params);
        session_ = make_session_capabilities(player_count_);
        if (dip_override.has_value()) {
            sys_->dip_a = static_cast<std::uint8_t>(*dip_override & 0xFFU);
            sys_->dip_b = static_cast<std::uint8_t>((*dip_override >> 8U) & 0xFFU);
        }
        sys_->opnb.enable_audio_capture(true);
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu, &sys_->opnb,
                      &sys_->sound_comm};
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
        auto publish_words = [&publish](std::size_t index, std::string_view name,
                                        std::span<const std::uint16_t> words) {
            publish(index, name,
                    std::span<const std::uint8_t>{
                        reinterpret_cast<const std::uint8_t*>(words.data()),
                        words.size() * sizeof(std::uint16_t)});
        };
        publish(0U, "work_ram", sys_->work_ram);
        publish(1U, "palette_ram", sys_->palette_ram);
        publish(2U, "tile_ram", sys_->tile_ram);
        publish(3U, "tile_ram_secondary", sys_->tile_ram_secondary);
        publish(4U, "sprite_ram", sys_->sprite_ram);
        publish(5U, "sprite_latched_ram", sys_->video.latched_sprite_buffer());
        publish(6U, "sprite_rendered_ram", sys_->video.last_render_sprite_buffer());
        publish(7U, "roz_ram", sys_->roz_ram);
        publish(8U, "z80_ram", sys_->z80_ram);
        publish(9U, "sound_bank_state", sys_->sound_bank_state);
        publish(10U, "io_output_regs", sys_->io_output_regs);
        publish(11U, "io_output_state", sys_->io_output_state);
        publish(12U, "palette_write_state", sys_->palette_write_state);
        publish_words(13U, "video_regs_raw", sys_->video_regs);
        publish_words(14U, "video_regs_secondary_raw", sys_->secondary_video_regs);
        publish_words(15U, "sprite_bank_regs_raw", sys_->sprite_bank_regs);
        publish_words(16U, "priority_regs_raw", sys_->priority_regs);
        publish_words(17U, "roz_control_regs_raw", sys_->roz_control_regs);
        publish_words(18U, "tc0480scp_control_regs_raw", sys_->tc0480scp_control_regs);
        publish(19U, "irq_state", sys_->irq_state);
        publish(20U, "board_profile_state", sys_->board_profile_state);
        publish(21U, "sound_reset_state", sys_->sound_reset_state);
        publish(22U, "watchdog_state", sys_->watchdog_state);
        publish(23U, "main_bus_state", sys_->main_bus_state);
        publish(24U, "io_access_state", sys_->io_access_state);
    }

    void taito_f2_adapter::step_one_frame() {
        sys_->run_frame();
        ++frames_stepped_;
    }

    bool taito_f2_adapter::run_debug_probe(std::string_view id) noexcept {
        if (id == "palette-readback") {
            sys_->run_palette_readback_probe();
            return true;
        }
        return false;
    }

    frontend_sdk::audio_chunk taito_f2_adapter::drain_audio() noexcept {
        constexpr std::uint32_t sample_rate = manifests::taito_f2::ym2610_clock_hz /
                                              chips::audio::ym2610::default_clock_divider;
        const std::size_t pending = sys_->opnb.pending_samples();
        if (pending == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = sample_rate};
        }
        audio_buf_.assign(pending * 2U, 0);
        const std::size_t drained = sys_->opnb.drain_samples(audio_buf_.data(), pending);
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(drained),
                .sample_rate = sample_rate};
    }

    void taito_f2_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || static_cast<std::size_t>(port) >= player_count_) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        refresh_inputs();
    }

    std::vector<std::uint8_t> taito_f2_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result taito_f2_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            audio_buf_.clear();
        }
        return result;
    }

    void taito_f2_adapter::refresh_inputs() noexcept {
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c, dpad_layout{},
                                       {{c.a, 0x10U}, {c.b, 0x20U}, {c.c, 0x40U}});
        };
        const auto pack_with_start =
            [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c,
                                       dpad_layout{.right = 0x08U,
                                                   .left = 0x04U,
                                                   .down = 0x02U,
                                                   .up = 0x01U},
                                       {{c.a, 0x10U},
                                        {c.b, 0x20U},
                                        {c.c, 0x40U},
                                        {c.start, 0x80U}});
        };

        if (uses_split_panel_layout(sys_->params)) {
            std::uint8_t system = 0xFFU;
            if (ports_[0].test) {
                system &= static_cast<std::uint8_t>(~0x01U);
            }
            if (ports_[0].select) {
                system &= static_cast<std::uint8_t>(~0x04U);
            }
            if (ports_[1].select) {
                system &= static_cast<std::uint8_t>(~0x08U);
            }
            if (ports_[0].start) {
                system &= static_cast<std::uint8_t>(~0x10U);
            }
            if (ports_[1].start) {
                system &= static_cast<std::uint8_t>(~0x20U);
            }
            if (ports_[0].service || ports_[0].mode) {
                system &= static_cast<std::uint8_t>(~0x02U);
            }

            std::uint8_t coin_extension = 0xFFU;
            if (player_count_ > 2U && ports_[2].select) {
                coin_extension &= static_cast<std::uint8_t>(~0x01U);
            }
            if (player_count_ > 3U && ports_[3].select) {
                coin_extension &= static_cast<std::uint8_t>(~0x02U);
            }
            if (player_count_ > 2U && (ports_[2].service || ports_[2].mode)) {
                coin_extension &= static_cast<std::uint8_t>(~0x04U);
            }
            if (player_count_ > 3U && (ports_[3].service || ports_[3].mode)) {
                coin_extension &= static_cast<std::uint8_t>(~0x08U);
            }

            sys_->set_inputs(pack_with_start(ports_[0]), pack_with_start(ports_[1]), system,
                             sys_->dip_a, sys_->dip_b);
            sys_->input_p3 = player_count_ > 2U ? pack_with_start(ports_[2]) : 0xFFU;
            sys_->input_p4 = player_count_ > 3U ? pack_with_start(ports_[3]) : 0xFFU;
            sys_->input_coin_extension = coin_extension;
            return;
        }

        std::uint8_t system = 0xFFU;
        if (ports_[0].start) {
            system &= static_cast<std::uint8_t>(~0x01U);
        }
        if (ports_[1].start) {
            system &= static_cast<std::uint8_t>(~0x02U);
        }
        if (player_count_ > 2U && ports_[2].start) {
            system &= static_cast<std::uint8_t>(~0x04U);
        }
        if (player_count_ > 3U && ports_[3].start) {
            system &= static_cast<std::uint8_t>(~0x08U);
        }
        if (ports_[0].select) {
            system &= static_cast<std::uint8_t>(~0x10U);
        }
        if (ports_[1].select) {
            system &= static_cast<std::uint8_t>(~0x20U);
        }
        if (player_count_ > 2U && ports_[2].select) {
            system &= static_cast<std::uint8_t>(~0x40U);
        }
        if (player_count_ > 3U && ports_[3].select) {
            system &= static_cast<std::uint8_t>(~0x80U);
        }
        if (player_count_ <= 2U) {
            bool test = false;
            bool service = false;
            for (std::size_t i = 0U; i < player_count_; ++i) {
                test = test || ports_[i].test;
                service = service || ports_[i].service || ports_[i].mode;
            }
            if (test) {
                system &= static_cast<std::uint8_t>(~0x04U);
            }
            if (service) {
                system &= static_cast<std::uint8_t>(~0x08U);
            }
        }
        sys_->set_inputs(pack(ports_[0]), pack(ports_[1]), system, sys_->dip_a, sys_->dip_b);
        sys_->input_p3 = player_count_ > 2U ? pack(ports_[2]) : 0xFFU;
        sys_->input_p4 = player_count_ > 3U ? pack(ports_[3]) : 0xFFU;
        if (uses_te7750_quad_layout(sys_->params)) {
            std::uint8_t auxiliary = 0xFFU;
            if (ports_[0].test) {
                auxiliary &= static_cast<std::uint8_t>(~0x01U);
            }
            if (ports_[0].service || ports_[0].mode) {
                auxiliary &= static_cast<std::uint8_t>(~0x02U);
            }
            if (ports_[1].service || ports_[1].mode) {
                auxiliary &= static_cast<std::uint8_t>(~0x04U);
            }
            if (player_count_ > 2U && (ports_[2].service || ports_[2].mode)) {
                auxiliary &= static_cast<std::uint8_t>(~0x08U);
            }
            if (player_count_ > 3U && (ports_[3].service || ports_[3].mode)) {
                auxiliary &= static_cast<std::uint8_t>(~0x10U);
            }
            sys_->input_coin_extension = auxiliary;
        }
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(taito_f2_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "taito_f2.adapter";
        target.manifest_rev = 1U;
        target.master_cycle = adapter.sys_->main_cpu.elapsed_cycles();
        target.components.push_back(
            {"board",
             [&adapter](chips::state_writer& writer) { adapter.sys_->save_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.sys_->load_state(reader); }});
        target.components.push_back(
            {"adapter",
             [&adapter](chips::state_writer& writer) {
                 writer.u32(taito_f2_adapter_state_version);
                 writer.u64(adapter.frames_stepped_);
                 for (const frontend_sdk::controller_state& port : adapter.ports_) {
                     save_controller_state(writer, port);
                 }
             },
             [&adapter](chips::state_reader& reader) {
                 const std::uint32_t version = reader.u32();
                 if (version == 0U || version > taito_f2_adapter_state_version) {
                     reader.fail();
                     return;
                 }
                 adapter.frames_stepped_ = reader.u64();
                 const std::size_t serialized_ports = version == 1U ? 2U : adapter.ports_.size();
                 for (std::size_t i = 0U; i < serialized_ports; ++i) {
                     adapter.ports_[i] = load_controller_state(reader);
                 }
                 for (std::size_t i = serialized_ports; i < adapter.ports_.size(); ++i) {
                     adapter.ports_[i] = {};
                 }
                 if (reader.ok()) {
                     adapter.refresh_inputs();
                 }
             }});
        return target;
    }

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
