// Ported from the Emu reference (chips/ppu2c02); clean-room per public Ricoh
// 2C02 PPU documentation. The register surface (loopy v/t/x/w, the $2000..$2007
// ports, OAM/palette/VRAM access, the mirroring fold, the NMI gate) is ported
// integer-exact from the reference; the frame renderer (background + sprite
// tile decode through the index palette and the 64-colour master palette) is
// added here per the public PPU documentation the reference defers.

#include "ppu2c02.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // The fixed 64-colour 2C02 master palette as 0x00RRGGBB. These are the
        // canonical NTSC values from the public PPU palette documentation; the
        // index-palette bytes select one of these.
        constexpr std::array<std::uint32_t, 64> master_palette{
            0x626262U, 0x002E98U, 0x0C11C2U, 0x3B00C2U, 0x650098U, 0x7D004EU, 0x7D0000U, 0x651900U,
            0x3B3600U, 0x0C4F00U, 0x005B00U, 0x005900U, 0x00494EU, 0x000000U, 0x000000U, 0x000000U,
            0xABABABU, 0x0064F4U, 0x353CFFU, 0x761BFFU, 0xAE0AF4U, 0xCF0C8FU, 0xCF231CU, 0xAE4700U,
            0x766F00U, 0x359000U, 0x00A100U, 0x009E22U, 0x00888FU, 0x000000U, 0x000000U, 0x000000U,
            0xFFFFFFU, 0x4AB5FFU, 0x858CFFU, 0xC86AFFU, 0xFF58FFU, 0xFF5BE2U, 0xFF726AU, 0xFF9702U,
            0xC8C100U, 0x85E300U, 0x4AF625U, 0x22F476U, 0x22D9E2U, 0x4E4E4EU, 0x000000U, 0x000000U,
            0xFFFFFFU, 0xB6E1FFU, 0xCED1FFU, 0xE9C3FFU, 0xFFBCFFU, 0xFFBDF4U, 0xFFC6C3U, 0xFFD59AU,
            0xE9E681U, 0xCEF481U, 0xB6FB9AU, 0xA9FAC3U, 0xA9F0F4U, 0xB8B8B8U, 0x000000U, 0x000000U,
        };

        [[nodiscard]] std::uint16_t ctrl_vram_increment(std::uint8_t ctrl) noexcept {
            return (ctrl & ppu2c02::ctrl_vram_inc32) != 0U ? 32U : 1U;
        }
    } // namespace

    chip_metadata ppu2c02::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "2C02",
            .family = "2c0x",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void ppu2c02::reset(reset_kind /*kind*/) {
        // Reset clears CTRL/MASK/STATUS, the loopy state, the read buffer, and
        // the write latch; CIRAM/OAM/palette clear on power-on.
        ctrl_ = 0U;
        mask_ = 0U;
        status_ = 0U;
        oam_addr_ = 0U;
        v_ = 0U;
        t_ = 0U;
        x_ = 0U;
        w_ = false;
        read_buffer_ = 0U;
        open_bus_ = 0U;
        nmi_line_ = false;
        dot_ = 0U;
        scanline_ = 0U;
        frame_index_ = 0U;
        oam_.fill(0U);
        palette_.fill(0U);
        nametables_.fill(0U);
        bg_opaque_.fill(0U);
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void ppu2c02::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (dot_ == 0U) {
                // Frame-boundary events first, so the status clear precedes the
                // render of line 0.
                if (scanline_ == vblank_line) {
                    // The frame finished rendering line-by-line below; enter vblank.
                    status_ |= status_vblank;
                    refresh_nmi_line();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(scanline_);
                    }
                } else if (scanline_ == 0U) {
                    status_ &= static_cast<std::uint8_t>(
                        ~(status_vblank | status_spr0_hit | status_spr_over));
                    refresh_nmi_line();
                }

                // Render the visible scanline from the live loopy address, so a
                // mid-frame CHR-bank / scroll change takes effect from the next
                // line. A mapper IRQ (clocked by scanline_cb below) thus affects
                // line+1, matching the hardware split point.
                if (scanline_ < visible_height) {
                    spr0_hit_dot_ = -1;
                    const std::size_t row = static_cast<std::size_t>(scanline_) * visible_width;
                    if ((mask_ & mask_bg_enable) != 0U) {
                        render_bg_scanline(scanline_, v_);
                    } else {
                        const std::uint32_t backdrop = master_rgb(palette_[0]);
                        for (std::uint32_t sx = 0; sx < visible_width; ++sx) {
                            pixels_[row + sx] = backdrop;
                            bg_opaque_[sx] = 0U;
                        }
                    }
                    if ((mask_ & mask_spr_enable) != 0U) {
                        render_sprites_scanline(scanline_);
                    }
                }

                if (scanline_cb_) {
                    scanline_cb_(scanline_);
                }
            }

            // Dot-scheduled sprite-0 hit: set the flag when the beam reaches the
            // first sprite-0/opaque-BG overlap dot on this visible line.
            if (scanline_ < visible_height && spr0_hit_dot_ >= 0 &&
                dot_ == static_cast<std::uint32_t>(spr0_hit_dot_)) {
                status_ |= status_spr0_hit;
            }

            // Loopy address updates during rendering (visible + pre-render lines).
            if (rendering_enabled()) {
                const bool render_line =
                    scanline_ < visible_height || scanline_ == lines_per_frame - 1U;
                if (render_line) {
                    if (dot_ == 256U) {
                        inc_y(v_);
                    } else if (dot_ == 257U) {
                        copy_horizontal(v_, t_);
                    }
                }
                if (scanline_ == lines_per_frame - 1U && dot_ >= 280U && dot_ <= 304U) {
                    copy_vertical(v_, t_);
                }
            }

            if (++dot_ == dots_per_line) {
                dot_ = 0U;
                if (++scanline_ == lines_per_frame) {
                    scanline_ = 0U;
                }
            }
        }
    }

    frame_buffer_view ppu2c02::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    // ───────────────── Mirroring / palette folds ─────────────────

    std::uint16_t ppu2c02::nametable_index(std::uint16_t addr) const noexcept {
        // addr is in $2000..$2FFF (the caller strips the $3000 mirror).
        const std::uint16_t nt_sel = static_cast<std::uint16_t>((addr >> 10U) & 0x03U);
        const std::uint16_t offset = static_cast<std::uint16_t>(addr & 0x03FFU);
        std::uint16_t bank = 0U;
        switch (mirroring_) {
        case mirroring::horizontal:
            bank = static_cast<std::uint16_t>((nt_sel >> 1U) & 0x01U);
            break;
        case mirroring::vertical:
            bank = static_cast<std::uint16_t>(nt_sel & 0x01U);
            break;
        case mirroring::single_a:
            bank = 0U;
            break;
        case mirroring::single_b:
            bank = 1U;
            break;
        case mirroring::four_screen:
            bank = static_cast<std::uint16_t>(nt_sel & 0x01U);
            break;
        }
        return static_cast<std::uint16_t>((bank << 10U) | offset);
    }

    std::uint16_t ppu2c02::palette_index(std::uint16_t addr) noexcept {
        std::uint16_t i = static_cast<std::uint16_t>(addr & 0x1FU);
        if ((i & 0x13U) == 0x10U) {
            // $10/$14/$18/$1C mirror to $00/$04/$08/$0C.
            i &= 0x0FU;
        }
        return i;
    }

    // ───────────────── PPU bus read / write ─────────────────

    std::uint8_t ppu2c02::ppu_read(std::uint16_t addr) const noexcept {
        addr &= 0x3FFFU;
        if (addr < 0x2000U) {
            if (addr < chr_.size()) {
                return chr_[addr];
            }
            return 0U;
        }
        if (addr < 0x3F00U) {
            const std::uint16_t folded = static_cast<std::uint16_t>(addr & 0x2FFFU);
            const std::uint16_t index = nametable_index(folded);
            // A four-screen board with extra CIRAM exposes it as an external
            // span; otherwise the chip's own 2 KB CIRAM backs the access.
            if (mirroring_ == mirroring::four_screen && index < external_nametables_.size()) {
                return external_nametables_[index];
            }
            return nametables_[index % nametable_size];
        }
        return palette_[palette_index(addr)];
    }

    void ppu2c02::ppu_write(std::uint16_t addr, std::uint8_t value) noexcept {
        addr &= 0x3FFFU;
        if (addr < 0x2000U) {
            // CHR-ROM drops writes; a CHR-RAM cart attaches a writable window.
            if (addr < chr_ram_.size()) {
                chr_ram_[addr] = value;
            }
            return;
        }
        if (addr < 0x3F00U) {
            const std::uint16_t folded = static_cast<std::uint16_t>(addr & 0x2FFFU);
            nametables_[nametable_index(folded) % nametable_size] = value;
            return;
        }
        palette_[palette_index(addr)] = static_cast<std::uint8_t>(value & 0x3FU);
    }

    // ───────────────── NMI gate ─────────────────

    void ppu2c02::refresh_nmi_line() noexcept {
        const bool vblank = (status_ & status_vblank) != 0U;
        const bool enabled = (ctrl_ & ctrl_nmi_enable) != 0U;
        nmi_line_ = vblank && enabled;
    }

    // ───────────────── Register read ─────────────────

    std::uint8_t ppu2c02::reg_read(std::uint8_t reg) noexcept {
        reg &= 0x07U;
        switch (reg) {
        case reg_status: {
            const std::uint8_t value =
                static_cast<std::uint8_t>((status_ & 0xE0U) | (open_bus_ & 0x1FU));
            // Reading STATUS clears vblank and resets the write latch.
            status_ &= static_cast<std::uint8_t>(~status_vblank);
            w_ = false;
            refresh_nmi_line();
            open_bus_ = value;
            return value;
        }
        case reg_oamdata:
            open_bus_ = oam_[oam_addr_];
            return open_bus_;
        case reg_data: {
            const std::uint16_t addr = static_cast<std::uint16_t>(v_ & 0x3FFFU);
            std::uint8_t value{};
            if (addr >= 0x3F00U) {
                // Palette reads are immediate; the read buffer still refills
                // from the underlying nametable mirror for the next read.
                value = palette_[palette_index(addr)];
                read_buffer_ = ppu_read(static_cast<std::uint16_t>(addr & 0x2FFFU));
            } else {
                value = read_buffer_;
                read_buffer_ = ppu_read(addr);
            }
            v_ = static_cast<std::uint16_t>((v_ + ctrl_vram_increment(ctrl_)) & 0x7FFFU);
            open_bus_ = value;
            return value;
        }
        default:
            // $2000/$2001/$2003/$2005/$2006 are write-only -> open-bus latch.
            return open_bus_;
        }
    }

    // ───────────────── Register write ─────────────────

    void ppu2c02::reg_write(std::uint8_t reg, std::uint8_t value) noexcept {
        reg &= 0x07U;
        open_bus_ = value;
        switch (reg) {
        case reg_ctrl:
            ctrl_ = value;
            // t bits 10-11 pick up the base nametable select.
            t_ = static_cast<std::uint16_t>((t_ & 0x73FFU) |
                                            (static_cast<std::uint16_t>(value & 0x03U) << 10U));
            refresh_nmi_line();
            break;
        case reg_mask:
            mask_ = value;
            break;
        case reg_status:
            // Writes are ignored.
            break;
        case reg_oamaddr:
            oam_addr_ = value;
            break;
        case reg_oamdata:
            oam_[oam_addr_] = value;
            oam_addr_ = static_cast<std::uint8_t>(oam_addr_ + 1U);
            break;
        case reg_scroll:
            if (!w_) {
                // First write: X scroll -- t: coarse X, x: fine X.
                t_ = static_cast<std::uint16_t>((t_ & 0x7FE0U) | ((value >> 3U) & 0x1FU));
                x_ = static_cast<std::uint8_t>(value & 0x07U);
                w_ = true;
            } else {
                // Second write: Y scroll -- t: coarse Y + fine Y.
                std::uint16_t t = t_;
                t = static_cast<std::uint16_t>((t & 0x0C1FU) |
                                               (static_cast<std::uint16_t>(value & 0xF8U) << 2U) |
                                               (static_cast<std::uint16_t>(value & 0x07U) << 12U));
                t_ = t;
                w_ = false;
            }
            break;
        case reg_addr:
            if (!w_) {
                // First write: high 6 bits. Bit 14 is cleared.
                t_ = static_cast<std::uint16_t>((t_ & 0x00FFU) |
                                                (static_cast<std::uint16_t>(value & 0x3FU) << 8U));
                w_ = true;
            } else {
                t_ = static_cast<std::uint16_t>((t_ & 0x7F00U) | value);
                v_ = t_;
                w_ = false;
            }
            break;
        case reg_data:
            ppu_write(static_cast<std::uint16_t>(v_ & 0x3FFFU), value);
            v_ = static_cast<std::uint16_t>((v_ + ctrl_vram_increment(ctrl_)) & 0x7FFFU);
            break;
        default:
            break;
        }
    }

    // ───────────────── Frame renderer ─────────────────

    std::uint32_t ppu2c02::master_rgb(std::uint8_t index) noexcept {
        return master_palette[index & 0x3FU];
    }

    std::uint32_t ppu2c02::fetch_pattern_pixel(std::uint16_t pattern_base, std::uint32_t tile,
                                               std::uint32_t fine_x,
                                               std::uint32_t fine_y) const noexcept {
        const std::uint16_t row =
            static_cast<std::uint16_t>(pattern_base + tile * tile_bytes + fine_y);
        const std::uint8_t plane0 = ppu_read(row);
        const std::uint8_t plane1 = ppu_read(static_cast<std::uint16_t>(row + 8U));
        const std::uint32_t bit = 7U - (fine_x & 7U);
        return ((plane0 >> bit) & 1U) | (((plane1 >> bit) & 1U) << 1U);
    }

    // ───────────────── Loopy scroll-address mechanics ─────────────────

    void ppu2c02::inc_coarse_x(std::uint16_t& v) noexcept {
        if ((v & 0x001FU) == 0x001FU) {
            v = static_cast<std::uint16_t>((v & ~0x001FU) ^ 0x0400U); // wrap + flip h nametable
        } else {
            v = static_cast<std::uint16_t>(v + 1U);
        }
    }

    void ppu2c02::inc_y(std::uint16_t& v) noexcept {
        if ((v & 0x7000U) != 0x7000U) {
            v = static_cast<std::uint16_t>(v + 0x1000U); // fine Y++
            return;
        }
        v = static_cast<std::uint16_t>(v & ~0x7000U); // fine Y = 0
        std::uint16_t y = static_cast<std::uint16_t>((v & 0x03E0U) >> 5U);
        if (y == 29U) {
            y = 0U;
            v = static_cast<std::uint16_t>(v ^ 0x0800U); // flip v nametable
        } else if (y == 31U) {
            y = 0U; // wrap inside the attribute region without flipping
        } else {
            ++y;
        }
        v = static_cast<std::uint16_t>((v & ~0x03E0U) | (y << 5U));
    }

    void ppu2c02::copy_horizontal(std::uint16_t& v, std::uint16_t t) noexcept {
        v = static_cast<std::uint16_t>((v & ~0x041FU) | (t & 0x041FU)); // coarse X + h nametable
    }

    void ppu2c02::copy_vertical(std::uint16_t& v, std::uint16_t t) noexcept {
        v = static_cast<std::uint16_t>((v & ~0x7BE0U) |
                                       (t & 0x7BE0U)); // fine/coarse Y + v nametable
    }

    // ───────────────── Per-scanline renderer ─────────────────

    void ppu2c02::render_bg_scanline(std::uint32_t sy, std::uint16_t line_v) noexcept {
        const std::size_t row = static_cast<std::size_t>(sy) * visible_width;
        const std::uint32_t backdrop = master_rgb(palette_[0]);
        const std::uint16_t pattern_base =
            static_cast<std::uint16_t>((ctrl_ & ctrl_bg_pt) != 0U ? 0x1000U : 0x0000U);
        const bool left_clip = (mask_ & mask_bg_left) == 0U;
        std::uint16_t lv = line_v;
        const std::uint16_t fine_y = static_cast<std::uint16_t>((lv >> 12U) & 0x07U);
        std::uint32_t fx = x_; // fine X into the current tile

        // The current tile's two bitplanes + palette, reloaded each 8 px.
        std::uint8_t plane0 = 0U;
        std::uint8_t plane1 = 0U;
        std::uint32_t pal = 0U;
        const auto load_tile = [&]() {
            const std::uint8_t tile =
                ppu_read(static_cast<std::uint16_t>(0x2000U | (lv & 0x0FFFU)));
            const std::uint16_t paddr =
                static_cast<std::uint16_t>(pattern_base + tile * tile_bytes + fine_y);
            plane0 = ppu_read(paddr);
            plane1 = ppu_read(static_cast<std::uint16_t>(paddr + 8U));
            const std::uint8_t attr = ppu_read(static_cast<std::uint16_t>(
                0x23C0U | (lv & 0x0C00U) | ((lv >> 4U) & 0x38U) | ((lv >> 2U) & 0x07U)));
            const std::uint32_t shift = ((lv >> 4U) & 0x04U) | (lv & 0x02U);
            pal = (attr >> shift) & 0x03U;
        };
        load_tile();

        for (std::uint32_t sx = 0; sx < visible_width; ++sx) {
            const std::uint32_t bit = 7U - fx;
            const std::uint32_t pixel = ((plane0 >> bit) & 1U) | (((plane1 >> bit) & 1U) << 1U);
            if (pixel == 0U || (left_clip && sx < 8U)) {
                bg_opaque_[sx] = 0U;
                pixels_[row + sx] = backdrop;
            } else {
                bg_opaque_[sx] = 1U;
                pixels_[row + sx] = master_rgb(palette_[(pal * 4U + pixel) & 0x1FU]);
            }
            if (++fx == 8U) {
                fx = 0U;
                inc_coarse_x(lv);
                load_tile();
            }
        }
    }

    void ppu2c02::render_sprites_scanline(std::uint32_t sy) noexcept {
        const bool tall = (ctrl_ & ctrl_spr_size16) != 0U;
        const std::uint32_t height = tall ? 16U : 8U;
        const bool left_clip = (mask_ & mask_spr_left) == 0U;
        const std::size_t row = static_cast<std::size_t>(sy) * visible_width;

        // Sprite evaluation: walk OAM in order and keep the first eight sprites that
        // cover this line (the hardware's 8-entry secondary OAM). A ninth in-range
        // sprite sets the overflow flag and is dropped -- this is what produces the
        // per-line sprite flicker games are designed around.
        std::array<std::uint8_t, 8> line_sprites{};
        std::size_t sprite_count = 0U;
        for (std::size_t s = 0; s < 64U; ++s) {
            const std::uint32_t spr_y = oam_[s * 4U];
            if (spr_y >= 0xEFU) {
                continue; // off-screen Y
            }
            const std::int32_t rin =
                static_cast<std::int32_t>(sy) - static_cast<std::int32_t>(spr_y + 1U);
            if (rin < 0 || rin >= static_cast<std::int32_t>(height)) {
                continue; // this sprite doesn't cover line sy
            }
            if (sprite_count == line_sprites.size()) {
                status_ |= status_spr_over; // a ninth in-range sprite: overflow
                break;
            }
            line_sprites[sprite_count++] = static_cast<std::uint8_t>(s);
        }

        // Draw the selected sprites back-to-front (highest OAM index first) so the
        // lower index -- higher priority -- lands on top, and sprite 0 stays frontmost.
        for (std::size_t i = sprite_count; i-- > 0U;) {
            const std::size_t s = line_sprites[i];
            const std::size_t base = s * 4U;
            const std::uint32_t spr_y = oam_[base + 0U];
            const std::int32_t rin =
                static_cast<std::int32_t>(sy) - static_cast<std::int32_t>(spr_y + 1U);
            const std::uint32_t tile_raw = oam_[base + 1U];
            const std::uint8_t attr = oam_[base + 2U];
            const std::uint32_t spr_x = oam_[base + 3U];
            const std::uint32_t pal = attr & 0x03U;
            const bool behind = (attr & 0x20U) != 0U;
            const bool flip_x = (attr & 0x40U) != 0U;
            const bool flip_y = (attr & 0x80U) != 0U;

            std::uint16_t pattern_base;
            std::uint32_t tile_index;
            if (tall) {
                pattern_base =
                    static_cast<std::uint16_t>((tile_raw & 1U) != 0U ? 0x1000U : 0x0000U);
                tile_index = tile_raw & 0xFEU;
            } else {
                pattern_base =
                    static_cast<std::uint16_t>((ctrl_ & ctrl_spr_pt) != 0U ? 0x1000U : 0x0000U);
                tile_index = tile_raw;
            }
            const std::uint32_t src_row = flip_y ? (height - 1U - static_cast<std::uint32_t>(rin))
                                                 : static_cast<std::uint32_t>(rin);
            const std::uint32_t cell = tile_index + (src_row >= 8U ? 1U : 0U);
            const std::uint32_t fine_y = src_row & 7U;

            for (std::uint32_t col = 0; col < 8U; ++col) {
                const std::uint32_t dx = spr_x + col;
                if (dx >= visible_width) {
                    continue;
                }
                if (left_clip && dx < 8U) {
                    continue;
                }
                const std::uint32_t fine_x = flip_x ? (7U - col) : col;
                const std::uint32_t pixel = fetch_pattern_pixel(pattern_base, cell, fine_x, fine_y);
                if (pixel == 0U) {
                    continue; // transparent
                }
                // Sprite-0 hit: an opaque sprite-0 pixel over opaque background,
                // never at x=255. Scheduled to the beam dot it is drawn at.
                if (s == 0U && bg_opaque_[dx] != 0U && dx != 255U) {
                    const int hit_dot = static_cast<int>(dx) + 1;
                    if (spr0_hit_dot_ < 0 || hit_dot < spr0_hit_dot_) {
                        spr0_hit_dot_ = hit_dot;
                    }
                }
                if (behind && bg_opaque_[dx] != 0U) {
                    continue; // background-priority sprite loses to opaque BG
                }
                pixels_[row + dx] = master_rgb(palette_[0x10U + ((pal * 4U + pixel) & 0x0FU)]);
            }
        }
    }

    void ppu2c02::render_frame() noexcept {
        // A static force-render: simulate the per-line loopy walk from t_ without
        // disturbing the live beam (v_, dot_, scanline_). Used by tests / hosts
        // that want the current frame without advancing time.
        const std::uint32_t backdrop = master_rgb(palette_[0]);
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }
        const bool bg = (mask_ & mask_bg_enable) != 0U;
        const bool spr = (mask_ & mask_spr_enable) != 0U;
        spr0_hit_dot_ = -1;
        std::uint16_t sim_v = t_;
        for (std::uint32_t sy = 0; sy < visible_height; ++sy) {
            if (bg) {
                render_bg_scanline(sy, sim_v);
            } else {
                bg_opaque_.fill(0U);
            }
            if (spr) {
                render_sprites_scanline(sy);
            }
            inc_y(sim_v);
            copy_horizontal(sim_v, t_);
        }
        if (spr0_hit_dot_ >= 0) {
            status_ |= status_spr0_hit; // a held-state render reflects the hit
        }
    }

    // ───────────────── Save state ─────────────────

    void ppu2c02::save_state(state_writer& writer) const {
        writer.u8(ctrl_);
        writer.u8(mask_);
        writer.u8(status_);
        writer.u8(oam_addr_);
        writer.u16(v_);
        writer.u16(t_);
        writer.u8(x_);
        writer.boolean(w_);
        writer.u8(read_buffer_);
        writer.u8(open_bus_);
        writer.u8(static_cast<std::uint8_t>(mirroring_));
        writer.boolean(nmi_line_);
        writer.u32(dot_);
        writer.u32(scanline_);
        writer.u64(frame_index_);
        writer.bytes(oam_);
        writer.bytes(palette_);
        writer.bytes(nametables_);
    }

    void ppu2c02::load_state(state_reader& reader) {
        ctrl_ = reader.u8();
        mask_ = reader.u8();
        status_ = reader.u8();
        oam_addr_ = reader.u8();
        v_ = reader.u16();
        t_ = reader.u16();
        x_ = reader.u8();
        w_ = reader.boolean();
        read_buffer_ = reader.u8();
        open_bus_ = reader.u8();
        mirroring_ = static_cast<mirroring>(reader.u8());
        nmi_line_ = reader.boolean();
        dot_ = reader.u32();
        scanline_ = reader.u32();
        frame_index_ = reader.u64();
        reader.bytes(oam_);
        reader.bytes(palette_);
        reader.bytes(nametables_);
    }

    instrumentation::ichip_introspection& ppu2c02::introspection() noexcept {
        return introspection_;
    }

    frame_buffer_view ppu2c02::introspection_surface::pattern_sheet_layer::view() const {
        // Decode the attached CHR as a 16-tiles-per-row greyscale sheet (pixel
        // value 0..3 ramped to white). Both 4 KB pattern tables stacked.
        const ppu2c02& p = *owner_;
        constexpr std::uint32_t sheet_tiles_per_row = 16U;
        constexpr std::uint32_t sheet_width = sheet_tiles_per_row * 8U;

        const std::size_t tile_count = p.chr_.size() / tile_bytes;
        const std::uint32_t rows = static_cast<std::uint32_t>(
            (tile_count + sheet_tiles_per_row - 1U) / sheet_tiles_per_row);
        const std::uint32_t sheet_height = std::max<std::uint32_t>(rows * 8U, 8U);
        p.pattern_sheet_.assign(static_cast<std::size_t>(sheet_width) * sheet_height, 0U);

        for (std::size_t tile = 0; tile < tile_count; ++tile) {
            const std::uint32_t base_x =
                static_cast<std::uint32_t>(tile % sheet_tiles_per_row) * 8U;
            const std::uint32_t base_y =
                static_cast<std::uint32_t>(tile / sheet_tiles_per_row) * 8U;
            for (std::uint32_t ty = 0; ty < 8U; ++ty) {
                const std::uint8_t plane0 = p.chr_[tile * tile_bytes + ty];
                const std::uint8_t plane1 = p.chr_[tile * tile_bytes + ty + 8U];
                for (std::uint32_t tx = 0; tx < 8U; ++tx) {
                    const std::uint32_t bit = 7U - tx;
                    const std::uint32_t pixel =
                        ((plane0 >> bit) & 1U) | (((plane1 >> bit) & 1U) << 1U);
                    const std::uint32_t gray = pixel * 85U; // 0..255 ramp
                    p.pattern_sheet_[static_cast<std::size_t>(base_y + ty) * sheet_width + base_x +
                                     tx] = (gray << 16U) | (gray << 8U) | gray;
                }
            }
        }
        return {.pixels = p.pattern_sheet_.data(),
                .width = sheet_width,
                .height = sheet_height,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto ppu2c02_registration =
            register_factory("ricoh.2c02", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<ppu2c02>();
            });
    } // namespace

} // namespace mnemos::chips::video
