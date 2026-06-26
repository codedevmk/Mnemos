#include "irem_m15_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "file.hpp"
#include "m15_game_manifests.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::irem_m15 {

    namespace {

        namespace fs = std::filesystem;
        namespace m15 = mnemos::manifests::irem_m15;

        using mnemos::manifests::common::rom_load_issue;
        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            std::string set_name;
        };

        struct nested_zip_source final {
            std::vector<std::uint8_t> bytes;
            std::string entry_name;
        };

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m15.panel.p1",
                 .label = "Player 1 Panel"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m15.panel.p2",
                 .label = "Player 2 Panel"},
            };
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return mnemos::security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::string hex32(std::uint32_t value) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out(8U, '0');
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto shift = static_cast<unsigned>((out.size() - 1U - i) * 4U);
                out[i] = digits[(value >> shift) & 0x0FU];
            }
            return out;
        }

        [[nodiscard]] std::uint32_t crc32_string(std::uint32_t crc,
                                                 std::string_view text) noexcept {
            crc = crc32_u64(crc, text.size());
            return mnemos::security::cryptography::crc32(text, crc);
        }

        [[nodiscard]] std::uint64_t resident_image_byte_count(const rom_set_image& image) noexcept {
            std::uint64_t bytes = 0U;
            for (const auto& [_, region] : image.regions) {
                bytes += region.size();
            }
            return bytes;
        }

        [[nodiscard]] std::string resident_media_crc32(const loaded_set& set) {
            if (set.image.regions.empty()) {
                return {};
            }
            std::uint32_t crc = mnemos::security::cryptography::crc32("irem_m15.resident_media.v1");
            crc = crc32_string(crc, set.set_name);
            crc = crc32_u64(crc, set.image.regions.size());
            for (const auto& [name, bytes] : set.image.regions) {
                crc = crc32_string(crc, name);
                crc = crc32_u64(crc, bytes.size());
                crc = mnemos::security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return hex32(crc);
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes) {
            const std::uint64_t resident_bytes = resident_image_byte_count(set.image);
            std::vector<frontend_sdk::media_validation_issue> validation_issues;
            validation_issues.reserve(set.image.issues.size());
            for (const rom_load_issue& issue : set.image.issues) {
                std::string detail = issue.message;
                if (!issue.file.empty()) {
                    detail = issue.file + ": " + detail;
                }
                validation_issues.push_back(
                    {.code = "media.rom_set.load_issue", .detail = std::move(detail)});
            }
            std::string full_hash = resident_media_crc32(set);
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "rom_set",
                .label = display_name.empty() ? std::string{"ROM set"} : std::string{display_name},
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = resident_bytes == 0U ? source_bytes : resident_bytes,
                .hash_algorithm = full_hash.empty() ? frontend_sdk::media_hash_algorithm::none
                                                    : frontend_sdk::media_hash_algorithm::crc32,
                .full_hash = std::move(full_hash),
                .provider_id = "irem_m15.adapter",
                .revision = "loaded",
                .cache_hint = "resident",
                .validation_issues = std::move(validation_issues)});
            return media;
        }

        [[nodiscard]] bool ends_with_zip(std::string_view value) noexcept {
            constexpr std::string_view ext = ".zip";
            if (value.size() < ext.size()) {
                return false;
            }
            value.remove_prefix(value.size() - ext.size());
            for (std::size_t i = 0; i < ext.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(value[i])) != ext[i]) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool has_zip_signature(std::span<const std::uint8_t> bytes) noexcept {
            return bytes.size() >= 4U && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 0x03U &&
                   bytes[3] == 0x04U;
        }

        [[nodiscard]] bool is_directory_path(const std::string& path) {
            if (path.empty()) {
                return false;
            }
            std::error_code ec;
            return fs::is_directory(fs::path{path}, ec);
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

        [[nodiscard]] std::optional<std::string> set_id_from_rom_path(const std::string& rom_path) {
            if (rom_path.empty()) {
                return std::nullopt;
            }
            std::string normalized = rom_path;
            while (!normalized.empty() && (normalized.back() == '/' || normalized.back() == '\\')) {
                normalized.pop_back();
            }
            if (normalized.empty()) {
                return std::nullopt;
            }
            const auto slash = normalized.find_last_of("/\\");
            std::string basename =
                slash == std::string::npos ? normalized : normalized.substr(slash + 1U);
            const auto dot = basename.find_last_of('.');
            if (dot != std::string::npos) {
                basename.resize(dot);
            }
            if (!is_plain_set_id(basename)) {
                return std::nullopt;
            }
            for (char& c : basename) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return basename;
        }

        [[nodiscard]] std::optional<nested_zip_source>
        unwrap_single_nested_zip(std::span<const std::uint8_t> archive_bytes) {
            auto outer = mnemos::compression::zip_archive::open(archive_bytes);
            if (!outer.has_value()) {
                return std::nullopt;
            }
            const mnemos::compression::zip_entry* nested = nullptr;
            std::size_t file_count = 0U;
            for (const mnemos::compression::zip_entry& entry : outer->entries()) {
                if (!entry.name.empty() &&
                    (entry.name.back() == '/' || entry.name.back() == '\\')) {
                    continue;
                }
                ++file_count;
                if (ends_with_zip(entry.name)) {
                    nested = &entry;
                }
            }
            if (file_count != 1U || nested == nullptr) {
                return std::nullopt;
            }
            auto inner = outer->extract(*nested);
            if (!inner.has_value() || !has_zip_signature(*inner)) {
                return std::nullopt;
            }
            return nested_zip_source{.bytes = std::move(*inner), .entry_name = nested->name};
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        parse_irem_decl(std::string_view text, std::string source,
                        std::optional<std::string_view> expected_set = std::nullopt,
                        std::vector<rom_load_issue>* issues = nullptr) {
            auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[irem_m15] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                    if (issues != nullptr) {
                        issues->push_back(
                            {source, "invalid ROM-set declaration: " + error.message});
                    }
                }
                return std::nullopt;
            }
            if (parsed.value->board != "irem_m15") {
                std::fprintf(stderr, "[irem_m15] %s declares board '%s', expected 'irem_m15'\n",
                             source.c_str(), parsed.value->board.c_str());
                if (issues != nullptr) {
                    issues->push_back({source, "unsupported Irem board '" + parsed.value->board +
                                                   "' for the M15 adapter"});
                }
                return std::nullopt;
            }
            if (expected_set.has_value() && parsed.value->name != *expected_set) {
                if (issues != nullptr) {
                    issues->push_back({source, "declares set '" + parsed.value->name +
                                                   "', expected '" + std::string{*expected_set} +
                                                   "'"});
                }
                return std::nullopt;
            }
            return std::move(*parsed.value);
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        embedded_decl_for_set(std::string_view set_name) {
            const std::string_view toml = m15::game_manifest_toml(set_name);
            if (toml.empty()) {
                return std::nullopt;
            }
            return parse_irem_decl(toml, "embedded:irem_m15/" + std::string(set_name) + ".toml",
                                   set_name);
        }

        [[nodiscard]] loaded_set
        load_declared_set(mnemos::manifests::common::rom_set_decl decl,
                          const mnemos::manifests::common::rom_file_provider& provider) {
            loaded_set result;
            result.image = mnemos::manifests::common::load_rom_set(decl, provider);
            result.set_name = decl.name;
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[irem_m15] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            return result;
        }

        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
            loaded_set result;
            if (rom.empty() && is_directory_path(rom_path)) {
                auto provider = mnemos::manifests::common::make_directory_rom_provider(rom_path);
                if (auto manifest_bytes = provider("game.toml")) {
                    const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                    auto decl =
                        parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                    if (!decl.has_value()) {
                        return result;
                    }
                    return load_declared_set(std::move(*decl), provider);
                }
                if (auto set_id = set_id_from_rom_path(rom_path)) {
                    if (auto decl = embedded_decl_for_set(*set_id)) {
                        return load_declared_set(std::move(*decl), provider);
                    }
                }
                result.image.issues.push_back(
                    {rom_path, "directory ROM set has no game.toml and no embedded manifest"});
                return result;
            }

            std::string effective_rom_path = rom_path;
            if (has_zip_signature(rom)) {
                if (auto nested = unwrap_single_nested_zip(rom)) {
                    const fs::path nested_name = fs::path{nested->entry_name}.filename();
                    const fs::path outer_path{rom_path};
                    effective_rom_path = outer_path.has_parent_path()
                                             ? (outer_path.parent_path() / nested_name).string()
                                             : nested_name.string();
                    rom = std::move(nested->bytes);
                }
            }
            if (has_zip_signature(rom)) {
                if (auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
                    if (auto manifest_bytes = (*provider)("game.toml")) {
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        auto decl =
                            parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                        if (!decl.has_value()) {
                            return result;
                        }
                        return load_declared_set(std::move(*decl), *provider);
                    }
                    if (auto set_id = set_id_from_rom_path(effective_rom_path)) {
                        if (auto decl = embedded_decl_for_set(*set_id)) {
                            return load_declared_set(std::move(*decl), *provider);
                        }
                    }
                    if (auto bytes = (*provider)("maincpu.bin")) {
                        result.image.regions.emplace("maincpu", std::move(*bytes));
                    }
                }
                return result;
            }
            result.image.regions.emplace("maincpu", std::move(rom));
            return result;
        }

        [[nodiscard]] std::unique_ptr<m15::m15_system> assemble_from(loaded_set set) {
            return m15::assemble_m15(std::move(set.image), m15::board_params_for(set.set_name));
        }

        constexpr std::uint32_t irem_m15_adapter_state_version = 1U;
        constexpr std::uint32_t irem_m15_adapter_save_target_manifest_rev = 2U;

        void write_i16(chips::state_writer& writer, std::int16_t value) {
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(value) + 32768));
        }

        [[nodiscard]] std::int16_t read_i16(chips::state_reader& reader) noexcept {
            return static_cast<std::int16_t>(static_cast<std::int32_t>(reader.u16()) - 32768);
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
            write_i16(writer, state.aim_x);
            write_i16(writer, state.aim_y);
            writer.boolean(state.trigger);
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
            state.aim_x = read_i16(reader);
            state.aim_y = read_i16(reader);
            state.trigger = reader.boolean();
            return state;
        }

    } // namespace

    irem_m15_adapter::irem_m15_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path)
        : session_(make_session_capabilities()) {
        (void)scheduler_factory;
        const std::uint64_t source_byte_count = rom.size();
        loaded_set set = load_set(std::move(rom), rom_path);
        media_ = make_media_capabilities(display_name, set, source_byte_count);
        loaded_set_name_ = set.set_name;
        sys_ = assemble_from(std::move(set));
        if (dip_override.has_value()) {
            sys_->dip_switches = static_cast<std::uint8_t>(*dip_override);
        }
        chip_view_ = {&sys_->main_cpu, &sys_->video, &sys_->speaker};
        publish_memory_views();
        const std::string game_label =
            !loaded_set_name_.empty()
                ? loaded_set_name_
                : (display_name.empty() ? std::string{"unknown"} : display_name);
        spec_ = {{"System", "Arcade"}, {"Board", "Irem M15"}, {"Game", game_label}};
    }

    void irem_m15_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };

        publish(0U, "scratch_ram", sys_->scratch_ram);
        publish(1U, "video_ram", sys_->video_ram);
        publish(2U, "color_ram", sys_->color_ram);
        publish(3U, "chargen_ram", sys_->chargen_ram);
    }

    void irem_m15_adapter::sync_inputs_from_ports() noexcept {
        const auto pack_controls = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            std::uint8_t value = 0U;
            if (c.a) {
                value |= m15::panel_button1_bit;
            }
            if (c.up) {
                value |= m15::panel_up_bit;
            }
            if (c.down) {
                value |= m15::panel_down_bit;
            }
            if (c.left) {
                value |= m15::panel_left_bit;
            }
            if (c.right) {
                value |= m15::panel_right_bit;
            }
            return value;
        };
        std::uint8_t p1 = pack_controls(ports_[0]);
        if (ports_[0].start) {
            p1 |= m15::p1_start1_bit;
        }
        if (ports_[1].start) {
            p1 |= m15::p1_start2_bit;
        }
        std::uint8_t system = 0U;
        if (ports_[0].select) {
            system |= m15::coin1_bit;
        }
        if (ports_[1].select) {
            system |= m15::coin1_bit;
        }
        sys_->set_inputs(p1, pack_controls(ports_[1]), system);
    }

    void irem_m15_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(irem_m15_adapter_state_version);
        writer.u64(frames_stepped_);
        for (const auto& port : ports_) {
            save_controller_state(writer, port);
        }
    }

    void irem_m15_adapter::load_adapter_state(chips::state_reader& reader) {
        if (reader.u32() != irem_m15_adapter_state_version) {
            reader.fail();
            return;
        }
        frames_stepped_ = reader.u64();
        for (auto& port : ports_) {
            port = load_controller_state(reader);
        }
        if (reader.ok()) {
            sync_inputs_from_ports();
        }
    }

    void irem_m15_adapter::step_one_frame() {
        sys_->run_frame();
        ++frames_stepped_;
    }

    void irem_m15_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        sync_inputs_from_ports();
    }

    frontend_sdk::audio_chunk irem_m15_adapter::drain_audio() noexcept {
        const std::size_t pending = sys_->speaker.pending_samples();
        if (pending == 0U) {
            return {};
        }
        mono_buf_.assign(pending, 0);
        const std::size_t drained = sys_->speaker.drain_samples(mono_buf_.data(), mono_buf_.size());
        if (drained == 0U) {
            return {};
        }
        audio_buf_.assign(drained * 2U, 0);
        for (std::size_t i = 0; i < drained; ++i) {
            audio_buf_[i * 2U] = mono_buf_[i];
            audio_buf_[i * 2U + 1U] = mono_buf_[i];
        }
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(drained),
                .sample_rate = m15::audio_rate_hz};
    }

    std::vector<std::uint8_t> irem_m15_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result irem_m15_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            audio_buf_.clear();
            mono_buf_.clear();
        }
        return result;
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(manifests::irem_m15::m15_system& sys) {
        runtime::save_target target;
        target.manifest_id = "irem_m15";
        target.manifest_rev = manifests::irem_m15::m15_system_state_version;
        target.components.push_back(
            {"board", [&sys](chips::state_writer& writer) { sys.save_state(writer); },
             [&sys](chips::state_reader& reader) { sys.load_state(reader); }});
        return target;
    }

    runtime::save_target build_save_target(irem_m15_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "irem_m15.adapter";
        target.manifest_rev = irem_m15_adapter_save_target_manifest_rev;
        target.components.push_back(
            {"board",
             [&adapter](chips::state_writer& writer) { adapter.machine().save_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.machine().load_state(reader); }});
        target.components.push_back(
            {"adapter",
             [&adapter](chips::state_writer& writer) { adapter.save_adapter_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.load_adapter_state(reader); }});
        return target;
    }

    namespace {
        const auto register_irem_m15 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "irem_m15",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_m15_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::irem_m15
