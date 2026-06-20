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
                if (scanline_ == vblank_line) {
                    // Entering vblank: render the completed frame, raise the
                    // vblank flag + NMI gate, and bump the frame counter.
                    render_frame();
                    status_ |= status_vblank;
                    refresh_nmi_line();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(scanline_);
                    }
                } else if (scanline_ == 0U) {
                    // Pre-render boundary: vblank / sprite-0 / overflow clear.
                    status_ &= static_cast<std::uint8_t>(
                        ~(status_vblank | status_spr0_hit | status_spr_over));
                    refresh_nmi_line();
                }
                if (scanline_cb_) {
                    scanline_cb_(scanline_);
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

    void ppu2c02::render_background() noexcept {
        const std::uint16_t pattern_base =
            static_cast<std::uint16_t>((ctrl_ & ctrl_bg_pt) != 0U ? 0x1000U : 0x0000U);
        // Scroll origin from the loopy t register plus fine X.
        const std::uint32_t coarse_x0 = t_ & 0x1FU;
        const std::uint32_t coarse_y0 = (t_ >> 5U) & 0x1FU;
        const std::uint32_t fine_y0 = (t_ >> 12U) & 0x07U;
        const std::uint32_t nt_x0 = (t_ >> 10U) & 0x01U;
        const std::uint32_t nt_y0 = (t_ >> 11U) & 0x01U;
        const bool left_clip = (mask_ & mask_bg_left) == 0U;

        for (std::uint32_t sy = 0; sy < visible_height; ++sy) {
            // Walk the scroll position across the 2x2 nametable torus.
            const std::uint32_t abs_y = sy + coarse_y0 * 8U + fine_y0 + nt_y0 * 240U;
            const std::uint32_t world_y = abs_y % 480U; // two 240-tall screens
            const std::uint32_t ty = (world_y % 240U) / 8U;
            const std::uint32_t fine_y = world_y % 8U;
            const std::uint32_t nt_y = world_y / 240U;
            for (std::uint32_t sx = 0; sx < visible_width; ++sx) {
                const std::uint32_t abs_x = sx + coarse_x0 * 8U + x_ + nt_x0 * 256U;
                const std::uint32_t world_x = abs_x % 512U; // two 256-wide screens
                const std::uint32_t tx = (world_x % 256U) / 8U;
                const std::uint32_t fine_x = world_x % 8U;
                const std::uint32_t nt_x = world_x / 256U;

                const std::uint16_t nt_base =
                    static_cast<std::uint16_t>(0x2000U + ((nt_y << 11U) | (nt_x << 10U)));
                const std::uint16_t name_addr = static_cast<std::uint16_t>(nt_base + ty * 32U + tx);
                const std::uint32_t tile = ppu_read(name_addr);
                const std::uint32_t pixel = fetch_pattern_pixel(pattern_base, tile, fine_x, fine_y);

                const std::size_t out = static_cast<std::size_t>(sy) * visible_width + sx;
                if (pixel == 0U || (left_clip && sx < 8U)) {
                    // Transparent (or clipped left column): the backdrop colour.
                    bg_opaque_[out] = 0U;
                    pixels_[out] = master_rgb(palette_[0]);
                    continue;
                }
                // Attribute byte: one per 4x4-tile region, 2 bits per 2x2 quad.
                const std::uint16_t attr_addr =
                    static_cast<std::uint16_t>(nt_base + 0x3C0U + (ty / 4U) * 8U + (tx / 4U));
                const std::uint8_t attr = ppu_read(attr_addr);
                const std::uint32_t quad =
                    ((ty & 2U) != 0U ? 2U : 0U) | ((tx & 2U) != 0U ? 1U : 0U);
                const std::uint32_t pal = (attr >> (quad * 2U)) & 0x03U;
                const std::uint8_t entry = palette_[(pal * 4U + pixel) & 0x1FU];
                bg_opaque_[out] = 1U;
                pixels_[out] = master_rgb(entry);
            }
        }
    }

    void ppu2c02::render_sprites() noexcept {
        const bool tall = (ctrl_ & ctrl_spr_size16) != 0U;
        const std::uint32_t height = tall ? 16U : 8U;
        const bool left_clip = (mask_ & mask_spr_left) == 0U;

        // Lower OAM index = higher priority; draw high indices first so index 0
        // lands on top.
        for (std::size_t s = 64U; s-- > 0U;) {
            const std::size_t base = s * 4U;
            const std::uint32_t spr_y = oam_[base + 0U];
            const std::uint32_t tile_raw = oam_[base + 1U];
            const std::uint8_t attr = oam_[base + 2U];
            const std::uint32_t spr_x = oam_[base + 3U];
            if (spr_y >= 0xEFU) {
                continue; // off-screen Y (>= 239)
            }
            const std::uint32_t pal = attr & 0x03U;
            const bool behind = (attr & 0x20U) != 0U;
            const bool flip_x = (attr & 0x40U) != 0U;
            const bool flip_y = (attr & 0x80U) != 0U;

            // 8x16 sprites take the pattern table from the tile's bit 0 and use
            // an even/odd tile pair; 8x8 sprites use the CTRL-selected table.
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

            for (std::uint32_t row = 0; row < height; ++row) {
                const std::int32_t dest_y = static_cast<std::int32_t>(spr_y + row + 1U);
                if (dest_y < 0 || dest_y >= static_cast<std::int32_t>(visible_height)) {
                    continue;
                }
                const std::uint32_t src_row = flip_y ? (height - 1U - row) : row;
                // For 8x16 the second cell follows the first in pattern memory.
                const std::uint32_t cell = tile_index + (src_row >= 8U ? 1U : 0U);
                const std::uint32_t fine_y = src_row & 7U;
                for (std::uint32_t col = 0; col < 8U; ++col) {
                    const std::int32_t dest_x = static_cast<std::int32_t>(spr_x + col);
                    if (dest_x < 0 || dest_x >= static_cast<std::int32_t>(visible_width)) {
                        continue;
                    }
                    if (left_clip && dest_x < 8) {
                        continue;
                    }
                    const std::uint32_t fine_x = flip_x ? (7U - col) : col;
                    const std::uint32_t pixel =
                        fetch_pattern_pixel(pattern_base, cell, fine_x, fine_y);
                    if (pixel == 0U) {
                        continue; // transparent
                    }
                    const std::size_t out = static_cast<std::size_t>(dest_y) * visible_width +
                                            static_cast<std::size_t>(dest_x);
                    // Sprite 0 hit: the first non-transparent sprite-0 pixel
                    // over an opaque background pixel.
                    if (s == 0U && bg_opaque_[out] != 0U) {
                        status_ |= status_spr0_hit;
                    }
                    // Background-priority sprites lose to opaque background.
                    if (behind && bg_opaque_[out] != 0U) {
                        continue;
                    }
                    const std::uint8_t entry = palette_[0x10U + ((pal * 4U + pixel) & 0x0FU)];
                    pixels_[out] = master_rgb(entry);
                }
            }
        }
    }

    void ppu2c02::render_frame() noexcept {
        // Backdrop fill so a disabled background still shows palette entry 0.
        const std::uint32_t backdrop = master_rgb(palette_[0]);
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }
        bg_opaque_.fill(0U);
        if ((mask_ & mask_bg_enable) != 0U) {
            render_background();
        }
        if ((mask_ & mask_spr_enable) != 0U) {
            render_sprites();
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
