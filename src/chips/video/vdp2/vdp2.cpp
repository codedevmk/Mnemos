#include "vdp2.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // TVMD bits.
        constexpr std::uint16_t tvmd_disp = 1U << 15U;
        constexpr std::uint16_t tvmd_lsmd_mask = 0x03U << 6U;
        constexpr std::uint16_t tvmd_vres_mask = 0x0030U;
        constexpr std::uint16_t tvmd_hres_mask = 0x0007U;

        // BGON bits.
        constexpr std::uint16_t bgon_n0on = 1U << 0U;
        constexpr std::uint16_t bgon_n0tpon = 1U << 8U;

        // TVSTAT read-only bits.
        constexpr std::uint16_t tvstat_vblank = 1U << 3U;
        constexpr std::uint16_t tvstat_hblank = 1U << 2U;
        constexpr std::uint16_t tvstat_field = 1U << 1U;
        constexpr std::uint16_t tvstat_pal = 1U << 0U;

        // RAMCTL: CRAM mode in bits 12..13.
        constexpr std::uint16_t ramctl_crmd_mask = 0x03U << 12U;
        constexpr std::uint16_t ramctl_crmd_shift = 12U;

        // Saturn-native 0BGR1555 word -> packed 0x00RRGGBB (5-bit guns
        // replicated into the low bits).
        [[nodiscard]] constexpr std::uint32_t expand5(std::uint32_t v5) noexcept {
            const std::uint32_t v = v5 & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }
        [[nodiscard]] constexpr std::uint32_t unpack_bgr1555(std::uint16_t raw) noexcept {
            const std::uint32_t r = expand5(raw & 0x1FU);
            const std::uint32_t g = expand5((raw >> 5U) & 0x1FU);
            const std::uint32_t b = expand5((raw >> 10U) & 0x1FU);
            return (r << 16U) | (g << 8U) | b;
        }
        [[nodiscard]] constexpr std::uint32_t pack_888(std::uint8_t r, std::uint8_t g,
                                                       std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) |
                   static_cast<std::uint32_t>(b);
        }

        // CRAM index assembly per the cell's pattern depth (ST-058 Fig 4.11).
        [[nodiscard]] std::uint16_t palette_index(std::uint8_t pattern_bits,
                                                  std::uint16_t palette_no,
                                                  std::uint16_t cram_offset,
                                                  std::uint16_t dot_code) noexcept {
            std::uint16_t base = 0U;
            if (pattern_bits == 0U) {
                base = static_cast<std::uint16_t>((static_cast<std::uint32_t>(palette_no) << 4U) &
                                                  ~0x0FU);
            } else if (pattern_bits == 1U) {
                base = static_cast<std::uint16_t>((static_cast<std::uint32_t>(palette_no) << 4U) &
                                                  ~0xFFU);
            }
            return static_cast<std::uint16_t>(cram_offset + base + dot_code);
        }
    } // namespace

    chip_metadata vdp2::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "vdp2",
            .family = "saturn",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void vdp2::reset(reset_kind /*kind*/) {
        std::fill(vram_.begin(), vram_.end(), std::uint8_t{0U});
        cram_.fill(0U);
        regs_.fill(0U);
        display_on_ = false;
        hres_ = 0U;
        vres_ = 0U;
        interlace_ = 0U;
        nbg_on_.fill(false);
        nbg_transparent_code_enabled_.fill(true);
        cram_mode_ = 0U;
        vblank_ = false;
        hblank_ = false;
        field_odd_ = true;
        is_pal_ = false;
        beam_y_ = 0U;
        frame_index_ = 0U;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void vdp2::decode_tvmd() noexcept {
        const std::uint16_t tvmd = regs_[reg_tvmd / 2U];
        display_on_ = (tvmd & tvmd_disp) != 0U;
        hres_ = static_cast<std::uint8_t>(tvmd & tvmd_hres_mask);
        vres_ = static_cast<std::uint8_t>((tvmd & tvmd_vres_mask) >> 4U);
        interlace_ = static_cast<std::uint8_t>((tvmd & tvmd_lsmd_mask) >> 6U);
    }

    void vdp2::decode_bgon() noexcept {
        const std::uint16_t bgon = regs_[reg_bgon / 2U];
        for (int n = 0; n < 4; ++n) {
            nbg_on_[static_cast<std::size_t>(n)] = (bgon & (bgon_n0on << n)) != 0U;
            nbg_transparent_code_enabled_[static_cast<std::size_t>(n)] =
                (bgon & (bgon_n0tpon << n)) == 0U;
        }
    }

    void vdp2::decode_ramctl() noexcept {
        cram_mode_ = static_cast<std::uint8_t>((regs_[reg_ramctl / 2U] & ramctl_crmd_mask) >>
                                               ramctl_crmd_shift);
    }

    std::uint16_t vdp2::reg_read(std::uint16_t reg) const noexcept {
        reg &= 0x1FEU;
        const std::uint16_t idx = static_cast<std::uint16_t>(reg >> 1U);
        switch (reg) {
        case reg_tvstat: {
            std::uint16_t s = 0U;
            const bool odd_field = (interlace_ == 0U) ? true : field_odd_;
            // When TVMD.DISP=0 the VBLANK flag stays asserted regardless of
            // beam position (pre-display boot contract).
            if (vblank_ || !display_on_) {
                s |= tvstat_vblank;
            }
            if (hblank_) {
                s |= tvstat_hblank;
            }
            if (odd_field) {
                s |= tvstat_field;
            }
            if (is_pal_) {
                s |= tvstat_pal;
            }
            return s;
        }
        case reg_hcnt:
        case reg_vcnt:
            return regs_[idx]; // host-latched counters live in the register file
        default:
            return regs_[idx];
        }
    }

    void vdp2::reg_write(std::uint16_t reg, std::uint16_t value) noexcept {
        reg &= 0x1FEU;
        const std::uint16_t idx = static_cast<std::uint16_t>(reg >> 1U);
        if (reg == reg_tvstat || reg == reg_hcnt || reg == reg_vcnt) {
            return; // read-only
        }
        regs_[idx] = value;
        switch (reg) {
        case reg_tvmd:
            decode_tvmd();
            break;
        case reg_bgon:
            decode_bgon();
            break;
        case reg_ramctl:
            decode_ramctl();
            break;
        default:
            break;
        }
    }

    std::uint32_t vdp2::display_width() const noexcept {
        static constexpr std::array<std::uint32_t, 8> widths{320U, 352U, 640U, 704U,
                                                             320U, 352U, 640U, 704U};
        return widths[hres_ & 0x7U];
    }

    std::uint32_t vdp2::display_height() const noexcept {
        static constexpr std::array<std::uint32_t, 4> heights{224U, 240U, 256U, 256U};
        return heights[vres_ & 0x3U];
    }

    std::uint16_t vdp2::vram_read_word(std::uint32_t off) const noexcept {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(vram_[(off + 0U) & (vram_size - 1U)]) << 8U) |
            static_cast<std::uint16_t>(vram_[(off + 1U) & (vram_size - 1U)]));
    }

    std::uint8_t vdp2::vram_read_byte(std::uint32_t off) const noexcept {
        return vram_[off & (vram_size - 1U)];
    }

    std::uint32_t vdp2::palette_read(std::uint16_t index) const noexcept {
        switch (cram_mode_) {
        case 0U:   // 1024 x 15-bit
        case 1U: { // 2048 x 15-bit
            std::uint32_t off = static_cast<std::uint32_t>(index) * 2U;
            off &= (cram_size - 1U);
            const std::uint16_t raw =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(cram_[off + 0U]) << 8U) |
                                           static_cast<std::uint16_t>(cram_[off + 1U]));
            return unpack_bgr1555(raw);
        }
        case 2U: { // 1024 x 24-bit: 4-byte slots [unused, B, G, R]
            std::uint32_t off = static_cast<std::uint32_t>(index) * 4U;
            off &= (cram_size - 1U);
            const std::uint8_t b = cram_[off + 1U];
            const std::uint8_t g = cram_[off + 2U];
            const std::uint8_t r = cram_[off + 3U];
            return pack_888(r, g, b);
        }
        default:
            return 0U;
        }
    }

    std::uint32_t vdp2::back_color(int scanline) const noexcept {
        const std::uint16_t bktau = regs_[reg_bktau / 2U];
        const std::uint16_t bktal = regs_[reg_bktal / 2U];
        std::uint32_t tbl = ((static_cast<std::uint32_t>(bktau & 0x7U) << 16U) | bktal) * 2U;
        const bool per_line = (bktau & 0x8000U) != 0U;
        std::uint32_t line = scanline < 0 ? 0U : static_cast<std::uint32_t>(scanline);
        if (interlace_ == 2U) {
            line /= 2U;
        }
        const std::uint32_t off = per_line ? (tbl + line * 2U) : tbl;
        return unpack_bgr1555(vram_read_word(off));
    }

    // ── NBG plane geometry helpers (subset: 1x1, 2x1, 2x2 planes) ──

    namespace {
        [[nodiscard]] std::uint32_t nbg_page_bytes(const auto& cfg) noexcept {
            if (cfg.pattern_name != 0U) {
                return cfg.char_size ? 0x0800U : 0x2000U;
            }
            return cfg.char_size ? 0x1000U : 0x4000U;
        }
        [[nodiscard]] int nbg_plane_pages_x(const auto& cfg) noexcept {
            return (cfg.plane_size == 1U || cfg.plane_size == 3U) ? 2 : 1;
        }
        [[nodiscard]] int nbg_plane_pages_y(const auto& cfg) noexcept {
            return cfg.plane_size == 3U ? 2 : 1;
        }
        [[nodiscard]] std::uint8_t nbg_map_selection_bits(const auto& cfg) noexcept {
            if (cfg.pattern_name != 0U) {
                return cfg.char_size ? 9U : 7U;
            }
            return cfg.char_size ? 8U : 6U;
        }
        [[nodiscard]] std::uint32_t nbg_plane_base_from_selection(const auto& cfg,
                                                                  std::uint8_t map_offset,
                                                                  std::uint8_t map_reg) noexcept {
            const std::uint32_t combined = static_cast<std::uint32_t>(map_reg & 0x3FU) |
                                           (static_cast<std::uint32_t>(map_offset & 0x7U) << 6U);
            const std::uint32_t page_bytes = nbg_page_bytes(cfg);
            const std::uint32_t plane_bytes = page_bytes *
                                              static_cast<std::uint32_t>(nbg_plane_pages_x(cfg)) *
                                              static_cast<std::uint32_t>(nbg_plane_pages_y(cfg));
            std::uint8_t shift = 0U;
            if (cfg.plane_size == 1U) {
                shift = 1U;
            } else if (cfg.plane_size == 3U) {
                shift = 2U;
            }
            const std::uint8_t used_bits = nbg_map_selection_bits(cfg);
            if (shift >= used_bits) {
                return 0U;
            }
            return ((combined >> shift) & ((1U << (used_bits - shift)) - 1U)) * plane_bytes;
        }
    } // namespace

    void vdp2::decode_nbg_cfg(int n, nbg_cfg& cfg) const noexcept {
        cfg = nbg_cfg{};
        cfg.enabled = nbg_on_[static_cast<std::size_t>(n)];
        cfg.transparent_code_enabled = nbg_transparent_code_enabled_[static_cast<std::size_t>(n)];

        const std::uint16_t prin = (n < 2) ? regs_[reg_prina / 2U] : regs_[reg_prinb / 2U];
        const int shift = (n & 1) ? 8 : 0;
        cfg.priority = static_cast<std::uint8_t>((prin >> shift) & 0x7U);

        const std::uint16_t chctl = (n < 2) ? regs_[reg_chctla / 2U] : regs_[reg_chctlb / 2U];
        if (n == 0) {
            cfg.pattern_bits = static_cast<std::uint8_t>((chctl >> 4U) & 0x7U);
            cfg.char_size = static_cast<std::uint8_t>(chctl & 0x1U);
        } else if (n == 1) {
            cfg.pattern_bits = static_cast<std::uint8_t>((chctl >> 12U) & 0x3U);
            cfg.char_size = static_cast<std::uint8_t>((chctl >> 8U) & 0x1U);
        } else if (n == 2) {
            cfg.pattern_bits = static_cast<std::uint8_t>((chctl >> 1U) & 0x1U);
            cfg.char_size = static_cast<std::uint8_t>(chctl & 0x1U);
        } else {
            cfg.pattern_bits = static_cast<std::uint8_t>((chctl >> 5U) & 0x1U);
            cfg.char_size = static_cast<std::uint8_t>((chctl >> 4U) & 0x1U);
        }

        const std::uint16_t pncn = regs_[(reg_pncn0 + static_cast<std::uint16_t>(n * 2)) / 2U];
        cfg.pattern_name = static_cast<std::uint8_t>((pncn >> 15U) & 0x1U);
        cfg.pal_bank = static_cast<std::uint16_t>((pncn & 0x70U) >> 4U);
        cfg.cram_offset =
            static_cast<std::uint16_t>(((regs_[reg_craofa / 2U] >> (n * 4)) & 0x7U) << 8U);
        cfg.aux_mode = static_cast<std::uint8_t>((pncn >> 14U) & 0x1U);
        cfg.supplement_data = static_cast<std::uint16_t>(pncn & 0x3FFU);

        cfg.plane_size = static_cast<std::uint8_t>((regs_[reg_plsz / 2U] >> (n * 2)) & 0x3U);
        if (cfg.plane_size == 2U) {
            cfg.plane_size = 0U; // invalid on hardware; treat as 1x1
        }

        const std::uint16_t mpofn = regs_[reg_mpofn / 2U];
        const std::uint8_t mp_off = static_cast<std::uint8_t>((mpofn >> (n * 4)) & 0x7U);
        const std::uint16_t mpab = regs_[(reg_mpabn0 + static_cast<std::uint16_t>(n * 4)) / 2U];
        const std::uint16_t mpcd = regs_[(reg_mpabn0 + static_cast<std::uint16_t>(n * 4 + 2)) / 2U];
        cfg.map_base[0] =
            nbg_plane_base_from_selection(cfg, mp_off, static_cast<std::uint8_t>(mpab & 0x3FU));
        cfg.map_base[1] = nbg_plane_base_from_selection(
            cfg, mp_off, static_cast<std::uint8_t>((mpab >> 8U) & 0x3FU));
        cfg.map_base[2] =
            nbg_plane_base_from_selection(cfg, mp_off, static_cast<std::uint8_t>(mpcd & 0x3FU));
        cfg.map_base[3] = nbg_plane_base_from_selection(
            cfg, mp_off, static_cast<std::uint8_t>((mpcd >> 8U) & 0x3FU));

        if (n == 0) {
            cfg.scroll_x = static_cast<std::int16_t>(regs_[reg_scxin0 / 2U]);
            cfg.scroll_y = static_cast<std::int16_t>(regs_[reg_scyin0 / 2U]);
        } else if (n == 1) {
            cfg.scroll_x = static_cast<std::int16_t>(regs_[reg_scxin1 / 2U]);
            cfg.scroll_y = static_cast<std::int16_t>(regs_[reg_scyin1 / 2U]);
        } else if (n == 2) {
            cfg.scroll_x = static_cast<std::int16_t>(regs_[reg_scxn2 / 2U]);
            cfg.scroll_y = static_cast<std::int16_t>(regs_[reg_scyn2 / 2U]);
        } else {
            cfg.scroll_x = static_cast<std::int16_t>(regs_[reg_scxn3 / 2U]);
            cfg.scroll_y = static_cast<std::int16_t>(regs_[reg_scyn3 / 2U]);
        }
    }

    std::uint32_t vdp2::nbg_fetch_pixel(const nbg_cfg& cfg, int x, int y,
                                        bool& transparent) const noexcept {
        transparent = true;

        int px = x + cfg.scroll_x;
        int py = y + cfg.scroll_y;

        // A normal-scroll map is always 2x2 planes; each plane is 1x1/2x1/2x2
        // pages, and one page is always 512x512 px.
        const int plane_px_w = 512 * nbg_plane_pages_x(cfg);
        const int plane_px_h = 512 * nbg_plane_pages_y(cfg);
        const int map_px_w = plane_px_w * 2;
        const int map_px_h = plane_px_h * 2;
        px = ((px % map_px_w) + map_px_w) % map_px_w;
        py = ((py % map_px_h) + map_px_h) % map_px_h;
        const int plane_col = px / plane_px_w;
        const int plane_row = py / plane_px_h;
        const int local_plane_x = px % plane_px_w;
        const int local_plane_y = py % plane_px_h;
        const std::uint32_t plane_base =
            cfg.map_base[static_cast<std::size_t>(plane_row * 2 + plane_col)];

        const std::uint32_t page_bytes = nbg_page_bytes(cfg);
        const int page_col = local_plane_x / 512;
        const int page_row = local_plane_y / 512;
        const int local_page_x = local_plane_x % 512;
        const int local_page_y = local_plane_y % 512;

        const int char_px = cfg.char_size ? 16 : 8;
        const int char_col = local_page_x / char_px;
        const int char_row = local_page_y / char_px;
        const int char_w = 512 / char_px;
        const int char_idx = char_row * char_w + char_col;

        const std::uint32_t map =
            plane_base +
            static_cast<std::uint32_t>(page_row * nbg_plane_pages_x(cfg) + page_col) * page_bytes;

        std::uint16_t palette = cfg.pal_bank;
        std::uint32_t char_num = 0U;
        bool flip_h = false;
        bool flip_v = false;
        if (cfg.pattern_name == 0U) {
            // 2-word pattern: word0 = palette/control, word1 = 15-bit char num.
            const std::uint32_t off = map + static_cast<std::uint32_t>(char_idx) * 4U;
            const std::uint16_t pname = vram_read_word(off);
            const std::uint16_t pname2 = vram_read_word(off + 2U);
            char_num = static_cast<std::uint32_t>(pname2 & 0x7FFFU);
            palette = static_cast<std::uint16_t>(pname & 0x007FU);
            flip_v = (pname & 0x8000U) != 0U;
            flip_h = (pname & 0x4000U) != 0U;
        } else {
            // 1-word pattern: supplemented by PNCN bits 0..9.
            const std::uint16_t pname =
                vram_read_word(map + static_cast<std::uint32_t>(char_idx) * 2U);
            const std::uint16_t supp = cfg.supplement_data;
            if (cfg.aux_mode == 0U) {
                flip_h = (pname & 0x0400U) != 0U;
                flip_v = (pname & 0x0800U) != 0U;
                if (cfg.char_size) {
                    char_num = (static_cast<std::uint32_t>(pname & 0x3FFU) << 2U) |
                               static_cast<std::uint32_t>(supp & 0x3U) |
                               (static_cast<std::uint32_t>(supp & 0x1CU) << 10U);
                } else {
                    char_num = static_cast<std::uint32_t>(pname & 0x3FFU) |
                               (static_cast<std::uint32_t>(supp & 0x1FU) << 10U);
                }
            } else {
                if (cfg.char_size) {
                    char_num = (static_cast<std::uint32_t>(pname & 0xFFFU) << 2U) |
                               static_cast<std::uint32_t>(supp & 0x3U) |
                               (static_cast<std::uint32_t>(supp & 0x10U) << 10U);
                } else {
                    char_num = static_cast<std::uint32_t>(pname & 0xFFFU) |
                               (static_cast<std::uint32_t>(supp & 0x1CU) << 10U);
                }
            }
            if (cfg.pattern_bits == 0U) {
                palette =
                    static_cast<std::uint16_t>(((pname >> 12U) & 0xFU) | (cfg.pal_bank << 4U));
            } else {
                palette = static_cast<std::uint16_t>(((pname >> 12U) & 0x7U) << 4U);
            }
        }

        int cx = local_plane_x % char_px;
        int cy = local_plane_y % char_px;
        if (flip_h) {
            cx = char_px - 1 - cx;
        }
        if (flip_v) {
            cy = char_px - 1 - cy;
        }

        std::uint32_t units_per_8x8 = 1U;
        switch (cfg.pattern_bits) {
        case 0:
            units_per_8x8 = 1U;
            break;
        case 1:
            units_per_8x8 = 2U;
            break;
        case 2:
            units_per_8x8 = 4U;
            break;
        default:
            units_per_8x8 = 1U;
            break;
        }
        const int sub_x = cx >> 3;
        const int sub_y = cy >> 3;
        const int local_x = cx & 7;
        const int local_y = cy & 7;
        const std::uint32_t subcell =
            cfg.char_size ? static_cast<std::uint32_t>(sub_y * 2 + sub_x) : 0U;
        const std::uint32_t cell_addr = (char_num + subcell * units_per_8x8) * 0x20U;
        const int pixel_in_cell = local_y * 8 + local_x;

        if (cfg.pattern_bits == 0U) { // 4bpp
            const std::uint8_t byte =
                vram_read_byte(cell_addr + static_cast<std::uint32_t>(pixel_in_cell / 2));
            const std::uint8_t idx = (pixel_in_cell & 1) ? (byte & 0x0FU) : (byte >> 4U);
            if (cfg.transparent_code_enabled && idx == 0U) {
                return 0U;
            }
            transparent = false;
            return palette_read(palette_index(cfg.pattern_bits, palette, cfg.cram_offset, idx));
        }
        if (cfg.pattern_bits == 1U) { // 8bpp
            const std::uint8_t idx =
                vram_read_byte(cell_addr + static_cast<std::uint32_t>(pixel_in_cell));
            if (cfg.transparent_code_enabled && idx == 0U) {
                return 0U;
            }
            transparent = false;
            return palette_read(palette_index(cfg.pattern_bits, palette, cfg.cram_offset, idx));
        }
        // 16bpp direct 0BGR1555.
        const std::uint16_t raw =
            vram_read_word(cell_addr + static_cast<std::uint32_t>(pixel_in_cell) * 2U);
        if ((raw & 0x7FFFU) == 0U) {
            return 0U;
        }
        transparent = false;
        return unpack_bgr1555(raw);
    }

    void vdp2::render_scanline(int scanline) noexcept {
        const std::size_t row = static_cast<std::size_t>(scanline) * render_width;
        const std::uint32_t back = back_color(scanline);

        if (!display_on_) {
            for (std::uint32_t x = 0; x < render_width; ++x) {
                pixels_[row + x] = back;
            }
            return;
        }

        std::array<nbg_cfg, 4> cfg{};
        std::array<int, 4> order{0, 1, 2, 3};
        for (int n = 0; n < 4; ++n) {
            decode_nbg_cfg(n, cfg[static_cast<std::size_t>(n)]);
        }
        // Stable bubble-sort ascending by priority so the highest priority
        // composites last and wins.
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3 - i; ++j) {
                if (cfg[static_cast<std::size_t>(order[static_cast<std::size_t>(j)])].priority >
                    cfg[static_cast<std::size_t>(order[static_cast<std::size_t>(j + 1)])]
                        .priority) {
                    const int tmp = order[static_cast<std::size_t>(j)];
                    order[static_cast<std::size_t>(j)] = order[static_cast<std::size_t>(j + 1)];
                    order[static_cast<std::size_t>(j + 1)] = tmp;
                }
            }
        }

        for (std::uint32_t x = 0; x < render_width; ++x) {
            std::uint32_t pixel = back;
            std::uint8_t pixel_prio = 0U;
            for (int i = 0; i < 4; ++i) {
                const int layer = order[static_cast<std::size_t>(i)];
                const nbg_cfg& c = cfg[static_cast<std::size_t>(layer)];
                if (!c.enabled || c.priority == 0U) {
                    continue;
                }
                bool transparent = true;
                const std::uint32_t px =
                    nbg_fetch_pixel(c, static_cast<int>(x), scanline, transparent);
                if (transparent) {
                    continue;
                }
                if (c.priority < pixel_prio) {
                    continue;
                }
                pixel = px;
                pixel_prio = c.priority;
            }
            pixels_[row + x] = pixel;
        }
    }

    void vdp2::render_frame() noexcept {
        for (std::uint32_t y = 0; y < render_height; ++y) {
            render_scanline(static_cast<int>(y));
        }
    }

    void vdp2::tick(std::uint64_t cycles) {
        // One "cycle" advances one scanline; rendering completes at the start
        // of the vblank region (line == render_height), bumping frame_index().
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_y_ == render_height) {
                render_frame();
                ++frame_index_;
            }
            if (++beam_y_ == total_lines) {
                beam_y_ = 0U;
            }
        }
    }

    frame_buffer_view vdp2::framebuffer() const noexcept {
        return {
            .pixels = pixels_.data(), .width = render_width, .height = render_height, .stride = 0U};
    }

    void vdp2::save_state(state_writer& writer) const {
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        writer.boolean(vblank_);
        writer.boolean(hblank_);
        writer.boolean(field_odd_);
        writer.boolean(is_pal_);
        for (std::uint16_t r : regs_) {
            writer.u16(r);
        }
        writer.bytes(vram_);
        writer.bytes(cram_);
    }

    void vdp2::load_state(state_reader& reader) {
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        vblank_ = reader.boolean();
        hblank_ = reader.boolean();
        field_odd_ = reader.boolean();
        is_pal_ = reader.boolean();
        for (std::uint16_t& r : regs_) {
            r = reader.u16();
        }
        reader.bytes(vram_);
        reader.bytes(cram_);
        if (!reader.ok()) {
            return;
        }
        decode_tvmd();
        decode_bgon();
        decode_ramctl();
    }

    instrumentation::ichip_introspection& vdp2::introspection() noexcept { return introspection_; }

    namespace {
        [[maybe_unused]] const auto vdp2_registration =
            register_factory("sega.vdp2", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<vdp2>(); });
    } // namespace

} // namespace mnemos::chips::video
