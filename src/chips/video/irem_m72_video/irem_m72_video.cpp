#include "irem_m72_video.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 5-bit gun to 8 bits, top bits replicated into the low bits.
        [[nodiscard]] constexpr std::uint32_t expand5(std::uint8_t gun) noexcept {
            const std::uint32_t v = gun & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }

        // Per-group pen skip masks (bit set = the pen does not render in this
        // pass). The "below" pass paints under sprites, the "above" pass over
        // them; together a group's two masks partition the opaque pens.
        // Group 0 of the back playfield is the opaque backdrop (even pen 0
        // paints below); group 0 of the front playfield hides nothing above.
        constexpr std::array<std::uint16_t, 4> fg_below{0x0001U, 0xFF01U, 0xFFFFU, 0xFFFFU};
        constexpr std::array<std::uint16_t, 4> fg_above{0xFFFFU, 0x00FFU, 0x0001U, 0x0001U};
        constexpr std::array<std::uint16_t, 4> bg_below{0x0000U, 0xFF00U, 0xFFFEU, 0xFFFEU};
        constexpr std::array<std::uint16_t, 4> bg_above{0xFFFFU, 0x00FFU, 0x0001U, 0x0001U};
    } // namespace

    chip_metadata irem_m72_video::metadata() const noexcept {
        return {
            .manufacturer = "Irem",
            .part_number = "m72_video",
            .family = "m72",
            .klass = chip_class::video,
            .revision = 2U,
        };
    }

    void irem_m72_video::reset(reset_kind /*kind*/) {
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        scroll_a_x_ = scroll_a_y_ = 0U;
        scroll_b_x_ = scroll_b_y_ = 0U;
        raster_compare_ = -1;
        display_enabled_ = true;
        flip_screen_ = false;
        sprite_buffer_.fill(0U);
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void irem_m72_video::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_x_ == 0U) {
                if (beam_y_ == 0U) {
                    begin_frame();
                }
                if (beam_y_ < visible_height) {
                    render_scanline(beam_y_);
                }
                if (beam_y_ == visible_height) {
                    finish_frame();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(beam_y_);
                    }
                }
                if (scanline_cb_) {
                    scanline_cb_(beam_y_);
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

    void irem_m72_video::latch_sprites() noexcept {
        const std::size_t count = std::min(sprite_ram_.size(), sprite_buffer_.size());
        std::copy_n(sprite_ram_.begin(), count, sprite_buffer_.begin());
    }

    frame_buffer_view irem_m72_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    std::uint32_t irem_m72_video::lookup_rgb(std::span<const std::uint8_t> palette,
                                             std::size_t index) noexcept {
        // 5-bit gun planes at +0x000 (R), +0x400 (G), +0x800 (B), word-wide
        // entries with only the low byte's D0-D4 driven.
        const std::size_t byte = (index & 0xFFU) * 2U;
        if (palette.size() < 0xC00U) {
            return 0U;
        }
        const std::uint32_t r = expand5(palette[byte]);
        const std::uint32_t g = expand5(palette[byte + 0x400U]);
        const std::uint32_t b = expand5(palette[byte + 0x800U]);
        return (r << 16U) | (g << 8U) | b;
    }

    void irem_m72_video::render_layer_scanline(
        std::uint32_t y,
        std::span<const std::uint8_t> vram,
        std::span<const std::uint8_t> tiles,
        std::uint16_t scroll_x,
        std::uint16_t scroll_y,
        std::span<const std::uint16_t, 4> skip_masks) noexcept {
        if (vram.size() < static_cast<std::size_t>(map_tiles) * map_tiles * vram_entry_bytes ||
            tiles.size() < 4U * 8U) {
            return; // nothing attached (yet); leave the backdrop
        }
        const std::size_t plane_size = tiles.size() / 4U;
        const std::size_t tile_count = plane_size / 8U;

        // Scroll Y lives in the 128-biased beam space (+384 ≡ -128 mod 512).
        const std::uint32_t map_y =
            (y + scroll_y + 512U - static_cast<std::uint32_t>(line_bias)) &
            (map_tiles * 8U - 1U);
        for (std::uint32_t x = 0; x < visible_width; ++x) {
            const std::uint32_t map_x = (x + beam_x_origin + scroll_x) & (map_tiles * 8U - 1U);
            const std::size_t entry =
                (static_cast<std::size_t>(map_y / 8U) * map_tiles + map_x / 8U) *
                vram_entry_bytes;
            const std::uint16_t word0 =
                static_cast<std::uint16_t>(vram[entry] | (vram[entry + 1U] << 8U));
            const std::uint16_t word1 =
                static_cast<std::uint16_t>(vram[entry + 2U] | (vram[entry + 3U] << 8U));
            const std::size_t code = (word0 & 0x3FFFU) % tile_count;
            const std::uint32_t color = word1 & 0x0FU;
            const std::uint32_t group = (word1 >> 6U) & 0x03U;
            const std::uint32_t tx = (word0 & 0x4000U) != 0U ? 7U - (map_x & 7U) : (map_x & 7U);
            const std::uint32_t ty = (word0 & 0x8000U) != 0U ? 7U - (map_y & 7U) : (map_y & 7U);
            const std::size_t row = code * 8U + ty;
            const std::uint32_t bit = 7U - tx;
            std::uint32_t pixel = 0U;
            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                pixel |= ((tiles[plane * plane_size + row] >> bit) & 1U) << plane;
            }
            if (((skip_masks[group] >> pixel) & 1U) != 0U) {
                continue;
            }
            pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                lookup_rgb(palette_b_, color * 16U + pixel);
        }
    }

    void irem_m72_video::render_sprites_scanline(std::uint32_t y) noexcept {
        if (sprites_.size() < 4U * sprite_cell_bytes) {
            return; // nothing attached (yet)
        }
        const std::size_t plane_size = sprites_.size() / 4U;
        const std::size_t cell_count = plane_size / sprite_cell_bytes;

        const auto word = [this](std::size_t index) -> std::uint16_t {
            return static_cast<std::uint16_t>(sprite_buffer_[index * 2U] |
                                              (sprite_buffer_[index * 2U + 1U] << 8U));
        };

        // A width-W entry occupies W slots; collect the entry offsets, then
        // draw back-to-front so earlier entries land on top.
        std::array<std::size_t, 0x400U / irem_m72_video::sprite_entry_bytes> entries{};
        std::size_t entry_count = 0U;
        const std::size_t total_words = sprite_buffer_.size() / 2U;
        for (std::size_t offs = 0U; offs < total_words && entry_count < entries.size();) {
            entries[entry_count++] = offs;
            const std::uint32_t blocks_w = 1U << ((word(offs + 2U) >> 14U) & 3U);
            offs += static_cast<std::size_t>(blocks_w) * 4U;
        }

        for (std::size_t i = entry_count; i-- > 0U;) {
            const std::size_t offs = entries[i];
            const std::uint16_t w0 = word(offs);
            const std::size_t code = word(offs + 1U);
            const std::uint16_t w2 = word(offs + 2U);
            const std::uint16_t w3 = word(offs + 3U);
            const std::uint32_t color = w2 & 0x0FU;
            const bool flip_y = (w2 & 0x0400U) != 0U;
            const bool flip_x = (w2 & 0x0800U) != 0U;
            const std::int32_t blocks_h = 1 << ((w2 >> 12U) & 3U);
            const std::int32_t blocks_w = 1 << ((w2 >> 14U) & 3U);
            // Beam-space position -> visible-window position.
            const std::int32_t sx = static_cast<std::int32_t>(w3 & 0x3FFU) - 256 -
                                    static_cast<std::int32_t>(beam_x_origin);
            const std::int32_t sy = 384 - static_cast<std::int32_t>(w0 & 0x1FFU) - 16 * blocks_h;

            for (std::int32_t col = 0; col < blocks_w; ++col) {
                for (std::int32_t row = 0; row < blocks_h; ++row) {
                    const std::size_t cell =
                        (code +
                         static_cast<std::size_t>(flip_x ? 8 * (blocks_w - 1 - col) : 8 * col) +
                         static_cast<std::size_t>(flip_y ? blocks_h - 1 - row : row)) %
                        cell_count;
                    const std::int32_t base_x = sx + 16 * col;
                    const std::int32_t base_y = sy + 16 * row;
                    const auto line_y = static_cast<std::int32_t>(y);
                    if (line_y < base_y || line_y >= base_y + 16) {
                        continue;
                    }
                    const std::int32_t py = line_y - base_y;
                    const std::uint32_t ty = static_cast<std::uint32_t>(flip_y ? 15 - py : py);
                    for (std::int32_t px = 0; px < 16; ++px) {
                        const std::int32_t dest_x = base_x + px;
                        if (dest_x < 0 || dest_x >= static_cast<std::int32_t>(visible_width)) {
                            continue;
                        }
                        const std::uint32_t tx = static_cast<std::uint32_t>(flip_x ? 15 - px : px);
                        // Cell layout per plane: left 8x16 half then the
                        // right half, one byte per row each.
                        const std::size_t byte_index =
                            cell * sprite_cell_bytes + (tx >= 8U ? 16U : 0U) + ty;
                        const std::uint32_t bit = 7U - (tx & 7U);
                        std::uint32_t pixel = 0U;
                        for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                            pixel |= ((sprites_[plane * plane_size + byte_index] >> bit) & 1U)
                                     << plane;
                        }
                        if (pixel == 0U) {
                            continue; // transparent
                        }
                        pixels_[static_cast<std::size_t>(y) * visible_width +
                                static_cast<std::size_t>(dest_x)] =
                            lookup_rgb(palette_a_, color * 16U + pixel);
                    }
                }
            }
        }
    }

    void irem_m72_video::begin_frame() noexcept {
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void irem_m72_video::render_scanline(std::uint32_t y) noexcept {
        const std::size_t offset = static_cast<std::size_t>(y) * visible_width;
        std::fill_n(pixels_.begin() + static_cast<std::ptrdiff_t>(offset), visible_width, 0U);
        if (!display_enabled_) {
            return;
        }
        // Below-sprite pens of both playfields, sprites, then the above-
        // sprite pens (back playfield first in each pass).
        render_layer_scanline(y, vram_b_, tiles_b_, scroll_b_x_, scroll_b_y_, bg_below);
        render_layer_scanline(y, vram_a_, tiles_a_, scroll_a_x_, scroll_a_y_, fg_below);
        render_sprites_scanline(y);
        render_layer_scanline(y, vram_b_, tiles_b_, scroll_b_x_, scroll_b_y_, bg_above);
        render_layer_scanline(y, vram_a_, tiles_a_, scroll_a_x_, scroll_a_y_, fg_above);
    }

    void irem_m72_video::finish_frame() noexcept {
        if (flip_screen_) {
            flip_framebuffer();
        }
    }

    void irem_m72_video::flip_framebuffer() noexcept {
        std::reverse(pixels_.begin(), pixels_.end());
    }

    void irem_m72_video::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.u16(scroll_a_x_);
        writer.u16(scroll_a_y_);
        writer.u16(scroll_b_x_);
        writer.u16(scroll_b_y_);
        writer.u32(static_cast<std::uint32_t>(raster_compare_));
        writer.boolean(display_enabled_);
        writer.boolean(flip_screen_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
        writer.bytes(sprite_buffer_);
    }

    void irem_m72_video::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        scroll_a_x_ = reader.u16();
        scroll_a_y_ = reader.u16();
        scroll_b_x_ = reader.u16();
        scroll_b_y_ = reader.u16();
        raster_compare_ = static_cast<std::int32_t>(reader.u32());
        display_enabled_ = reader.boolean();
        flip_screen_ = reader.boolean();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
        reader.bytes(sprite_buffer_);
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
