#include "s_ppu.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 5-bit gun to 8 bits, top bits replicated into the low bits.
        [[nodiscard]] constexpr std::uint32_t expand5(std::uint32_t gun) noexcept {
            const std::uint32_t v = gun & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }

        // Scale an 8-bit channel by the 4-bit INIDISP brightness (0 = black,
        // 15 = full); brightness 15 leaves the channel unchanged.
        [[nodiscard]] constexpr std::uint32_t apply_brightness(std::uint32_t channel,
                                                               std::uint32_t bright) noexcept {
            return (channel * (bright & 0x0FU)) / 15U;
        }
    } // namespace

    chip_metadata s_ppu::metadata() const noexcept {
        return {
            .manufacturer = "Sony",
            .part_number = "s_ppu",
            .family = "s_ppu",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    std::uint16_t s_ppu::vram_increment(std::uint8_t vmain) noexcept {
        switch (vmain & vmain_step_mask) {
        case 0U:
            return 1U;
        case 1U:
            return 32U;
        default:
            return 128U;
        }
    }

    void s_ppu::reset(reset_kind /*kind*/) {
        vram_.fill(0U);
        cgram_.fill(0U);
        oam_.fill(0U);
        regs_.fill(0U);
        vram_addr_ = 0U;
        vmain_ = 0U;
        cgram_addr_ = 0U;
        cgram_latch_ = 0U;
        cgram_latch_high_ = false;
        oam_addr_ = 0U;
        oam_latch_ = 0U;
        oam_latch_high_ = false;
        bg_scroll_latch_ = 0U;
        inidisp_ = 0x80U; // force blank on reset
        force_blank_ = true;
        brightness_ = 0U;
        bgmode_ = 0U;
        bg_hofs_.fill(0U);
        bg_vofs_.fill(0U);
        bg_sc_base_.fill(0U);
        bg_char_base_.fill(0U);
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void s_ppu::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_x_ == 0U) {
                if (beam_y_ == visible_height) {
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
            if (++beam_x_ == line_dots) {
                beam_x_ = 0U;
                if (++beam_y_ == frame_lines) {
                    beam_y_ = 0U;
                }
            }
        }
    }

    // ───────────────── Register write (integer-exact reference port) ─────────────────

    void s_ppu::write_register(std::uint8_t reg, std::uint8_t value) noexcept {
        reg &= 0x3FU;
        regs_[reg] = value;

        const auto write_bg_hofs = [this](int bg, std::uint8_t v) {
            const std::uint16_t next = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(v) << 8U) | bg_scroll_latch_);
            bg_hofs_[static_cast<std::size_t>(bg)] = static_cast<std::uint16_t>(next & 0x03FFU);
            bg_scroll_latch_ = v;
        };
        const auto write_bg_vofs = [this](int bg, std::uint8_t v) {
            const std::uint16_t next = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(v) << 8U) | bg_scroll_latch_);
            bg_vofs_[static_cast<std::size_t>(bg)] = static_cast<std::uint16_t>(next & 0x03FFU);
            bg_scroll_latch_ = v;
        };

        switch (reg) {
        case reg_inidisp:
            inidisp_ = value;
            force_blank_ = (value & inidisp_force_blank) != 0U;
            brightness_ = static_cast<std::uint8_t>(value & inidisp_brightness);
            break;

        case reg_oamaddl:
            oam_addr_ = static_cast<std::uint16_t>((oam_addr_ & 0x0300U) | value);
            oam_latch_high_ = false;
            break;
        case reg_oamaddh:
            oam_addr_ = static_cast<std::uint16_t>(
                (oam_addr_ & 0x00FFU) | (static_cast<std::uint16_t>(value & 0x01U) << 8U));
            oam_latch_high_ = false;
            break;
        case reg_oamdata: {
            const std::uint16_t addr = static_cast<std::uint16_t>(oam_addr_ & 0x03FFU);
            if (!oam_latch_high_) {
                oam_latch_ = value;
                oam_latch_high_ = true;
            } else {
                // The first 512 bytes commit as a word on the second byte; the
                // remaining 32 bytes commit immediately.
                if (addr < oam_bytes) {
                    if (addr < 0x200U) {
                        oam_[addr & ~static_cast<std::uint16_t>(1U)] = oam_latch_;
                        oam_[(addr & ~static_cast<std::uint16_t>(1U)) + 1U] = value;
                    } else {
                        oam_[addr] = value;
                    }
                }
                oam_addr_ = static_cast<std::uint16_t>((oam_addr_ + 1U) & 0x03FFU);
                oam_latch_high_ = false;
            }
            break;
        }

        case reg_bgmode:
            bgmode_ = value;
            break;

        case reg_bg1sc:
            bg_sc_base_[0] = static_cast<std::uint16_t>((value & 0xFCU) << 8U);
            break;
        case reg_bg2sc:
            bg_sc_base_[1] = static_cast<std::uint16_t>((value & 0xFCU) << 8U);
            break;
        case reg_bg3sc:
            bg_sc_base_[2] = static_cast<std::uint16_t>((value & 0xFCU) << 8U);
            break;
        case reg_bg4sc:
            bg_sc_base_[3] = static_cast<std::uint16_t>((value & 0xFCU) << 8U);
            break;

        case reg_bg12nba:
            bg_char_base_[0] = static_cast<std::uint16_t>((value & 0x0FU) << 12U);
            bg_char_base_[1] = static_cast<std::uint16_t>((value & 0xF0U) << 8U);
            break;
        case reg_bg34nba:
            bg_char_base_[2] = static_cast<std::uint16_t>((value & 0x0FU) << 12U);
            bg_char_base_[3] = static_cast<std::uint16_t>((value & 0xF0U) << 8U);
            break;

        case reg_bg1hofs:
            write_bg_hofs(0, value);
            break;
        case reg_bg1vofs:
            write_bg_vofs(0, value);
            break;
        case reg_bg2hofs:
            write_bg_hofs(1, value);
            break;
        case reg_bg2vofs:
            write_bg_vofs(1, value);
            break;
        case reg_bg3hofs:
            write_bg_hofs(2, value);
            break;
        case reg_bg3vofs:
            write_bg_vofs(2, value);
            break;
        case reg_bg4hofs:
            write_bg_hofs(3, value);
            break;
        case reg_bg4vofs:
            write_bg_vofs(3, value);
            break;

        case reg_vmain:
            vmain_ = value;
            break;
        case reg_vmaddl:
            vram_addr_ = static_cast<std::uint16_t>((vram_addr_ & 0xFF00U) | value);
            break;
        case reg_vmaddh:
            vram_addr_ = static_cast<std::uint16_t>((vram_addr_ & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U));
            break;
        case reg_vmdatal:
            vram_[vram_addr_ & 0x7FFFU] =
                static_cast<std::uint16_t>((vram_[vram_addr_ & 0x7FFFU] & 0xFF00U) | value);
            if ((vmain_ & vmain_inc_on_high) == 0U) {
                vram_addr_ = static_cast<std::uint16_t>(vram_addr_ + vram_increment(vmain_));
            }
            break;
        case reg_vmdatah:
            vram_[vram_addr_ & 0x7FFFU] =
                static_cast<std::uint16_t>((vram_[vram_addr_ & 0x7FFFU] & 0x00FFU) |
                                           (static_cast<std::uint16_t>(value) << 8U));
            if ((vmain_ & vmain_inc_on_high) != 0U) {
                vram_addr_ = static_cast<std::uint16_t>(vram_addr_ + vram_increment(vmain_));
            }
            break;

        case reg_cgadd:
            cgram_addr_ = static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 1U);
            cgram_latch_high_ = false;
            break;
        case reg_cgdata:
            if (!cgram_latch_high_) {
                cgram_latch_ = value;
                cgram_latch_high_ = true;
            } else {
                const std::uint16_t word = static_cast<std::uint16_t>(
                    cgram_latch_ | (static_cast<std::uint16_t>(value & 0x7FU) << 8U));
                const std::uint16_t idx = static_cast<std::uint16_t>((cgram_addr_ >> 1U) & 0xFFU);
                cgram_[idx] = word;
                cgram_addr_ = static_cast<std::uint16_t>((cgram_addr_ + 2U) & 0x1FFU);
                cgram_latch_high_ = false;
            }
            break;

        default:
            break;
        }
    }

    std::uint8_t s_ppu::read_register(std::uint8_t reg) const noexcept {
        // Most PPU registers are write-only on silicon; return the stored
        // shadow as the reference does (open-bus latch is not modelled).
        return regs_[reg & 0x3FU];
    }

    // ───────────────── Renderer (clean-room character + colour path) ─────────────────

    std::uint16_t s_ppu::vram_word(std::size_t word_index) const noexcept {
        word_index &= (vram_words - 1U);
        if (!vram_ext_.empty()) {
            const std::size_t byte = word_index * 2U;
            if (byte + 1U < vram_ext_.size()) {
                return static_cast<std::uint16_t>(vram_ext_[byte] | (vram_ext_[byte + 1U] << 8U));
            }
            return 0U;
        }
        return vram_[word_index];
    }

    std::uint32_t s_ppu::lookup_rgb(std::size_t index) const noexcept {
        index &= (cgram_words - 1U);
        std::uint16_t entry = 0U;
        if (!cgram_ext_.empty()) {
            const std::size_t byte = index * 2U;
            if (byte + 1U < cgram_ext_.size()) {
                entry =
                    static_cast<std::uint16_t>(cgram_ext_[byte] | (cgram_ext_[byte + 1U] << 8U));
            }
        } else {
            entry = cgram_[index];
        }
        // 15-bit BGR: bbbbbgggggrrrrr.
        const std::uint32_t r = expand5(entry & 0x1FU);
        const std::uint32_t g = expand5((entry >> 5U) & 0x1FU);
        const std::uint32_t b = expand5((entry >> 10U) & 0x1FU);
        const std::uint32_t bright = brightness_;
        return (apply_brightness(r, bright) << 16U) | (apply_brightness(g, bright) << 8U) |
               apply_brightness(b, bright);
    }

    void s_ppu::render_bg1() noexcept {
        // One 4bpp playfield (BG1): a 32x32 tilemap of one-word entries over an
        // 8x8 character grid, scrolled by BG1HOFS/VOFS, decoded through CGRAM.
        const std::uint16_t map_base = static_cast<std::uint16_t>(bg_sc_base_[0] & 0x7FFFU);
        const std::uint16_t char_base = static_cast<std::uint16_t>(bg_char_base_[0] & 0x7FFFU);
        const std::uint32_t scroll_x = bg_hofs_[0];
        const std::uint32_t scroll_y = bg_vofs_[0];
        constexpr std::uint32_t plane_pixels = map_tiles * tile_pixels; // 256

        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint32_t map_y = (y + scroll_y) & (plane_pixels - 1U);
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint32_t map_x = (x + scroll_x) & (plane_pixels - 1U);
                const std::size_t entry =
                    static_cast<std::size_t>(map_base) +
                    (static_cast<std::size_t>(map_y / tile_pixels) * map_tiles +
                     map_x / tile_pixels);
                const std::uint16_t word = vram_word(entry);
                const std::uint32_t tile = word & 0x03FFU;
                const std::uint32_t palette = (word >> 10U) & 0x07U;
                const bool flip_x = (word & 0x4000U) != 0U;
                const bool flip_y = (word & 0x8000U) != 0U;
                const std::uint32_t tx = flip_x ? 7U - (map_x & 7U) : (map_x & 7U);
                const std::uint32_t ty = flip_y ? 7U - (map_y & 7U) : (map_y & 7U);
                // 4bpp character = 16 words: planes 0/1 in words 0..7, planes
                // 2/3 in words 8..15; low byte = even plane, high byte = odd.
                const std::size_t char_word = static_cast<std::size_t>(char_base) + tile * 16U;
                const std::uint16_t lo = vram_word(char_word + ty);
                const std::uint16_t hi = vram_word(char_word + 8U + ty);
                const std::uint32_t bit = 7U - tx;
                std::uint32_t pixel = 0U;
                pixel |= ((lo >> bit) & 1U) << 0U;
                pixel |= ((lo >> (bit + 8U)) & 1U) << 1U;
                pixel |= ((hi >> bit) & 1U) << 2U;
                pixel |= ((hi >> (bit + 8U)) & 1U) << 3U;
                if (pixel == 0U) {
                    continue; // pen 0 is transparent; backdrop shows through
                }
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    lookup_rgb(palette * 16U + pixel);
            }
        }
    }

    void s_ppu::render_frame() noexcept {
        // Force blank / brightness 0 paint black; otherwise the backdrop pen
        // (CGRAM entry 0) fills, then the playfield draws over it.
        if (force_blank_ || brightness_ == 0U) {
            for (std::uint32_t& px : pixels_) {
                px = 0U;
            }
            return;
        }
        const std::uint32_t backdrop = lookup_rgb(0U);
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }
        render_bg1();
    }

    frame_buffer_view s_ppu::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    // ───────────────── State + introspection ─────────────────

    void s_ppu::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.u16(vram_addr_);
        writer.u8(vmain_);
        writer.u16(cgram_addr_);
        writer.u8(cgram_latch_);
        writer.boolean(cgram_latch_high_);
        writer.u16(oam_addr_);
        writer.u8(oam_latch_);
        writer.boolean(oam_latch_high_);
        writer.u8(bg_scroll_latch_);
        writer.u8(inidisp_);
        writer.u8(bgmode_);
        writer.boolean(force_blank_);
        writer.u8(brightness_);
        for (const std::uint16_t v : bg_hofs_) {
            writer.u16(v);
        }
        for (const std::uint16_t v : bg_vofs_) {
            writer.u16(v);
        }
        for (const std::uint16_t v : bg_sc_base_) {
            writer.u16(v);
        }
        for (const std::uint16_t v : bg_char_base_) {
            writer.u16(v);
        }
        for (const std::uint16_t v : vram_) {
            writer.u16(v);
        }
        for (const std::uint16_t v : cgram_) {
            writer.u16(v);
        }
        writer.bytes(oam_);
        writer.bytes(regs_);
    }

    void s_ppu::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        vram_addr_ = reader.u16();
        vmain_ = reader.u8();
        cgram_addr_ = reader.u16();
        cgram_latch_ = reader.u8();
        cgram_latch_high_ = reader.boolean();
        oam_addr_ = reader.u16();
        oam_latch_ = reader.u8();
        oam_latch_high_ = reader.boolean();
        bg_scroll_latch_ = reader.u8();
        inidisp_ = reader.u8();
        bgmode_ = reader.u8();
        force_blank_ = reader.boolean();
        brightness_ = reader.u8();
        for (std::uint16_t& v : bg_hofs_) {
            v = reader.u16();
        }
        for (std::uint16_t& v : bg_vofs_) {
            v = reader.u16();
        }
        for (std::uint16_t& v : bg_sc_base_) {
            v = reader.u16();
        }
        for (std::uint16_t& v : bg_char_base_) {
            v = reader.u16();
        }
        for (std::uint16_t& v : vram_) {
            v = reader.u16();
        }
        for (std::uint16_t& v : cgram_) {
            v = reader.u16();
        }
        reader.bytes(oam_);
        reader.bytes(regs_);
    }

    instrumentation::ichip_introspection& s_ppu::introspection() noexcept { return introspection_; }

    frame_buffer_view s_ppu::introspection_surface::char_sheet_layer::view() const {
        // Decode the first 512 VRAM characters (4bpp) as a 16-per-row grayscale
        // sheet (pixel value 0-15 ramped to white). Rebuilt on each call.
        const s_ppu& v = *owner_;
        constexpr std::uint32_t sheet_chars_per_row = 16U;
        constexpr std::uint32_t sheet_width = sheet_chars_per_row * tile_pixels; // 128
        constexpr std::uint32_t char_count = 512U;
        constexpr std::uint32_t rows = char_count / sheet_chars_per_row;
        v.char_sheet_height_ = rows * tile_pixels;
        v.char_sheet_.assign(static_cast<std::size_t>(sheet_width) * v.char_sheet_height_, 0U);

        for (std::uint32_t ch = 0; ch < char_count; ++ch) {
            const std::uint32_t base_x = (ch % sheet_chars_per_row) * tile_pixels;
            const std::uint32_t base_y = (ch / sheet_chars_per_row) * tile_pixels;
            const std::size_t char_word = static_cast<std::size_t>(ch) * 16U;
            for (std::uint32_t ty = 0; ty < tile_pixels; ++ty) {
                const std::uint16_t lo = v.vram_word(char_word + ty);
                const std::uint16_t hi = v.vram_word(char_word + 8U + ty);
                for (std::uint32_t tx = 0; tx < tile_pixels; ++tx) {
                    const std::uint32_t bit = 7U - tx;
                    std::uint32_t pixel = 0U;
                    pixel |= ((lo >> bit) & 1U) << 0U;
                    pixel |= ((lo >> (bit + 8U)) & 1U) << 1U;
                    pixel |= ((hi >> bit) & 1U) << 2U;
                    pixel |= ((hi >> (bit + 8U)) & 1U) << 3U;
                    const std::uint32_t gray = pixel * 17U; // 0..255 ramp
                    v.char_sheet_[static_cast<std::size_t>(base_y + ty) * sheet_width + base_x +
                                  tx] = (gray << 16U) | (gray << 8U) | gray;
                }
            }
        }
        return {.pixels = v.char_sheet_.data(),
                .width = sheet_width,
                .height = v.char_sheet_height_,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto s_ppu_registration =
            register_factory("nintendo.s_ppu", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<s_ppu>(); });
    } // namespace

} // namespace mnemos::chips::video
