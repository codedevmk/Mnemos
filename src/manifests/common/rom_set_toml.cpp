#include "rom_set_toml.hpp"

// Non-throwing toml++ like the system manifest loader (manifest.cpp).
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

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
                       {"name", "offset", "stride", "unit", "swap", "source_offset", "length",
                        "size", "crc32"},
                       "[[region.file]]");
            rom_set_file file;
            if (auto name = require_string(ctx, table, "name", "[[region.file]]")) {
                file.name = std::move(*name);
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
        check_keys(ctx, root, {"set", "region"}, "the document root");

        rom_set_decl decl;
        if (const auto* set = root.get_as<toml::table>("set")) {
            check_keys(ctx, *set, {"schema", "name", "board", "cps_b_profile", "orientation"},
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
            // Optional monitor orientation ("horizontal" / "vertical"); the
            // frontend rotates a vertical set. Absent => horizontal.
            if (const toml::node* node = set->get("orientation")) {
                if (const auto* value = node->as_string()) {
                    if (value->get() == "vertical") {
                        decl.orientation = screen_orientation::vertical;
                    } else if (value->get() == "horizontal") {
                        decl.orientation = screen_orientation::horizontal;
                    } else {
                        ctx.error("'orientation' in [set] must be \"horizontal\" or \"vertical\"",
                                  node);
                    }
                } else {
                    ctx.error("'orientation' in [set] must be a string", node);
                }
            }
        } else {
            ctx.error("missing required [set] table");
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
