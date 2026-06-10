#include "irem_m72_video.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 5-bit gun to 8 bits, top bits replicated into the low bits.
        [[nodiscard]] constexpr std::uint32_t expand5(std::uint8_t gun) noexcept {
            const std::uint32_t v = gun & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }
    } // namespace

    chip_metadata irem_m72_video::metadata() const noexcept {
        return {
            .manufacturer = "Irem",
            .part_number = "m72_video",
            .family = "m72",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void irem_m72_video::reset(reset_kind /*kind*/) {
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        scroll_a_x_ = scroll_a_y_ = 0U;
        scroll_b_x_ = scroll_b_y_ = 0U;
        raster_compare_ = frame_lines;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void irem_m72_video::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_x_ == 0U) {
                if (beam_y_ == raster_compare_ && raster_cb_) {
                    raster_cb_(beam_y_);
                }
                if (beam_y_ == visible_height) {
                    render_frame();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(beam_y_);
                    }
                }
            }
            if (++beam_x_ == line_pixels) {
                beam_x_ = 0U;
                if (++beam_y_ == frame_lines) {
                    beam_y_ = 0U;
                }
            }
        }
    }

    frame_buffer_view irem_m72_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    std::uint32_t irem_m72_video::lookup_rgb(std::span<const std::uint8_t> palette,
                                             std::size_t index) noexcept {
        // 5-bit gun planes at +0x000 (R), +0x400 (G), +0x800 (B).
        if (palette.size() < 0xC00U) {
            return 0U;
        }
        const std::uint32_t r = expand5(palette[index]);
        const std::uint32_t g = expand5(palette[index + 0x400U]);
        const std::uint32_t b = expand5(palette[index + 0x800U]);
        return (r << 16U) | (g << 8U) | b;
    }

    std::uint32_t irem_m72_video::palette_rgb(std::size_t index) const noexcept {
        return lookup_rgb(palette_a_, index);
    }

    void irem_m72_video::render_layer(std::span<const std::uint8_t> vram,
                                      std::span<const std::uint8_t> tiles, std::uint16_t scroll_x,
                                      std::uint16_t scroll_y, bool transparent_pixel0) noexcept {
        if (vram.size() < static_cast<std::size_t>(map_tiles) * map_tiles * vram_entry_bytes ||
            tiles.size() < 4U * 8U) {
            return; // nothing attached (yet); leave the backdrop
        }
        const std::size_t plane_size = tiles.size() / 4U;
        const std::size_t tile_count = plane_size / 8U;

        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint32_t map_y = (y + scroll_y) & (map_tiles * 8U - 1U);
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint32_t map_x = (x + scroll_x) & (map_tiles * 8U - 1U);
                const std::size_t entry =
                    (static_cast<std::size_t>(map_y / 8U) * map_tiles + map_x / 8U) *
                    vram_entry_bytes;
                const std::size_t code =
                    static_cast<std::size_t>(vram[entry]) | (vram[entry + 1U] << 8U);
                const std::uint32_t color = vram[entry + 2U] & 0x0FU;
                const std::uint8_t attributes = vram[entry + 3U];
                if (code >= tile_count) {
                    continue; // beyond the attached tile ROM
                }
                const std::uint32_t tx =
                    (attributes & 0x01U) != 0U ? 7U - (map_x & 7U) : (map_x & 7U);
                const std::uint32_t ty =
                    (attributes & 0x02U) != 0U ? 7U - (map_y & 7U) : (map_y & 7U);
                const std::size_t row = code * 8U + ty;
                const std::uint32_t bit = 7U - tx;
                std::uint32_t pixel = 0U;
                for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                    pixel |= ((tiles[plane * plane_size + row] >> bit) & 1U) << plane;
                }
                if (transparent_pixel0 && pixel == 0U) {
                    continue;
                }
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    palette_rgb(color * 16U + pixel);
            }
        }
    }

    void irem_m72_video::render_sprites() noexcept {
        if (sprite_ram_.size() < sprite_entry_bytes || sprites_.size() < 4U * sprite_cell_bytes) {
            return; // nothing attached (yet)
        }
        const std::size_t plane_size = sprites_.size() / 4U;
        const std::size_t cell_count = plane_size / sprite_cell_bytes;
        const std::size_t entries = sprite_ram_.size() / sprite_entry_bytes;

        // Highest index first so the lowest entry index ends up on top.
        for (std::size_t i = entries; i-- > 0U;) {
            const std::span<const std::uint8_t> e =
                sprite_ram_.subspan(i * sprite_entry_bytes, sprite_entry_bytes);
            // First-cut empty-slot convention: an all-zero entry is disabled
            // (the real board's offscreen-park convention is a parity-pass item).
            bool empty = true;
            for (const std::uint8_t byte : e) {
                if (byte != 0U) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                continue;
            }
            const std::uint32_t y = static_cast<std::uint32_t>(e[0]) | ((e[1] & 1U) << 8U);
            const std::size_t code = static_cast<std::size_t>(e[2]) | (e[3] << 8U);
            const std::uint32_t color = e[4] & 0x0FU;
            const std::uint8_t attributes = e[5];
            const std::uint32_t x = static_cast<std::uint32_t>(e[6]) | ((e[7] & 1U) << 8U);
            const bool flip_x = (attributes & 0x01U) != 0U;
            const bool flip_y = (attributes & 0x02U) != 0U;
            const std::uint32_t blocks_w = 1U << ((attributes >> 2U) & 3U);
            const std::uint32_t blocks_h = 1U << ((attributes >> 4U) & 3U);

            for (std::uint32_t col = 0; col < blocks_w; ++col) {
                for (std::uint32_t row = 0; row < blocks_h; ++row) {
                    const std::size_t cell = code + col * blocks_h + row;
                    if (cell >= cell_count) {
                        continue; // beyond the attached sprite ROM
                    }
                    // Flips mirror the block arrangement as well as the pixels.
                    const std::uint32_t screen_col = flip_x ? blocks_w - 1U - col : col;
                    const std::uint32_t screen_row = flip_y ? blocks_h - 1U - row : row;
                    const std::uint32_t base_x = x + screen_col * 16U;
                    const std::uint32_t base_y = y + screen_row * 16U;
                    for (std::uint32_t py = 0; py < 16U; ++py) {
                        const std::uint32_t sy = base_y + py;
                        if (sy >= visible_height) {
                            continue;
                        }
                        const std::uint32_t ty = flip_y ? 15U - py : py;
                        for (std::uint32_t px = 0; px < 16U; ++px) {
                            const std::uint32_t sx = base_x + px;
                            if (sx >= visible_width) {
                                continue;
                            }
                            const std::uint32_t tx = flip_x ? 15U - px : px;
                            const std::size_t byte_index =
                                cell * sprite_cell_bytes + ty * 2U + (tx >> 3U);
                            const std::uint32_t bit = 7U - (tx & 7U);
                            std::uint32_t pixel = 0U;
                            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                                pixel |= ((sprites_[plane * plane_size + byte_index] >> bit) & 1U)
                                         << plane;
                            }
                            if (pixel == 0U) {
                                continue; // transparent
                            }
                            pixels_[static_cast<std::size_t>(sy) * visible_width + sx] =
                                lookup_rgb(palette_b_, color * 16U + pixel);
                        }
                    }
                }
            }
        }
    }

    void irem_m72_video::render_frame() noexcept {
        const std::uint32_t backdrop = palette_rgb(0U);
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }
        // Back playfield opaque, front playfield with pixel-0 transparency,
        // sprites on top (per-tile priority bits land with the parity pass).
        render_layer(vram_b_, tiles_b_, scroll_b_x_, scroll_b_y_, false);
        render_layer(vram_a_, tiles_a_, scroll_a_x_, scroll_a_y_, true);
        render_sprites();
    }

    void irem_m72_video::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.u16(scroll_a_x_);
        writer.u16(scroll_a_y_);
        writer.u16(scroll_b_x_);
        writer.u16(scroll_b_y_);
        writer.u32(raster_compare_);
    }

    void irem_m72_video::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        scroll_a_x_ = reader.u16();
        scroll_a_y_ = reader.u16();
        scroll_b_x_ = reader.u16();
        scroll_b_y_ = reader.u16();
        raster_compare_ = reader.u32();
    }

    instrumentation::ichip_introspection& irem_m72_video::introspection() noexcept {
        return introspection_;
    }

    frame_buffer_view irem_m72_video::introspection_surface::tile_sheet_layer::view() const {
        // Decode the attached tile ROM as a 32-tiles-per-row grayscale sheet
        // (pixel value 0-15 ramped to white). Rebuilt on each call.
        const irem_m72_video& v = *owner_;
        constexpr std::uint32_t sheet_tiles_per_row = 32U;
        constexpr std::uint32_t sheet_width = sheet_tiles_per_row * 8U;

        const std::size_t plane_size = v.tiles_a_.size() / 4U;
        const std::size_t tile_count = plane_size / 8U;
        const std::uint32_t rows = static_cast<std::uint32_t>(
            (tile_count + sheet_tiles_per_row - 1U) / sheet_tiles_per_row);
        v.tile_sheet_height_ = rows * 8U;
        v.tile_sheet_.assign(static_cast<std::size_t>(sheet_width) * v.tile_sheet_height_, 0U);

        for (std::size_t tile = 0; tile < tile_count; ++tile) {
            const std::uint32_t base_x =
                static_cast<std::uint32_t>(tile % sheet_tiles_per_row) * 8U;
            const std::uint32_t base_y =
                static_cast<std::uint32_t>(tile / sheet_tiles_per_row) * 8U;
            for (std::uint32_t ty = 0; ty < 8U; ++ty) {
                for (std::uint32_t tx = 0; tx < 8U; ++tx) {
                    std::uint32_t pixel = 0U;
                    for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                        pixel |=
                            ((v.tiles_a_[plane * plane_size + tile * 8U + ty] >> (7U - tx)) & 1U)
                            << plane;
                    }
                    const std::uint32_t gray = pixel * 17U; // 0..255 ramp
                    v.tile_sheet_[static_cast<std::size_t>(base_y + ty) * sheet_width + base_x +
                                  tx] = (gray << 16U) | (gray << 8U) | gray;
                }
            }
        }
        return {.pixels = v.tile_sheet_.data(),
                .width = sheet_width,
                .height = v.tile_sheet_height_,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto irem_m72_video_registration =
            register_factory("irem.m72_video", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<irem_m72_video>();
            });
    } // namespace

} // namespace mnemos::chips::video
