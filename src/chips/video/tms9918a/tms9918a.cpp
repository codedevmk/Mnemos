#include "tms9918a.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;

        constexpr std::array<std::uint32_t, 16> k_palette = {
            0x000000U, // transparent: resolved to backdrop by callers
            0x000000U, // black
            0x21C842U, // medium green
            0x5EDC78U, // light green
            0x5455EDU, // dark blue
            0x7D76FCU, // light blue
            0xD4524DU, // dark red
            0x42EBF5U, // cyan
            0xFC5554U, // medium red
            0xFF7978U, // light red
            0xD4C154U, // dark yellow
            0xE6CE80U, // light yellow
            0x21B03BU, // dark green
            0xC95BBAU, // magenta
            0xCCCCCCU, // gray
            0xFFFFFFU, // white
        };

        [[nodiscard]] bool display_enabled(const std::array<std::uint8_t, 8>& r) noexcept {
            return (r[1] & 0x40U) != 0U;
        }

        [[nodiscard]] bool frame_irq_enabled(const std::array<std::uint8_t, 8>& r) noexcept {
            return (r[1] & 0x20U) != 0U;
        }

        [[nodiscard]] bool sprite_16x16(const std::array<std::uint8_t, 8>& r) noexcept {
            return (r[1] & 0x02U) != 0U;
        }

        [[nodiscard]] bool sprite_magnified(const std::array<std::uint8_t, 8>& r) noexcept {
            return (r[1] & 0x01U) != 0U;
        }

        [[nodiscard]] std::uint16_t name_base(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[2] & 0x0FU) << 10U);
        }

        [[nodiscard]] std::uint16_t color_base_g1(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>(r[3] << 6U);
        }

        [[nodiscard]] std::uint16_t pattern_base_g1(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[4] & 0x07U) << 11U);
        }

        [[nodiscard]] std::uint16_t color_base_g2(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[3] & 0x80U) << 6U);
        }

        [[nodiscard]] std::uint16_t pattern_base_g2(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[4] & 0x04U) << 11U);
        }

        [[nodiscard]] std::uint16_t
        graphics_ii_pattern_address(const std::array<std::uint8_t, 8>& r, std::uint8_t pattern,
                                    int page, int fine_y) noexcept {
            const auto char_index =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) | pattern);
            const auto offset =
                static_cast<std::uint16_t>((char_index << 3U) | static_cast<std::uint16_t>(fine_y));
            const auto mask = static_cast<std::uint16_t>(((r[4] & 0x03U) << 11U) | 0x07FFU);
            return static_cast<std::uint16_t>(pattern_base_g2(r) | (offset & mask));
        }

        [[nodiscard]] std::uint16_t graphics_ii_color_address(const std::array<std::uint8_t, 8>& r,
                                                              std::uint8_t pattern, int page,
                                                              int fine_y) noexcept {
            const auto char_index =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) | pattern);
            const auto offset =
                static_cast<std::uint16_t>((char_index << 3U) | static_cast<std::uint16_t>(fine_y));
            const auto mask = static_cast<std::uint16_t>(((r[3] & 0x7FU) << 6U) | 0x003FU);
            return static_cast<std::uint16_t>(color_base_g2(r) | (offset & mask));
        }

        [[nodiscard]] std::uint16_t
        sprite_attr_base(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[5] & 0x7FU) << 7U);
        }

        [[nodiscard]] std::uint16_t
        sprite_pattern_base(const std::array<std::uint8_t, 8>& r) noexcept {
            return static_cast<std::uint16_t>((r[6] & 0x07U) << 11U);
        }

        [[nodiscard]] int signed_sprite_y(std::uint8_t y) noexcept {
            int sy = static_cast<int>(y) + 1;
            if (sy >= 225) {
                sy -= 256;
            }
            return sy;
        }
    } // namespace

    chip_metadata tms9918a::metadata() const noexcept {
        return {.manufacturer = "Texas Instruments",
                .part_number = "TMS9918A",
                .family = "VDP",
                .klass = chip_class::video,
                .revision = 1U};
    }

    tms9918a::display_mode tms9918a::mode() const noexcept {
        const bool m1 = (reg_[1] & 0x10U) != 0U;
        const bool m2 = (reg_[1] & 0x08U) != 0U;
        const bool m3 = (reg_[0] & 0x02U) != 0U;
        if (m1 && !m2 && !m3) {
            return display_mode::text;
        }
        if (!m1 && m2 && !m3) {
            return display_mode::multicolor;
        }
        if (!m1 && !m2 && m3) {
            return display_mode::graphics_ii;
        }
        return display_mode::graphics_i;
    }

    void tms9918a::set_pal(bool pal) noexcept {
        pal_mode_ = pal;
        total_scanlines_ = pal ? scanlines_pal : scanlines_ntsc;
    }

    std::uint32_t tms9918a::palette_rgb(std::uint8_t colour) const noexcept {
        const std::uint8_t idx = static_cast<std::uint8_t>(colour & 0x0FU);
        if (idx == 0U) {
            return k_palette[reg_[7] & 0x0FU];
        }
        return k_palette[idx];
    }

    void tms9918a::ctrl_write(std::uint8_t value) noexcept {
        if (!cmd_pending_) {
            cmd_first_ = value;
            cmd_pending_ = true;
            addr_ = static_cast<std::uint16_t>((addr_ & 0x3F00U) | value);
            return;
        }

        cmd_pending_ = false;
        addr_ = static_cast<std::uint16_t>(((value & 0x3FU) << 8U) | cmd_first_);
        code_ = static_cast<std::uint8_t>((value >> 6U) & 0x03U);
        if (code_ == 0U) {
            read_buffer_ = vram_[addr_ & 0x3FFFU];
            addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
        } else if (code_ == 2U) {
            const int r = value & 0x07U;
            reg_[static_cast<std::size_t>(r)] = cmd_first_;
            update_irq();
        }
    }

    std::uint8_t tms9918a::data_read() noexcept {
        cmd_pending_ = false;
        const std::uint8_t result = read_buffer_;
        read_buffer_ = vram_[addr_ & 0x3FFFU];
        addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
        return result;
    }

    void tms9918a::data_write(std::uint8_t value) noexcept {
        cmd_pending_ = false;
        vram_[addr_ & 0x3FFFU] = value;
        read_buffer_ = value;
        addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
    }

    std::uint8_t tms9918a::status_read() noexcept {
        cmd_pending_ = false;
        const std::uint8_t result = status_;
        status_ &= 0x1FU;
        update_irq();
        return result;
    }

    void tms9918a::render_graphics_i_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const std::uint16_t nt = name_base(reg_);
        const std::uint16_t pt = pattern_base_g1(reg_);
        const std::uint16_t ct = color_base_g1(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(static_cast<std::uint16_t>(nt + tile_y * 32 + col));
            const std::uint8_t bits = vram_at(
                static_cast<std::uint16_t>(pt + static_cast<std::uint16_t>(pattern) * 8U + fine_y));
            const std::uint8_t colours =
                vram_at(static_cast<std::uint16_t>(ct + static_cast<std::uint16_t>(pattern >> 3U)));
            const std::uint8_t fg = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t bg = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 8; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[col * 8 + px] = palette_rgb(on ? fg : bg);
            }
        }
    }

    void tms9918a::render_graphics_ii_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const int page = line >> 6;
        const std::uint16_t nt = name_base(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(static_cast<std::uint16_t>(nt + tile_y * 32 + col));
            const std::uint8_t bits =
                vram_at(graphics_ii_pattern_address(reg_, pattern, page, fine_y));
            const std::uint8_t colours =
                vram_at(graphics_ii_color_address(reg_, pattern, page, fine_y));
            const std::uint8_t fg = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t bg = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 8; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[col * 8 + px] = palette_rgb(on ? fg : bg);
            }
        }
    }

    void tms9918a::render_text_scanline(int line, std::uint32_t* out) noexcept {
        const std::uint32_t bg = palette_rgb(static_cast<std::uint8_t>(reg_[7] & 0x0FU));
        std::fill(out, out + display_width, bg);

        const int row = line >> 3;
        const int fine_y = line & 7;
        const std::uint16_t nt = name_base(reg_);
        const std::uint16_t pt = pattern_base_g1(reg_);
        const std::uint8_t fg = static_cast<std::uint8_t>(reg_[7] >> 4U);
        constexpr int x_offset = 8;
        for (int col = 0; col < 40; ++col) {
            const std::uint8_t pattern = vram_at(static_cast<std::uint16_t>(nt + row * 40 + col));
            const std::uint8_t bits = vram_at(
                static_cast<std::uint16_t>(pt + static_cast<std::uint16_t>(pattern) * 8U + fine_y));
            for (int px = 0; px < 6; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[x_offset + col * 6 + px] = palette_rgb(on ? fg : (reg_[7] & 0x0FU));
            }
        }
    }

    void tms9918a::render_multicolor_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const std::uint16_t nt = name_base(reg_);
        const std::uint16_t pt = pattern_base_g1(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(static_cast<std::uint16_t>(nt + tile_y * 32 + col));
            const std::uint16_t row_pair =
                static_cast<std::uint16_t>(((tile_y & 3) * 2) + ((fine_y >> 2) & 1));
            const std::uint8_t colours = vram_at(static_cast<std::uint16_t>(
                pt + static_cast<std::uint16_t>(pattern) * 8U + row_pair));
            const std::uint8_t left = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t right = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 4; ++px) {
                out[col * 8 + px] = palette_rgb(left);
                out[col * 8 + 4 + px] = palette_rgb(right);
            }
        }
    }

    void tms9918a::render_sprites(int line, std::uint32_t* out) noexcept {
        if (mode() == display_mode::text) {
            return;
        }

        const int base_size = sprite_16x16(reg_) ? 16 : 8;
        const int zoom = sprite_magnified(reg_) ? 2 : 1;
        const int visible_size = base_size * zoom;
        const std::uint16_t sat = sprite_attr_base(reg_);
        const std::uint16_t spt = sprite_pattern_base(reg_);

        std::array<bool, display_width> collision_occupied{};
        std::array<bool, display_width> draw_occupied{};
        int sprites_on_line = 0;

        for (int i = 0; i < sprite_count; ++i) {
            const std::uint16_t attr = static_cast<std::uint16_t>(sat + i * 4);
            const std::uint8_t raw_y = vram_at(attr);
            if (raw_y == 0xD0U) {
                break;
            }
            const int sy = signed_sprite_y(raw_y);
            if (line < sy || line >= sy + visible_size) {
                continue;
            }

            if (sprites_on_line >= 4) {
                if ((status_ & 0x40U) == 0U) {
                    status_ = static_cast<std::uint8_t>((status_ & 0xE0U) | (i & 0x1F) | 0x40U);
                }
                continue;
            }
            ++sprites_on_line;

            int sx = vram_at(static_cast<std::uint16_t>(attr + 1U));
            std::uint8_t pattern = vram_at(static_cast<std::uint16_t>(attr + 2U));
            const std::uint8_t colour = vram_at(static_cast<std::uint16_t>(attr + 3U));
            if ((colour & 0x80U) != 0U) {
                sx -= 32;
            }
            const std::uint8_t pen = static_cast<std::uint8_t>(colour & 0x0FU);

            int row = (line - sy) / zoom;
            if (base_size == 16) {
                pattern = static_cast<std::uint8_t>(pattern & 0xFCU);
            }
            const int tile_x = base_size == 16 ? 2 : 1;
            const int sub_y = row >> 3;
            const int row_in_tile = row & 7;

            for (int tx = 0; tx < tile_x; ++tx) {
                const std::uint8_t tile =
                    static_cast<std::uint8_t>(pattern + sub_y + (tx == 0 ? 0 : 2));
                const std::uint8_t bits = vram_at(static_cast<std::uint16_t>(
                    spt + static_cast<std::uint16_t>(tile) * 8U + row_in_tile));
                for (int px = 0; px < 8; ++px) {
                    if ((bits & (0x80U >> px)) == 0U) {
                        continue;
                    }
                    for (int zx = 0; zx < zoom; ++zx) {
                        const int x = sx + ((tx * 8 + px) * zoom) + zx;
                        if (x < 0 || x >= display_width) {
                            continue;
                        }
                        const auto xs = static_cast<std::size_t>(x);
                        if (collision_occupied[xs]) {
                            status_ |= 0x20U;
                        } else {
                            collision_occupied[xs] = true;
                        }
                        if (pen == 0U || draw_occupied[xs]) {
                            continue;
                        }
                        draw_occupied[xs] = true;
                        out[x] = palette_rgb(pen);
                    }
                }
            }
        }
    }

    void tms9918a::render_scanline(int line) noexcept {
        if (line < 0 || line >= display_height) {
            return;
        }
        std::uint32_t* const out =
            framebuffer_.data() + static_cast<std::size_t>(line) * display_width;
        if (!display_enabled(reg_)) {
            std::fill(out, out + display_width, palette_rgb(static_cast<std::uint8_t>(reg_[7])));
            return;
        }

        switch (mode()) {
        case display_mode::text:
            render_text_scanline(line, out);
            break;
        case display_mode::multicolor:
            render_multicolor_scanline(line, out);
            break;
        case display_mode::graphics_ii:
            render_graphics_ii_scanline(line, out);
            break;
        case display_mode::graphics_i:
        default:
            render_graphics_i_scanline(line, out);
            break;
        }
        render_sprites(line, out);
    }

    void tms9918a::render_frame() noexcept {
        for (int line = 0; line < display_height; ++line) {
            render_scanline(line);
        }
    }

    void tms9918a::update_irq() noexcept {
        const bool asserted = ((status_ & 0x80U) != 0U) && frame_irq_enabled(reg_);
        if (asserted == irq_asserted_) {
            return;
        }
        irq_asserted_ = asserted;
        if (irq_callback_) {
            irq_callback_(asserted);
        }
    }

    void tms9918a::finish_scanline() noexcept {
        if (scanline_ < display_height) {
            render_scanline(scanline_);
        } else if (scanline_ == display_height) {
            status_ |= 0x80U;
            update_irq();
        }

        ++scanline_;
        if (scanline_ >= total_scanlines_) {
            scanline_ = 0;
            ++frame_index_;
        }
    }

    void tms9918a::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++scanline_cycle_;
            if (scanline_cycle_ >= cycles_per_line) {
                scanline_cycle_ = 0;
                finish_scanline();
            }
        }
    }

    frame_buffer_view tms9918a::framebuffer() const noexcept {
        return {.pixels = framebuffer_.data(),
                .width = static_cast<std::uint32_t>(display_width),
                .height = static_cast<std::uint32_t>(display_height),
                .stride = 0U};
    }

    void tms9918a::reset(reset_kind /*kind*/) {
        vram_.fill(0U);
        reg_.fill(0U);
        addr_ = 0U;
        code_ = 0U;
        cmd_pending_ = false;
        cmd_first_ = 0U;
        read_buffer_ = 0U;
        status_ = 0U;
        scanline_ = 0;
        scanline_cycle_ = 0;
        total_scanlines_ = pal_mode_ ? scanlines_pal : scanlines_ntsc;
        frame_index_ = 0U;
        std::memset(framebuffer_.data(), 0, framebuffer_.size() * sizeof(std::uint32_t));
        update_irq();
    }

    void tms9918a::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(vram_);
        writer.bytes(reg_);
        writer.u16(addr_);
        writer.u8(code_);
        writer.boolean(cmd_pending_);
        writer.u8(cmd_first_);
        writer.u8(read_buffer_);
        writer.u8(status_);
        writer.boolean(irq_asserted_);
        writer.u32(static_cast<std::uint32_t>(scanline_));
        writer.u32(static_cast<std::uint32_t>(scanline_cycle_));
        writer.u32(static_cast<std::uint32_t>(total_scanlines_));
        writer.boolean(pal_mode_);
        writer.u64(frame_index_);
    }

    void tms9918a::load_state(state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(vram_);
        reader.bytes(reg_);
        addr_ = reader.u16();
        code_ = reader.u8();
        cmd_pending_ = reader.boolean();
        cmd_first_ = reader.u8();
        read_buffer_ = reader.u8();
        status_ = reader.u8();
        irq_asserted_ = reader.boolean();
        scanline_ = static_cast<int>(reader.u32());
        scanline_cycle_ = static_cast<int>(reader.u32());
        total_scanlines_ = static_cast<int>(reader.u32());
        pal_mode_ = reader.boolean();
        frame_index_ = reader.u64();
        update_irq();
    }

    namespace {
        [[maybe_unused]] const auto tms9918a_registration =
            register_factory("ti.tms9918a", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<tms9918a>();
            });
    } // namespace

} // namespace mnemos::chips::video
