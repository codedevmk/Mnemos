#include "cps2_video.hpp"

#include "state.hpp"

#include <algorithm>
#include <cstring>

namespace mnemos::chips::video {

    namespace {
        // The gfx mapper banks tile codes in 0x20000-tile units (the scroll layers
        // address the upper bank, shifted per layer).
        constexpr std::uint32_t gfx_bank_size_tiles = 0x20000U;
        constexpr std::uint32_t gfx_bank_mask = gfx_bank_size_tiles - 1U;

        // A 4-bit channel scaled by the 4-bit brightness: c*0x11 expands the nibble
        // to 8 bits, then the (0x0F + 2*brightness)/0x2D factor dims/brightens it.
        [[nodiscard]] std::uint8_t scale_channel(std::uint8_t channel,
                                                 std::uint8_t brightness) noexcept {
            const std::uint32_t intensity = 0x0FU + (static_cast<std::uint32_t>(brightness) << 1U);
            return static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(channel) * 0x11U * intensity) / 0x2DU);
        }
    } // namespace

    chip_metadata cps2_video::metadata() const noexcept {
        return {
            .manufacturer = "Capcom",
            .part_number = "CPS-2 video",
            .family = "CPS2",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void cps2_video::tick(std::uint64_t /*cycles*/) {
        // Frame-at-render for now (the board calls render()); no per-cycle work.
    }

    void cps2_video::reset(reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        std::fill(pixel_layer_.begin(), pixel_layer_.end(), std::uint8_t{0xFFU});
        std::fill(pixel_pen_.begin(), pixel_pen_.end(), std::uint8_t{0U});
        std::fill(pixel_group_.begin(), pixel_group_.end(), std::uint8_t{0U});
        std::fill(pixel_priority_.begin(), pixel_priority_.end(), std::uint8_t{0U});
        palette_ram_.fill(0U);
        cps_b_regs_.fill(0U);
        scroll1_x_ = scroll1_y_ = scroll2_x_ = scroll2_y_ = scroll3_x_ = scroll3_y_ = 0U;
        scroll1_base_ = scroll2_base_ = scroll3_base_ = 0U;
        object_base_ = 0U;
        rowscroll_base_ = 0U;
        rowscroll_line_offset_ = 0U;
        rowscroll_enabled_ = false;
        video_control_ = 0U;
        display_enabled_ = true;
        frame_index_ = 0U;
    }

    std::uint32_t cps2_video::decode_color(std::uint16_t value) noexcept {
        const auto brightness = static_cast<std::uint8_t>((value >> 12U) & 0x0FU);
        const auto r = scale_channel(static_cast<std::uint8_t>((value >> 8U) & 0x0FU), brightness);
        const auto g = scale_channel(static_cast<std::uint8_t>((value >> 4U) & 0x0FU), brightness);
        const auto b = scale_channel(static_cast<std::uint8_t>(value & 0x0FU), brightness);
        return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) | b;
    }

    std::uint16_t cps2_video::palette_color(std::uint16_t index) const noexcept {
        const std::size_t offset = static_cast<std::size_t>(index) * 2U;
        if (offset + 1U < palette_ram_.size()) {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(palette_ram_[offset]) << 8U) |
                palette_ram_[offset + 1U]);
        }
        return 0U;
    }

    void cps2_video::copy_palette(std::uint32_t source_base, std::uint16_t control) noexcept {
        if (video_ram_.empty() || source_base >= video_ram_.size()) {
            return;
        }
        // A zero control word means "all six pages" (the reference default).
        const std::uint16_t pages = control != 0U ? control : 0x003FU;
        std::uint32_t page_source = source_base;
        bool source_advanced = false;
        for (std::size_t page = 0U; page < palette_copy_pages; ++page) {
            if ((pages & static_cast<std::uint16_t>(1U << page)) != 0U) {
                if (page_source + palette_page_bytes <= video_ram_.size()) {
                    std::memcpy(palette_ram_.data() + page * palette_page_bytes,
                                video_ram_.data() + page_source, palette_page_bytes);
                }
                page_source += static_cast<std::uint32_t>(palette_page_bytes);
                source_advanced = true;
            } else if (source_advanced) {
                page_source += static_cast<std::uint32_t>(palette_page_bytes);
            }
        }
    }

    bool cps2_video::map_gfx_code(gfx_type type, std::uint32_t code,
                                  std::uint32_t& mapped) const noexcept {
        if (gfx_.empty()) {
            return false;
        }
        if (type == gfx_type::sprites) {
            mapped = code;
            return true;
        }
        int shift = 0;
        switch (type) {
        case gfx_type::scroll1:
            shift = 0;
            break;
        case gfx_type::scroll2:
            shift = 1;
            break;
        case gfx_type::scroll3:
            shift = 3;
            break;
        default:
            return false;
        }
        const std::uint32_t shifted = code << shift;
        if (shifted > gfx_bank_mask) {
            return false;
        }
        mapped = (gfx_bank_size_tiles + (shifted & gfx_bank_mask)) >> shift;
        return true;
    }

    std::uint8_t cps2_video::decode_packed_pixel(std::uint32_t tile_base, std::uint32_t row_stride,
                                                 int x, int y) const noexcept {
        const std::uint32_t group = static_cast<std::uint32_t>(x >> 3);
        const std::uint32_t bit = static_cast<std::uint32_t>(7 - (x & 7));
        const std::uint32_t offset =
            tile_base + static_cast<std::uint32_t>(y) * row_stride + group * 4U;
        if (gfx_.empty() || offset + 3U >= gfx_.size()) {
            return transparent_pen;
        }
        return static_cast<std::uint8_t>(
            (((gfx_[offset + 3U] >> bit) & 1U) << 0U) | (((gfx_[offset + 2U] >> bit) & 1U) << 1U) |
            (((gfx_[offset + 1U] >> bit) & 1U) << 2U) | (((gfx_[offset + 0U] >> bit) & 1U) << 3U));
    }

    std::uint8_t cps2_video::tile_pixel(gfx_type type, std::uint32_t code, int x, int y,
                                        int tile_size, int x_bias) const noexcept {
        std::uint32_t mapped = 0U;
        if (!map_gfx_code(type, code, mapped)) {
            return transparent_pen;
        }
        const int rom_x = x + x_bias;
        if (tile_size == 8 && x >= 0 && x < 8 && rom_x >= 0 && rom_x < 16 && y >= 0 && y < 8) {
            return decode_packed_pixel(mapped * 64U, 8U, rom_x, y);
        }
        if (tile_size == 16 && x >= 0 && x < 16 && y >= 0 && y < 16) {
            return decode_packed_pixel(mapped * 128U, 8U, x, y);
        }
        if (tile_size == 32 && x >= 0 && x < 32 && y >= 0 && y < 32) {
            return decode_packed_pixel(mapped * 512U, 16U, x, y);
        }
        return transparent_pen;
    }

    std::uint16_t cps2_video::video_read16(std::uint32_t offset) const noexcept {
        if (offset + 1U >= video_ram_.size()) {
            return 0xFFFFU;
        }
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(video_ram_[offset]) << 8U) |
                                          video_ram_[offset + 1U]);
    }

    void cps2_video::reset_pixel_buffers(std::uint32_t backdrop) noexcept {
        std::fill(pixels_.begin(), pixels_.end(), backdrop);
        std::fill(pixel_layer_.begin(), pixel_layer_.end(), std::uint8_t{0xFFU});
        std::fill(pixel_pen_.begin(), pixel_pen_.end(), std::uint8_t{0U});
        std::fill(pixel_group_.begin(), pixel_group_.end(), std::uint8_t{0U});
        std::fill(pixel_priority_.begin(), pixel_priority_.end(), std::uint8_t{0U});
    }

    void cps2_video::plot_layer_pixel(int x, int y, std::uint8_t layer, std::uint8_t pen,
                                      std::uint8_t group, std::uint8_t priority,
                                      std::uint32_t color) noexcept {
        if (flip_screen()) {
            x = static_cast<int>(visible_width) - 1 - x;
            y = static_cast<int>(visible_height) - 1 - y;
        }
        if (x < 0 || x >= static_cast<int>(visible_width) || y < 0 ||
            y >= static_cast<int>(visible_height)) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(y) * visible_width + x;
        pixels_[index] = color;
        pixel_layer_[index] = layer;
        pixel_pen_[index] = pen;
        pixel_group_[index] = group;
        pixel_priority_[index] |= priority;
    }

    void cps2_video::draw_scroll1(std::uint8_t priority) noexcept {
        const std::uint32_t map_base = scroll1_base_;
        const int scroll_x = static_cast<std::int16_t>(scroll1_x_);
        const int scroll_y = static_cast<std::int16_t>(scroll1_y_);

        for (int py = 0; py < static_cast<int>(visible_height); ++py) {
            const int world_y = (py + visible_y_start + scroll_y) & 0x01FF;
            const int row = world_y / 8;
            const int ty = world_y % 8;
            for (int px = 0; px < static_cast<int>(visible_width); ++px) {
                const int world_x = (px + visible_x_start + scroll_x) & 0x01FF;
                const int col = world_x / 8;
                const int tx = world_x % 8;
                // scroll1 name-table index: row in 0x1F, col in 0x3F, row bit5 banks.
                const std::uint32_t tile_index = (static_cast<std::uint32_t>(row) & 0x1FU) +
                                                 ((static_cast<std::uint32_t>(col) & 0x3FU) << 5U) +
                                                 ((static_cast<std::uint32_t>(row) & 0x20U) << 6U);
                const std::uint32_t map_offset = tile_index * 4U;
                if (map_base + map_offset + 3U >= video_ram_.size()) {
                    continue;
                }
                const std::uint16_t tile_code = video_read16(map_base + map_offset);
                const std::uint16_t attr = video_read16(map_base + map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) != 0U ? (7 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) != 0U ? (7 - ty) : ty;
                const int x_bias = (tile_index & 0x20U) != 0U ? 8 : 0;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll1, tile_code, fetch_x, fetch_y, 8, x_bias);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num =
                    static_cast<std::uint16_t>(scroll1_palette_base + (attr & 0x001FU));
                const std::uint32_t color =
                    decode_color(palette_color(static_cast<std::uint16_t>(pal_num * 16U + pen)));
                const auto group = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(px, py, 1U, pen, group, priority, color);
            }
        }
    }

    void cps2_video::draw_scroll2(std::uint8_t priority) noexcept {
        const std::uint32_t map_base = scroll2_base_;
        const int scroll_y = static_cast<std::int16_t>(scroll2_y_);
        const std::uint16_t otheroffs = rowscroll_line_offset_;
        for (int py = 0; py < static_cast<int>(visible_height); ++py) {
            const int screen_y = py + visible_y_start;
            int scroll_x = static_cast<std::int16_t>(scroll2_x_);
            const int world_y = (screen_y + scroll_y) & 0x03FF;
            const int row = world_y / 16;
            const int ty = world_y % 16;
            // Per-line horizontal row-scroll: a screen-line-indexed table of signed
            // x offsets in video RAM (biased by the CPS-A "other" offset).
            if (rowscroll_enabled_) {
                const std::uint32_t n =
                    (static_cast<std::uint32_t>(screen_y) + otheroffs) & 0x03FFU;
                const std::uint32_t entry = rowscroll_base_ + n * 2U;
                if (entry + 1U < video_ram_.size()) {
                    scroll_x += static_cast<std::int16_t>(video_read16(entry));
                }
            }
            for (int px = 0; px < static_cast<int>(visible_width); ++px) {
                const int world_x = (px + visible_x_start + scroll_x) & 0x03FF;
                const int col = world_x / 16;
                const int tx = world_x % 16;
                // scroll2 name-table index: row in 0x0F, col in 0x3F, row bits4-5 bank.
                const std::uint32_t tile_index = (static_cast<std::uint32_t>(row) & 0x0FU) +
                                                 ((static_cast<std::uint32_t>(col) & 0x3FU) << 4U) +
                                                 ((static_cast<std::uint32_t>(row) & 0x30U) << 6U);
                const std::uint32_t map_offset = tile_index * 4U;
                if (map_base + map_offset + 3U >= video_ram_.size()) {
                    continue;
                }
                const std::uint16_t tile_code = video_read16(map_base + map_offset);
                const std::uint16_t attr = video_read16(map_base + map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) != 0U ? (15 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) != 0U ? (15 - ty) : ty;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll2, tile_code, fetch_x, fetch_y, 16, 0);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num =
                    static_cast<std::uint16_t>(scroll2_palette_base + (attr & 0x001FU));
                const std::uint32_t color =
                    decode_color(palette_color(static_cast<std::uint16_t>(pal_num * 16U + pen)));
                const auto group = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(px, py, 2U, pen, group, priority, color);
            }
        }
    }

    void cps2_video::draw_scroll3(std::uint8_t priority) noexcept {
        const std::uint32_t map_base = scroll3_base_;
        const int scroll_x = static_cast<std::int16_t>(scroll3_x_);
        const int scroll_y = static_cast<std::int16_t>(scroll3_y_);
        for (int py = 0; py < static_cast<int>(visible_height); ++py) {
            const int world_y = (py + visible_y_start + scroll_y) & 0x07FF;
            const int row = world_y / 32;
            const int ty = world_y % 32;
            for (int px = 0; px < static_cast<int>(visible_width); ++px) {
                const int world_x = (px + visible_x_start + scroll_x) & 0x07FF;
                const int col = world_x / 32;
                const int tx = world_x % 32;
                // scroll3 name-table index: row in 0x07, col in 0x3F, row bits3-5 bank.
                const std::uint32_t tile_index = (static_cast<std::uint32_t>(row) & 0x07U) +
                                                 ((static_cast<std::uint32_t>(col) & 0x3FU) << 3U) +
                                                 ((static_cast<std::uint32_t>(row) & 0x38U) << 6U);
                const std::uint32_t map_offset = tile_index * 4U;
                if (map_base + map_offset + 3U >= video_ram_.size()) {
                    continue;
                }
                const std::uint16_t tile_code =
                    static_cast<std::uint16_t>(video_read16(map_base + map_offset) & 0x3FFFU);
                const std::uint16_t attr = video_read16(map_base + map_offset + 2U);
                const int fetch_x = (attr & 0x0020U) != 0U ? (31 - tx) : tx;
                const int fetch_y = (attr & 0x0040U) != 0U ? (31 - ty) : ty;
                const std::uint8_t pen =
                    tile_pixel(gfx_type::scroll3, tile_code, fetch_x, fetch_y, 32, 0);
                if (pen == transparent_pen) {
                    continue;
                }
                const std::uint16_t pal_num =
                    static_cast<std::uint16_t>(scroll3_palette_base + (attr & 0x001FU));
                const std::uint32_t color =
                    decode_color(palette_color(static_cast<std::uint16_t>(pal_num * 16U + pen)));
                const auto group = static_cast<std::uint8_t>((attr & 0x0180U) >> 7U);
                plot_layer_pixel(px, py, 3U, pen, group, priority, color);
            }
        }
    }

    void cps2_video::render(std::uint32_t palette_source, std::uint16_t palette_control) noexcept {
        copy_palette(palette_source, palette_control);
        const std::uint32_t backdrop = decode_color(palette_color(backdrop_color_index));
        reset_pixel_buffers(backdrop);
        if (display_enabled_) {
            // Fixed back-to-front order for now (scroll3 background -> scroll2 ->
            // scroll1 foreground/text); the CPS-B layer-control priority order +
            // sprite interleave land in later increments.
            draw_scroll3(tile_priority_0);
            draw_scroll2(tile_priority_0);
            draw_scroll1(tile_priority_0);
        }
        ++frame_index_;
    }

    frame_buffer_view cps2_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    void cps2_video::save_state(state_writer& writer) const {
        writer.u64(frame_index_);
        for (const std::uint8_t b : palette_ram_) {
            writer.u8(b);
        }
        // Register state the board re-pushes each frame; serialised for a clean
        // mid-frame round-trip. (The framebuffer + compositor buffers are render
        // scratch, regenerated on the next render, so they are not saved.)
        writer.u16(scroll1_x_);
        writer.u16(scroll1_y_);
        writer.u16(scroll2_x_);
        writer.u16(scroll2_y_);
        writer.u16(scroll3_x_);
        writer.u16(scroll3_y_);
        writer.u32(scroll1_base_);
        writer.u32(scroll2_base_);
        writer.u32(scroll3_base_);
        writer.u32(object_base_);
        writer.u32(rowscroll_base_);
        writer.u16(rowscroll_line_offset_);
        writer.u8(rowscroll_enabled_ ? 1U : 0U);
        writer.u16(video_control_);
        writer.u8(display_enabled_ ? 1U : 0U);
        for (const std::uint16_t r : cps_b_regs_) {
            writer.u16(r);
        }
    }

    void cps2_video::load_state(state_reader& reader) {
        frame_index_ = reader.u64();
        for (std::uint8_t& b : palette_ram_) {
            b = reader.u8();
        }
        scroll1_x_ = reader.u16();
        scroll1_y_ = reader.u16();
        scroll2_x_ = reader.u16();
        scroll2_y_ = reader.u16();
        scroll3_x_ = reader.u16();
        scroll3_y_ = reader.u16();
        scroll1_base_ = reader.u32();
        scroll2_base_ = reader.u32();
        scroll3_base_ = reader.u32();
        object_base_ = reader.u32();
        rowscroll_base_ = reader.u32();
        rowscroll_line_offset_ = reader.u16();
        rowscroll_enabled_ = reader.u8() != 0U;
        video_control_ = reader.u16();
        display_enabled_ = reader.u8() != 0U;
        for (std::uint16_t& r : cps_b_regs_) {
            r = reader.u16();
        }
    }

    instrumentation::ichip_introspection& cps2_video::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> cps2_video::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FRAME", frame_index_ & 0xFFFFFFFFULL, 32U, fmt::unsigned_integer};
        register_view_[1] = {"BACKDROP", palette_color(0U), 16U, fmt::unsigned_integer};
        return register_view_;
    }

} // namespace mnemos::chips::video
