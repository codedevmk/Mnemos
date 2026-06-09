#include "asset_export.hpp"

#include "asset_views.hpp"
#include "file.hpp"
#include "json_util.hpp"
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

        // Write an asset as a palette-indexed PNG (lossless: raw indices + a PLTE
        // built from the referenced palette, with tRNS for its transparent entry).
        bool write_indexed_png(const std::string& path, const instrumentation::indexed_image& img,
                               std::span<const instrumentation::palette_view> pals) {
            if (img.width == 0U || img.height == 0U) {
                return false;
            }
            std::vector<std::uint32_t> colors;
            int transparent = -1;
            if (img.palette < pals.size()) {
                const instrumentation::palette_view& p = pals[img.palette];
                colors.assign(p.colors.begin(), p.colors.end());
                transparent = p.transparent_index;
            }
            std::vector<std::uint8_t> idx(img.indices.begin(), img.indices.end());
            const graphics::images::indexed_png_image png(img.width, img.height, std::move(idx),
                                                          std::move(colors), transparent);
            if (!png.write(path)) {
                std::fprintf(stderr, "[asset_export] could not write %s\n", path.c_str());
                return false;
            }
            return true;
        }

        // Write a palette as a JASC-PAL text file (the widely-importable sprite-tool
        // palette format): a header, the entry count, then "R G B" decimal triplets.
        bool write_pal(const std::string& path, const instrumentation::palette_view& pal) {
            std::string out = "JASC-PAL\n0100\n" + std::to_string(pal.colors.size()) + "\n";
            for (std::uint32_t c : pal.colors) {
                out += std::to_string((c >> 16U) & 0xFFU) + " " +
                       std::to_string((c >> 8U) & 0xFFU) + " " + std::to_string(c & 0xFFU) + "\n";
            }
            const std::span<const std::uint8_t> bytes(
                reinterpret_cast<const std::uint8_t*>(out.data()), out.size());
            if (!io::write_file(path, bytes)) {
                std::fprintf(stderr, "[asset_export] could not write %s\n", path.c_str());
                return false;
            }
            return true;
        }

        // Pack a chip's debug_layer framebuffer (possibly strided) into a tight
        // RGB raster and write it as PNG. These are palette-resolved RGB scenes
        // (a composed plane), distinct from the indexed graphic assets.
        bool write_layer_png(const std::string& path, const chips::frame_buffer_view& fb) {
            if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
                return false;
            }
            const std::uint32_t stride = fb.effective_stride();
            std::vector<std::uint32_t> packed;
            packed.reserve(static_cast<std::size_t>(fb.width) * fb.height);
            for (std::uint32_t y = 0; y < fb.height; ++y) {
                const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
                packed.insert(packed.end(), row, row + fb.width);
            }
            return write_png(path, fb.width, fb.height, std::move(packed));
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
            const std::span<instrumentation::debug_layer* const> layers =
                chip->introspection().debug_layers();
            // Include a chip with decoded graphics (asset_source) and/or composed
            // RGB scenes (debug_layers); skip chips that expose neither.
            if (src == nullptr && layers.empty()) {
                continue;
            }
            const std::string chip_id = sanitize_id(chip->metadata().part_number);
            const std::span<const instrumentation::palette_view> pals =
                src != nullptr ? src->palettes() : std::span<const instrumentation::palette_view>{};
            const std::span<const instrumentation::graphic_asset> assets =
                src != nullptr ? src->graphics()
                               : std::span<const instrumentation::graphic_asset>{};

            json += first_chip ? "\n" : ",\n";
            first_chip = false;
            json += "    {\n      \"id\": " + json_string(chip_id) +
                    ",\n      \"part_number\": " + json_string(chip->metadata().part_number) +
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
                const std::string pal_path =
                    base_path + "." + chip_id + ".pal." + std::string(p.name) + ".pal";
                (void)write_pal(pal_path, p);
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_string(p.name) +
                        ", \"colors\": " + std::to_string(p.colors.size()) +
                        ", \"transparent_index\": " + std::to_string(p.transparent_index) +
                        ", \"file\": " + json_string(path_basename(path)) +
                        ", \"pal_file\": " + json_string(path_basename(pal_path)) + "}";
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
                const std::string idx_path =
                    base_path + "." + chip_id + "." + kind + "." + std::string(a.name) + ".idx.png";
                if (write_indexed_png(idx_path, a.image, pals)) {
                    ++written;
                }
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_string(a.name) +
                        ", \"kind\": " + json_string(kind) +
                        ", \"width\": " + std::to_string(a.image.width) +
                        ", \"height\": " + std::to_string(a.image.height) +
                        ", \"tile_w\": " + std::to_string(a.tile_w) +
                        ", \"tile_h\": " + std::to_string(a.tile_h) +
                        ", \"palette\": " + std::to_string(a.image.palette) +
                        ", \"source_addr\": " + std::to_string(a.source_addr) +
                        ", \"file\": " + json_string(path_basename(path)) +
                        ", \"indexed_file\": " + json_string(path_basename(idx_path)) + "}";
            }
            json += assets.empty() ? "],\n      \"layers\": [" : "\n      ],\n      \"layers\": [";

            // Composed RGB scenes (a full plane / nametable). Distinct from the
            // indexed graphic assets above; written as resolved-RGB PNG only.
            std::size_t emitted_layers = 0;
            for (instrumentation::debug_layer* layer : layers) {
                if (layer == nullptr) {
                    continue;
                }
                const chips::frame_buffer_view fb = layer->view();
                const std::string path =
                    base_path + "." + chip_id + ".layer." + std::string(layer->name()) + ".png";
                if (write_layer_png(path, fb)) {
                    ++written;
                }
                json += emitted_layers == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_string(layer->name()) +
                        ", \"width\": " + std::to_string(fb.width) +
                        ", \"height\": " + std::to_string(fb.height) +
                        ", \"file\": " + json_string(path_basename(path)) + "}";
                ++emitted_layers;
            }
            json += emitted_layers == 0 ? "]\n    }" : "\n      ]\n    }";
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
