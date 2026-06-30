#include "rom_set_toml.hpp"

// Non-throwing toml++ like the system manifest loader (manifest.cpp).
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <utility>

namespace mnemos::manifests::common {

    namespace {

        constexpr std::string_view expected_schema = "mnemos-romset/1";

        struct parse_context final {
            std::string source;
            std::vector<diagnostic>* errors{};

            void error(std::string message, const toml::node* node = nullptr) const {
                diagnostic d;
                d.message = std::move(message);
                d.source = source;
                if (node != nullptr) {
                    d.line = static_cast<unsigned>(node->source().begin.line);
                    d.column = static_cast<unsigned>(node->source().begin.column);
                }
                errors->push_back(std::move(d));
            }
        };

        // Reject typo'd keys the same way the manifest loader's strictness
        // demands: every key in `table` must be in `allowed`.
        void check_keys(const parse_context& ctx, const toml::table& table,
                        std::initializer_list<std::string_view> allowed, std::string_view where) {
            for (const auto& [key, node] : table) {
                bool known = false;
                for (const std::string_view candidate : allowed) {
                    if (key.str() == candidate) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    ctx.error("unknown key '" + std::string(key.str()) + "' in " +
                                  std::string(where),
                              &node);
                }
            }
        }

        [[nodiscard]] std::optional<std::string> require_string(const parse_context& ctx,
                                                                const toml::table& table,
                                                                std::string_view key,
                                                                std::string_view where) {
            const toml::node* node = table.get(key);
            if (node == nullptr) {
                ctx.error(std::string(where) + " is missing required key '" + std::string(key) +
                          "'");
                return std::nullopt;
            }
            if (const auto* value = node->as_string()) {
                if (!value->get().empty()) {
                    return value->get();
                }
            }
            ctx.error("'" + std::string(key) + "' in " + std::string(where) +
                          " must be a non-empty string",
                      node);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint64_t> read_unsigned(const parse_context& ctx,
                                                                 const toml::node& node,
                                                                 std::string_view key,
                                                                 std::string_view where) {
            if (const auto* value = node.as_integer()) {
                if (value->get() >= 0) {
                    return static_cast<std::uint64_t>(value->get());
                }
            }
            ctx.error("'" + std::string(key) + "' in " + std::string(where) +
                          " must be a non-negative integer",
                      &node);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::int64_t> read_signed(const parse_context& ctx,
                                                              const toml::node& node,
                                                              std::string_view key,
                                                              std::string_view where) {
            if (const auto* value = node.as_integer()) {
                return value->get();
            }
            ctx.error("'" + std::string(key) + "' in " + std::string(where) +
                          " must be an integer",
                      &node);
            return std::nullopt;
        }

        [[nodiscard]] bool is_choice(std::string_view value,
                                      std::initializer_list<std::string_view> choices) noexcept {
            return std::any_of(choices.begin(), choices.end(),
                               [value](std::string_view choice) { return value == choice; });
        }

        [[nodiscard]] std::optional<std::string>
        read_string_choice(const parse_context& ctx, const toml::table& table,
                           std::string_view key, std::initializer_list<std::string_view> choices,
                           std::string_view description) {
            const toml::node* node = table.get(key);
            if (node == nullptr) {
                return std::nullopt;
            }
            if (const auto* value = node->as_string()) {
                const std::string& text = value->get();
                if (is_choice(text, choices)) {
                    return text;
                }
                ctx.error("'" + std::string(key) + "' in [set] must be a known " +
                              std::string(description),
                          node);
                return std::nullopt;
            }
            ctx.error("'" + std::string(key) + "' in [set] must be a string", node);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint8_t>
        read_irq_level(const parse_context& ctx, const toml::table& table,
                       std::string_view key) {
            const toml::node* node = table.get(key);
            if (node == nullptr) {
                return std::nullopt;
            }
            if (auto level = read_unsigned(ctx, *node, key, "[set]")) {
                if (*level < 1U || *level > 7U) {
                    ctx.error("'" + std::string(key) +
                                  "' in [set] must be a 68000 IRQ level in the range 1..7",
                              node);
                    return std::nullopt;
                }
                return static_cast<std::uint8_t>(*level);
            }
            return std::nullopt;
        }

        // crc32 accepts a TOML integer or a hex string ("DEADBEEF" / "0x...").
        [[nodiscard]] std::optional<std::uint32_t>
        read_crc32(const parse_context& ctx, const toml::node& node, std::string_view where) {
            if (const auto* value = node.as_integer()) {
                if (value->get() >= 0 && value->get() <= 0xFFFFFFFFLL) {
                    return static_cast<std::uint32_t>(value->get());
                }
                ctx.error("'crc32' in " + std::string(where) + " is out of 32-bit range", &node);
                return std::nullopt;
            }
            if (const auto* text = node.as_string()) {
                std::string_view token = text->get();
                if (token.starts_with("0x") || token.starts_with("0X")) {
                    token.remove_prefix(2);
                }
                std::uint32_t value = 0;
                const char* begin = token.data();
                const char* end = begin + token.size();
                const auto [ptr, ec] = std::from_chars(begin, end, value, 16);
                if (!token.empty() && ec == std::errc{} && ptr == end) {
                    return value;
                }
            }
            ctx.error("'crc32' in " + std::string(where) + " must be an integer or a hex string",
                      &node);
            return std::nullopt;
        }

        void parse_file(const parse_context& ctx, const toml::table& table,
                        rom_set_region& region) {
            check_keys(ctx, table,
                       {"name", "aliases", "offset", "stride", "unit", "swap", "source_offset",
                        "length", "size", "crc32"},
                       "[[region.file]]");
            rom_set_file file;
            if (auto name = require_string(ctx, table, "name", "[[region.file]]")) {
                file.name = std::move(*name);
            }
            if (const toml::node* node = table.get("aliases")) {
                if (const auto* aliases = node->as_array()) {
                    for (const toml::node& entry : *aliases) {
                        if (const auto* alias = entry.as_string()) {
                            if (alias->get().empty()) {
                                ctx.error("'aliases' in [[region.file]] must not contain an "
                                          "empty string",
                                          &entry);
                            } else {
                                file.aliases.push_back(alias->get());
                            }
                        } else {
                            ctx.error("'aliases' in [[region.file]] must contain only strings",
                                      &entry);
                        }
                    }
                } else {
                    ctx.error("'aliases' in [[region.file]] must be an array of strings", node);
                }
            }
            if (const toml::node* node = table.get("offset")) {
                if (auto offset = read_unsigned(ctx, *node, "offset", "[[region.file]]")) {
                    file.offset = static_cast<std::size_t>(*offset);
                }
            }
            if (const toml::node* node = table.get("stride")) {
                if (auto stride = read_unsigned(ctx, *node, "stride", "[[region.file]]")) {
                    if (*stride == 0U) {
                        ctx.error("'stride' in [[region.file]] must be at least 1", node);
                    } else {
                        file.stride = static_cast<std::size_t>(*stride);
                    }
                }
            }
            if (const toml::node* node = table.get("unit")) {
                if (auto unit = read_unsigned(ctx, *node, "unit", "[[region.file]]")) {
                    if (*unit == 0U) {
                        ctx.error("'unit' in [[region.file]] must be at least 1", node);
                    } else {
                        file.unit = static_cast<std::size_t>(*unit);
                    }
                }
            }
            if (const toml::node* node = table.get("swap")) {
                if (const auto* value = node->as_boolean()) {
                    file.swap = value->get();
                } else {
                    ctx.error("'swap' in [[region.file]] must be a boolean", node);
                }
            }
            if (const toml::node* node = table.get("source_offset")) {
                if (auto so = read_unsigned(ctx, *node, "source_offset", "[[region.file]]")) {
                    file.source_offset = static_cast<std::size_t>(*so);
                }
            }
            if (const toml::node* node = table.get("length")) {
                if (auto length = read_unsigned(ctx, *node, "length", "[[region.file]]")) {
                    file.length = static_cast<std::size_t>(*length);
                }
            }
            if (const toml::node* node = table.get("size")) {
                if (auto size = read_unsigned(ctx, *node, "size", "[[region.file]]")) {
                    file.size = static_cast<std::size_t>(*size);
                }
            }
            if (const toml::node* node = table.get("crc32")) {
                file.crc32 = read_crc32(ctx, *node, "[[region.file]]");
            }
            region.files.push_back(std::move(file));
        }

        void parse_region(const parse_context& ctx, const toml::table& table, rom_set_decl& decl) {
            check_keys(ctx, table, {"name", "size", "fill", "file"}, "[[region]]");
            rom_set_region region;
            if (auto name = require_string(ctx, table, "name", "[[region]]")) {
                region.name = std::move(*name);
            }
            if (const toml::node* node = table.get("size")) {
                if (auto size = read_unsigned(ctx, *node, "size", "[[region]]")) {
                    region.size = static_cast<std::size_t>(*size);
                }
            } else {
                ctx.error("[[region]] is missing required key 'size'");
            }
            if (region.size == 0U) {
                ctx.error("[[region]] '" + region.name + "' declares size 0");
            }
            if (const toml::node* node = table.get("fill")) {
                if (auto fill = read_unsigned(ctx, *node, "fill", "[[region]]")) {
                    if (*fill > 0xFFU) {
                        ctx.error("'fill' in [[region]] must fit a byte", node);
                    } else {
                        region.fill = static_cast<std::uint8_t>(*fill);
                    }
                }
            }
            if (const toml::node* files = table.get("file")) {
                if (const auto* array = files->as_array()) {
                    for (const toml::node& entry : *array) {
                        if (const auto* file_table = entry.as_table()) {
                            parse_file(ctx, *file_table, region);
                        } else {
                            ctx.error("[[region.file]] entries must be tables", &entry);
                        }
                    }
                } else {
                    ctx.error("'file' in [[region]] must be an array of tables", files);
                }
            }
            decl.regions.push_back(std::move(region));
        }

        void parse_hle_sample_trigger(const parse_context& ctx,
                                      const toml::table& table,
                                      rom_set_hle_decl& hle,
                                      std::array<const toml::node*, 256>& trigger_sources) {
            constexpr std::string_view where = "[[hle.sample_trigger]]";
            check_keys(ctx, table, {"trigger", "start"}, where);

            rom_set_hle_sample_trigger sample_trigger;
            const toml::node* trigger_node = nullptr;
            bool valid = true;
            if (const toml::node* node = table.get("trigger")) {
                if (auto value = read_unsigned(ctx, *node, "trigger", where)) {
                    if (*value <= 0xFFU) {
                        sample_trigger.trigger = static_cast<std::uint8_t>(*value);
                        trigger_node = node;
                    } else {
                        ctx.error("'trigger' in " + std::string(where) +
                                      " is out of 8-bit range",
                                  node);
                        valid = false;
                    }
                } else {
                    valid = false;
                }
            } else {
                ctx.error(std::string(where) + " is missing required key 'trigger'");
                valid = false;
            }

            if (const toml::node* node = table.get("start")) {
                if (auto value = read_unsigned(ctx, *node, "start", where)) {
                    if (*value <= 0xFFFFFFFFULL) {
                        sample_trigger.start = static_cast<std::uint32_t>(*value);
                    } else {
                        ctx.error("'start' in " + std::string(where) +
                                      " is out of 32-bit range",
                                  node);
                        valid = false;
                    }
                } else {
                    valid = false;
                }
            } else {
                ctx.error(std::string(where) + " is missing required key 'start'");
                valid = false;
            }

            if (valid) {
                if (trigger_sources[sample_trigger.trigger] != nullptr) {
                    ctx.error("duplicate 'trigger' in " + std::string(where), trigger_node);
                    return;
                }
                trigger_sources[sample_trigger.trigger] = trigger_node;
                hle.sample_triggers.push_back(sample_trigger);
            }
        }

        void parse_hle(const parse_context& ctx, const toml::table& table, rom_set_decl& decl) {
            check_keys(ctx, table, {"chip", "profile", "rationale", "sample_trigger"}, "[[hle]]");
            rom_set_hle_decl hle;
            if (auto chip = require_string(ctx, table, "chip", "[[hle]]")) {
                hle.chip = std::move(*chip);
            }
            // `profile` is optional: CPS2 substitutions declare only chip + rationale,
            // while M72 MCU substitutions carry a profile id.
            if (table.get("profile") != nullptr) {
                if (auto profile = require_string(ctx, table, "profile", "[[hle]]")) {
                    hle.profile = std::move(*profile);
                }
            }
            if (auto rationale = require_string(ctx, table, "rationale", "[[hle]]")) {
                hle.rationale = std::move(*rationale);
            }
            if (const toml::node* sample_triggers = table.get("sample_trigger")) {
                if (const auto* array = sample_triggers->as_array()) {
                    std::array<const toml::node*, 256> trigger_sources{};
                    for (const toml::node& entry : *array) {
                        if (const auto* sample_table = entry.as_table()) {
                            parse_hle_sample_trigger(ctx, *sample_table, hle, trigger_sources);
                        } else {
                            ctx.error("[[hle.sample_trigger]] entries must be tables", &entry);
                        }
                    }
                } else {
                    ctx.error("'sample_trigger' in [[hle]] must be an array of tables",
                              sample_triggers);
                }
            }
            decl.hle.push_back(std::move(hle));
        }

        void parse_dip_option(const parse_context& ctx, const toml::table& table,
                              rom_set_dip_switch& dip) {
            check_keys(ctx, table, {"label", "value"}, "[[dip.option]]");
            rom_set_dip_option option;
            if (auto label = require_string(ctx, table, "label", "[[dip.option]]")) {
                option.label = std::move(*label);
            }
            if (const toml::node* node = table.get("value")) {
                if (auto value = read_unsigned(ctx, *node, "value", "[[dip.option]]")) {
                    if (*value > 0xFFFFU) {
                        ctx.error("'value' in [[dip.option]] is out of 16-bit range", node);
                    } else if ((*value & ~static_cast<std::uint64_t>(dip.mask)) != 0U) {
                        ctx.error("'value' in [[dip.option]] sets bits outside the parent mask",
                                  node);
                    } else {
                        option.value = static_cast<std::uint16_t>(*value);
                    }
                }
            } else {
                ctx.error("[[dip.option]] is missing required key 'value'");
            }
            dip.options.push_back(std::move(option));
        }

        void parse_dip(const parse_context& ctx, const toml::table& table, rom_set_decl& decl) {
            check_keys(ctx, table,
                       {"bank", "name", "mask", "default", "condition_mask",
                        "condition_value", "option"},
                       "[[dip]]");
            rom_set_dip_switch dip;
            if (auto bank = require_string(ctx, table, "bank", "[[dip]]")) {
                dip.bank = std::move(*bank);
            }
            if (auto name = require_string(ctx, table, "name", "[[dip]]")) {
                dip.name = std::move(*name);
            }
            bool saw_mask = false;
            if (const toml::node* node = table.get("mask")) {
                if (auto mask = read_unsigned(ctx, *node, "mask", "[[dip]]")) {
                    if (*mask == 0U) {
                        ctx.error("'mask' in [[dip]] must be non-zero", node);
                    } else if (*mask > 0xFFFFU) {
                        ctx.error("'mask' in [[dip]] is out of 16-bit range", node);
                    } else {
                        saw_mask = true;
                        dip.mask = static_cast<std::uint16_t>(*mask);
                    }
                }
            } else {
                ctx.error("[[dip]] is missing required key 'mask'");
            }
            if (const toml::node* node = table.get("default")) {
                if (auto value = read_unsigned(ctx, *node, "default", "[[dip]]")) {
                    if (*value > 0xFFFFU) {
                        ctx.error("'default' in [[dip]] is out of 16-bit range", node);
                    } else if (saw_mask &&
                               (*value & ~static_cast<std::uint64_t>(dip.mask)) != 0U) {
                        ctx.error("'default' in [[dip]] sets bits outside the switch mask", node);
                    } else {
                        dip.default_value = static_cast<std::uint16_t>(*value);
                    }
                }
            } else {
                ctx.error("[[dip]] is missing required key 'default'");
            }
            const toml::node* condition_mask_node = table.get("condition_mask");
            const toml::node* condition_value_node = table.get("condition_value");
            if (condition_mask_node != nullptr || condition_value_node != nullptr) {
                rom_set_dip_condition condition;
                bool saw_condition_mask = false;
                if (condition_mask_node != nullptr) {
                    if (auto mask =
                            read_unsigned(ctx, *condition_mask_node, "condition_mask", "[[dip]]")) {
                        if (*mask == 0U) {
                            ctx.error("'condition_mask' in [[dip]] must be non-zero",
                                      condition_mask_node);
                        } else if (*mask > 0xFFFFU) {
                            ctx.error("'condition_mask' in [[dip]] is out of 16-bit range",
                                      condition_mask_node);
                        } else {
                            saw_condition_mask = true;
                            condition.mask = static_cast<std::uint16_t>(*mask);
                        }
                    }
                } else {
                    ctx.error("[[dip]] is missing required key 'condition_mask'");
                }
                if (condition_value_node != nullptr) {
                    if (auto value = read_unsigned(
                            ctx, *condition_value_node, "condition_value", "[[dip]]")) {
                        if (*value > 0xFFFFU) {
                            ctx.error("'condition_value' in [[dip]] is out of 16-bit range",
                                      condition_value_node);
                        } else if (saw_condition_mask &&
                                   (*value & ~static_cast<std::uint64_t>(condition.mask)) != 0U) {
                            ctx.error("'condition_value' in [[dip]] sets bits outside the "
                                      "condition mask",
                                      condition_value_node);
                        } else {
                            condition.value = static_cast<std::uint16_t>(*value);
                        }
                    }
                } else {
                    ctx.error("[[dip]] is missing required key 'condition_value'");
                }
                if (saw_condition_mask && condition_value_node != nullptr) {
                    dip.condition = condition;
                }
            }
            if (const toml::node* options = table.get("option")) {
                if (const auto* array = options->as_array()) {
                    for (const toml::node& entry : *array) {
                        if (const auto* option_table = entry.as_table()) {
                            parse_dip_option(ctx, *option_table, dip);
                        } else {
                            ctx.error("[[dip.option]] entries must be tables", &entry);
                        }
                    }
                } else {
                    ctx.error("'option' in [[dip]] must be an array of tables", options);
                }
            }
            if (dip.options.empty()) {
                ctx.error("[[dip]] '" + dip.name + "' must declare at least one option");
            }
            decl.dips.push_back(std::move(dip));
        }

    } // namespace

    rom_set_load_result parse_rom_set_decl(std::string_view text, std::string_view source_name) {
        rom_set_load_result result;
        parse_context ctx{std::string(source_name), &result.errors};

        toml::parse_result parsed = toml::parse(text, source_name);
        if (!parsed) {
            diagnostic d;
            d.message = std::string(parsed.error().description());
            d.source = ctx.source;
            d.line = static_cast<unsigned>(parsed.error().source().begin.line);
            d.column = static_cast<unsigned>(parsed.error().source().begin.column);
            result.errors.push_back(std::move(d));
            return result;
        }
        const toml::table& root = parsed.table();
        check_keys(ctx, root, {"set", "region", "hle", "dip"}, "the document root");

        rom_set_decl decl;
        if (const auto* set = root.get_as<toml::table>("set")) {
            check_keys(ctx, *set,
                       {"schema", "name", "board", "parent", "cps_b_profile", "orientation",
                        "players", "input", "sprite_order", "sound", "kabuki", "taito_f2_map",
                        "taito_f2_sprite_policy", "taito_f2_sprite_buffering",
                        "taito_f2_palette_format", "taito_f2_sprite_extension_base",
                        "taito_f2_sprite_extension_size", "taito_f2_sprite_active_area",
                        "taito_f2_sprite_hide_pixels", "taito_f2_sprite_flip_hide_pixels",
                        "taito_f2_input_profile", "taito_f2_text_gfx_source",
                        "taito_f2_text_gfx_base", "taito_f2_tc0100scn_bg_x_offset",
                        "taito_f2_tc0100scn_text_x_offset",
                        "taito_f2_tc0100scn_text_y_origin",
                        "taito_f2_tc0100scn_positive_text_y_origin",
                        "taito_f2_io_profile", "taito_f2_palette_profile",
                        "taito_f2_priority_profile", "taito_f2_sprite_chip_pair",
                        "taito_f2_sound_comm_chip", "taito_f2_video_profile",
                        "taito_f2_tc0480scp_profile", "taito_f2_roz_x_offset",
                        "taito_f2_roz_y_offset", "taito_f2_aux_profile",
                        "taito_f2_vblank_irq_level", "taito_f2_sprite_dma_irq_level"},
                       "[set]");
            if (auto schema = require_string(ctx, *set, "schema", "[set]")) {
                if (*schema != expected_schema) {
                    ctx.error("unsupported schema '" + *schema + "' (expected '" +
                              std::string(expected_schema) + "')");
                }
            }
            if (auto name = require_string(ctx, *set, "name", "[set]")) {
                decl.name = std::move(*name);
            }
            if (set->get("board") != nullptr) {
                if (auto board = require_string(ctx, *set, "board", "[set]")) {
                    decl.board = std::move(*board);
                }
            }
            // Optional parent set name (MAME-style clone -> parent): the board
            // adapter loads the shared dumps from the parent set's zip. The value
            // is concatenated into a filesystem path (<dir>/<parent>.zip) and the
            // toml lives inside an untrusted ROM zip, so constrain it to a plain
            // set id here at the trust boundary -- reject path separators, "..",
            // and absolute/drive paths (every real CPS1 set id is [A-Za-z0-9_]).
            if (const toml::node* node = set->get("parent")) {
                if (const auto* value = node->as_string()) {
                    const std::string& p = value->get();
                    const auto plain_id = [](char c) {
                        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                               (c >= '0' && c <= '9') || c == '_';
                    };
                    if (p.empty()) {
                        ctx.error("'parent' in [set] must be a non-empty string", node);
                    } else if (!std::all_of(p.begin(), p.end(), plain_id)) {
                        ctx.error("'parent' in [set] must be a plain set id [A-Za-z0-9_] "
                                  "(no path separators)",
                                  node);
                    } else {
                        decl.parent = p;
                    }
                } else {
                    ctx.error("'parent' in [set] must be a string", node);
                }
            }
            // Optional CPS-B board / PAL profile id (capcom_cps1 selects its
            // hardware profile by this id; ignored by families that don't use it).
            // The id is a 16-bit value: diagnose out-of-range rather than silently
            // truncating (mirrors read_crc32's range handling).
            if (const toml::node* node = set->get("cps_b_profile")) {
                if (auto profile = read_unsigned(ctx, *node, "cps_b_profile", "[set]")) {
                    if (*profile > 0xFFFFU) {
                        ctx.error("'cps_b_profile' in [set] is out of 16-bit range", node);
                    } else {
                        decl.cps_b_profile = static_cast<std::uint16_t>(*profile);
                    }
                }
            }
            // Optional monitor orientation. "vertical" preserves the legacy
            // clockwise presentation; ROT270 boards should declare
            // "vertical_ccw" so the frontend rotates the other way.
            if (const toml::node* node = set->get("orientation")) {
                if (const auto* value = node->as_string()) {
                    if (value->get() == "vertical") {
                        decl.orientation = screen_orientation::vertical;
                    } else if (value->get() == "vertical_cw" ||
                               value->get() == "vertical_clockwise") {
                        decl.orientation = screen_orientation::vertical_clockwise;
                    } else if (value->get() == "vertical_ccw" ||
                               value->get() == "vertical_counterclockwise") {
                        decl.orientation = screen_orientation::vertical_counterclockwise;
                    } else if (value->get() == "horizontal") {
                        decl.orientation = screen_orientation::horizontal;
                    } else {
                        ctx.error("'orientation' in [set] must be \"horizontal\", \"vertical\", "
                                  "\"vertical_cw\", or \"vertical_ccw\"",
                                  node);
                    }
                } else {
                    ctx.error("'orientation' in [set] must be a string", node);
                }
            }
            // Optional local player panel count. CPS-style arcade input words
            // expose at most START/COIN 1-4; reject invalid declarations at the
            // manifest boundary rather than silently truncating adapter state.
            if (const toml::node* node = set->get("players")) {
                if (auto players = read_unsigned(ctx, *node, "players", "[set]")) {
                    if (*players < 1U || *players > 4U) {
                        ctx.error("'players' in [set] must be in the range 1..4", node);
                    } else {
                        decl.players = static_cast<std::uint8_t>(*players);
                    }
                }
            }
            // Optional board-interpreted input/cabinet wiring profile.
            if (const toml::node* node = set->get("input")) {
                if (const auto* value = node->as_string()) {
                    decl.input = value->get();
                } else {
                    ctx.error("'input' in [set] must be a string", node);
                }
            }
            // Optional sprite-list draw order ("ascending" / "descending"); a few
            // bootleg sets relocate the object list. Absent => ascending.
            if (const toml::node* node = set->get("sprite_order")) {
                if (const auto* value = node->as_string()) {
                    if (value->get() == "descending") {
                        decl.sprite_order = sprite_draw_order::descending;
                    } else if (value->get() == "ascending") {
                        decl.sprite_order = sprite_draw_order::ascending;
                    } else {
                        ctx.error("'sprite_order' in [set] must be \"ascending\" or \"descending\"",
                                  node);
                    }
                } else {
                    ctx.error("'sprite_order' in [set] must be a string", node);
                }
            }
            // Optional board-interpreted sound + Kabuki selectors (capcom_cps1
            // reads "qsound" + a game key; ignored by families that don't use them).
            if (const toml::node* node = set->get("sound")) {
                if (const auto* value = node->as_string()) {
                    decl.sound = value->get();
                } else {
                    ctx.error("'sound' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("kabuki")) {
                if (const auto* value = node->as_string()) {
                    decl.kabuki = value->get();
                } else {
                    ctx.error("'kabuki' in [set] must be a string", node);
                }
            }
            // Optional Taito F2 selectors. Keep the schema strict enough to catch
            // typos in per-game manifests while leaving the board code in charge
            // of interpreting the behavior.
            if (const toml::node* node = set->get("taito_f2_map")) {
                if (const auto* value = node->as_string()) {
                    const std::string& map = value->get();
                    if (map == "synthetic" || map == "dondokod" || map == "gunfront" ||
                        map == "liquidk" || map == "pulirula" || map == "quizhq" ||
                        map == "qtorimon" || map == "qzchikyu" || map == "qzquest" ||
                        map == "metalb" || map == "footchmp" || map == "deadconx" ||
                        map == "dinorex" || map == "thundfox" || map == "growl" ||
                        map == "ninjak" || map == "solfigtr") {
                        decl.taito_f2_map = map;
                    } else {
                        ctx.error("'taito_f2_map' in [set] must be \"synthetic\", "
                                  "\"dondokod\", \"gunfront\", \"liquidk\", \"pulirula\", or "
                                  "\"quizhq\", \"qtorimon\", \"qzchikyu\", \"qzquest\", "
                                  "\"metalb\", \"footchmp\", \"deadconx\", \"dinorex\", "
                                  "\"thundfox\", \"growl\", \"ninjak\", or \"solfigtr\"",
                                  node);
                    }
                } else {
                    ctx.error("'taito_f2_map' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_policy")) {
                if (const auto* value = node->as_string()) {
                    const std::string& policy = value->get();
                    if (policy == "standard" || policy == "partial_buffer" ||
                        policy == "banked" || policy == "extension_1" ||
                        policy == "extension_2" || policy == "extension_3") {
                        decl.taito_f2_sprite_policy = policy;
                    } else {
                        ctx.error("'taito_f2_sprite_policy' in [set] must be a known "
                                  "Taito F2 sprite policy",
                                  node);
                    }
                } else {
                    ctx.error("'taito_f2_sprite_policy' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_buffering")) {
                if (const auto* value = node->as_string()) {
                    const std::string& buffering = value->get();
                    if (buffering == "immediate" || buffering == "full_delayed" ||
                        buffering == "partial_delayed" ||
                        buffering == "partial_delayed_thundfox" ||
                        buffering == "partial_delayed_qzchikyu") {
                        decl.taito_f2_sprite_buffering = buffering;
                    } else {
                        ctx.error("'taito_f2_sprite_buffering' in [set] must be a known "
                                  "Taito F2 sprite-buffer policy",
                                  node);
                    }
                } else {
                    ctx.error("'taito_f2_sprite_buffering' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("taito_f2_palette_format")) {
                if (const auto* value = node->as_string()) {
                    const std::string& format = value->get();
                    if (format == "xbgr_555" || format == "rgbx_444" ||
                        format == "xrgb_555" || format == "rrrr_gggg_bbbb_rgbx" ||
                        format == "rrrrggggbbbbrgbx") {
                        decl.taito_f2_palette_format = format;
                    } else {
                        ctx.error("'taito_f2_palette_format' in [set] must be "
                                  "\"xbgr_555\", \"rgbx_444\", \"xrgb_555\", or "
                                  "\"rrrr_gggg_bbbb_rgbx\"",
                                  node);
                    }
                } else {
                    ctx.error("'taito_f2_palette_format' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_active_area")) {
                if (const auto* value = node->as_string()) {
                    const std::string& active_area = value->get();
                    if (active_area == "mode_default" || active_area == "none" ||
                        active_area == "control_word_bit0" || active_area == "y_word_bit0") {
                        decl.taito_f2_sprite_active_area = active_area;
                    } else {
                        ctx.error("'taito_f2_sprite_active_area' in [set] must be "
                                  "\"mode_default\", \"none\", \"control_word_bit0\", or "
                                  "\"y_word_bit0\"",
                                  node);
                    }
                } else {
                    ctx.error("'taito_f2_sprite_active_area' in [set] must be a string", node);
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_extension_base")) {
                if (auto base = read_unsigned(ctx, *node, "taito_f2_sprite_extension_base",
                                              "[set]")) {
                    if (*base > 0xFFFFFFU) {
                        ctx.error("'taito_f2_sprite_extension_base' in [set] is out of "
                                  "24-bit 68000 address range",
                                  node);
                    } else if ((*base & 1U) != 0U) {
                        ctx.error("'taito_f2_sprite_extension_base' in [set] must be "
                                  "word-aligned",
                                  node);
                    } else {
                        decl.taito_f2_sprite_extension_base =
                            static_cast<std::uint32_t>(*base);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_extension_size")) {
                if (auto size = read_unsigned(ctx, *node, "taito_f2_sprite_extension_size",
                                              "[set]")) {
                    if (*size == 0U || *size > 0x4000U) {
                        ctx.error("'taito_f2_sprite_extension_size' in [set] must be "
                                  "between 2 and 0x4000 bytes",
                                  node);
                    } else if ((*size & 1U) != 0U) {
                        ctx.error("'taito_f2_sprite_extension_size' in [set] must be "
                                  "word-aligned",
                                  node);
                    } else {
                        decl.taito_f2_sprite_extension_size =
                            static_cast<std::uint32_t>(*size);
                    }
                }
            }
            if (decl.taito_f2_sprite_extension_size.has_value() &&
                !decl.taito_f2_sprite_extension_base.has_value()) {
                ctx.error("'taito_f2_sprite_extension_size' in [set] requires "
                          "'taito_f2_sprite_extension_base'",
                          set->get("taito_f2_sprite_extension_size"));
            }
            if (const toml::node* node = set->get("taito_f2_sprite_hide_pixels")) {
                if (auto hide = read_signed(ctx, *node, "taito_f2_sprite_hide_pixels", "[set]")) {
                    if (*hide < -16 || *hide > 16) {
                        ctx.error("'taito_f2_sprite_hide_pixels' in [set] must be between "
                                  "-16 and 16",
                                  node);
                    } else {
                        decl.taito_f2_sprite_hide_pixels = static_cast<std::int16_t>(*hide);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_sprite_flip_hide_pixels")) {
                if (auto hide = read_signed(ctx, *node, "taito_f2_sprite_flip_hide_pixels",
                                            "[set]")) {
                    if (*hide < -16 || *hide > 16) {
                        ctx.error("'taito_f2_sprite_flip_hide_pixels' in [set] must be between "
                                  "-16 and 16",
                                  node);
                    } else {
                        decl.taito_f2_sprite_flip_hide_pixels =
                            static_cast<std::int16_t>(*hide);
                    }
                }
            }
            decl.taito_f2_input_profile =
                read_string_choice(ctx, *set, "taito_f2_input_profile",
                                   {"standard", "split_tmp82c265", "te7750_quad"},
                                   "Taito F2 input profile");
            decl.taito_f2_text_gfx_source =
                read_string_choice(ctx, *set, "taito_f2_text_gfx_source",
                                   {"tc0100scn_ram_2bpp", "program_1bpp"},
                                   "Taito F2 text graphics source");
            if (const toml::node* node = set->get("taito_f2_text_gfx_base")) {
                if (auto base = read_unsigned(ctx, *node, "taito_f2_text_gfx_base",
                                              "[set]")) {
                    if (*base > 0xFFFFFFU) {
                        ctx.error("'taito_f2_text_gfx_base' in [set] is out of "
                                  "24-bit region offset range",
                                  node);
                    } else {
                        decl.taito_f2_text_gfx_base =
                            static_cast<std::uint32_t>(*base);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_tc0100scn_bg_x_offset")) {
                if (auto offset =
                        read_signed(ctx, *node, "taito_f2_tc0100scn_bg_x_offset", "[set]")) {
                    if (*offset < -256 || *offset > 256) {
                        ctx.error("'taito_f2_tc0100scn_bg_x_offset' in [set] must be "
                                  "between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_tc0100scn_bg_x_offset =
                            static_cast<std::int16_t>(*offset);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_tc0100scn_text_x_offset")) {
                if (auto offset =
                        read_signed(ctx, *node, "taito_f2_tc0100scn_text_x_offset", "[set]")) {
                    if (*offset < -256 || *offset > 256) {
                        ctx.error("'taito_f2_tc0100scn_text_x_offset' in [set] must be "
                                  "between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_tc0100scn_text_x_offset =
                            static_cast<std::int16_t>(*offset);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_tc0100scn_text_y_origin")) {
                if (auto origin =
                        read_signed(ctx, *node, "taito_f2_tc0100scn_text_y_origin", "[set]")) {
                    if (*origin < -256 || *origin > 256) {
                        ctx.error("'taito_f2_tc0100scn_text_y_origin' in [set] must be "
                                  "between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_tc0100scn_text_y_origin =
                            static_cast<std::int16_t>(*origin);
                    }
                }
            }
            if (const toml::node* node =
                    set->get("taito_f2_tc0100scn_positive_text_y_origin")) {
                if (auto origin = read_signed(
                        ctx, *node, "taito_f2_tc0100scn_positive_text_y_origin", "[set]")) {
                    if (*origin < -256 || *origin > 256) {
                        ctx.error("'taito_f2_tc0100scn_positive_text_y_origin' in [set] "
                                  "must be between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_tc0100scn_positive_text_y_origin =
                            static_cast<std::int16_t>(*origin);
                    }
                }
            }
            decl.taito_f2_io_profile =
                read_string_choice(ctx, *set, "taito_f2_io_profile",
                                   {"tc0220ioc", "tc0510nio", "te7750", "tmp82c265"},
                                   "Taito F2 I/O profile");
            decl.taito_f2_palette_profile =
                read_string_choice(ctx, *set, "taito_f2_palette_profile",
                                   {"tc0110pcr_tc0070rgb", "tc0260dar"},
                                   "Taito F2 palette profile");
            decl.taito_f2_priority_profile =
                read_string_choice(ctx, *set, "taito_f2_priority_profile",
                                   {"none", "tc0360pri"},
                                   "Taito F2 priority profile");
            decl.taito_f2_sprite_chip_pair =
                read_string_choice(ctx, *set, "taito_f2_sprite_chip_pair",
                                   {"tc0200obj_tc0210fbc", "tc0540obn_tc0520tbc"},
                                   "Taito F2 sprite chip pair");
            decl.taito_f2_sound_comm_chip =
                read_string_choice(ctx, *set, "taito_f2_sound_comm_chip",
                                   {"tc0140syt", "tc0530syc"},
                                   "Taito F2 sound-communication chip");
            decl.taito_f2_video_profile =
                read_string_choice(ctx, *set, "taito_f2_video_profile",
                                   {"tc0100scn", "dual_tc0100scn",
                                    "tc0100scn_tc0280grd", "tc0100scn_tc0430grw",
                                    "tc0480scp"},
                                   "Taito F2 video profile");
            decl.taito_f2_tc0480scp_profile =
                read_string_choice(ctx, *set, "taito_f2_tc0480scp_profile",
                                   {"none", "metalb", "footchmp", "deadconx"},
                                   "Taito F2 TC0480SCP profile");
            if (const toml::node* node = set->get("taito_f2_roz_x_offset")) {
                if (auto offset = read_signed(ctx, *node, "taito_f2_roz_x_offset", "[set]")) {
                    if (*offset < -256 || *offset > 256) {
                        ctx.error("'taito_f2_roz_x_offset' in [set] must be "
                                  "between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_roz_x_offset =
                            static_cast<std::int16_t>(*offset);
                    }
                }
            }
            if (const toml::node* node = set->get("taito_f2_roz_y_offset")) {
                if (auto offset = read_signed(ctx, *node, "taito_f2_roz_y_offset", "[set]")) {
                    if (*offset < -256 || *offset > 256) {
                        ctx.error("'taito_f2_roz_y_offset' in [set] must be "
                                  "between -256 and 256",
                                  node);
                    } else {
                        decl.taito_f2_roz_y_offset =
                            static_cast<std::int16_t>(*offset);
                    }
                }
            }
            decl.taito_f2_aux_profile =
                read_string_choice(ctx, *set, "taito_f2_aux_profile",
                                   {"none", "tc0030cmd_cchip", "rtc", "printer",
                                    "rtc_printer"},
                                   "Taito F2 auxiliary-device profile");
            decl.taito_f2_vblank_irq_level =
                read_irq_level(ctx, *set, "taito_f2_vblank_irq_level");
            decl.taito_f2_sprite_dma_irq_level =
                read_irq_level(ctx, *set, "taito_f2_sprite_dma_irq_level");
        } else {
            ctx.error("missing required [set] table");
        }

        if (const toml::node* dips = root.get("dip")) {
            if (const auto* array = dips->as_array()) {
                for (const toml::node& entry : *array) {
                    if (const auto* table = entry.as_table()) {
                        parse_dip(ctx, *table, decl);
                    } else {
                        ctx.error("[[dip]] entries must be tables", &entry);
                    }
                }
            } else {
                ctx.error("'dip' in the document root must be an array of tables", dips);
            }
        }

        if (const auto* hle = root.get_as<toml::array>("hle")) {
            for (const toml::node& entry : *hle) {
                if (const auto* table = entry.as_table()) {
                    parse_hle(ctx, *table, decl);
                } else {
                    ctx.error("[[hle]] entries must be tables", &entry);
                }
            }
        }

        if (const auto* regions = root.get_as<toml::array>("region")) {
            for (const toml::node& entry : *regions) {
                if (const auto* table = entry.as_table()) {
                    parse_region(ctx, *table, decl);
                } else {
                    ctx.error("[[region]] entries must be tables", &entry);
                }
            }
        }
        if (decl.regions.empty()) {
            ctx.error("a ROM-set declaration needs at least one [[region]]");
        }

        if (result.errors.empty()) {
            result.value = std::move(decl);
        }
        return result;
    }

} // namespace mnemos::manifests::common
