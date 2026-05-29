#include "manifest.hpp"

// Use toml++ in non-throwing mode: toml::parse returns a parse_result we inspect,
// rather than throwing. Keeps the loader's error path explicit and exception-free.
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace mnemos::manifests {
    namespace {

        [[nodiscard]] std::string_view trim(std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
                s.remove_suffix(1);
            }
            return s;
        }

        [[nodiscard]] std::optional<std::uint32_t> parse_hex(std::string_view token) {
            token = trim(token);
            if (token.starts_with("0x") || token.starts_with("0X")) {
                token.remove_prefix(2);
            }
            if (token.empty()) {
                return std::nullopt;
            }
            std::uint32_t value = 0;
            const char* begin = token.data();
            const char* end = begin + token.size();
            const auto [ptr, ec] = std::from_chars(begin, end, value, 16);
            if (ec != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        // Parse "0xSTART-0xEND" into an inclusive range.
        [[nodiscard]] std::optional<address_range> parse_range(std::string_view text) {
            const auto dash = text.find('-');
            if (dash == std::string_view::npos) {
                return std::nullopt;
            }
            const auto lo = parse_hex(text.substr(0, dash));
            const auto hi = parse_hex(text.substr(dash + 1));
            if (!lo || !hi || *hi < *lo) {
                return std::nullopt;
            }
            return address_range{*lo, *hi};
        }

    } // namespace

    load_result parse_manifest(std::string_view text, std::string_view source_name) {
        load_result out;

        const toml::parse_result parsed = toml::parse(text, source_name);
        if (!parsed) {
            const auto& e = parsed.error();
            out.errors.push_back({std::string{e.description()}, std::string{source_name},
                                  e.source().begin.line, e.source().begin.column});
            return out;
        }

        const toml::table& root = parsed.table();
        manifest m;
        auto& errs = out.errors;
        const std::string source{source_name};

        auto report = [&](std::string message, const toml::node* node) {
            errs.push_back({std::move(message), source,
                            node != nullptr ? node->source().begin.line : 0U,
                            node != nullptr ? node->source().begin.column : 0U});
        };

        if (const toml::table* mt = root["manifest"].as_table()) {
            if (const auto sc = (*mt)["schema"].value<std::string>()) {
                if (*sc != schema_id) {
                    report("manifest.schema must be '" + std::string{schema_id} + "'",
                           (*mt)["schema"].node());
                } else {
                    m.schema = *sc;
                }
            } else {
                report("manifest.schema is required", mt);
            }
            m.id = (*mt)["id"].value_or(std::string{});
            m.display_name = (*mt)["display_name"].value_or(std::string{});
            m.family = (*mt)["family"].value_or(std::string{});
            m.revision = (*mt)["revision"].value_or(std::uint32_t{0});
            if (m.id.empty()) {
                report("manifest.id is required", mt);
            }
        } else {
            report("missing [manifest] table", nullptr);
        }

        if (const toml::table* ct = root["clock"].as_table()) {
            m.clock.master_hz = (*ct)["master_hz"].value_or(std::uint64_t{0});
            m.clock.master_to_cpu_divider =
                (*ct)["master_to_cpu_divider"].value_or(std::uint32_t{0});
            m.clock.master_to_video_divider =
                (*ct)["master_to_video_divider"].value_or(std::uint32_t{0});
            if (m.clock.master_hz == 0U) {
                report("clock.master_hz is required and must be non-zero", ct);
            }
        } else {
            report("missing [clock] table", nullptr);
        }

        if (const toml::array* chips = root["chip"].as_array()) {
            for (const toml::node& el : *chips) {
                const toml::table* c = el.as_table();
                if (c == nullptr) {
                    report("[[chip]] entry is not a table", &el);
                    continue;
                }
                chip_decl cd;
                cd.id = (*c)["id"].value_or(std::string{});
                cd.type = (*c)["type"].value_or(std::string{});
                cd.attached_bus = (*c)["attached_bus"].value_or(std::string{});
                if (cd.id.empty()) {
                    report("chip.id is required", c);
                }
                if (cd.type.empty()) {
                    report("chip.type is required", c);
                }
                if (const auto mr = (*c)["mmio_range"].value<std::string>()) {
                    if (const auto r = parse_range(*mr)) {
                        cd.mmio_range = *r;
                    } else {
                        report("chip.mmio_range is invalid: " + *mr, (*c)["mmio_range"].node());
                    }
                }
                if (const auto ap = (*c)["mmio_active_predicate"].value<std::string>()) {
                    cd.mmio_active_predicate = *ap;
                }
                // Optional [chip.config] sub-table: scalar key->value pairs the
                // chip's configure() will consume. Use exact type predicates --
                // toml++'s `.value<T>()` performs lossy conversions (e.g.
                // int 12 narrows to bool true), which would collide variant
                // alternatives. Predicates-first keeps each TOML scalar type
                // in its own config_value bucket.
                if (const toml::table* cfg = (*c)["config"].as_table()) {
                    for (const auto& [key, node] : *cfg) {
                        const std::string key_str{key.str()};
                        if (node.is_boolean()) {
                            cd.config.emplace(key_str, chips::config_value{*node.value<bool>()});
                        } else if (node.is_integer()) {
                            cd.config.emplace(key_str,
                                              chips::config_value{*node.value<std::int64_t>()});
                        } else if (node.is_floating_point()) {
                            cd.config.emplace(key_str, chips::config_value{*node.value<double>()});
                        } else if (node.is_string()) {
                            cd.config.emplace(key_str,
                                              chips::config_value{*node.value<std::string>()});
                        } else {
                            report("chip.config." + key_str + " has unsupported type", &node);
                        }
                    }
                }
                m.chips.push_back(std::move(cd));
            }
        }

        const toml::array* buses = root["bus"].as_array();
        if (buses == nullptr || buses->empty()) {
            report("at least one [[bus]] is required", nullptr);
        } else {
            for (const toml::node& el : *buses) {
                const toml::table* bt = el.as_table();
                if (bt == nullptr) {
                    report("[[bus]] entry is not a table", &el);
                    continue;
                }
                bus_decl bd;
                bd.id = (*bt)["id"].value_or(std::string{});
                if (bd.id.empty()) {
                    report("bus.id is required", bt);
                }
                bd.address_bits = (*bt)["address_bits"].value_or(16U);
                const auto endian = (*bt)["endianness"].value_or(std::string{"little"});
                if (endian == "little") {
                    bd.endian = endianness::little;
                } else if (endian == "big") {
                    bd.endian = endianness::big;
                } else {
                    report("bus.endianness must be 'little' or 'big'", bt);
                }

                if (const toml::array* regs = (*bt)["region"].as_array()) {
                    for (const toml::node& rel : *regs) {
                        const toml::table* rt = rel.as_table();
                        if (rt == nullptr) {
                            report("[[bus.region]] entry is not a table", &rel);
                            continue;
                        }
                        region_decl rd;
                        rd.name = (*rt)["name"].value_or(std::string{});
                        if (const auto rng = (*rt)["range"].value<std::string>()) {
                            if (const auto pr = parse_range(*rng)) {
                                rd.range = *pr;
                            } else {
                                report("region.range is invalid: " + *rng, (*rt)["range"].node());
                            }
                        } else {
                            report("region.range is required", rt);
                        }
                        const auto backing = (*rt)["backing"].value_or(std::string{});
                        if (backing == "ram") {
                            rd.backing = region_backing::ram;
                        } else if (backing == "rom") {
                            rd.backing = region_backing::rom;
                        } else if (backing == "mmio_chip") {
                            rd.backing = region_backing::mmio_chip;
                        } else if (backing == "mapper") {
                            rd.backing = region_backing::mapper;
                        } else {
                            report("region.backing is invalid: " + backing, rt);
                        }
                        rd.size = (*rt)["size"].value_or(std::uint32_t{0});
                        if (const auto f = (*rt)["file"].value<std::string>()) {
                            rd.file = *f;
                        }
                        if (const auto s = (*rt)["sha256"].value<std::string>()) {
                            rd.sha256 = *s;
                        }
                        rd.overlay = (*rt)["overlay"].value_or(false);
                        if (const auto pr = (*rt)["priority"].value<std::int64_t>()) {
                            rd.priority = static_cast<std::uint32_t>(*pr);
                        }
                        if (const auto ap = (*rt)["active_predicate"].value<std::string>()) {
                            rd.active_predicate = *ap;
                        }
                        if (const auto mid = (*rt)["mapper_id"].value<std::string>()) {
                            rd.mapper_id = *mid;
                        }
                        // A rom region needs a source file; sha256 is optional --
                        // present for fixed system ROMs (integrity-pinned), absent
                        // for runtime cartridges that carry no canonical hash. The
                        // builder verifies the digest only when sha256 is given.
                        if (rd.backing == region_backing::rom && !rd.file) {
                            report("rom region '" + rd.name + "' requires a file", rt);
                        }
                        if (rd.backing == region_backing::mapper && !rd.mapper_id) {
                            report("mapper region '" + rd.name + "' requires mapper_id", rt);
                        }
                        bd.regions.push_back(std::move(rd));
                    }
                }
                m.buses.push_back(std::move(bd));
            }
        }

        // Optional [[mmio_block]] entries: each names a host-supplied MMIO
        // factory + the bus to bind it onto + an address range. The builder
        // resolves the factory through mmio_factory_table at build time.
        if (const toml::array* blocks = root["mmio_block"].as_array()) {
            for (const toml::node& el : *blocks) {
                const toml::table* b = el.as_table();
                if (b == nullptr) {
                    report("[[mmio_block]] entry is not a table", &el);
                    continue;
                }
                mmio_block_decl mb;
                mb.name = (*b)["name"].value_or(std::string{});
                mb.attached_bus = (*b)["attached_bus"].value_or(std::string{});
                if (mb.name.empty()) {
                    report("mmio_block.name is required", b);
                }
                if (mb.attached_bus.empty()) {
                    report("mmio_block.attached_bus is required", b);
                }
                if (const auto rs = (*b)["range"].value<std::string>()) {
                    if (const auto r = parse_range(*rs)) {
                        mb.range = *r;
                    } else {
                        report("mmio_block.range is invalid: " + *rs, (*b)["range"].node());
                    }
                } else {
                    report("mmio_block.range is required", b);
                }
                if (!mb.name.empty() && !mb.attached_bus.empty()) {
                    m.mmio_blocks.push_back(std::move(mb));
                }
            }
        }

        // Optional [[gate]] entries: each names a chip and a predicate the
        // builder will resolve through the host-supplied predicate_table.
        if (const toml::array* gates = root["gate"].as_array()) {
            for (const toml::node& el : *gates) {
                const toml::table* g = el.as_table();
                if (g == nullptr) {
                    report("[[gate]] entry is not a table", &el);
                    continue;
                }
                gate_decl gd;
                gd.chip_id = (*g)["chip"].value_or(std::string{});
                gd.predicate = (*g)["predicate"].value_or(std::string{});
                if (gd.chip_id.empty()) {
                    report("gate.chip is required", g);
                    continue;
                }
                if (gd.predicate.empty()) {
                    report("gate.predicate is required", g);
                    continue;
                }
                m.gates.push_back(std::move(gd));
            }
        }

        if (errs.empty()) {
            out.value = std::move(m);
        }
        return out;
    }

    load_result load_manifest_file(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            load_result out;
            out.errors.push_back({"cannot open manifest file", path.string(), 0U, 0U});
            return out;
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return parse_manifest(buffer.str(), path.string());
    }

} // namespace mnemos::manifests
