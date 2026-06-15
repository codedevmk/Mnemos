#include "cps_a_b.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    chip_metadata cps_a_b::metadata() const noexcept {
        return {
            .manufacturer = "Capcom",
            .part_number = "CPS-A/B",
            .family = "CPS1",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    std::uint32_t cps_a_b::decode_color(std::uint16_t entry) noexcept {
        // iiii rrrr gggg bbbb: the high nibble is a global brightness. Each 4-bit
        // gun is scaled by intensity = 0x0F + 2*brightness (15..45), normalised by
        // 0x2D (45) so full brightness maps a gun nibble straight to 8 bits.
        const std::uint32_t brightness = (static_cast<std::uint32_t>(entry) >> 12U) & 0x0FU;
        const std::uint32_t intensity = 0x0FU + (brightness << 1U);
        const auto scale = [intensity](std::uint32_t gun4) -> std::uint32_t {
            return (gun4 * 0x11U * intensity) / 0x2DU;
        };
        const std::uint32_t r = scale((static_cast<std::uint32_t>(entry) >> 8U) & 0x0FU);
        const std::uint32_t g = scale((static_cast<std::uint32_t>(entry) >> 4U) & 0x0FU);
        const std::uint32_t b = scale(static_cast<std::uint32_t>(entry) & 0x0FU);
        return (r << 16U) | (g << 8U) | b;
    }

    std::uint16_t cps_a_b::read_palette(std::uint16_t pal_num, std::uint8_t pen) const noexcept {
        const std::size_t off = (static_cast<std::size_t>(pal_num) * 16U + pen) * 2U;
        if (off + 1U >= palette_.size()) {
            return 0U;
        }
        // Palette words are stored big-endian (high byte first).
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(palette_[off]) << 8U) |
                                          palette_[off + 1U]);
    }

    std::uint16_t cps_a_b::read_cps_b(std::uint8_t offset) const noexcept {
        if (offset == reg_none || (offset & 1U) != 0U) {
            return 0U;
        }
        const std::uint8_t index = static_cast<std::uint8_t>(offset >> 1U);
        return index < cps_b_reg_count ? cps_b_regs_[index] : 0U;
    }

    std::uint16_t cps_a_b::priority_mask(std::uint8_t group) const noexcept {
        if (group >= profile_.priority_offset.size()) {
            return 0U;
        }
        return read_cps_b(profile_.priority_offset[group]);
    }

    bool cps_a_b::tile_pen_has_sprite_priority(std::uint8_t group,
                                               std::uint8_t pen) const noexcept {
        if (pen >= 16U) {
            return false;
        }
        return (priority_mask(group) & static_cast<std::uint16_t>(1U << pen)) != 0U;
    }

    bool cps_a_b::layer_enabled(std::uint16_t layercontrol, std::uint8_t layer) const noexcept {
        if (layer < 1U || layer > 3U || layercontrol == 0U) {
            return true;
        }
        const std::uint16_t mask = profile_.layer_enable_mask[layer - 1U];
        if (mask == 0U) {
            return true;
        }
        if ((layer == 2U && (video_control_ & 0x0004U) == 0U) ||
            (layer == 3U && (video_control_ & 0x0008U) == 0U)) {
            return false;
        }
        return (layercontrol & mask) != 0U;
    }

    void cps_a_b::reset(reset_kind /*kind*/) {
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        scroll1_x_ = scroll1_y_ = 0U;
        scroll2_x_ = scroll2_y_ = 0U;
        scroll3_x_ = scroll3_y_ = 0U;
        scroll1_base_ = scroll2_base_ = scroll3_base_ = 0U;
        rowscroll_enabled_ = false;
        rowscroll_base_ = 0U;
        rowscroll_offset_ = 0U;
        object_base_ = 0U;
        sprite_order_ = sprite_order::ascending;
        sprite_buffer_.fill(0U);
        sprite_buffer_valid_ = false;
        cps_b_regs_.fill(0U);
        profile_ = cps_b_profile{};
        video_control_ = 0U;
        display_enabled_ = true;
        raster_compare_ = -1;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
        std::fill(pixel_layer_.begin(), pixel_layer_.end(), std::uint8_t{0xFFU});
        std::fill(pixel_pen_.begin(), pixel_pen_.end(), std::uint8_t{0U});
        std::fill(pixel_group_.begin(), pixel_group_.end(), std::uint8_t{0U});
    }

    void cps_a_b::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_x_ == 0U) {
                if (beam_y_ == vblank_start) {
                    render_frame();
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

    frame_buffer_view cps_a_b::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    void cps_a_b::plot_layer_pixel(int px, int py, bool flip, std::uint8_t layer, std::uint8_t pen,
                                   std::uint8_t group, std::uint32_t rgb) noexcept {
        if (flip) {
            px = static_cast<int>(visible_width) - 1 - px;
            py = static_cast<int>(visible_height) - 1 - py;
        }
        if (px < 0 || px >= static_cast<int>(visible_width) || py < 0 ||
            py >= static_cast<int>(visible_height)) {
            return;
        }
        const std::size_t idx = static_cast<std::size_t>(py) * visible_width + px;
        pixels_[idx] = rgb;
        pixel_layer_[idx] = layer;
        pixel_pen_[idx] = pen;
        pixel_group_[idx] = group;
    }

    void cps_a_b::render_frame() noexcept {
        if (!display_enabled_) {
            for (std::uint32_t& px : pixels_) {
                px = 0U;
            }
            return;
        }
        const std::uint32_t backdrop = backdrop_rgb();
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }
        std::fill(pixel_layer_.begin(), pixel_layer_.end(), std::uint8_t{0xFFU});
        std::fill(pixel_pen_.begin(), pixel_pen_.end(), std::uint8_t{0U});
        std::fill(pixel_group_.begin(), pixel_group_.end(), std::uint8_t{0U});

        const bool flip = flip_screen();
        const std::uint16_t lc = layer_control();
        int l0 = 3;
        int l1 = 2;
        int l2 = 1;
        int l3 = 0;
        // A zero layer-control latch falls back to the canonical bottom-to-top
        // order only for the synthetic legacy profile; a real board profile
        // decodes the latch literally (matching the hardware).
        if (lc != 0U || !profile_.legacy) {
            l0 = (lc >> 6U) & 3;
            l1 = (lc >> 8U) & 3;
            l2 = (lc >> 10U) & 3;
            l3 = (lc >> 12U) & 3;
        }
        int layer_below = -1;
        if (l1 == 0) {
            layer_below = l0;
        } else if (l2 == 0) {
            layer_below = l1;
        } else if (l3 == 0) {
            layer_below = l2;
        }

        const std::array<int, 4> layers = {l0, l1, l2, l3};
        for (const int layer : layers) {
            switch (layer) {
            case 0:
                draw_sprites(layer_below, flip);
                break;
            case 1:
                if (layer_enabled(lc, 1)) {
                    draw_scroll1(flip);
                }
                break;
            case 2:
                if (layer_enabled(lc, 2)) {
                    draw_scroll2(flip);
                }
                break;
            case 3:
                if (layer_enabled(lc, 3)) {
                    draw_scroll3(flip);
                }
                break;
            default:
                break;
            }
        }
    }

    std::uint32_t cps_a_b::map_gfx_code(gfx_type /*type*/, std::uint32_t code) const noexcept {
        // Identity until the per-board CPS-B graphics mapper (profile data) lands.
        return code;
    }

    std::uint16_t cps_a_b::read_tile16(std::uint32_t offset) const noexcept {
        if (static_cast<std::size_t>(offset) + 1U >= tile_ram_.size()) {
            return 0xFFFFU;
        }
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(tile_ram_[offset]) << 8U) |
                                          tile_ram_[offset + 1U]);
    }

    std::uint8_t cps_a_b::decode_packed(std::uint32_t tile_base, std::uint32_t row_stride, int x,
                                        int y) const noexcept {
        const std::uint32_t group = static_cast<std::uint32_t>(x) >> 3U;
        const std::uint32_t bit = 7U - (static_cast<std::uint32_t>(x) & 7U);
        const std::uint32_t offset =
            tile_base + static_cast<std::uint32_t>(y) * row_stride + group * 4U;
        if (static_cast<std::size_t>(offset) + 3U >= gfx_.size()) {
            return transparent_pen;
        }
        return static_cast<std::uint8_t>(
            (((gfx_[offset + 0U] >> bit) & 1U) << 0U) | (((gfx_[offset + 1U] >> bit) & 1U) << 1U) |
            (((gfx_[offset + 2U] >> bit) & 1U) << 2U) | (((gfx_[offset + 3U] >> bit) & 1U) << 3U));
    }

    std::uint8_t cps_a_b::tile_pixel(gfx_type type, std::uint32_t code, int x, int y, int tile_size,
                                     int x_bias) const noexcept {
        if (gfx_.empty()) {
            return transparent_pen;
        }
        const std::uint32_t mapped = map_gfx_code(type, code);
        const int rom_x = x + x_bias;
        if (tile_size == 8) {
            return decode_packed(mapped * 64U, 8U, rom_x, y);
        }
        if (tile_size == 16) {
            return decode_packed(mapped * 128U, 8U, x, y);
        }
        if (tile_size == 32) {
            return decode_packed(mapped * 512U, 16U, x, y);
        }
        return 0U;
    }

    std::uint32_t cps_a_b::scroll1_tile_index(std::uint32_t row, std::uint32_t col) noexcept {
        return (row & 0x1FU) + ((col & 0x3FU) << 5U) + ((row & 0x20U) << 6U);
    }
    std::uint32_t cps_a_b::scroll2_tile_index(std::uint32_t row, std::uint32_t col) noexcept {
        return (row & 0x0FU) + ((col & 0x3FU) << 4U) + ((row & 0x30U) << 6U);
    }
    std::uint32_t cps_a_b::scroll3_tile_index(std::uint32_t row, std::uint32_t col) noexcept {
        return (row & 0x07U) + ((col & 0x3FU) << 3U) + ((row & 0x38U) << 6U);
    }

    void cps_a_b::draw_scroll1(bool flip) noexcept {
        if (gfx_.empty() || tile_ram_.empty()) {
            return;
        }
        const int scroll_x = static_cast<std::int16_t>(scroll1_x_);
        const int scroll_y = static_cast<std::int16_t>(scroll1_y_);
        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const int screen_y = static_cast<int>(py + visible_y_start);
            const std::uint32_t world_y = static_cast<std::uint32_t>(screen_y + scroll_y) & 0x1FFU;
            const std::uint32_t row = world_y / 8U;
            const int ty = static_cast<int>(world_y % 8U);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const int screen_x = static_cast<int>(px + visible_x_start);
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(screen_x + scroll_x) & 0x1FFU;
                const std::uint32_t col = world_x / 8U;
                const int tx = static_cast<int>(world_x % 8U);

                const std::uint32_t tile_index = scroll1_tile_index(row, col);
                const std::uint32_t map_offset = scroll1_base_ + tile_index * 4U;
                if (static_cast<std::size_t>(map_offset) + 3U >= tile_ram_.size()) {
                    continue;
                }
                const std::uint16_t code = read_tile16(map_offset);
                const std::uint16_t attr = read_tile16(map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) ? (7 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) ? (7 - ty) : ty;
                const int x_bias = (tile_index & 0x20U) ? 8 : 0;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll1, code, fetch_x, fetch_y, 8, x_bias);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num = static_cast<std::uint16_t>(32U + (attr & 0x001FU));
                const std::uint8_t pgroup = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(static_cast<int>(px), static_cast<int>(py), flip, 1U, pen, pgroup,
                                 decode_color(read_palette(pal_num, pen)));
            }
        }
    }

    void cps_a_b::draw_scroll2(bool flip) noexcept {
        if (gfx_.empty() || tile_ram_.empty()) {
            return;
        }
        const int scroll_y = static_cast<std::int16_t>(scroll2_y_);
        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const int screen_y = static_cast<int>(py + visible_y_start);
            int scroll_x = static_cast<std::int16_t>(scroll2_x_);
            if (rowscroll_enabled_) {
                const std::uint32_t n =
                    (static_cast<std::uint32_t>(screen_y) + rowscroll_offset_) & 0x03FFU;
                const std::uint32_t entry = rowscroll_base_ + n * 2U;
                const std::int16_t line_scroll =
                    (static_cast<std::size_t>(entry) + 1U < tile_ram_.size())
                        ? static_cast<std::int16_t>(read_tile16(entry))
                        : std::int16_t{0};
                scroll_x += line_scroll;
            }
            const std::uint32_t world_y = static_cast<std::uint32_t>(screen_y + scroll_y) & 0x3FFU;
            const std::uint32_t row = world_y / 16U;
            const int ty = static_cast<int>(world_y % 16U);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const int screen_x = static_cast<int>(px + visible_x_start);
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(screen_x + scroll_x) & 0x3FFU;
                const std::uint32_t col = world_x / 16U;
                const int tx = static_cast<int>(world_x % 16U);

                const std::uint32_t map_offset = scroll2_base_ + scroll2_tile_index(row, col) * 4U;
                if (static_cast<std::size_t>(map_offset) + 3U >= tile_ram_.size()) {
                    continue;
                }
                const std::uint16_t code = read_tile16(map_offset);
                const std::uint16_t attr = read_tile16(map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) ? (15 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) ? (15 - ty) : ty;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll2, code, fetch_x, fetch_y, 16, 0);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num = static_cast<std::uint16_t>(64U + (attr & 0x001FU));
                const std::uint8_t pgroup = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(static_cast<int>(px), static_cast<int>(py), flip, 2U, pen, pgroup,
                                 decode_color(read_palette(pal_num, pen)));
            }
        }
    }

    void cps_a_b::draw_scroll3(bool flip) noexcept {
        if (gfx_.empty() || tile_ram_.empty()) {
            return;
        }
        const int scroll_x = static_cast<std::int16_t>(scroll3_x_);
        const int scroll_y = static_cast<std::int16_t>(scroll3_y_);
        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const int screen_y = static_cast<int>(py + visible_y_start);
            const std::uint32_t world_y = static_cast<std::uint32_t>(screen_y + scroll_y) & 0x7FFU;
            const std::uint32_t row = world_y / 32U;
            const int ty = static_cast<int>(world_y % 32U);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const int screen_x = static_cast<int>(px + visible_x_start);
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(screen_x + scroll_x) & 0x7FFU;
                const std::uint32_t col = world_x / 32U;
                const int tx = static_cast<int>(world_x % 32U);

                const std::uint32_t map_offset = scroll3_base_ + scroll3_tile_index(row, col) * 4U;
                if (static_cast<std::size_t>(map_offset) + 3U >= tile_ram_.size()) {
                    continue;
                }
                const std::uint16_t code =
                    static_cast<std::uint16_t>(read_tile16(map_offset) & 0x3FFFU);
                const std::uint16_t attr = read_tile16(map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) ? (31 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) ? (31 - ty) : ty;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll3, code, fetch_x, fetch_y, 32, 0);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num = static_cast<std::uint16_t>(96U + (attr & 0x001FU));
                const std::uint8_t pgroup = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(static_cast<int>(px), static_cast<int>(py), flip, 3U, pen, pgroup,
                                 decode_color(read_palette(pal_num, pen)));
            }
        }
    }

    void cps_a_b::latch_sprites() noexcept {
        sprite_buffer_.fill(0U);
        if (object_base_ != 0U) {
            if (static_cast<std::size_t>(object_base_) < tile_ram_.size()) {
                const std::size_t n =
                    std::min(sprite_buffer_.size(), tile_ram_.size() - object_base_);
                std::copy_n(tile_ram_.begin() + static_cast<std::ptrdiff_t>(object_base_), n,
                            sprite_buffer_.begin());
            }
        } else if (!object_ram_.empty()) {
            const std::size_t n = std::min(sprite_buffer_.size(), object_ram_.size());
            std::copy_n(object_ram_.begin(), n, sprite_buffer_.begin());
        }
        sprite_buffer_valid_ = true;
    }

    bool cps_a_b::read_sprite_entry(std::uint32_t index, std::uint16_t& x, std::uint16_t& y,
                                    std::uint16_t& code, std::uint16_t& attr) const noexcept {
        const std::size_t offset = static_cast<std::size_t>(index) * object_entry_bytes;
        if (offset + object_entry_bytes > sprite_buffer_.size()) {
            return false;
        }
        x = static_cast<std::uint16_t>((sprite_buffer_[offset + 0U] << 8U) |
                                       sprite_buffer_[offset + 1U]);
        y = static_cast<std::uint16_t>((sprite_buffer_[offset + 2U] << 8U) |
                                       sprite_buffer_[offset + 3U]);
        code = static_cast<std::uint16_t>((sprite_buffer_[offset + 4U] << 8U) |
                                          sprite_buffer_[offset + 5U]);
        attr = static_cast<std::uint16_t>((sprite_buffer_[offset + 6U] << 8U) |
                                          sprite_buffer_[offset + 7U]);
        return true;
    }

    std::uint32_t cps_a_b::sprite_entry_count() const noexcept {
        const std::uint32_t limit =
            static_cast<std::uint32_t>(object_table_bytes / object_entry_bytes);
        for (std::uint32_t i = 0; i < limit; ++i) {
            std::uint16_t x = 0;
            std::uint16_t y = 0;
            std::uint16_t code = 0;
            std::uint16_t attr = 0;
            if (!read_sprite_entry(i, x, y, code, attr)) {
                return i;
            }
            if ((attr & 0xFF00U) == 0xFF00U) {
                return i;
            }
        }
        return limit;
    }

    std::uint32_t cps_a_b::sprite_block_tile(std::uint32_t mapped, int blocks_x, int blocks_y,
                                             int bx, int by, bool flip_x, bool flip_y) noexcept {
        const int tile_x = flip_x ? (blocks_x - 1 - bx) : bx;
        const int tile_y = flip_y ? (blocks_y - 1 - by) : by;
        return (mapped & ~0x0FU) + ((mapped + static_cast<std::uint32_t>(tile_x)) & 0x0FU) +
               0x10U * static_cast<std::uint32_t>(tile_y);
    }

    void cps_a_b::draw_sprite_entry(std::uint32_t index, int layer_below, bool flip) noexcept {
        std::uint16_t raw_x = 0;
        std::uint16_t raw_y = 0;
        std::uint16_t code = 0;
        std::uint16_t attr = 0;
        if (!read_sprite_entry(index, raw_x, raw_y, code, attr)) {
            return;
        }
        if (raw_y == 0xF000U) {
            return;
        }
        // The original code is mapped once; the per-cell block tiles derive from
        // the mapped code and decode directly (no second remap).
        const std::uint32_t mapped = map_gfx_code(gfx_type::sprites, code);
        const int sx = static_cast<int>(raw_x & 0x01FFU);
        const int sy = static_cast<int>(raw_y & 0x01FFU);
        const int blocks_x = static_cast<int>((attr >> 8U) & 0x0FU) + 1;
        const int blocks_y = static_cast<int>((attr >> 12U) & 0x0FU) + 1;
        const bool flip_x = (attr & 0x0020U) != 0U;
        const bool flip_y = (attr & 0x0040U) != 0U;
        const std::uint16_t pal_num = static_cast<std::uint16_t>(attr & 0x001FU);

        for (int by = 0; by < blocks_y; ++by) {
            for (int bx = 0; bx < blocks_x; ++bx) {
                const std::uint32_t block_tile =
                    sprite_block_tile(mapped, blocks_x, blocks_y, bx, by, flip_x, flip_y);
                bool draw_flip_x = flip_x;
                bool draw_flip_y = flip_y;
                int block_x = (sx + bx * 16) & 0x01FF;
                int block_y = (sy + by * 16) & 0x01FF;
                if (flip) {
                    block_x = static_cast<int>(full_screen_w) - 16 - block_x;
                    block_y = static_cast<int>(full_screen_h) - 16 - block_y;
                    draw_flip_x = !draw_flip_x;
                    draw_flip_y = !draw_flip_y;
                }
                for (int ty = 0; ty < 16; ++ty) {
                    const int vy = block_y + ty - static_cast<int>(visible_y_start);
                    if (vy < 0 || vy >= static_cast<int>(visible_height)) {
                        continue;
                    }
                    const int fy = draw_flip_y ? (15 - ty) : ty;
                    for (int tx = 0; tx < 16; ++tx) {
                        const int vx = block_x + tx - static_cast<int>(visible_x_start);
                        if (vx < 0 || vx >= static_cast<int>(visible_width)) {
                            continue;
                        }
                        const int fx = draw_flip_x ? (15 - tx) : tx;
                        const std::uint8_t pen = decode_packed(block_tile * 128U, 8U, fx, fy);
                        if (pen == transparent_pen) {
                            continue;
                        }
                        const std::size_t idx = static_cast<std::size_t>(vy) * visible_width +
                                                static_cast<std::size_t>(vx);
                        // A high-priority tile pixel directly below the sprite
                        // (the layer drawn just before sprites) occludes it.
                        if (layer_below != -1 &&
                            pixel_layer_[idx] == static_cast<std::uint8_t>(layer_below) &&
                            tile_pen_has_sprite_priority(pixel_group_[idx], pixel_pen_[idx])) {
                            continue;
                        }
                        pixels_[idx] = decode_color(read_palette(pal_num, pen));
                        pixel_layer_[idx] = 0U;
                        pixel_pen_[idx] = pen;
                        pixel_group_[idx] = 0U;
                    }
                }
            }
        }
    }

    void cps_a_b::draw_sprites(int layer_below, bool flip) noexcept {
        if (gfx_.empty()) {
            return;
        }
        if (!sprite_buffer_valid_) {
            latch_sprites();
        }
        const std::uint32_t count = sprite_entry_count();
        if (sprite_order_ == sprite_order::descending) {
            for (std::uint32_t remaining = count; remaining > 0U; --remaining) {
                draw_sprite_entry(remaining - 1U, layer_below, flip);
            }
        } else {
            for (std::uint32_t i = 0; i < count; ++i) {
                draw_sprite_entry(i, layer_below, flip);
            }
        }
    }

    void cps_a_b::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.u16(scroll1_x_);
        writer.u16(scroll1_y_);
        writer.u16(scroll2_x_);
        writer.u16(scroll2_y_);
        writer.u16(scroll3_x_);
        writer.u16(scroll3_y_);
        writer.boolean(display_enabled_);
        writer.u32(static_cast<std::uint32_t>(raster_compare_));
        writer.u32(scroll1_base_);
        writer.u32(scroll2_base_);
        writer.u32(scroll3_base_);
        writer.boolean(rowscroll_enabled_);
        writer.u32(rowscroll_base_);
        writer.u16(rowscroll_offset_);
        writer.u32(object_base_);
        writer.u8(static_cast<std::uint8_t>(sprite_order_));
        writer.boolean(sprite_buffer_valid_);
        writer.bytes(sprite_buffer_);
        for (const std::uint16_t reg : cps_b_regs_) {
            writer.u16(reg);
        }
        writer.u16(video_control_);
        writer.boolean(profile_.legacy);
        writer.u8(profile_.layer_control_offset);
        for (const std::uint8_t off : profile_.priority_offset) {
            writer.u8(off);
        }
        writer.u8(profile_.palette_control_offset);
        for (const std::uint16_t mask : profile_.layer_enable_mask) {
            writer.u16(mask);
        }
    }

    void cps_a_b::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        scroll1_x_ = reader.u16();
        scroll1_y_ = reader.u16();
        scroll2_x_ = reader.u16();
        scroll2_y_ = reader.u16();
        scroll3_x_ = reader.u16();
        scroll3_y_ = reader.u16();
        display_enabled_ = reader.boolean();
        raster_compare_ = static_cast<std::int32_t>(reader.u32());
        scroll1_base_ = reader.u32();
        scroll2_base_ = reader.u32();
        scroll3_base_ = reader.u32();
        rowscroll_enabled_ = reader.boolean();
        rowscroll_base_ = reader.u32();
        rowscroll_offset_ = reader.u16();
        object_base_ = reader.u32();
        sprite_order_ = static_cast<sprite_order>(reader.u8());
        sprite_buffer_valid_ = reader.boolean();
        reader.bytes(sprite_buffer_);
        for (std::uint16_t& reg : cps_b_regs_) {
            reg = reader.u16();
        }
        video_control_ = reader.u16();
        profile_.legacy = reader.boolean();
        profile_.layer_control_offset = reader.u8();
        for (std::uint8_t& off : profile_.priority_offset) {
            off = reader.u8();
        }
        profile_.palette_control_offset = reader.u8();
        for (std::uint16_t& mask : profile_.layer_enable_mask) {
            mask = reader.u16();
        }
    }

    instrumentation::ichip_introspection& cps_a_b::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> cps_a_b::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FRAME", frame_index_ & 0xFFFFFFFFULL, 32U, fmt::unsigned_integer};
        register_view_[1] = {"S1X", scroll1_x_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"S1Y", scroll1_y_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"S2X", scroll2_x_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"S2Y", scroll2_y_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"S3X", scroll3_x_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"S3Y", scroll3_y_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"LAYERCTL", layer_control(), 16U, fmt::flags};
        register_view_[8] = {"VIDEOCTL", video_control_, 16U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto cps_a_b_registration =
            register_factory("capcom.cps_a_b", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<cps_a_b>();
            });
    } // namespace

} // namespace mnemos::chips::video
