#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "input_pack.hpp"
#include "m72_game_manifests.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::irem_m72 {

    namespace {

        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            std::string set_name; // from the declaration; empty for dev formats
            frontend_sdk::display_orientation orientation{
                frontend_sdk::display_orientation::horizontal};
            std::optional<std::string> protection_hle_profile{};
            std::vector<mnemos::manifests::common::rom_set_dip_switch> dip_switches{};
        };

        struct parent_resolution final {
            mnemos::manifests::common::rom_file_provider provider;
            std::optional<mnemos::manifests::common::rom_set_decl> decl;
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
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes);

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc,
                                               std::uint64_t value) noexcept {
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

        [[nodiscard]] std::uint64_t resident_image_byte_count(const rom_set_image& image) noexcept {
            std::uint64_t bytes = 0U;
            for (const auto& [_, region] : image.regions) {
                bytes += region.size();
            }
            return bytes;
        }

        [[nodiscard]] std::uint32_t crc32_string(std::uint32_t crc,
                                                 std::string_view text) noexcept {
            crc = crc32_u64(crc, text.size());
            return mnemos::security::cryptography::crc32(text, crc);
        }

        [[nodiscard]] std::string resident_media_crc32(const loaded_set& set) {
            const auto& image = set.image;
            if (image.regions.empty()) {
                return {};
            }
            std::uint32_t crc =
                mnemos::security::cryptography::crc32("irem_m72.resident_media.v1");
            crc = crc32_string(crc, set.set_name);
            crc = crc32_u64(crc, static_cast<std::uint64_t>(set.orientation));
            crc = crc32_u64(crc, set.protection_hle_profile.has_value() ? 1U : 0U);
            if (set.protection_hle_profile.has_value()) {
                crc = crc32_string(crc, *set.protection_hle_profile);
            }
            crc = crc32_u64(crc, image.regions.size());
            for (const auto& [name, bytes] : image.regions) {
                crc = crc32_string(crc, name);
                crc = crc32_u64(crc, bytes.size());
                crc = mnemos::security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return hex32(crc);
        }

        frontend_sdk::media_capability_info
        make_media_capabilities(std::string_view display_name,
                                const loaded_set& set,
                                std::uint64_t source_bytes) {
            const auto& image = set.image;
            const std::uint64_t resident_bytes = resident_image_byte_count(image);
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
                .provider_id = "irem_m72.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});
            return media;
        }

        [[nodiscard]] frontend_sdk::display_orientation
        to_display_orientation(mnemos::manifests::common::screen_orientation orientation) noexcept {
            switch (orientation) {
            case mnemos::manifests::common::screen_orientation::vertical_counterclockwise:
                return frontend_sdk::display_orientation::vertical_counterclockwise;
            case mnemos::manifests::common::screen_orientation::vertical:
                return frontend_sdk::display_orientation::vertical_clockwise;
            case mnemos::manifests::common::screen_orientation::horizontal:
            default:
                return frontend_sdk::display_orientation::horizontal;
            }
        }

        [[nodiscard]] std::optional<std::string>
        declared_mcu_hle_profile(const mnemos::manifests::common::rom_set_decl& decl) {
            for (const auto& hle : decl.hle) {
                if (hle.chip == "mcu") {
                    return hle.profile;
                }
            }
            return std::nullopt;
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

        [[nodiscard]] std::optional<std::string>
        set_id_from_rom_path(const std::string& rom_path) {
            if (rom_path.empty()) {
                return std::nullopt;
            }
            const auto slash = rom_path.find_last_of("/\\");
            std::string basename =
                slash == std::string::npos ? rom_path : rom_path.substr(slash + 1U);
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

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        parse_irem_decl(std::string_view text,
                        std::string source,
                        std::optional<std::string_view> expected_set = std::nullopt) {
            auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[irem_m72] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                }
                return std::nullopt;
            }
            if (parsed.value->board != "irem_m72") {
                std::fprintf(stderr,
                             "[irem_m72] %s declares board '%s', expected 'irem_m72'\n",
                             source.c_str(), parsed.value->board.c_str());
                return std::nullopt;
            }
            if (expected_set.has_value() && parsed.value->name != *expected_set) {
                std::fprintf(stderr, "[irem_m72] %s declares set '%s', expected '%.*s'\n",
                             source.c_str(), parsed.value->name.c_str(),
                             static_cast<int>(expected_set->size()), expected_set->data());
                return std::nullopt;
            }
            return std::move(*parsed.value);
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        embedded_decl_for_set(std::string_view set_name) {
            const std::string_view toml =
                mnemos::manifests::irem_m72::game_manifest_toml(set_name);
            if (toml.empty()) {
                return std::nullopt;
            }
            return parse_irem_decl(toml, "embedded:irem_m72/" + std::string(set_name) + ".toml",
                                   set_name);
        }

        // Resolve a clone set's parent zip beside the clone on disk and compose
        // a fallback provider (clone first, then parent) so shared dumps in the
        // parent satisfy the clone declaration.
        [[nodiscard]] parent_resolution
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path) {
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[irem_m72] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return {.provider = clone};
            }
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[irem_m72] refusing to resolve parent '%s': not a plain set id\n",
                             parent.c_str());
                return {.provider = clone};
            }
            const auto slash = rom_path.find_last_of("/\\");
            const std::string dir =
                slash == std::string::npos ? std::string{} : rom_path.substr(0, slash + 1);
            const std::string parent_path = dir + parent + ".zip";
            bool unreadable_zip = false;
            auto parent_provider = mnemos::manifests::common::make_zip_rom_provider_from_path(
                parent_path, &unreadable_zip);
            if (!parent_provider.has_value()) {
                std::fprintf(stderr, "[irem_m72] parent set %s: %s\n",
                             parent_path.c_str(),
                             unreadable_zip ? "is not a readable zip" : "not found");
                return {.provider = clone};
            }
            parent_resolution resolved;
            if (auto manifest_bytes = (*parent_provider)("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                resolved.decl =
                    parse_irem_decl(text, parent + ".zip/game.toml", std::string_view{parent});
            } else {
                resolved.decl = embedded_decl_for_set(parent);
            }
            resolved.provider = mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
            return resolved;
        }

        [[nodiscard]] loaded_set
        load_declared_set(mnemos::manifests::common::rom_set_decl decl,
                          mnemos::manifests::common::rom_file_provider provider,
                          const std::string& rom_path) {
            loaded_set result;
            mnemos::manifests::common::rom_file_provider effective = std::move(provider);
            if (decl.parent.has_value()) {
                parent_resolution parent =
                    with_parent_fallback(effective, *decl.parent, rom_path);
                if (parent.decl.has_value()) {
                    decl = mnemos::manifests::common::inherit_parent_regions(
                        *parent.decl, std::move(decl));
                }
                effective = std::move(parent.provider);
            }
            result.image = mnemos::manifests::common::load_rom_set(decl, effective);
            result.set_name = decl.name;
            result.orientation = to_display_orientation(decl.orientation);
            result.protection_hle_profile = declared_mcu_hle_profile(decl);
            if (result.protection_hle_profile.has_value() &&
                !mnemos::manifests::irem_m72::supported_protection_hle_profile(
                    *result.protection_hle_profile)) {
                result.image.issues.push_back(
                    {"mcu",
                     "unsupported M72 MCU HLE profile '" + *result.protection_hle_profile + "'"});
                result.protection_hle_profile.reset();
            }
            result.dip_switches = decl.dips;
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[irem_m72] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            return result;
        }

        // Set loader. A .zip carrying a "game.toml" declaration (schema
        // mnemos-romset/1) loads declaratively -- per-file placement,
        // interleave, CRC verification -- with loader issues reported to
        // stderr; the declared set name selects the per-game board wiring. A
        // clone set can name a `parent`, whose zip is resolved beside the clone
        // using `rom_path` and used as a fallback for shared dumps. Without a
        // zip-local manifest, the zip basename selects a checked-in embedded
        // game manifest (e.g. rtype.zip -> rtype.toml). If no declaration can
        // be resolved, the development format applies: region-named entries
        // ("maincpu.bin", ...) loaded whole. A bare binary is the V30 program
        // image.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
            loaded_set result;
            const bool is_zip = rom.size() >= 4U && rom[0] == 'P' && rom[1] == 'K';
            if (is_zip) {
                if (auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
                    if (auto manifest_bytes = (*provider)("game.toml")) {
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        auto decl = parse_irem_decl(text, "game.toml");
                        if (!decl.has_value()) {
                            return result; // declared but invalid: boot an empty board
                        }
                        return load_declared_set(std::move(*decl), *provider, rom_path);
                    }
                    if (auto set_id = set_id_from_rom_path(rom_path)) {
                        if (auto decl = embedded_decl_for_set(*set_id)) {
                            return load_declared_set(std::move(*decl), *provider, rom_path);
                        }
                    }
                    for (const char* region :
                         {"maincpu", "soundcpu", "samples", "mcu", "tiles_a", "tiles_b",
                          "sprites"}) {
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
            auto params = manifests::irem_m72::board_params_for(set.set_name);
            params.protection_hle_profile = std::move(set.protection_hle_profile);
            return manifests::irem_m72::assemble_m72(std::move(set.image), std::move(params));
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

        [[nodiscard]] std::int16_t add_clamped(std::int16_t sample,
                                               std::int16_t addend) noexcept {
            const std::int32_t mixed = static_cast<std::int32_t>(sample) + addend;
            if (mixed > 32767) {
                return 32767;
            }
            if (mixed < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(mixed);
        }

        void mix_dac_range(std::span<std::int16_t> interleaved_stereo,
                           std::size_t first_frame,
                           std::size_t last_frame,
                           std::int16_t dac) noexcept {
            if (dac == 0 || first_frame >= last_frame) {
                return;
            }
            const std::size_t first = first_frame * 2U;
            const std::size_t last = last_frame * 2U;
            for (std::size_t i = first; i + 1U < last; i += 2U) {
                interleaved_stereo[i] = add_clamped(interleaved_stereo[i], dac);
                interleaved_stereo[i + 1U] = add_clamped(interleaved_stereo[i + 1U], dac);
            }
        }

        // Player-adapter state format. This is deliberately separate from the
        // board schema so a board-only save cannot masquerade as a frame-exact
        // player rollback point.
        constexpr std::uint32_t irem_m72_adapter_state_version = 1U;

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

    irem_m72_adapter::irem_m72_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path)
        : session_(make_session_capabilities()) {
        const std::uint64_t source_byte_count = rom.size();
        loaded_set set = load_set(std::move(rom), rom_path);
        media_ = make_media_capabilities(display_name, set, source_byte_count);
        orientation_ = set.orientation;
        dip_switches_ = std::move(set.dip_switches);
        loaded_set_name_ = set.set_name;
        sys_ = assemble_from(std::move(set));
        dac_mix_output_ = sys_->dac.output();
        scheduler_.emplace(
            frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->video));
        if (dip_override.has_value()) {
            sys_->dip_switches = *dip_override;
        }
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu,
                      &sys_->pic,   &sys_->fm,       &sys_->dac};
        if (sys_->mcu_present) {
            chip_view_.push_back(&sys_->mcu);
        }
        publish_memory_views();
        const std::string game_label =
            !loaded_set_name_.empty()
                ? loaded_set_name_
                : (display_name.empty() ? std::string{"unknown"} : display_name);
        spec_ = {{"System", "Arcade"},
                 {"Board", "Irem M72"},
                 {"Game", game_label}};
        if (!dip_switches_.empty()) {
            spec_.push_back({"DIP switches", std::to_string(dip_switches_.size())});
        }
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
        publish(7U, "mcu_shared_ram", sys_->mcu_shared_ram);
    }

    void irem_m72_adapter::sync_inputs_from_ports() noexcept {
        // Hardware bit layout (active low): joystick right/left/down/up from
        // bit 0 (the standard arcade nibble), buttons 4..1 from bit 4 -- button 1
        // is the MSB, button 2 next.
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c, dpad_layout{}, {{c.a, 0x80U}, {c.b, 0x40U}});
        };
        sys_->input_p1 = pack(ports_[0]);
        sys_->input_p2 = pack(ports_[1]);

        // System byte: start1/start2/coin1/coin2 from bit 0, service1/2 at
        // bits 4/5. The board read path supplies bit 7 as sprite-DMA-complete.
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
        if (ports_[0].mode) {
            system_byte &= static_cast<std::uint8_t>(~0x10U); // service 1
        }
        if (ports_[1].mode) {
            system_byte &= static_cast<std::uint8_t>(~0x20U); // service 2
        }
        sys_->input_system = system_byte;
    }

    void irem_m72_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(irem_m72_adapter_state_version);
        writer.u64(frames_stepped_);
        writer.u64(samples_drained_);
        write_i16(writer, dac_mix_output_);
        for (const auto& port : ports_) {
            save_controller_state(writer, port);
        }
    }

    void irem_m72_adapter::load_adapter_state(chips::state_reader& reader) {
        if (reader.u32() != irem_m72_adapter_state_version) {
            reader.fail();
            return;
        }
        frames_stepped_ = reader.u64();
        samples_drained_ = reader.u64();
        dac_mix_output_ = read_i16(reader);
        for (auto& port : ports_) {
            port = load_controller_state(reader);
        }
        if (reader.ok()) {
            sync_inputs_from_ports();
        }
    }

    frontend_sdk::audio_chunk irem_m72_adapter::drain_audio() noexcept {
        // One stereo frame per 64 YM2151 clocks; the chip's elapsed-clock
        // counter is the sample clock, so drains never drift from emulation.
        constexpr std::uint64_t clocks_per_sample =
            chips::audio::ym2151::clocks_per_sample;
        const std::uint64_t start_sample = samples_drained_;
        const std::uint64_t due =
            sys_->fm.elapsed_clocks() / clocks_per_sample;
        const std::uint64_t pending = due - start_sample;
        samples_drained_ = due;
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
        sys_->fm.update(audio_buf_);

        if (sys_->dac_write_events.empty()) {
            dac_mix_output_ = sys_->dac.output();
        }
        std::int16_t dac = dac_mix_output_;
        std::size_t cursor = 0U;
        for (const auto& event : sys_->dac_write_events) {
            const std::uint64_t event_sample = event.sound_clock / clocks_per_sample;
            if (event_sample < start_sample) {
                dac = event.output;
                continue;
            }
            if (event_sample >= due) {
                break;
            }
            const auto boundary = static_cast<std::size_t>(event_sample - start_sample);
            mix_dac_range(audio_buf_, cursor, boundary, dac);
            dac = event.output;
            cursor = boundary;
        }
        mix_dac_range(audio_buf_, cursor, static_cast<std::size_t>(pending), dac);
        dac_mix_output_ = dac;
        sys_->discard_dac_write_events_before(due * clocks_per_sample);

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
        sync_inputs_from_ports();
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(manifests::irem_m72::m72_system& sys) {
        runtime::save_target target;
        target.manifest_id = "irem_m72";
        target.manifest_rev = 6U;
        target.components.push_back({"board",
                                     [&sys](chips::state_writer& writer) {
                                         sys.save_state(writer);
                                     },
                                     [&sys](chips::state_reader& reader) {
                                         sys.load_state(reader);
                                     }});
        return target;
    }

    runtime::save_target build_save_target(irem_m72_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "irem_m72.adapter";
        target.manifest_rev = 1U;
        target.components.push_back({"board",
                                     [&adapter](chips::state_writer& writer) {
                                         adapter.machine().save_state(writer);
                                     },
                                     [&adapter](chips::state_reader& reader) {
                                         adapter.machine().load_state(reader);
                                     }});
        target.components.push_back({"adapter",
                                     [&adapter](chips::state_writer& writer) {
                                         adapter.save_adapter_state(writer);
                                     },
                                     [&adapter](chips::state_reader& reader) {
                                         adapter.load_adapter_state(reader);
                                     }});
        return target;
    }

    namespace {
        const auto register_irem_m72 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "irem_m72",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_m72_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::irem_m72
