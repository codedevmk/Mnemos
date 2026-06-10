#include "sms_vdp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::chips::video {
    namespace {
        using regs = std::array<std::uint8_t, sms_vdp::register_count>;

        // Register decode helpers (Mode 4).
        bool reg_line_int_en(const regs& r) { return (r[0] & 0x10U) != 0U; }
        bool reg_blank_left(const regs& r) { return (r[0] & 0x20U) != 0U; }
        bool reg_lock_hscrl(const regs& r) { return (r[0] & 0x40U) != 0U; }
        bool reg_lock_vscrl(const regs& r) { return (r[0] & 0x80U) != 0U; }
        bool reg_disp_en(const regs& r) { return (r[1] & 0x40U) != 0U; }
        bool reg_frame_int_en(const regs& r) { return (r[1] & 0x20U) != 0U; }
        bool reg_tall_sprites(const regs& r) { return (r[1] & 0x02U) != 0U; }
        bool reg_zoom_sprites(const regs& r) { return (r[1] & 0x01U) != 0U; }
        std::uint16_t reg_nt_base(const regs& r) {
            return static_cast<std::uint16_t>((r[2] & 0x0EU) << 10U);
        }
        std::uint16_t reg_sat_base(const regs& r) {
            return static_cast<std::uint16_t>((r[5] & 0x7EU) << 7U);
        }
        std::uint16_t reg_spr_base(const regs& r) {
            return static_cast<std::uint16_t>((r[6] & 0x04U) << 11U);
        }
        std::uint8_t reg_bg_color(const regs& r) {
            return static_cast<std::uint8_t>(16U + (r[7] & 0x0FU));
        }

        // SMS CRAM (--BBGGRR, 2 bits/channel) to 0x00RRGGBB.
        std::uint32_t cram_rgb(std::uint8_t c) {
            static constexpr std::array<std::uint8_t, 4> lut = {0U, 85U, 170U, 255U};
            const std::uint32_t r = lut[c & 3U];
            const std::uint32_t g = lut[(c >> 2U) & 3U];
            const std::uint32_t b = lut[(c >> 4U) & 3U];
            return (r << 16U) | (g << 8U) | b;
        }

        // Game Gear CRAM (16-bit ----BBBBGGGGRRRR, 4 bits/channel) to 0x00RRGGBB.
        std::uint32_t cram_rgb_gg(std::uint16_t c) {
            const auto chan = [](std::uint32_t v) { return (v & 0xFU) * 0x11U; };
            const std::uint32_t r = chan(c);
            const std::uint32_t g = chan(c >> 4U);
            const std::uint32_t b = chan(c >> 8U);
            return (r << 16U) | (g << 8U) | b;
        }

        // Decode an 8-pixel tile row (4 planar bytes) into 4-bit colour indices.
        void decode_tile_row(std::span<const std::uint8_t> vram, int tile_idx, int row, bool hflip,
                             std::array<std::uint8_t, 8>& pix) {
            const std::uint32_t addr =
                static_cast<std::uint32_t>(tile_idx) * 32U + static_cast<std::uint32_t>(row) * 4U;
            const std::uint8_t p0 = vram[addr & 0x3FFFU];
            const std::uint8_t p1 = vram[(addr + 1U) & 0x3FFFU];
            const std::uint8_t p2 = vram[(addr + 2U) & 0x3FFFU];
            const std::uint8_t p3 = vram[(addr + 3U) & 0x3FFFU];
            for (int c = 0; c < 8; ++c) {
                const int bit = hflip ? c : (7 - c);
                pix[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    ((p0 >> bit) & 1U) | (((p1 >> bit) & 1U) << 1U) | (((p2 >> bit) & 1U) << 2U) |
                    (((p3 >> bit) & 1U) << 3U));
            }
        }
    } // namespace

    chip_metadata sms_vdp::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "315-5124",
            .family = "VDP",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    int sms_vdp::visible_height() const noexcept {
        const std::uint8_t mode = reg_[1] & 0x18U;
        if (mode == 0x08U && pal_mode_) {
            return 224;
        }
        if (mode == 0x18U && pal_mode_) {
            return 240;
        }
        return 192;
    }

    void sms_vdp::set_pal(bool pal) noexcept {
        pal_mode_ = pal;
        total_scanlines_ = pal ? scanlines_pal : scanlines_ntsc;
    }

    void sms_vdp::set_gg(bool gg) noexcept {
        gg_mode_ = gg;
        cram_latch_ = 0U; // a mode toggle can't commit a stale latched low byte
    }

    std::uint32_t sms_vdp::palette_rgb(std::uint8_t index) const noexcept {
        const std::size_t e = static_cast<std::size_t>(index & 0x1FU);
        if (gg_mode_) {
            const std::uint16_t entry = static_cast<std::uint16_t>(
                gg_cram_[e * 2U] | (static_cast<std::uint16_t>(gg_cram_[e * 2U + 1U]) << 8U));
            return cram_rgb_gg(entry);
        }
        return cram_rgb(cram_[e]);
    }

    // ---- control / data ports ----

    void sms_vdp::ctrl_write(std::uint8_t value) noexcept {
        if (!cmd_pending_) {
            cmd_first_ = value;
            cmd_pending_ = true;
            addr_ = static_cast<std::uint16_t>((addr_ & 0x3F00U) | value);
        } else {
            cmd_pending_ = false;
            addr_ = static_cast<std::uint16_t>(((value & 0x3FU) << 8U) | cmd_first_);
            code_ = static_cast<std::uint8_t>((value >> 6U) & 3U);
            switch (code_) {
            case 0: // VRAM read prime
                read_buffer_ = vram_[addr_ & 0x3FFFU];
                addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
                break;
            case 2: { // register write
                const int r = value & 0x0FU;
                if (r < register_count) {
                    reg_[static_cast<std::size_t>(r)] = cmd_first_;
                }
                break;
            }
            default: // 1 = VRAM write, 3 = CRAM write (data port carries the bytes)
                break;
            }
        }
    }

    std::uint8_t sms_vdp::ctrl_read() noexcept {
        const std::uint8_t s = status_;
        cmd_pending_ = false;
        status_ = 0U;
        frame_irq_pending_ = false;
        line_irq_pending_ = false;
        update_irq();
        return s;
    }

    std::uint8_t sms_vdp::data_read() noexcept {
        cmd_pending_ = false;
        const std::uint8_t v = read_buffer_;
        read_buffer_ = vram_[addr_ & 0x3FFFU];
        addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
        return v;
    }

    void sms_vdp::data_write(std::uint8_t value) noexcept {
        cmd_pending_ = false;
        if (code_ == 3U) {
            if (gg_mode_) {
                // GG CRAM is 32 x 16-bit BGR444: the even byte latches the low half,
                // the odd byte commits the full entry.
                if ((addr_ & 1U) == 0U) {
                    cram_latch_ = value;
                } else {
                    gg_cram_[(addr_ - 1U) & 0x3FU] = cram_latch_;
                    gg_cram_[addr_ & 0x3FU] = value;
                }
            } else {
                cram_[addr_ & 0x1FU] = value;
            }
        } else {
            vram_[addr_ & 0x3FFFU] = value;
        }
        addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
    }

    std::uint8_t sms_vdp::vcounter() const noexcept {
        const int s = scanline_;
        if (pal_mode_) {
            // PAL (313 lines), per-mode discontinuities (SMS Power VDP docs):
            // 192-line: 0x00-0xF2 then 0xBA-0xFF; 224-line: 0x00-0xFF,
            // 0x00-0x02, 0xCA-0xFF; 240-line: 0x00-0xFF, 0x00-0x0A, 0xD2-0xFF.
            const int h = visible_height();
            if (h == 192) {
                if (s <= 0xF2) {
                    return static_cast<std::uint8_t>(s);
                }
                return static_cast<std::uint8_t>(s - (total_scanlines_ - 256));
            }
            if (s <= 0xFF) {
                return static_cast<std::uint8_t>(s);
            }
            const int second_end = (h == 240) ? 0x10A : 0x102;
            if (s <= second_end) {
                return static_cast<std::uint8_t>(s - 0x100);
            }
            return static_cast<std::uint8_t>(s - (total_scanlines_ - 256));
        }
        // NTSC (262 lines), 192-line mode: 0x00-0xDA then 0xD5-0xFF.
        if (s <= 0xDA) {
            return static_cast<std::uint8_t>(s);
        }
        return static_cast<std::uint8_t>(s - (total_scanlines_ - 256));
    }

    std::uint8_t sms_vdp::hcounter() const noexcept {
        int cycle = scanline_cycle_;
        if (cycle < 0) {
            cycle = 0;
        }
        if (cycle >= cycles_per_line) {
            cycle = cycles_per_line - 1;
        }
        const int count = (cycle * 3) / 4; // 0..170
        if (count < 12) {
            return static_cast<std::uint8_t>(0xF4 + count);
        }
        if (count < 160) {
            return static_cast<std::uint8_t>(count - 12);
        }
        return static_cast<std::uint8_t>(0xE9 + (count - 160));
    }

    // ---- rendering ----

    void sms_vdp::fill_scanline_bg(int line) noexcept {
        const std::uint32_t rgb = palette_rgb(reg_bg_color(reg_));
        const std::size_t off = static_cast<std::size_t>(line) * fb_width;
        for (int x = 0; x < fb_width; ++x) {
            framebuffer_[off + static_cast<std::size_t>(x)] = rgb;
        }
    }

    void sms_vdp::render_scanline(int line) noexcept {
        const int vis_h = visible_height();
        if (line >= vis_h) {
            return;
        }
        const std::size_t fb_off = static_cast<std::size_t>(line) * fb_width;
        const std::uint16_t nt_base = reg_nt_base(reg_);

        std::array<std::uint8_t, fb_width> bg_idx{};
        std::array<bool, fb_width> bg_pri{};

        // Background tiles.
        {
            int hscroll = reg_[8];
            const int vscroll = reg_[9];
            if (reg_lock_hscrl(reg_) && line < 16) {
                hscroll = 0;
            }
            const int wrap_h = (vis_h == 192) ? 224 : vis_h;
            const int eff_y = (line + vscroll) % wrap_h;
            const int tile_row = eff_y >> 3;
            const int fine_y = eff_y & 7;

            for (int col = 0; col < 32; ++col) {
                int use_row = tile_row;
                int use_fine = fine_y;
                if (reg_lock_vscrl(reg_) && col >= 24) {
                    use_row = line >> 3;
                    use_fine = line & 7;
                }
                const std::uint16_t nt_addr = static_cast<std::uint16_t>(
                    nt_base + static_cast<std::uint16_t>(((use_row & 0x1F) * 32 + col) * 2));
                const auto entry = static_cast<std::uint16_t>(
                    vram_[nt_addr & 0x3FFFU] |
                    (static_cast<std::uint16_t>(vram_[(nt_addr + 1U) & 0x3FFFU]) << 8U));
                const int tile = entry & 0x1FF;
                const bool hflip = ((entry >> 9U) & 1U) != 0U;
                const bool vflip = ((entry >> 10U) & 1U) != 0U;
                const int pal = (entry >> 11U) & 1;
                const bool pri = ((entry >> 12U) & 1U) != 0U;

                const int row_in_tile = vflip ? (7 - use_fine) : use_fine;
                std::array<std::uint8_t, 8> pix{};
                decode_tile_row(vram_, tile, row_in_tile, hflip, pix);

                for (int p = 0; p < 8; ++p) {
                    int screen_x = col * 8 + p;
                    // reg 8 scrolls the background right: screen x shows name-table
                    // pixel (x - hscroll), i.e. name-table pixel N lands at x + hscroll.
                    screen_x = (screen_x + hscroll) & 0xFF;
                    if (screen_x < fb_width) {
                        const auto sx = static_cast<std::size_t>(screen_x);
                        bg_idx[sx] =
                            static_cast<std::uint8_t>(pal * 16 + pix[static_cast<std::size_t>(p)]);
                        bg_pri[sx] = pri && pix[static_cast<std::size_t>(p)] != 0U;
                    }
                }
            }
        }

        // Sprites.
        {
            const std::uint16_t sat_base = reg_sat_base(reg_);
            const std::uint16_t spr_base = reg_spr_base(reg_);
            const int spr_h = reg_tall_sprites(reg_) ? 16 : 8;
            const int zoom = reg_zoom_sprites(reg_) ? 2 : 1;
            const int spr_h_eff = spr_h * zoom;
            int sprites_on_line = 0;

            std::array<std::uint8_t, fb_width> spr_buf{};
            std::array<bool, fb_width> has_sprite{};

            for (int i = 0; i < 64; ++i) {
                const std::uint8_t sy = vram_[(sat_base + i) & 0x3FFFU];
                if (vis_h == 192 && sy == 0xD0U) {
                    break; // list terminator
                }
                int spr_y = static_cast<int>(sy) + 1;
                if (spr_y > 240) {
                    spr_y -= 256;
                }
                if (line < spr_y || line >= spr_y + spr_h_eff) {
                    continue;
                }
                if (++sprites_on_line > 8) {
                    status_ |= 0x40U; // overflow
                    break;
                }

                const std::uint16_t info_addr = static_cast<std::uint16_t>(
                    sat_base + 128U + static_cast<std::uint16_t>(i) * 2U);
                const std::uint8_t sx = vram_[info_addr & 0x3FFFU];
                std::uint8_t tile = vram_[(info_addr + 1U) & 0x3FFFU];

                int spr_x = static_cast<int>(sx);
                if ((reg_[0] & 0x08U) != 0U) {
                    spr_x -= 8;
                }
                if (spr_h == 16) {
                    tile &= 0xFEU;
                }
                const int row_in_spr = (line - spr_y) / zoom;
                const int eff_tile = tile + (row_in_spr >= 8 ? 1 : 0);
                const int eff_row = row_in_spr & 7;

                std::array<std::uint8_t, 8> pix{};
                // spr_base is a VRAM byte address (reg 6 bit 2 selects $0000/$2000);
                // decode_tile_row wants a tile index, so convert (32 bytes per tile).
                decode_tile_row(vram_, static_cast<int>(spr_base >> 5U) + eff_tile, eff_row, false,
                                pix);

                for (int p = 0; p < 8; ++p) {
                    if (pix[static_cast<std::size_t>(p)] == 0U) {
                        continue; // transparent
                    }
                    for (int z = 0; z < zoom; ++z) {
                        const int x = spr_x + p * zoom + z;
                        if (x < 0 || x >= fb_width) {
                            continue;
                        }
                        const auto xs = static_cast<std::size_t>(x);
                        if (has_sprite[xs]) {
                            status_ |= 0x20U; // collision
                        } else {
                            spr_buf[xs] =
                                static_cast<std::uint8_t>(16U + pix[static_cast<std::size_t>(p)]);
                            has_sprite[xs] = true;
                        }
                    }
                }
            }

            for (std::size_t x = 0; x < fb_width; ++x) {
                if (has_sprite[x] && !bg_pri[x]) {
                    bg_idx[x] = spr_buf[x];
                }
            }
        }

        if (reg_blank_left(reg_)) {
            const std::uint8_t bg = reg_bg_color(reg_);
            for (std::size_t x = 0; x < 8; ++x) {
                bg_idx[x] = bg;
            }
        }

        for (int x = 0; x < fb_width; ++x) {
            framebuffer_[fb_off + static_cast<std::size_t>(x)] =
                palette_rgb(bg_idx[static_cast<std::size_t>(x)]);
        }
    }

    void sms_vdp::begin_scanline() noexcept {
        if (scanline_ >= visible_height()) {
            return;
        }
        if (reg_disp_en(reg_)) {
            render_scanline(scanline_);
        } else {
            fill_scanline_bg(scanline_);
        }
    }

    bool sms_vdp::run_scanline() noexcept {
        const int vis_h = visible_height();
        bool irq = false;

        if (scanline_ < vis_h) {
            if (line_counter_ <= 0) {
                line_counter_ = reg_[10];
                if (reg_line_int_en(reg_)) {
                    line_irq_pending_ = true;
                    irq = true;
                }
            } else {
                --line_counter_;
            }
        } else if (scanline_ == vis_h) {
            status_ |= 0x80U; // frame interrupt flag
            if (reg_frame_int_en(reg_)) {
                frame_irq_pending_ = true;
                irq = true;
            }
            line_counter_ = reg_[10];
        } else {
            line_counter_ = reg_[10];
        }

        ++scanline_;
        if (scanline_ >= total_scanlines_) {
            scanline_ = 0;
            ++frame_index_;
        }
        return irq;
    }

    void sms_vdp::update_irq() noexcept {
        const bool asserted = irq_asserted();
        if (asserted != irq_last_) {
            irq_last_ = asserted;
            if (irq_callback_) {
                irq_callback_(asserted);
            }
        }
    }

    void sms_vdp::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++scanline_cycle_;
            if (scanline_cycle_ >= cycles_per_line) {
                scanline_cycle_ = 0;
                begin_scanline();
                (void)run_scanline();
                update_irq();
            }
        }
    }

    frame_buffer_view sms_vdp::framebuffer() const noexcept {
        if (gg_mode_) {
            // The Game Gear LCD shows the central 160x144 of the Mode-4 256x192
            // frame: offset (48, 24), strided over the full 256-pixel-wide buffer.
            constexpr std::uint32_t gg_w = 160U, gg_h = 144U, gg_x = 48U, gg_y = 24U;
            return {framebuffer_.data() + (static_cast<std::size_t>(gg_y) * fb_width + gg_x), gg_w,
                    gg_h, static_cast<std::uint32_t>(fb_width)};
        }
        return {framebuffer_.data(), static_cast<std::uint32_t>(fb_width),
                static_cast<std::uint32_t>(visible_height())};
    }

    void sms_vdp::reset(reset_kind /*kind*/) {
        static constexpr std::array<std::uint8_t, 11> reg_defaults = {
            0x36U, 0xA0U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFBU, 0x00U, 0x00U, 0x00U, 0xFFU};
        vram_.fill(0U);
        cram_.fill(0U);
        gg_cram_.fill(0U);
        cram_latch_ = 0U;
        reg_.fill(0U);
        for (std::size_t i = 0; i < reg_defaults.size(); ++i) {
            reg_[i] = reg_defaults[i];
        }
        addr_ = 0U;
        code_ = 0U;
        cmd_pending_ = false;
        cmd_first_ = 0U;
        read_buffer_ = 0U;
        scanline_ = 0;
        scanline_cycle_ = 0;
        total_scanlines_ = pal_mode_ ? scanlines_pal : scanlines_ntsc;
        frame_irq_pending_ = false;
        line_irq_pending_ = false;
        line_counter_ = reg_[10];
        status_ = 0U;
        irq_last_ = false;
        frame_index_ = 0U;
        framebuffer_.assign(static_cast<std::size_t>(fb_width) * fb_height, 0U);
    }

    void sms_vdp::save_state(state_writer& writer) const {
        writer.bytes(std::span<const std::uint8_t>(vram_));
        writer.bytes(std::span<const std::uint8_t>(cram_));
        writer.bytes(std::span<const std::uint8_t>(reg_));
        writer.u16(addr_);
        writer.u8(code_);
        writer.boolean(cmd_pending_);
        writer.u8(cmd_first_);
        writer.u8(read_buffer_);
        writer.u32(static_cast<std::uint32_t>(scanline_));
        writer.u32(static_cast<std::uint32_t>(scanline_cycle_));
        writer.u32(static_cast<std::uint32_t>(total_scanlines_));
        writer.boolean(pal_mode_);
        writer.boolean(frame_irq_pending_);
        writer.boolean(line_irq_pending_);
        writer.u32(static_cast<std::uint32_t>(line_counter_));
        writer.u8(status_);
        writer.u64(frame_index_);
        // Game Gear fields appended last so the SMS save layout is unchanged: an
        // older SMS state reads these back as their (zero) defaults.
        writer.boolean(gg_mode_);
        writer.u8(cram_latch_);
        writer.bytes(std::span<const std::uint8_t>(gg_cram_));
    }

    void sms_vdp::load_state(state_reader& reader) {
        reader.bytes(std::span<std::uint8_t>(vram_));
        reader.bytes(std::span<std::uint8_t>(cram_));
        reader.bytes(std::span<std::uint8_t>(reg_));
        addr_ = reader.u16();
        code_ = reader.u8();
        cmd_pending_ = reader.boolean();
        cmd_first_ = reader.u8();
        read_buffer_ = reader.u8();
        scanline_ = static_cast<int>(reader.u32());
        scanline_cycle_ = static_cast<int>(reader.u32());
        total_scanlines_ = static_cast<int>(reader.u32());
        pal_mode_ = reader.boolean();
        frame_irq_pending_ = reader.boolean();
        line_irq_pending_ = reader.boolean();
        line_counter_ = static_cast<int>(reader.u32());
        status_ = reader.u8();
        frame_index_ = reader.u64();
        gg_mode_ = reader.boolean();
        cram_latch_ = reader.u8();
        reader.bytes(std::span<std::uint8_t>(gg_cram_));
        irq_last_ = irq_asserted();
    }

    instrumentation::ichip_introspection& sms_vdp::introspection() noexcept {
        return introspection_;
    }

    frame_buffer_view sms_vdp::introspection_surface::bg_layer_impl::view() const {
        // Compose the whole name-table background into an RGB scene: the 32-column
        // map, every tile decoded with its palette + H/V flip, resolved through
        // CRAM. No scroll and no sprites -- the raw map, like the Genesis plane_a.
        const auto& vram = owner_->vram_;
        const auto& reg = owner_->reg_;
        const std::uint16_t nt_base = reg_nt_base(reg);
        const int rows = owner_->visible_height() == 192 ? 28 : 32; // name-table height
        constexpr std::uint32_t cols = 32;
        width_ = cols * 8U; // 256
        height_ = static_cast<std::uint32_t>(rows) * 8U;
        buf_.assign(static_cast<std::size_t>(width_) * height_, 0U);

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < static_cast<int>(cols); ++col) {
                const auto nt_addr = static_cast<std::uint16_t>(nt_base + ((row * 32 + col) * 2));
                const auto entry = static_cast<std::uint16_t>(
                    vram[nt_addr & 0x3FFFU] |
                    (static_cast<std::uint16_t>(vram[(nt_addr + 1U) & 0x3FFFU]) << 8U));
                const int tile = entry & 0x1FF;
                const bool hflip = ((entry >> 9U) & 1U) != 0U;
                const bool vflip = ((entry >> 10U) & 1U) != 0U;
                const int pal = (entry >> 11U) & 1;
                for (int fy = 0; fy < 8; ++fy) {
                    const int row_in_tile = vflip ? (7 - fy) : fy;
                    std::array<std::uint8_t, 8> pix{};
                    decode_tile_row(vram, tile, row_in_tile, hflip, pix);
                    const std::size_t off =
                        (static_cast<std::size_t>(row) * 8U + static_cast<std::size_t>(fy)) *
                            width_ +
                        static_cast<std::size_t>(col) * 8U;
                    for (int p = 0; p < 8; ++p) {
                        const auto idx =
                            static_cast<std::uint8_t>(pal * 16 + pix[static_cast<std::size_t>(p)]);
                        buf_[off + static_cast<std::size_t>(p)] = owner_->palette_rgb(idx);
                    }
                }
            }
        }
        return {.pixels = buf_.data(), .width = width_, .height = height_, .stride = 0U};
    }

    // ---- asset extraction ----

    std::span<const instrumentation::palette_view>
    sms_vdp::introspection_surface::asset_source_impl::palettes() const {
        // CRAM is two 16-colour palettes: entries 0-15 back the background,
        // 16-31 back sprites (where colour 0 is transparent). palette_rgb
        // resolves either mode (SMS 6-bit or Game Gear 12-bit).
        for (std::size_t i = 0; i < 16U; ++i) {
            bg_rgb_[i] = owner_->palette_rgb(static_cast<std::uint8_t>(i));
            spr_rgb_[i] = owner_->palette_rgb(static_cast<std::uint8_t>(16U + i));
        }
        palettes_[0] =
            instrumentation::palette_view{.name = "bg", .colors = bg_rgb_, .transparent_index = -1};
        palettes_[1] = instrumentation::palette_view{
            .name = "sprite", .colors = spr_rgb_, .transparent_index = 0};
        return palettes_;
    }

    std::span<const instrumentation::graphic_asset>
    sms_vdp::introspection_surface::asset_source_impl::graphics() const {
        const auto& vram = owner_->vram_;

        // Tile sheet: every 32-byte pattern in VRAM decoded as an 8x8 4bpp tile,
        // laid out 16 tiles wide. Visualised against the background palette.
        constexpr int total_tiles = sms_vdp::vram_size / 32; // 512
        constexpr int tiles_per_row = 16;
        constexpr std::uint32_t sheet_w = tiles_per_row * 8;                 // 128
        constexpr std::uint32_t sheet_h = (total_tiles / tiles_per_row) * 8; // 256
        tileset_px_.assign(static_cast<std::size_t>(sheet_w) * sheet_h, 0U);
        for (int t = 0; t < total_tiles; ++t) {
            const int tcol = t % tiles_per_row;
            const int trow = t / tiles_per_row;
            for (int row = 0; row < 8; ++row) {
                std::array<std::uint8_t, 8> pix{};
                decode_tile_row(vram, t, row, false, pix);
                const std::size_t base =
                    (static_cast<std::size_t>(trow) * 8U + static_cast<std::size_t>(row)) *
                        sheet_w +
                    static_cast<std::size_t>(tcol) * 8U;
                for (int p = 0; p < 8; ++p) {
                    tileset_px_[base + static_cast<std::size_t>(p)] =
                        pix[static_cast<std::size_t>(p)];
                }
            }
        }

        // Sprites: walk the SAT, decoding each active sprite (8x8 or 8x16 in tall
        // mode) from the sprite pattern generator. In 192-line mode a Y of 0xD0
        // terminates the list.
        const auto& reg = owner_->reg_;
        const std::uint16_t sat_base = reg_sat_base(reg);
        const std::uint16_t spr_base = reg_spr_base(reg);
        const int spr_h = reg_tall_sprites(reg) ? 16 : 8;
        const int vis_h = owner_->visible_height();

        struct sprite_meta final {
            int tile;
            std::uint16_t src;
        };
        std::vector<sprite_meta> metas;
        for (int i = 0; i < 64; ++i) {
            const std::uint8_t sy = vram[(sat_base + static_cast<std::uint16_t>(i)) & 0x3FFFU];
            if (vis_h == 192 && sy == 0xD0U) {
                break;
            }
            const std::uint16_t info_addr =
                static_cast<std::uint16_t>(sat_base + 128U + static_cast<std::uint16_t>(i) * 2U);
            std::uint8_t tile = vram[(info_addr + 1U) & 0x3FFFU];
            if (spr_h == 16) {
                tile &= 0xFEU;
            }
            metas.push_back({static_cast<int>(spr_base >> 5U) + tile, info_addr});
        }

        const std::size_t spr_stride = 8U * static_cast<std::size_t>(spr_h);
        sprite_px_.assign(metas.size() * spr_stride, 0U);
        for (std::size_t s = 0; s < metas.size(); ++s) {
            for (int row = 0; row < spr_h; ++row) {
                const int eff_tile = metas[s].tile + (row >= 8 ? 1 : 0);
                std::array<std::uint8_t, 8> pix{};
                decode_tile_row(vram, eff_tile, row & 7, false, pix);
                const std::size_t base = s * spr_stride + static_cast<std::size_t>(row) * 8U;
                for (int p = 0; p < 8; ++p) {
                    sprite_px_[base + static_cast<std::size_t>(p)] =
                        pix[static_cast<std::size_t>(p)];
                }
            }
        }

        // Build the descriptor list. names_ is reserved up front so the
        // string_views the assets hold into it never dangle on reallocation.
        names_.clear();
        names_.reserve(metas.size());
        assets_.clear();
        assets_.reserve(metas.size() + 2U);
        assets_.push_back(instrumentation::graphic_asset{
            .kind = instrumentation::asset_kind::tileset,
            .name = "patterns",
            .image = {.width = sheet_w, .height = sheet_h, .indices = tileset_px_, .palette = 0U},
            .tile_w = 8U,
            .tile_h = 8U,
            .source_addr = 0U});
        for (std::size_t s = 0; s < metas.size(); ++s) {
            std::array<char, 16> buf{};
            std::snprintf(buf.data(), buf.size(), "sprite_%02zu", s);
            names_.emplace_back(buf.data());
            assets_.push_back(instrumentation::graphic_asset{
                .kind = instrumentation::asset_kind::sprite,
                .name = names_[s],
                .image = {.width = 8U,
                          .height = static_cast<std::uint32_t>(spr_h),
                          .indices = std::span<const std::uint8_t>(sprite_px_)
                                         .subspan(s * spr_stride, spr_stride),
                          .palette = 1U},
                .tile_w = 0U,
                .tile_h = 0U,
                .source_addr = metas[s].src});
        }

        // Optional font sheet: the manifest-hinted glyph tile range, decoded as
        // an 8x8 tile sheet 16 cells wide (against the background palette).
        if (owner_->font_count_ > 0) {
            const int count = owner_->font_count_;
            const int first = owner_->font_first_tile_;
            constexpr int per_row = 16;
            const auto fw = static_cast<std::uint32_t>(per_row * 8);
            const auto fh = static_cast<std::uint32_t>(((count + per_row - 1) / per_row) * 8);
            font_px_.assign(static_cast<std::size_t>(fw) * fh, 0U);
            for (int t = 0; t < count; ++t) {
                const int gcol = t % per_row;
                const int grow = t / per_row;
                for (int row = 0; row < 8; ++row) {
                    std::array<std::uint8_t, 8> pix{};
                    decode_tile_row(vram, first + t, row, false, pix);
                    const std::size_t base =
                        (static_cast<std::size_t>(grow) * 8U + static_cast<std::size_t>(row)) * fw +
                        static_cast<std::size_t>(gcol) * 8U;
                    for (int p = 0; p < 8; ++p) {
                        font_px_[base + static_cast<std::size_t>(p)] =
                            pix[static_cast<std::size_t>(p)];
                    }
                }
            }
            assets_.push_back(instrumentation::graphic_asset{
                .kind = instrumentation::asset_kind::font,
                .name = "font",
                .image = {.width = fw, .height = fh, .indices = font_px_, .palette = 0U},
                .tile_w = 8U,
                .tile_h = 8U,
                .source_addr = static_cast<std::uint32_t>(first) * 32U});
        }
        return assets_;
    }

    void sms_vdp::configure(const config_table& cfg, const callback_table& callbacks) {
        // Region selection: PAL = 313 scanlines / 50 Hz, NTSC = 262 / 60.
        // The SMS manifest sets `pal = true` for PAL variants; defaults to NTSC.
        if (const auto v = chips::cfg_bool(cfg, "pal")) {
            set_pal(*v);
        }
        // /INT line callback (typically ORed into the Z80 IRQ on SMS).
        // Manifest names a void(bool) callback; host registers it.
        if (const auto id = chips::cfg_string(cfg, "irq_callback")) {
            if (const auto* fn = chips::find_callback<void(bool)>(callbacks, *id)) {
                set_irq_callback(*fn);
            }
        }
        // Optional font-extraction hint: the glyph tile range surfaced as a
        // "font" asset. Clamped to the 512-pattern VRAM tile space; a missing
        // or non-positive count leaves font extraction off.
        constexpr int max_tiles = vram_size / 32; // 512
        if (const auto first = chips::cfg_int(cfg, "font_first_tile")) {
            font_first_tile_ = static_cast<int>(std::clamp<std::int64_t>(*first, 0, max_tiles - 1));
        }
        if (const auto count = chips::cfg_int(cfg, "font_count")) {
            font_count_ =
                static_cast<int>(std::clamp<std::int64_t>(*count, 0, max_tiles - font_first_tile_));
        }
    }

    namespace {
        [[maybe_unused]] const auto sms_vdp_registration =
            register_factory("sega.sms_vdp", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<sms_vdp>();
            });
    } // namespace

} // namespace mnemos::chips::video
