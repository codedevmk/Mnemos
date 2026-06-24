#include "taito_f2_video.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    chip_metadata taito_f2_video::metadata() const noexcept {
        return {
            .manufacturer = "Taito",
            .part_number = "tc0100scn_tc0200obj_tc0280grd_tc0430grw_tc0480scp",
            .family = "Taito F2",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    std::uint32_t taito_f2_video::decode_color(std::uint16_t entry) noexcept {
        const auto expand = [](std::uint32_t v) -> std::uint32_t { return (v << 3U) | (v >> 2U); };
        const std::uint32_t r = expand(entry & 0x001FU);
        const std::uint32_t g = expand((entry >> 5U) & 0x001FU);
        const std::uint32_t b = expand((entry >> 10U) & 0x001FU);
        return (r << 16U) | (g << 8U) | b;
    }

    std::uint32_t taito_f2_video::decode_color(palette_format format,
                                               std::uint16_t entry) noexcept {
        const auto expand4 = [](std::uint32_t v) -> std::uint32_t { return (v << 4U) | v; };
        const auto expand5 = [](std::uint32_t v) -> std::uint32_t {
            return (v << 3U) | (v >> 2U);
        };
        switch (format) {
        case palette_format::rgbx_444: {
            const std::uint32_t r = expand4((entry >> 12U) & 0x000FU);
            const std::uint32_t g = expand4((entry >> 8U) & 0x000FU);
            const std::uint32_t b = expand4((entry >> 4U) & 0x000FU);
            return (r << 16U) | (g << 8U) | b;
        }
        case palette_format::xrgb_555: {
            const std::uint32_t r = expand5((entry >> 10U) & 0x001FU);
            const std::uint32_t g = expand5((entry >> 5U) & 0x001FU);
            const std::uint32_t b = expand5(entry & 0x001FU);
            return (r << 16U) | (g << 8U) | b;
        }
        case palette_format::xbgr_555:
            return decode_color(entry);
        }
        return decode_color(entry);
    }

    std::uint16_t taito_f2_video::read16(std::span<const std::uint8_t> bytes,
                                         std::uint32_t offset) const noexcept {
        if (static_cast<std::size_t>(offset) + 1U >= bytes.size()) {
            return 0U;
        }
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                          bytes[offset + 1U]);
    }

    std::int32_t taito_f2_video::sign_extend_24(std::uint32_t value) noexcept {
        const std::int32_t raw = static_cast<std::int32_t>(value & 0x00FFFFFFU);
        return (raw & 0x00800000) != 0 ? raw - 0x01000000 : raw;
    }

    std::uint16_t taito_f2_video::read_palette(std::uint16_t bank,
                                               std::uint8_t pen) const noexcept {
        const std::uint32_t index = (static_cast<std::uint32_t>(bank) * 16U + pen) * 2U;
        return read16(palette_, index);
    }

    std::uint32_t taito_f2_video::palette_rgb(std::uint16_t bank,
                                              std::uint8_t pen) const noexcept {
        return decode_color(palette_format_, read_palette(bank, pen));
    }

    std::uint16_t taito_f2_video::palette_index(std::uint16_t bank,
                                                std::uint8_t pen) const noexcept {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(bank) * 16U + pen);
    }

    std::uint32_t taito_f2_video::palette_index_rgb(std::uint16_t index) const noexcept {
        return decode_color(palette_format_,
                            read16(palette_, static_cast<std::uint32_t>(index) * 2U));
    }

    std::uint8_t taito_f2_video::tile_pixel(std::span<const std::uint8_t> gfx,
                                            std::uint32_t code, int x, int y) const noexcept {
        if (x < 0 || x >= 8 || y < 0 || y >= 8) {
            return transparent_pen;
        }
        const std::uint32_t offset = code * 32U + static_cast<std::uint32_t>(y) * 4U +
                                     static_cast<std::uint32_t>(x / 2);
        if (static_cast<std::size_t>(offset) >= gfx.size()) {
            return transparent_pen;
        }
        const std::uint8_t packed = gfx[offset];
        return (x & 1) == 0 ? static_cast<std::uint8_t>(packed >> 4U)
                            : static_cast<std::uint8_t>(packed & 0x0FU);
    }

    std::uint8_t taito_f2_video::sprite_pixel(std::uint32_t code, int x, int y) const noexcept {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) {
            return transparent_pen;
        }
        const std::uint32_t offset = code * 128U + static_cast<std::uint32_t>(y) * 8U +
                                     static_cast<std::uint32_t>(x / 2);
        if (static_cast<std::size_t>(offset) >= sprite_gfx_.size()) {
            return transparent_pen;
        }
        const std::uint8_t packed = sprite_gfx_[offset];
        return (x & 1) == 0 ? static_cast<std::uint8_t>(packed & 0x0FU)
                            : static_cast<std::uint8_t>(packed >> 4U);
    }

    std::uint8_t taito_f2_video::tc0480scp_tile_pixel(std::uint32_t code, int x,
                                                       int y) const noexcept {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) {
            return transparent_pen;
        }
        const std::uint32_t offset = code * 128U + static_cast<std::uint32_t>(y) * 8U +
                                     static_cast<std::uint32_t>(x / 2);
        if (static_cast<std::size_t>(offset) >= tile_gfx_.size()) {
            return transparent_pen;
        }
        const std::uint8_t packed = tile_gfx_[offset];
        return (x & 1) == 0 ? static_cast<std::uint8_t>(packed & 0x0FU)
                            : static_cast<std::uint8_t>(packed >> 4U);
    }

    std::uint8_t taito_f2_video::tc0480scp_text_pixel(std::uint32_t code, int x,
                                                       int y) const noexcept {
        if (x < 0 || x >= 8 || y < 0 || y >= 8) {
            return transparent_pen;
        }
        static constexpr std::array<std::uint8_t, 8> x_offsets{12U, 8U, 4U, 0U,
                                                               28U, 24U, 20U, 16U};
        const std::uint32_t bit_offset =
            code * tc0480scp_text_char_bytes * 8U + static_cast<std::uint32_t>(y) * 32U +
            x_offsets[static_cast<std::size_t>(x)];
        const std::uint32_t byte_offset = tc0480scp_text_gfx_base + (bit_offset / 8U);
        if (static_cast<std::size_t>(byte_offset) >= tile_ram_.size()) {
            return transparent_pen;
        }
        return static_cast<std::uint8_t>((tile_ram_[byte_offset] >> (bit_offset & 7U)) &
                                         0x0FU);
    }

    std::uint16_t taito_f2_video::sprite_extension_word(std::uint32_t entry) const noexcept {
        std::uint32_t ext_entry = entry;
        if (ext_entry >= 0x8000U) {
            ext_entry -= 0x4000U;
        }
        return read16(sprite_extension_ram_, (ext_entry >> 4U) * 2U);
    }

    void taito_f2_video::reset(reset_kind /*kind*/) {
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        scroll0_x_ = scroll0_y_ = 0U;
        scroll1_x_ = scroll1_y_ = 0U;
        scroll2_x_ = scroll2_y_ = 0U;
        scroll0_secondary_x_ = scroll0_secondary_y_ = 0U;
        scroll1_secondary_x_ = scroll1_secondary_y_ = 0U;
        scroll2_secondary_x_ = scroll2_secondary_y_ = 0U;
        layer0_base_ = bg0_tilemap_base;
        layer1_base_ = bg1_tilemap_base;
        text_base_ = text_tilemap_base;
        text_gfx_base_ = text_gfx_base;
        layer_control_ = 0U;
        layer_control_secondary_ = 0U;
        display_enabled_ = true;
        palette_format_ = palette_format::xbgr_555;
        tilemap_variant_ = tilemap_variant::tc0100scn;
        sprite_buffer_.fill(0U);
        sprite_delay_buffer_.fill(0U);
        sprite_buffer_valid_ = false;
        for (std::size_t i = 0U; i < sprite_banks_.size(); ++i) {
            sprite_banks_[i] = static_cast<std::uint16_t>(0x400U * i);
        }
        sprite_mode_ = sprite_mode::standard;
        sprite_active_area_source_ = sprite_active_area_source::mode_default;
        sprite_buffer_policy_ = sprite_buffer_policy::immediate;
        hide_pixels_ = 0;
        flip_hide_pixels_ = 0;
        sprite_active_area_ = 0U;
        sprites_disabled_ = false;
        sprites_flip_screen_ = false;
        sprite_master_scroll_x_ = 0;
        sprite_master_scroll_y_ = 0;
        priority_regs_.fill(0U);
        roz_control_regs_.fill(0U);
        tc0480scp_control_regs_.fill(0U);
        for (std::size_t i = 0x08U; i <= 0x0BU; ++i) {
            tc0480scp_control_regs_[i] = 0x007FU;
        }
        tc0480scp_scroll_x_.fill(0);
        tc0480scp_scroll_y_.fill(0);
        tc0480scp_priority_reg_ = 0U;
        tc0480scp_palette_bank_base_ = 0U;
        tc0480scp_priority_model_ = tc0480scp_priority_model::metalb;
        tc0480scp_bg_x_offset_ = 0;
        tc0480scp_bg_y_offset_ = 0;
        tc0480scp_text_x_offset_ = 0;
        tc0480scp_text_y_offset_ = 0;
        tc0480scp_flip_x_offset_ = 0;
        tc0480scp_flip_y_offset_ = 0;
        roz_variant_ = roz_variant::tc0280grd;
        roz_x_offset_ = 0;
        roz_y_offset_ = 0;
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        std::fill(pixel_palette_index_.begin(), pixel_palette_index_.end(), std::uint16_t{0U});
        std::fill(pixel_priority_.begin(), pixel_priority_.end(), std::uint8_t{0U});
        std::fill(pixel_sprite_priority_.begin(), pixel_sprite_priority_.end(),
                  std::uint8_t{0U});
    }

    void taito_f2_video::tick(std::uint64_t cycles) {
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

    frame_buffer_view taito_f2_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    taito_f2_video::frame_priority_state taito_f2_video::priority_state() const noexcept {
        const int bottom = (layer_control_ & control_priority_swap) != 0U ? 1 : 0;
        const int top = bottom ^ 1;

        frame_priority_state state{};
        const bool configured =
            std::any_of(priority_regs_.begin(), priority_regs_.end(),
                        [](std::uint16_t value) { return value != 0U; });
        if (!configured) {
            state.layer[static_cast<std::size_t>(bottom)] = 0U;
            state.layer[static_cast<std::size_t>(top)] = 2U;
            state.roz = 1U;
            state.text = 15U;
            state.sprite = {1U, 1U, 3U, 3U};
            return state;
        }

        state.layer[static_cast<std::size_t>(bottom)] =
            static_cast<std::uint8_t>(priority_regs_[5] & 0x000FU);
        state.layer[static_cast<std::size_t>(top)] =
            static_cast<std::uint8_t>((priority_regs_[5] >> 4U) & 0x000FU);
        const std::uint8_t roz_selector =
            static_cast<std::uint8_t>((priority_regs_[1] & 0x00C0U) >> 6U);
        state.roz = static_cast<std::uint8_t>(
            (priority_regs_[8U + roz_selector / 2U] >> (4U * (roz_selector & 1U))) &
            0x000FU);
        state.text = static_cast<std::uint8_t>((priority_regs_[4] >> 4U) & 0x000FU);
        state.sprite[0] = static_cast<std::uint8_t>(priority_regs_[6] & 0x000FU);
        state.sprite[1] = static_cast<std::uint8_t>((priority_regs_[6] >> 4U) & 0x000FU);
        state.sprite[2] = static_cast<std::uint8_t>(priority_regs_[7] & 0x000FU);
        state.sprite[3] = static_cast<std::uint8_t>((priority_regs_[7] >> 4U) & 0x000FU);
        state.sprite_blend_mode = static_cast<std::uint8_t>(priority_regs_[0] & 0x00C0U);
        return state;
    }

    void taito_f2_video::render_frame() noexcept {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        std::fill(pixel_palette_index_.begin(), pixel_palette_index_.end(), std::uint16_t{0U});
        std::fill(pixel_priority_.begin(), pixel_priority_.end(), std::uint8_t{0U});
        std::fill(pixel_sprite_priority_.begin(), pixel_sprite_priority_.end(),
                  std::uint8_t{0U});
        if (!display_enabled_) {
            return;
        }

        if (tilemap_variant_ == tilemap_variant::tc0480scp) {
            draw_tc0480scp_layers();
            draw_sprites(priority_state());
            return;
        }
        if (tilemap_variant_ == tilemap_variant::dual_tc0100scn) {
            draw_dual_tc0100scn_layers();
            draw_sprites(priority_state());
            return;
        }

        const int bottom = (layer_control_ & control_priority_swap) != 0U ? 1 : 0;
        const int top = bottom ^ 1;
        const frame_priority_state priorities = priority_state();

        draw_background_layer(bottom, true, priorities.layer[static_cast<std::size_t>(bottom)]);
        draw_background_layer(top, false, priorities.layer[static_cast<std::size_t>(top)]);
        draw_roz_layer(priorities.roz);
        draw_text_layer(priorities.text);
        draw_sprites(priorities);
    }

    bool taito_f2_video::layer_enabled(std::uint16_t control, int layer) noexcept {
        const std::uint16_t bit = static_cast<std::uint16_t>(1U << layer);
        return (control & bit) == 0U;
    }

    void taito_f2_video::draw_background_layer(int layer, bool opaque,
                                               std::uint8_t priority) noexcept {
        draw_background_layer_from(tile_ram_, tile_gfx_, layer, opaque, priority,
                                   layer_control_, scroll0_x_, scroll0_y_, scroll1_x_,
                                   scroll1_y_, layer0_base_, layer1_base_);
    }

    void taito_f2_video::draw_background_layer_from(
        std::span<const std::uint8_t> ram, std::span<const std::uint8_t> gfx, int layer,
        bool opaque, std::uint8_t priority, std::uint16_t control,
        std::uint16_t scroll0_x, std::uint16_t scroll0_y, std::uint16_t scroll1_x,
        std::uint16_t scroll1_y, std::uint32_t layer0_base,
        std::uint32_t layer1_base) noexcept {
        if (!layer_enabled(control, layer) || ram.empty() || gfx.empty()) {
            return;
        }
        const std::uint32_t base = layer == 0 ? layer0_base : layer1_base;
        const int scroll_x = static_cast<std::int16_t>(layer == 0 ? scroll0_x : scroll1_x);
        const int scroll_y = static_cast<std::int16_t>(layer == 0 ? scroll0_y : scroll1_y);
        const std::uint32_t rowscroll_base =
            layer == 0 ? bg0_rowscroll_base : bg1_rowscroll_base;

        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const std::uint32_t row_scroll_index =
                (static_cast<std::uint32_t>(static_cast<int>(py) + scroll_y) & 0x01FFU) * 2U;
            const int row_scroll =
                    static_cast<std::int16_t>(read16(ram, rowscroll_base + row_scroll_index));
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const std::uint32_t world_x = static_cast<std::uint32_t>(
                                                  static_cast<int>(px) + scroll_x + row_scroll) &
                                              0x01FFU;
                std::uint32_t world_y = static_cast<std::uint32_t>(
                                            static_cast<int>(py) + scroll_y) &
                                        0x01FFU;
                if (layer == 1) {
                    const std::uint32_t col_scroll_offset =
                        bg1_colscroll_base + ((world_x & 0x03FFU) / 8U) * 2U;
                    const int col_scroll =
                        static_cast<std::int16_t>(read16(ram, col_scroll_offset));
                    world_y = static_cast<std::uint32_t>(
                                  static_cast<int>(world_y) - col_scroll) &
                              0x01FFU;
                }
                const std::uint32_t row = world_y / tile_size;
                const int ty = static_cast<int>(world_y & 7U);
                const std::uint32_t col = world_x / tile_size;
                const int tx = static_cast<int>(world_x & 7U);
                const std::uint32_t entry =
                    base + ((row * map_tiles + col) * static_cast<std::uint32_t>(tile_entry_bytes));
                if (static_cast<std::size_t>(entry) + 3U >= ram.size()) {
                    continue;
                }
                const std::uint16_t attr = read16(ram, entry);
                const std::uint16_t code = read16(ram, entry + 2U);
                const bool flip_y = (attr & 0x4000U) != 0U;
                const bool flip_x = (attr & 0x8000U) != 0U;
                const int fx = flip_x ? (7 - tx) : tx;
                const int fy = flip_y ? (7 - ty) : ty;
                const std::uint8_t pen = tile_pixel(gfx, code, fx, fy);
                if (!opaque && pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t bank = static_cast<std::uint16_t>(attr & 0x00FFU);
                const std::size_t pixel_index =
                    static_cast<std::size_t>(py) * visible_width + px;
                const std::uint16_t color_index = palette_index(bank, pen);
                pixels_[pixel_index] = palette_index_rgb(color_index);
                pixel_palette_index_[pixel_index] = color_index;
                pixel_priority_[pixel_index] = priority;
            }
        }
    }

    void taito_f2_video::draw_roz_layer(std::uint8_t priority) noexcept {
        if (roz_ram_.size() < roz_ram_bytes || roz_gfx_.empty()) {
            return;
        }

        const auto signed16 = [](std::uint16_t value) noexcept -> int {
            const int raw = static_cast<int>(value);
            return (raw & 0x8000) != 0 ? raw - 0x10000 : raw;
        };
        const auto floor_shift_16 = [](std::int64_t value) noexcept -> int {
            if (value >= 0) {
                return static_cast<int>(value >> 16U);
            }
            return -static_cast<int>(((-value) + 0xFFFF) >> 16U);
        };

        const std::int32_t start_x = sign_extend_24(
            ((static_cast<std::uint32_t>(roz_control_regs_[0]) & 0x00FFU) << 16U) |
            roz_control_regs_[1]);
        const std::int32_t start_y = sign_extend_24(
            ((static_cast<std::uint32_t>(roz_control_regs_[4]) & 0x00FFU) << 16U) |
            roz_control_regs_[5]);
        const int x_multiplier = roz_x_multiplier(roz_variant_);
        const int inc_xx = signed16(roz_control_regs_[2]) * x_multiplier;
        const int inc_yx = signed16(roz_control_regs_[3]);
        const int inc_xy = signed16(roz_control_regs_[6]) * x_multiplier;
        const int inc_yy = signed16(roz_control_regs_[7]);
        const std::int64_t base_x =
            static_cast<std::int64_t>(start_x - roz_x_offset_ * inc_xx -
                                      roz_y_offset_ * inc_yx) *
            16;
        const std::int64_t base_y =
            static_cast<std::int64_t>(start_y - roz_x_offset_ * inc_xy -
                                      roz_y_offset_ * inc_yy) *
            16;
        const std::int64_t step_xx = static_cast<std::int64_t>(inc_xx) * 16;
        const std::int64_t step_xy = static_cast<std::int64_t>(inc_xy) * 16;
        const std::int64_t step_yx = static_cast<std::int64_t>(inc_yx) * 16;
        const std::int64_t step_yy = static_cast<std::int64_t>(inc_yy) * 16;
        const std::uint16_t base_bank =
            static_cast<std::uint16_t>((priority_regs_[1] & 0x003FU) << 2U);

        for (std::uint32_t py = 0; py < visible_height; ++py) {
            std::int64_t source_x = base_x + static_cast<std::int64_t>(py) * step_yx;
            std::int64_t source_y = base_y + static_cast<std::int64_t>(py) * step_yy;
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(floor_shift_16(source_x)) & 0x01FFU;
                const std::uint32_t world_y =
                    static_cast<std::uint32_t>(floor_shift_16(source_y)) & 0x01FFU;
                const std::uint32_t col = world_x / tile_size;
                const std::uint32_t row = world_y / tile_size;
                const int tx = static_cast<int>(world_x & 7U);
                const int ty = static_cast<int>(world_y & 7U);
                const std::uint32_t tile_index = row * map_tiles + col;
                const std::uint16_t attr = read16(roz_ram_, tile_index * 2U);
                const std::uint8_t pen = tile_pixel(roz_gfx_, attr & 0x3FFFU, tx, ty);
                if (pen != transparent_pen) {
                    const std::size_t pixel_index =
                        static_cast<std::size_t>(py) * visible_width + px;
                    if (priority >= pixel_priority_[pixel_index]) {
                        const std::uint16_t bank = static_cast<std::uint16_t>(
                            base_bank + ((attr >> 14U) & 0x0003U));
                        const std::uint16_t color_index = palette_index(bank, pen);
                        pixels_[pixel_index] = palette_index_rgb(color_index);
                        pixel_palette_index_[pixel_index] = color_index;
                        pixel_priority_[pixel_index] = priority;
                    }
                }
                source_x += step_xx;
                source_y += step_xy;
            }
        }
    }

    std::uint8_t taito_f2_video::text_pixel(std::span<const std::uint8_t> ram,
                                            std::uint32_t gfx_base, std::uint32_t code,
                                            int x, int y) const noexcept {
        if (x < 0 || x >= 8 || y < 0 || y >= 8) {
            return transparent_pen;
        }
        const std::uint32_t offset =
            gfx_base + code * text_char_bytes + static_cast<std::uint32_t>(y) * 2U;
        if (static_cast<std::size_t>(offset) + 1U >= ram.size()) {
            return transparent_pen;
        }
        const std::uint8_t mask = static_cast<std::uint8_t>(0x80U >> x);
        const std::uint8_t lo = (ram[offset] & mask) != 0U ? 1U : 0U;
        const std::uint8_t hi = (ram[offset + 1U] & mask) != 0U ? 2U : 0U;
        return static_cast<std::uint8_t>(lo | hi);
    }

    void taito_f2_video::draw_text_layer(std::uint8_t priority) noexcept {
        draw_text_layer_from(tile_ram_, priority, layer_control_, scroll2_x_, scroll2_y_,
                             text_base_, text_gfx_base_);
    }

    void taito_f2_video::draw_text_layer_from(std::span<const std::uint8_t> ram,
                                              std::uint8_t priority,
                                              std::uint16_t control,
                                              std::uint16_t scroll_x_word,
                                              std::uint16_t scroll_y_word,
                                              std::uint32_t tilemap_base,
                                              std::uint32_t gfx_base) noexcept {
        if (!layer_enabled(control, 2) || ram.empty()) {
            return;
        }
        const int scroll_x = static_cast<std::int16_t>(scroll_x_word);
        const int scroll_y = static_cast<std::int16_t>(scroll_y_word);
        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const std::uint32_t world_y = static_cast<std::uint32_t>(
                                              static_cast<int>(py) + scroll_y) &
                                          0x01FFU;
            const std::uint32_t row = world_y / tile_size;
            const int ty = static_cast<int>(world_y & 7U);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const std::uint32_t world_x = static_cast<std::uint32_t>(
                                                  static_cast<int>(px) + scroll_x) &
                                              0x01FFU;
                const std::uint32_t col = world_x / tile_size;
                const int tx = static_cast<int>(world_x & 7U);
                const std::uint32_t entry = tilemap_base + (row * map_tiles + col) * 2U;
                const std::uint16_t attr = read16(ram, entry);
                const std::uint32_t code = attr & 0x00FFU;
                const bool flip_y = (attr & 0x4000U) != 0U;
                const bool flip_x = (attr & 0x8000U) != 0U;
                const int fx = flip_x ? (7 - tx) : tx;
                const int fy = flip_y ? (7 - ty) : ty;
                const std::uint8_t pen = text_pixel(ram, gfx_base, code, fx, fy);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t bank = static_cast<std::uint16_t>((attr >> 8U) & 0x003FU);
                const std::size_t pixel_index =
                    static_cast<std::size_t>(py) * visible_width + px;
                const std::uint16_t color_index = palette_index(bank, pen);
                pixels_[pixel_index] = palette_index_rgb(color_index);
                pixel_palette_index_[pixel_index] = color_index;
                pixel_priority_[pixel_index] = priority;
            }
        }
    }

    void taito_f2_video::draw_dual_tc0100scn_layers() noexcept {
        struct tile_stream final {
            std::array<int, 3> layers{};
            std::array<std::uint8_t, 3> priority{};
        };

        const auto make_stream = [](std::uint16_t control, std::uint16_t layer_priority,
                                    std::uint16_t text_priority) noexcept {
            const int bottom = (control & control_priority_swap) != 0U ? 1 : 0;
            tile_stream stream{};
            stream.layers = {bottom, bottom ^ 1, 2};
            stream.priority = {
                static_cast<std::uint8_t>(layer_priority & 0x000FU),
                static_cast<std::uint8_t>((layer_priority >> 4U) & 0x000FU),
                static_cast<std::uint8_t>((text_priority >> 4U) & 0x000FU)};
            return stream;
        };

        const std::array<tile_stream, 2> streams{
            make_stream(layer_control_, priority_regs_[5], priority_regs_[4]),
            make_stream(layer_control_secondary_, priority_regs_[9], priority_regs_[8])};
        std::array<std::size_t, 2> drawn{};
        bool wrote_layer = false;

        const auto draw_one = [&](std::size_t chip, std::size_t index) {
            const int layer = streams[chip].layers[index];
            const std::uint8_t priority = streams[chip].priority[index];
            const bool opaque = !wrote_layer;
            if (chip == 0U) {
                if (layer == 2) {
                    draw_text_layer_from(tile_ram_, priority, layer_control_, scroll2_x_,
                                         scroll2_y_, text_base_, text_gfx_base_);
                } else {
                    draw_background_layer_from(tile_ram_, tile_gfx_, layer, opaque, priority,
                                               layer_control_, scroll0_x_, scroll0_y_,
                                               scroll1_x_, scroll1_y_, layer0_base_,
                                               layer1_base_);
                }
            } else {
                if (layer == 2) {
                    draw_text_layer_from(tile_ram_secondary_, priority,
                                         layer_control_secondary_, scroll2_secondary_x_,
                                         scroll2_secondary_y_, text_base_, text_gfx_base_);
                } else {
                    draw_background_layer_from(tile_ram_secondary_, tile_gfx_secondary_, layer,
                                               opaque, priority, layer_control_secondary_,
                                               scroll0_secondary_x_, scroll0_secondary_y_,
                                               scroll1_secondary_x_, scroll1_secondary_y_,
                                               layer0_base_, layer1_base_);
                }
            }
            wrote_layer = true;
        };

        while (drawn[0] < 3U && drawn[1] < 3U) {
            const std::uint8_t left = streams[0].priority[drawn[0]];
            const std::uint8_t right = streams[1].priority[drawn[1]];
            const std::size_t chip = left < right ? 0U : 1U;
            draw_one(chip, drawn[chip]);
            ++drawn[chip];
        }
        while (drawn[0] < 3U) {
            draw_one(0U, drawn[0]);
            ++drawn[0];
        }
        while (drawn[1] < 3U) {
            draw_one(1U, drawn[1]);
            ++drawn[1];
        }
    }

    bool taito_f2_video::tc0480scp_double_width() const noexcept {
        return (tc0480scp_priority_reg_ & tc0480scp_control_double_width) != 0U;
    }

    std::uint32_t taito_f2_video::tc0480scp_bg_layer_base(std::uint8_t layer) const noexcept {
        const std::uint32_t per_layer = tc0480scp_double_width() ? 0x2000U : 0x1000U;
        return static_cast<std::uint32_t>(layer & 0x03U) * per_layer;
    }

    std::uint32_t taito_f2_video::tc0480scp_rowscroll_base(std::uint8_t layer) const noexcept {
        const std::uint32_t base = tc0480scp_double_width() ? 0x8000U : 0x4000U;
        return base + static_cast<std::uint32_t>(layer & 0x03U) * 0x0400U;
    }

    std::uint32_t
    taito_f2_video::tc0480scp_rowscroll_low_base(std::uint8_t layer) const noexcept {
        const std::uint32_t base = tc0480scp_double_width() ? 0x9000U : 0x5000U;
        return base + static_cast<std::uint32_t>(layer & 0x03U) * 0x0400U;
    }

    std::uint32_t taito_f2_video::tc0480scp_rowzoom_base(std::uint8_t layer) const noexcept {
        const std::uint32_t base = tc0480scp_double_width() ? 0xA000U : 0x6000U;
        const std::uint32_t rowzoom_layer = layer > 2U ? 1U : 0U;
        return base + rowzoom_layer * 0x0400U;
    }

    std::uint32_t taito_f2_video::tc0480scp_colscroll_base(std::uint8_t layer) const noexcept {
        const std::uint32_t base = tc0480scp_double_width() ? 0xA800U : 0x6800U;
        const std::uint32_t colscroll_layer = layer > 2U ? 1U : 0U;
        return base + colscroll_layer * 0x0400U;
    }

    std::int32_t taito_f2_video::fixed_floor_shift_16(std::int64_t value) noexcept {
        if (value >= 0) {
            return static_cast<std::int32_t>(value >> 16U);
        }
        return -static_cast<std::int32_t>(((-value) + 0xFFFF) >> 16U);
    }

    std::uint32_t taito_f2_video::tc0480scp_zoom_x_step(std::uint16_t word) noexcept {
        return 0x10000U - (static_cast<std::uint32_t>(word & 0xFF00U));
    }

    std::uint32_t taito_f2_video::tc0480scp_zoom_y_step(std::uint16_t word) noexcept {
        const int low = static_cast<int>(word & 0x00FFU);
        return static_cast<std::uint32_t>(0x10000 - ((low - 0x7F) * 512));
    }

    std::uint16_t taito_f2_video::tc0480scp_bg_priority_order() const noexcept {
        static constexpr std::array<std::uint16_t, 8> lookup{
            0x0123U, 0x1230U, 0x2301U, 0x3012U,
            0x3210U, 0x2103U, 0x1032U, 0x0321U};
        return lookup[(tc0480scp_priority_reg_ & 0x001CU) >> 2U];
    }

    void taito_f2_video::draw_tc0480scp_layers() noexcept {
        const std::uint16_t order = tc0480scp_bg_priority_order();
        const std::array<std::uint8_t, 4> layer_order{
            static_cast<std::uint8_t>((order >> 12U) & 0x000FU),
            static_cast<std::uint8_t>((order >> 8U) & 0x000FU),
            static_cast<std::uint8_t>((order >> 4U) & 0x000FU),
            static_cast<std::uint8_t>(order & 0x000FU)};
        std::array<std::uint8_t, 4> layer_priority{};
        std::uint8_t text_priority{};
        switch (tc0480scp_priority_model_) {
        case tc0480scp_priority_model::deadconx_footchmp:
            layer_priority[0] = static_cast<std::uint8_t>((priority_regs_[4] >> 4U) & 0x000FU);
            layer_priority[1] = static_cast<std::uint8_t>(priority_regs_[5] & 0x000FU);
            layer_priority[2] = static_cast<std::uint8_t>((priority_regs_[5] >> 4U) & 0x000FU);
            layer_priority[3] = static_cast<std::uint8_t>(priority_regs_[4] & 0x000FU);
            text_priority = static_cast<std::uint8_t>((priority_regs_[9] >> 4U) & 0x000FU);
            break;
        case tc0480scp_priority_model::metalb:
            layer_priority[0] = static_cast<std::uint8_t>(priority_regs_[4] & 0x000FU);
            layer_priority[1] = static_cast<std::uint8_t>((priority_regs_[4] >> 4U) & 0x000FU);
            layer_priority[2] = static_cast<std::uint8_t>(priority_regs_[5] & 0x000FU);
            layer_priority[3] = static_cast<std::uint8_t>((priority_regs_[5] >> 4U) & 0x000FU);
            text_priority = static_cast<std::uint8_t>(priority_regs_[9] & 0x000FU);
            break;
        }

        for (const std::uint8_t layer : layer_order) {
            if (layer < layer_priority.size()) {
                draw_tc0480scp_bg_layer(layer, layer_priority[layer]);
            }
        }
        draw_tc0480scp_text_layer(text_priority);
    }

    void taito_f2_video::draw_tc0480scp_bg_layer(std::uint8_t layer,
                                                 std::uint8_t priority) noexcept {
        if (tile_ram_.empty() || tile_gfx_.empty()) {
            return;
        }
        const bool flip_screen =
            (tc0480scp_priority_reg_ & tc0480scp_control_flip_screen) != 0U;
        const std::uint32_t columns = tc0480scp_double_width() ? 64U : 32U;
        const std::uint32_t x_mask = columns * tc0480scp_bg_tile_size - 1U;
        const std::uint32_t y_mask = 32U * tc0480scp_bg_tile_size - 1U;
        const std::uint32_t base = tc0480scp_bg_layer_base(layer);
        const int scroll_x = tc0480scp_scroll_x_[layer] + tc0480scp_bg_x_offset_ +
                             (flip_screen ? tc0480scp_flip_x_offset_ : 0);
        const int scroll_y = tc0480scp_scroll_y_[layer] + tc0480scp_bg_y_offset_ +
                             (flip_screen ? tc0480scp_flip_y_offset_ : 0);
        const std::uint16_t zoom_word = tc0480scp_control_regs_[0x08U + layer];
        const std::uint32_t zoom_x_step = tc0480scp_zoom_x_step(zoom_word);
        const std::uint32_t zoom_y_step = tc0480scp_zoom_y_step(zoom_word);
        const bool row_zoom_enabled =
            layer == 2U ? (tc0480scp_priority_reg_ & tc0480scp_control_row_zoom_bg2) != 0U
                        : layer == 3U
                              ? (tc0480scp_priority_reg_ &
                                 tc0480scp_control_row_zoom_bg3) != 0U
                              : false;

        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const int screen_y =
                flip_screen ? static_cast<int>(visible_height - 1U - py) : static_cast<int>(py);
            std::int64_t source_y_fixed =
                static_cast<std::int64_t>(screen_y + scroll_y) * zoom_y_step;
            std::uint32_t world_y =
                static_cast<std::uint32_t>(fixed_floor_shift_16(source_y_fixed)) & y_mask;
            if (layer >= 2U) {
                const std::uint32_t column_row =
                    flip_screen ? (tc0480scp_rowscroll_rows - 1U -
                                   (static_cast<std::uint32_t>(screen_y) & 0x01FFU))
                                : (static_cast<std::uint32_t>(screen_y) & 0x01FFU);
                const int col_scroll = static_cast<std::int16_t>(
                    read16(tile_ram_, tc0480scp_colscroll_base(layer) + column_row * 2U));
                world_y = static_cast<std::uint32_t>(static_cast<int>(world_y) + col_scroll) &
                          y_mask;
            }
            const std::uint32_t row_index =
                flip_screen ? (tc0480scp_rowscroll_rows - 1U - (world_y & 0x01FFU))
                            : (world_y & 0x01FFU);
            const std::int64_t row_scroll =
                static_cast<std::int64_t>(static_cast<std::int16_t>(read16(
                    tile_ram_, tc0480scp_rowscroll_base(layer) + row_index * 2U)));
            const std::uint32_t row_scroll_low =
                read16(tile_ram_, tc0480scp_rowscroll_low_base(layer) + row_index * 2U) & 0x00FFU;
            const std::int64_t row_scroll_fixed =
                (row_scroll << 16U) + (static_cast<std::int64_t>(row_scroll_low) << 8U);
            const std::uint16_t row_zoom =
                row_zoom_enabled
                    ? read16(tile_ram_, tc0480scp_rowzoom_base(layer) + row_index * 2U)
                    : 0U;
            const std::uint32_t row_x_step =
                row_zoom == 0U
                    ? zoom_x_step
                    : zoom_x_step -
                          ((static_cast<std::uint32_t>(row_zoom & 0x00FFU) << 8U) & 0xFFFFU);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const int screen_x =
                    flip_screen ? static_cast<int>(visible_width - 1U - px)
                                : static_cast<int>(px);
                const std::int64_t source_x_fixed =
                    static_cast<std::int64_t>(screen_x) * row_x_step +
                    (static_cast<std::int64_t>(scroll_x) << 16U) - row_scroll_fixed;
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(fixed_floor_shift_16(source_x_fixed)) & x_mask;
                const std::uint32_t row = world_y / tc0480scp_bg_tile_size;
                const int ty = static_cast<int>(world_y & 15U);
                const std::uint32_t col = world_x / tc0480scp_bg_tile_size;
                const int tx = static_cast<int>(world_x & 15U);
                const std::uint32_t entry =
                    base + ((row * columns + col) *
                            static_cast<std::uint32_t>(tc0480scp_bg_entry_bytes));
                if (static_cast<std::size_t>(entry) + 3U >= tile_ram_.size()) {
                    continue;
                }
                const std::uint16_t attr = read16(tile_ram_, entry);
                const std::uint16_t code =
                    static_cast<std::uint16_t>(read16(tile_ram_, entry + 2U) & 0x7FFFU);
                const bool flip_y = (attr & 0x4000U) != 0U;
                const bool flip_x = (attr & 0x8000U) != 0U;
                const int fx = flip_x ^ flip_screen ? (15 - tx) : tx;
                const int fy = flip_y ^ flip_screen ? (15 - ty) : ty;
                const std::uint8_t pen = tc0480scp_tile_pixel(code, fx, fy);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::size_t pixel_index =
                    static_cast<std::size_t>(py) * visible_width + px;
                if (priority < pixel_priority_[pixel_index]) {
                    continue;
                }
                const std::uint16_t bank = static_cast<std::uint16_t>(
                    tc0480scp_palette_bank_base_ + (attr & 0x00FFU));
                const std::uint16_t color_index = palette_index(bank, pen);
                pixels_[pixel_index] = palette_index_rgb(color_index);
                pixel_palette_index_[pixel_index] = color_index;
                pixel_priority_[pixel_index] = priority;
            }
        }
    }

    void taito_f2_video::draw_tc0480scp_text_layer(std::uint8_t priority) noexcept {
        if (tile_ram_.empty()) {
            return;
        }
        const bool flip_screen =
            (tc0480scp_priority_reg_ & tc0480scp_control_flip_screen) != 0U;
        const int scroll_x =
            -static_cast<int>(static_cast<std::int16_t>(tc0480scp_control_regs_[0x0CU])) +
            tc0480scp_text_x_offset_ + (flip_screen ? tc0480scp_flip_x_offset_ : 0);
        const int scroll_y =
            -static_cast<int>(static_cast<std::int16_t>(tc0480scp_control_regs_[0x0DU])) +
            tc0480scp_text_y_offset_ + (flip_screen ? tc0480scp_flip_y_offset_ : 0);
        for (std::uint32_t py = 0; py < visible_height; ++py) {
            const int screen_y =
                flip_screen ? static_cast<int>(visible_height - 1U - py) : static_cast<int>(py);
            const std::uint32_t world_y =
                static_cast<std::uint32_t>(screen_y + scroll_y) & 0x01FFU;
            const std::uint32_t row = world_y / tile_size;
            const int ty = static_cast<int>(world_y & 7U);
            for (std::uint32_t px = 0; px < visible_width; ++px) {
                const int screen_x =
                    flip_screen ? static_cast<int>(visible_width - 1U - px)
                                : static_cast<int>(px);
                const std::uint32_t world_x =
                    static_cast<std::uint32_t>(screen_x + scroll_x) & 0x01FFU;
                const std::uint32_t col = world_x / tile_size;
                const int tx = static_cast<int>(world_x & 7U);
                const std::uint32_t entry =
                    tc0480scp_text_tilemap_base + (row * map_tiles + col) * 2U;
                const std::uint16_t attr = read16(tile_ram_, entry);
                const std::uint32_t code = attr & 0x00FFU;
                const bool flip_y = (attr & 0x4000U) != 0U;
                const bool flip_x = (attr & 0x8000U) != 0U;
                const int fx = flip_x ^ flip_screen ? (7 - tx) : tx;
                const int fy = flip_y ^ flip_screen ? (7 - ty) : ty;
                const std::uint8_t pen = tc0480scp_text_pixel(code, fx, fy);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::size_t pixel_index =
                    static_cast<std::size_t>(py) * visible_width + px;
                if (priority < pixel_priority_[pixel_index]) {
                    continue;
                }
                const std::uint16_t bank = static_cast<std::uint16_t>(
                    tc0480scp_palette_bank_base_ + ((attr >> 8U) & 0x003FU));
                const std::uint16_t color_index = palette_index(bank, pen);
                pixels_[pixel_index] = palette_index_rgb(color_index);
                pixel_palette_index_[pixel_index] = color_index;
                pixel_priority_[pixel_index] = priority;
            }
        }
    }

    void taito_f2_video::copy_current_sprite_ram(
        std::array<std::uint8_t, sprite_buffer_bytes>& target) const noexcept {
        target.fill(0U);
        if (sprite_ram_.empty()) {
            return;
        }
        const std::size_t n = std::min(target.size(), sprite_ram_.size());
        std::copy_n(sprite_ram_.begin(), n, target.begin());
    }

    void taito_f2_video::overlay_current_sprite_word(std::uint32_t word_index) noexcept {
        const std::uint32_t byte_index = word_index * 2U;
        if (static_cast<std::size_t>(byte_index) + 1U >= sprite_buffer_.size() ||
            static_cast<std::size_t>(byte_index) + 1U >= sprite_ram_.size()) {
            return;
        }
        sprite_buffer_[byte_index] = sprite_ram_[byte_index];
        sprite_buffer_[byte_index + 1U] = sprite_ram_[byte_index + 1U];
    }

    void taito_f2_video::overlay_current_sprite_words(
        std::span<const std::uint8_t> word_offsets) noexcept {
        constexpr std::uint32_t words_per_entry =
            static_cast<std::uint32_t>(sprite_entry_bytes / 2U);
        constexpr std::uint32_t total_words =
            static_cast<std::uint32_t>(sprite_buffer_bytes / 2U);
        for (std::uint32_t base = 0U; base < total_words; base += words_per_entry) {
            for (const std::uint8_t word_offset : word_offsets) {
                overlay_current_sprite_word(base + word_offset);
            }
        }
    }

    void taito_f2_video::latch_sprites() noexcept {
        static constexpr std::array<std::uint8_t, 2U> partial_words{0U, 4U};
        static constexpr std::array<std::uint8_t, 3U> thundfox_words{0U, 1U, 4U};
        static constexpr std::array<std::uint8_t, 6U> qzchikyu_words{0U, 1U, 4U,
                                                                    5U, 6U, 7U};

        switch (sprite_buffer_policy_) {
        case sprite_buffer_policy::partial_delayed:
            sprite_buffer_ = sprite_delay_buffer_;
            overlay_current_sprite_words(partial_words);
            copy_current_sprite_ram(sprite_delay_buffer_);
            break;
        case sprite_buffer_policy::partial_delayed_thundfox:
            sprite_buffer_ = sprite_delay_buffer_;
            overlay_current_sprite_words(thundfox_words);
            copy_current_sprite_ram(sprite_delay_buffer_);
            break;
        case sprite_buffer_policy::partial_delayed_qzchikyu:
            sprite_buffer_ = sprite_delay_buffer_;
            overlay_current_sprite_words(qzchikyu_words);
            copy_current_sprite_ram(sprite_delay_buffer_);
            break;
        case sprite_buffer_policy::full_delayed:
        case sprite_buffer_policy::immediate:
            copy_current_sprite_ram(sprite_buffer_);
            copy_current_sprite_ram(sprite_delay_buffer_);
            break;
        }
        sprite_buffer_valid_ = true;
        update_sprite_control_state();
    }

    void taito_f2_video::write_sprite_bank_register(std::uint32_t offset,
                                                    std::uint16_t value) noexcept {
        offset &= 0x07U;
        if (offset < 2U) {
            return;
        }
        if (offset < 4U) {
            const std::uint32_t pair = (offset & 1U) << 1U;
            const std::uint16_t base = static_cast<std::uint16_t>(value << 11U);
            sprite_banks_[pair] = base;
            sprite_banks_[pair + 1U] = static_cast<std::uint16_t>(base + 0x400U);
            return;
        }
        sprite_banks_[offset] = static_cast<std::uint16_t>(value << 10U);
    }

    void taito_f2_video::write_priority_register(std::uint32_t offset,
                                                 std::uint16_t value) noexcept {
        if (offset >= priority_regs_.size()) {
            return;
        }
        priority_regs_[offset] = value;
    }

    void taito_f2_video::write_roz_control_register(std::uint32_t offset,
                                                    std::uint16_t value) noexcept {
        if (offset >= roz_control_regs_.size()) {
            return;
        }
        roz_control_regs_[offset] = value;
    }

    void taito_f2_video::write_tc0480scp_control_register(std::uint32_t offset,
                                                          std::uint16_t value) noexcept {
        if (offset >= tc0480scp_control_regs_.size()) {
            return;
        }
        tc0480scp_control_regs_[offset] = value;

        const bool flip_screen =
            (tc0480scp_priority_reg_ & tc0480scp_control_flip_screen) != 0U;
        if (offset < 4U) {
            const int layer = static_cast<int>(offset);
            const int raw = static_cast<int>(static_cast<std::int16_t>(value)) + layer * 4;
            tc0480scp_scroll_x_[static_cast<std::size_t>(layer)] =
                flip_screen ? raw : -raw;
        } else if (offset >= 4U && offset < 8U) {
            const int layer = static_cast<int>(offset - 4U);
            const int raw = static_cast<int>(static_cast<std::int16_t>(value));
            tc0480scp_scroll_y_[static_cast<std::size_t>(layer)] =
                flip_screen ? raw : -raw;
        } else if (offset == 0x0FU) {
            tc0480scp_priority_reg_ = value;
            for (std::uint32_t layer = 0U; layer < 4U; ++layer) {
                write_tc0480scp_control_register(layer, tc0480scp_control_regs_[layer]);
                write_tc0480scp_control_register(layer + 4U,
                                                 tc0480scp_control_regs_[layer + 4U]);
            }
        }
    }

    void taito_f2_video::draw_sprites(const frame_priority_state& priorities) noexcept {
        if (sprite_gfx_.empty()) {
            return;
        }
        if (!sprite_buffer_valid_) {
            latch_sprites();
        }

        struct sprite_sequence_state final {
            bool big_sprite{};
            bool last_continuation_tile{};
            int x_no{};
            int y_no{};
            int xlatch{};
            int ylatch{};
            std::uint32_t zoom_x_latch{};
            std::uint32_t zoom_y_latch{};
            int x{};
            int y{};
            int x_current{};
            int y_current{};
            int scroll_x{-static_cast<int>(sprite_screen_x_bias)};
            int scroll_y{};
            std::uint16_t color{};
        };

        sprite_sequence_state seq{};
        std::uint32_t active_area = sprite_active_area_;
        bool disabled = sprites_disabled_;
        bool flip_screen = sprites_flip_screen_;
        int master_scroll_x = sprite_master_scroll_x_;
        int master_scroll_y = sprite_master_scroll_y_;
        int extra_scroll_x = 0;
        int extra_scroll_y = 0;
        for (std::uint32_t off = 0U; off < sprite_area_bytes; off += sprite_entry_bytes) {
            const std::uint32_t entry = active_area + off;
            if (static_cast<std::size_t>(entry) + sprite_entry_bytes > sprite_buffer_.size()) {
                break;
            }
            const std::uint16_t code_word = read16(sprite_buffer_, entry + 0U);
            const std::uint16_t zoom_word = read16(sprite_buffer_, entry + 2U);
            const std::uint16_t x_word = read16(sprite_buffer_, entry + 4U);
            const std::uint16_t y_word = read16(sprite_buffer_, entry + 6U);
            const std::uint16_t attr = read16(sprite_buffer_, entry + 8U);
            const std::uint16_t ctrl = read16(sprite_buffer_, entry + 10U);
            if ((y_word & 0x8000U) != 0U) {
                disabled = (ctrl & 0x1000U) != 0U;
                flip_screen = (ctrl & 0x2000U) != 0U;
                seq.big_sprite = false;
                seq.last_continuation_tile = false;
                active_area = sprite_active_area_from_marker(y_word, ctrl, active_area);
                continue;
            }
            const std::uint16_t x_mode = x_word & 0xF000U;
            if (x_mode == 0xA000U) {
                master_scroll_x = sign_extend_12(x_word);
                master_scroll_y = sign_extend_12(y_word);
            } else if (x_mode == 0x5000U) {
                extra_scroll_x = sign_extend_12(x_word);
                extra_scroll_y = sign_extend_12(y_word);
            }
            if (disabled) {
                continue;
            }

            const std::uint16_t sprite_cont = static_cast<std::uint16_t>(attr >> 8U);
            if ((sprite_cont & 0x08U) != 0U) {
                if (!seq.big_sprite) {
                    seq.xlatch = static_cast<int>(x_word & 0x0FFFU);
                    seq.ylatch = static_cast<int>(y_word & 0x0FFFU);
                    seq.x_no = 0;
                    seq.y_no = 0;
                    seq.zoom_y_latch = (zoom_word >> 8U) & 0x00FFU;
                    seq.zoom_x_latch = zoom_word & 0x00FFU;
                    seq.big_sprite = true;
                }
            } else if (seq.big_sprite) {
                seq.last_continuation_tile = true;
            }

            if ((sprite_cont & 0x04U) == 0U) {
                seq.color = static_cast<std::uint16_t>(attr & 0x00FFU);
            }

            if (!seq.big_sprite || (sprite_cont & 0xF0U) == 0U) {
                seq.x = static_cast<int>(x_word & 0x0FFFU);
                seq.y = static_cast<int>(y_word & 0x0FFFU);
                seq.x_current = seq.x;
                seq.y_current = seq.y;
                const int x_offset = sprite_x_offset(flip_screen);
                if ((x_word & 0x8000U) != 0U) {
                    seq.scroll_x = -static_cast<int>(sprite_screen_x_bias) - x_offset;
                    seq.scroll_y = 0;
                } else if ((x_word & 0x4000U) != 0U) {
                    seq.scroll_x =
                        master_scroll_x - static_cast<int>(sprite_screen_x_bias) - x_offset;
                    seq.scroll_y = master_scroll_y;
                } else {
                    seq.scroll_x = extra_scroll_x + master_scroll_x -
                                   static_cast<int>(sprite_screen_x_bias) - x_offset;
                    seq.scroll_y = extra_scroll_y + master_scroll_y;
                }
            } else {
                if ((sprite_cont & 0x10U) == 0U) {
                    seq.y = seq.y_current;
                } else if ((sprite_cont & 0x20U) != 0U) {
                    seq.y += 16;
                    ++seq.y_no;
                }
                if ((sprite_cont & 0x40U) == 0U) {
                    seq.x = seq.x_current;
                } else if ((sprite_cont & 0x80U) != 0U) {
                    seq.x += 16;
                    seq.y_no = 0;
                    ++seq.x_no;
                }
            }

            std::uint32_t width = 16U;
            std::uint32_t height = 16U;
            if (seq.big_sprite) {
                const std::uint32_t zoom_x = seq.zoom_x_latch;
                const std::uint32_t zoom_y = seq.zoom_y_latch;
                if (zoom_x != 0U || zoom_y != 0U) {
                    const std::uint32_t span_x = 0xFFU - zoom_x;
                    const std::uint32_t span_y = 0xFFU - zoom_y;
                    const int x0 =
                        seq.xlatch + static_cast<int>((seq.x_no * span_x + 15U) / 16U);
                    const int y0 =
                        seq.ylatch + static_cast<int>((seq.y_no * span_y + 15U) / 16U);
                    const int x1 =
                        seq.xlatch + static_cast<int>(((seq.x_no + 1) * span_x + 15U) / 16U);
                    const int y1 =
                        seq.ylatch + static_cast<int>(((seq.y_no + 1) * span_y + 15U) / 16U);
                    seq.x = x0;
                    seq.y = y0;
                    width = static_cast<std::uint32_t>(std::max(0, x1 - x0));
                    height = static_cast<std::uint32_t>(std::max(0, y1 - y0));
                }
            } else {
                const std::uint32_t zoom_x = zoom_word & 0x00FFU;
                const std::uint32_t zoom_y = (zoom_word >> 8U) & 0x00FFU;
                width = (0x100U - zoom_x) / 16U;
                height = (0x100U - zoom_y) / 16U;
            }

            if (seq.last_continuation_tile) {
                seq.big_sprite = false;
                seq.last_continuation_tile = false;
            }

            std::uint32_t code = code_word & 0x1FFFU;
            if (sprite_mode_ == sprite_mode::extension_low) {
                const std::uint16_t ext = sprite_extension_word(entry);
                code = (code_word & 0x03FFU) | ((static_cast<std::uint32_t>(ext) & 0x003FU)
                                                << 10U);
            } else if (sprite_mode_ == sprite_mode::extension_high) {
                const std::uint16_t ext = sprite_extension_word(entry);
                code = (code_word & 0x00FFU) | (static_cast<std::uint32_t>(ext) & 0xFF00U);
            } else if (sprite_mode_ == sprite_mode::extension_low_as_high) {
                const std::uint16_t ext = sprite_extension_word(entry);
                code = (code_word & 0x00FFU) | ((static_cast<std::uint32_t>(ext) & 0x00FFU)
                                                << 8U);
            } else {
                const std::uint32_t bank = (code & 0x1C00U) >> 10U;
                code = sprite_banks_[bank] + (code & 0x03FFU);
            }
            if (code == 0U || width == 0U || height == 0U) {
                continue;
            }

            const std::uint8_t priority_group =
                static_cast<std::uint8_t>((seq.color >> 6U) & 0x03U);
            const std::uint8_t sprite_priority = priorities.sprite[priority_group];
            const bool flip_x = (sprite_cont & 0x01U) != 0U;
            const bool flip_y = (sprite_cont & 0x02U) != 0U;
            const std::uint16_t palette_bank = static_cast<std::uint16_t>(128U + seq.color);
            int sx = sign_extend_12(static_cast<std::uint16_t>(seq.x + seq.scroll_x));
            int sy = sign_extend_12(static_cast<std::uint16_t>(seq.y + seq.scroll_y));
            bool draw_flip_x = flip_x;
            bool draw_flip_y = flip_y;
            if (flip_screen) {
                sx = static_cast<int>(visible_width) - sx - static_cast<int>(width);
                sy = 256 - sy - static_cast<int>(height);
                draw_flip_x = !draw_flip_x;
                draw_flip_y = !draw_flip_y;
            }
            draw_sprite_cell(code, sx, sy, width, height, draw_flip_x, draw_flip_y,
                             palette_bank, sprite_priority, priorities.sprite_blend_mode);
        }
    }

    int taito_f2_video::sign_extend_12(std::uint16_t value) noexcept {
        const int raw = static_cast<int>(value & 0x0FFFU);
        return (raw & 0x0800) != 0 ? raw - 0x1000 : raw;
    }

    int taito_f2_video::roz_x_multiplier(roz_variant variant) noexcept {
        switch (variant) {
        case roz_variant::tc0430grw:
            return 1;
        case roz_variant::tc0280grd:
            return 2;
        }
        return 2;
    }

    taito_f2_video::sprite_active_area_source
    taito_f2_video::resolved_sprite_active_area_source() const noexcept {
        if (sprite_active_area_source_ != sprite_active_area_source::mode_default) {
            return sprite_active_area_source_;
        }
        return sprite_mode_ == sprite_mode::banked
                   ? sprite_active_area_source::control_word_bit0
                   : sprite_active_area_source::none;
    }

    std::uint32_t taito_f2_video::sprite_active_area_from_marker(
        std::uint16_t y_word, std::uint16_t ctrl, std::uint32_t fallback) const noexcept {
        switch (resolved_sprite_active_area_source()) {
        case sprite_active_area_source::control_word_bit0:
            return (ctrl & 0x0001U) != 0U
                       ? static_cast<std::uint32_t>(sprite_active_area_stride)
                       : 0U;
        case sprite_active_area_source::y_word_bit0:
            return (y_word & 0x0001U) != 0U
                       ? static_cast<std::uint32_t>(sprite_active_area_stride)
                       : 0U;
        case sprite_active_area_source::mode_default:
        case sprite_active_area_source::none:
            return fallback;
        }
        return fallback;
    }

    int taito_f2_video::sprite_x_offset(bool flip_screen) const noexcept {
        return flip_screen ? -flip_hide_pixels_ : hide_pixels_;
    }

    void taito_f2_video::update_sprite_control_state() noexcept {
        std::uint32_t active_area = sprite_active_area_;
        bool disabled = sprites_disabled_;
        bool flip_screen = sprites_flip_screen_;
        int master_scroll_x = sprite_master_scroll_x_;
        int master_scroll_y = sprite_master_scroll_y_;

        if (active_area == sprite_active_area_stride &&
            read16(sprite_buffer_, sprite_active_area_stride + 6U) == 0U &&
            read16(sprite_buffer_, sprite_active_area_stride + 10U) == 0U) {
            active_area = 0U;
        }

        for (std::uint32_t off = 0U; off < sprite_area_bytes; off += sprite_entry_bytes) {
            const std::uint32_t entry = active_area + off;
            if (static_cast<std::size_t>(entry) + sprite_entry_bytes > sprite_buffer_.size()) {
                break;
            }
            const std::uint16_t x_word = read16(sprite_buffer_, entry + 4U);
            const std::uint16_t y_word = read16(sprite_buffer_, entry + 6U);
            const std::uint16_t ctrl = read16(sprite_buffer_, entry + 10U);
            if ((y_word & 0x8000U) != 0U) {
                disabled = (ctrl & 0x1000U) != 0U;
                flip_screen = (ctrl & 0x2000U) != 0U;
                active_area = sprite_active_area_from_marker(y_word, ctrl, active_area);
                continue;
            }
            if ((x_word & 0xF000U) == 0xA000U) {
                master_scroll_x = sign_extend_12(x_word);
                master_scroll_y = sign_extend_12(y_word);
            }
        }

        sprite_active_area_ = active_area;
        sprites_disabled_ = disabled;
        sprites_flip_screen_ = flip_screen;
        sprite_master_scroll_x_ = master_scroll_x;
        sprite_master_scroll_y_ = master_scroll_y;
    }

    void taito_f2_video::draw_sprite_cell(std::uint32_t code, int sx, int sy,
                                          std::uint32_t width, std::uint32_t height,
                                          bool flip_x, bool flip_y,
                                          std::uint16_t palette_bank,
                                          std::uint8_t sprite_priority,
                                          std::uint8_t sprite_blend_mode) noexcept {
        const std::uint8_t sprite_rank = static_cast<std::uint8_t>(sprite_priority + 1U);
        for (std::uint32_t ty = 0; ty < height; ++ty) {
            const int py = sy + static_cast<int>(ty);
            if (py < 0 || py >= static_cast<int>(visible_height)) {
                continue;
            }
            int local_y = static_cast<int>((ty * 16U) / height);
            if (local_y > 15) {
                local_y = 15;
            }
            if (flip_y) {
                local_y = 15 - local_y;
            }
            for (std::uint32_t tx = 0; tx < width; ++tx) {
                const int px = sx + static_cast<int>(tx);
                if (px < 0 || px >= static_cast<int>(visible_width)) {
                    continue;
                }
                int local_x = static_cast<int>((tx * 16U) / width);
                if (local_x > 15) {
                    local_x = 15;
                }
                if (flip_x) {
                    local_x = 15 - local_x;
                }
                const std::uint8_t pen = sprite_pixel(code, local_x, local_y);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::size_t pixel_index =
                    static_cast<std::size_t>(py) * visible_width + static_cast<std::size_t>(px);
                if (pixel_sprite_priority_[pixel_index] > sprite_rank) {
                    continue;
                }
                const std::uint8_t tile_priority = pixel_priority_[pixel_index];
                const std::uint16_t sprite_color_index = palette_index(palette_bank, pen);
                const std::uint16_t tile_color_index = pixel_palette_index_[pixel_index];
                std::uint16_t output_index = sprite_color_index;
                bool should_write = false;

                if (sprite_blend_mode == 0xC0U &&
                    static_cast<std::uint16_t>(sprite_priority + 1U) == tile_priority) {
                    output_index = static_cast<std::uint16_t>((sprite_color_index & 0xFFF0U) |
                                                              (tile_color_index & 0x000FU));
                    should_write = true;
                } else if (sprite_blend_mode == 0xC0U &&
                           sprite_priority ==
                               static_cast<std::uint8_t>(tile_priority + 1U)) {
                    output_index = (tile_color_index & 0x000FU) != 0U
                                       ? static_cast<std::uint16_t>(
                                             (tile_color_index & 0xFFF0U) |
                                             (sprite_color_index & 0x000FU))
                                       : sprite_color_index;
                    should_write = true;
                } else if (sprite_blend_mode == 0x80U &&
                           static_cast<std::uint16_t>(sprite_priority + 1U) == tile_priority) {
                    output_index = static_cast<std::uint16_t>(tile_color_index & 0xFFEFU);
                    should_write = true;
                } else if (sprite_blend_mode == 0x80U &&
                           sprite_priority ==
                               static_cast<std::uint8_t>(tile_priority + 1U)) {
                    output_index = static_cast<std::uint16_t>(sprite_color_index & 0xFFEFU);
                    should_write = true;
                } else if (sprite_priority > tile_priority) {
                    should_write = true;
                }

                if (!should_write) {
                    continue;
                }
                pixels_[pixel_index] = palette_index_rgb(output_index);
                pixel_palette_index_[pixel_index] = output_index;
                pixel_sprite_priority_[pixel_index] = sprite_rank;
            }
        }
    }

    void taito_f2_video::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.u16(scroll0_x_);
        writer.u16(scroll0_y_);
        writer.u16(scroll1_x_);
        writer.u16(scroll1_y_);
        writer.u16(scroll2_x_);
        writer.u16(scroll2_y_);
        writer.u16(scroll0_secondary_x_);
        writer.u16(scroll0_secondary_y_);
        writer.u16(scroll1_secondary_x_);
        writer.u16(scroll1_secondary_y_);
        writer.u16(scroll2_secondary_x_);
        writer.u16(scroll2_secondary_y_);
        writer.u32(layer0_base_);
        writer.u32(layer1_base_);
        writer.u32(text_base_);
        writer.u32(text_gfx_base_);
        writer.u16(layer_control_);
        writer.u16(layer_control_secondary_);
        writer.boolean(display_enabled_);
        writer.u8(static_cast<std::uint8_t>(palette_format_));
        writer.u8(static_cast<std::uint8_t>(tilemap_variant_));
        writer.boolean(sprite_buffer_valid_);
        writer.bytes(sprite_buffer_);
        writer.bytes(sprite_delay_buffer_);
        for (const std::uint16_t bank : sprite_banks_) {
            writer.u16(bank);
        }
        for (const std::uint16_t priority : priority_regs_) {
            writer.u16(priority);
        }
        for (const std::uint16_t control : roz_control_regs_) {
            writer.u16(control);
        }
        for (const std::uint16_t control : tc0480scp_control_regs_) {
            writer.u16(control);
        }
        for (const int scroll_x : tc0480scp_scroll_x_) {
            writer.u32(static_cast<std::uint32_t>(scroll_x));
        }
        for (const int scroll_y : tc0480scp_scroll_y_) {
            writer.u32(static_cast<std::uint32_t>(scroll_y));
        }
        writer.u16(tc0480scp_priority_reg_);
        writer.u16(tc0480scp_palette_bank_base_);
        writer.u8(static_cast<std::uint8_t>(tc0480scp_priority_model_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_bg_x_offset_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_bg_y_offset_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_text_x_offset_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_text_y_offset_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_flip_x_offset_));
        writer.u32(static_cast<std::uint32_t>(tc0480scp_flip_y_offset_));
        writer.u8(static_cast<std::uint8_t>(roz_variant_));
        writer.u32(static_cast<std::uint32_t>(roz_x_offset_));
        writer.u32(static_cast<std::uint32_t>(roz_y_offset_));
        writer.u8(static_cast<std::uint8_t>(sprite_mode_));
        writer.u8(static_cast<std::uint8_t>(sprite_active_area_source_));
        writer.u8(static_cast<std::uint8_t>(sprite_buffer_policy_));
        writer.u32(static_cast<std::uint32_t>(hide_pixels_));
        writer.u32(static_cast<std::uint32_t>(flip_hide_pixels_));
        writer.u32(sprite_active_area_);
        writer.boolean(sprites_disabled_);
        writer.boolean(sprites_flip_screen_);
        writer.u32(static_cast<std::uint32_t>(sprite_master_scroll_x_));
        writer.u32(static_cast<std::uint32_t>(sprite_master_scroll_y_));
    }

    void taito_f2_video::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        scroll0_x_ = reader.u16();
        scroll0_y_ = reader.u16();
        scroll1_x_ = reader.u16();
        scroll1_y_ = reader.u16();
        scroll2_x_ = reader.u16();
        scroll2_y_ = reader.u16();
        scroll0_secondary_x_ = reader.u16();
        scroll0_secondary_y_ = reader.u16();
        scroll1_secondary_x_ = reader.u16();
        scroll1_secondary_y_ = reader.u16();
        scroll2_secondary_x_ = reader.u16();
        scroll2_secondary_y_ = reader.u16();
        layer0_base_ = reader.u32();
        layer1_base_ = reader.u32();
        text_base_ = reader.u32();
        text_gfx_base_ = reader.u32();
        layer_control_ = reader.u16();
        layer_control_secondary_ = reader.u16();
        display_enabled_ = reader.boolean();
        palette_format_ = static_cast<palette_format>(reader.u8());
        tilemap_variant_ = static_cast<tilemap_variant>(reader.u8());
        sprite_buffer_valid_ = reader.boolean();
        reader.bytes(sprite_buffer_);
        reader.bytes(sprite_delay_buffer_);
        for (std::uint16_t& bank : sprite_banks_) {
            bank = reader.u16();
        }
        for (std::uint16_t& priority : priority_regs_) {
            priority = reader.u16();
        }
        for (std::uint16_t& control : roz_control_regs_) {
            control = reader.u16();
        }
        for (std::uint16_t& control : tc0480scp_control_regs_) {
            control = reader.u16();
        }
        for (int& scroll_x : tc0480scp_scroll_x_) {
            scroll_x = static_cast<int>(reader.u32());
        }
        for (int& scroll_y : tc0480scp_scroll_y_) {
            scroll_y = static_cast<int>(reader.u32());
        }
        tc0480scp_priority_reg_ = reader.u16();
        tc0480scp_palette_bank_base_ = reader.u16();
        tc0480scp_priority_model_ = static_cast<tc0480scp_priority_model>(reader.u8());
        tc0480scp_bg_x_offset_ = static_cast<int>(reader.u32());
        tc0480scp_bg_y_offset_ = static_cast<int>(reader.u32());
        tc0480scp_text_x_offset_ = static_cast<int>(reader.u32());
        tc0480scp_text_y_offset_ = static_cast<int>(reader.u32());
        tc0480scp_flip_x_offset_ = static_cast<int>(reader.u32());
        tc0480scp_flip_y_offset_ = static_cast<int>(reader.u32());
        roz_variant_ = static_cast<roz_variant>(reader.u8());
        roz_x_offset_ = static_cast<int>(reader.u32());
        roz_y_offset_ = static_cast<int>(reader.u32());
        sprite_mode_ = static_cast<sprite_mode>(reader.u8());
        sprite_active_area_source_ = static_cast<sprite_active_area_source>(reader.u8());
        sprite_buffer_policy_ = static_cast<sprite_buffer_policy>(reader.u8());
        hide_pixels_ = static_cast<int>(reader.u32());
        flip_hide_pixels_ = static_cast<int>(reader.u32());
        sprite_active_area_ = reader.u32();
        sprites_disabled_ = reader.boolean();
        sprites_flip_screen_ = reader.boolean();
        sprite_master_scroll_x_ = static_cast<int>(reader.u32());
        sprite_master_scroll_y_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& taito_f2_video::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> taito_f2_video::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FRAME", frame_index_ & 0xFFFFFFFFULL, 32U, fmt::unsigned_integer};
        register_view_[1] = {"SCN0X", scroll0_x_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"SCN0Y", scroll0_y_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"SCN1X", scroll1_x_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"SCN1Y", scroll1_y_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"TXSCNX", scroll2_x_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"TXSCNY", scroll2_y_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"LAYERCTL", layer_control_, 16U, fmt::flags};
        register_view_[8] = {"BEAMY", beam_y_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"DISPLAY", display_enabled_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[10] = {"PRISWAP", (layer_control_ & control_priority_swap) != 0U ? 1U : 0U,
                              1U, fmt::flags};
        register_view_[11] = {"SPRAREA", sprite_active_area_, 17U, fmt::unsigned_integer};
        register_view_[12] = {"SPRDIS", sprites_disabled_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[13] = {"SPRFLIP", sprites_flip_screen_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[14] = {"SPRMX", static_cast<std::uint16_t>(sprite_master_scroll_x_), 16U,
                              fmt::signed_integer};
        register_view_[15] = {"SPRMY", static_cast<std::uint16_t>(sprite_master_scroll_y_), 16U,
                              fmt::signed_integer};
        register_view_[16] = {"PRI4", priority_regs_[4], 16U, fmt::unsigned_integer};
        register_view_[17] = {"PRI5", priority_regs_[5], 16U, fmt::unsigned_integer};
        register_view_[18] = {"PRI6", priority_regs_[6], 16U, fmt::unsigned_integer};
        register_view_[19] = {"PRI7", priority_regs_[7], 16U, fmt::unsigned_integer};
        const std::uint8_t roz_selector =
            static_cast<std::uint8_t>((priority_regs_[1] & 0x00C0U) >> 6U);
        const std::uint8_t roz_priority = static_cast<std::uint8_t>(
            (priority_regs_[8U + roz_selector / 2U] >> (4U * (roz_selector & 1U))) &
            0x000FU);
        register_view_[20] = {"PRI1", priority_regs_[1], 16U, fmt::unsigned_integer};
        register_view_[21] = {"ROZPRI", roz_priority, 4U, fmt::unsigned_integer};
        register_view_[22] = {"ROZCTL2", roz_control_regs_[2], 16U, fmt::signed_integer};
        register_view_[23] = {"ROZCTL7", roz_control_regs_[7], 16U, fmt::signed_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto taito_f2_video_registration =
            register_factory("taito.f2_video", chip_class::video,
                             []() -> std::unique_ptr<ichip> {
                                 return std::make_unique<taito_f2_video>();
                             });
    } // namespace

} // namespace mnemos::chips::video
