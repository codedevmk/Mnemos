#include "asset_export.hpp"

#include "asset_views.hpp"
#include "file.hpp"
#include "path_id.hpp"
#include "png_image.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace mnemos::debug {

    namespace {

        constexpr std::uint32_t swatch_cell = 8U; // palette swatch cell size in px

        // Resolve an indexed image to packed 0x00RRGGBB via its referenced
        // palette. Out-of-range indices and a missing palette resolve to black;
        // PNG output is truecolour with no alpha, so a transparent index renders
        // as its CRAM colour (the JSON records which index is transparent).
        std::vector<std::uint32_t> resolve(const instrumentation::indexed_image& img,
                                           std::span<const instrumentation::palette_view> pals) {
            std::vector<std::uint32_t> out(static_cast<std::size_t>(img.width) * img.height, 0U);
            const instrumentation::palette_view* pal =
                img.palette < pals.size() ? &pals[img.palette] : nullptr;
            const std::size_t n = std::min(img.indices.size(), out.size());
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint8_t idx = img.indices[i];
                if (pal != nullptr && idx < pal->colors.size()) {
                    out[i] = pal->colors[idx];
                }
            }
            return out;
        }

        // Render a palette as a horizontal strip of `swatch_cell`-sized cells.
        std::vector<std::uint32_t> render_palette(const instrumentation::palette_view& pal,
                                                  std::uint32_t& w, std::uint32_t& h) {
            const auto n = static_cast<std::uint32_t>(pal.colors.size());
            w = (n == 0U ? 1U : n) * swatch_cell;
            h = swatch_cell;
            std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0U);
            for (std::uint32_t c = 0; c < n; ++c) {
                for (std::uint32_t y = 0; y < swatch_cell; ++y) {
                    for (std::uint32_t x = 0; x < swatch_cell; ++x) {
                        px[static_cast<std::size_t>(y) * w + c * swatch_cell + x] = pal.colors[c];
                    }
                }
            }
            return px;
        }

        bool write_png(const std::string& path, std::uint32_t w, std::uint32_t h,
                       std::vector<std::uint32_t> px) {
            if (w == 0U || h == 0U) {
                return false;
            }
            const graphics::images::png_image img(w, h, std::move(px));
            if (!img.write(path)) {
                std::fprintf(stderr, "[asset_export] could not write %s\n", path.c_str());
                return false;
            }
            return true;
        }

        // A minimal JSON string literal: quoted, with '"' and '\\' escaped.
        // Asset/chip names are simple identifiers, so this is sufficient.
        std::string json_str(std::string_view s) {
            std::string out = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

    } // namespace

    std::size_t export_assets(const frontend_sdk::player_system& sys,
                              const std::string& base_path) {
        std::size_t written = 0;
        std::string json = "{\n  \"chips\": [";
        bool first_chip = true;

        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            instrumentation::asset_source* src = chip->introspection().assets();
            if (src == nullptr) {
                continue;
            }
            const std::string chip_id = sanitize_id(chip->metadata().part_number);
            const std::span<const instrumentation::palette_view> pals = src->palettes();
            const std::span<const instrumentation::graphic_asset> assets = src->graphics();

            json += first_chip ? "\n" : ",\n";
            first_chip = false;
            json += "    {\n      \"id\": " + json_str(chip_id) +
                    ",\n      \"part_number\": " + json_str(chip->metadata().part_number) +
                    ",\n      \"palettes\": [";

            for (std::size_t i = 0; i < pals.size(); ++i) {
                const instrumentation::palette_view& p = pals[i];
                const std::string path =
                    base_path + "." + chip_id + ".pal." + std::string(p.name) + ".png";
                std::uint32_t w = 0;
                std::uint32_t h = 0;
                // Render first so w/h are set before write_png reads them:
                // argument evaluation order is unspecified, so passing
                // render_palette(p, w, h) alongside w/h could read them as 0.
                std::vector<std::uint32_t> px = render_palette(p, w, h);
                if (write_png(path, w, h, std::move(px))) {
                    ++written;
                }
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_str(p.name) +
                        ", \"colors\": " + std::to_string(p.colors.size()) +
                        ", \"transparent_index\": " + std::to_string(p.transparent_index) +
                        ", \"file\": " + json_str(path_basename(path)) + "}";
            }
            json += pals.empty() ? "],\n      \"assets\": [" : "\n      ],\n      \"assets\": [";

            for (std::size_t i = 0; i < assets.size(); ++i) {
                const instrumentation::graphic_asset& a = assets[i];
                const std::string kind = std::string(instrumentation::asset_kind_name(a.kind));
                const std::string path =
                    base_path + "." + chip_id + "." + kind + "." + std::string(a.name) + ".png";
                if (write_png(path, a.image.width, a.image.height, resolve(a.image, pals))) {
                    ++written;
                }
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_str(a.name) + ", \"kind\": " + json_str(kind) +
                        ", \"width\": " + std::to_string(a.image.width) +
                        ", \"height\": " + std::to_string(a.image.height) +
                        ", \"tile_w\": " + std::to_string(a.tile_w) +
                        ", \"tile_h\": " + std::to_string(a.tile_h) +
                        ", \"palette\": " + std::to_string(a.image.palette) +
                        ", \"source_addr\": " + std::to_string(a.source_addr) +
                        ", \"file\": " + json_str(path_basename(path)) + "}";
            }
            json += assets.empty() ? "]\n    }" : "\n      ]\n    }";
        }

        json += first_chip ? "]\n}\n" : "\n  ]\n}\n";

        const std::string manifest_path = base_path + ".assets.json";
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
        if (!io::write_file(manifest_path, bytes)) {
            std::fprintf(stderr, "[asset_export] could not write %s\n", manifest_path.c_str());
        }
        return written;
    }

} // namespace mnemos::debug
