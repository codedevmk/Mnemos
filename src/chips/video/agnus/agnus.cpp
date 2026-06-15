#include "agnus.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 4-bit colour channel to 8 bits, the nibble replicated into the low
        // bits so 0xF -> 0xFF and 0x0 -> 0x00 (the hardware DAC rule, ported
        // integer-exact from the reference colour decode).
        [[nodiscard]] constexpr std::uint32_t expand4(std::uint8_t nibble) noexcept {
            const std::uint32_t v = nibble & 0x0FU;
            return (v << 4U) | v;
        }

        // Custom-register offsets the copper can MOVE into to drive the
        // display. Mirrors the standard OCS register map; only the registers
        // this display sequencer models are honoured (others are dropped).
        constexpr std::uint16_t reg_dmacon = 0x096U;
        constexpr std::uint16_t reg_bplcon0 = 0x100U;
        constexpr std::uint16_t reg_bpl1mod = 0x108U;
        constexpr std::uint16_t reg_bpl2mod = 0x10AU;
        constexpr std::uint16_t reg_diwstrt = 0x08EU;
        constexpr std::uint16_t reg_diwstop = 0x090U;
        constexpr std::uint16_t reg_ddfstrt = 0x092U;
        constexpr std::uint16_t reg_ddfstop = 0x094U;
        constexpr std::uint16_t reg_bplpth_base = 0x0E0U; // BPL1PTH; pairs step by 4
        constexpr std::uint16_t reg_color_base = 0x180U;  // COLOR00; entries step by 2

        // Copper WAIT/SKIP: IR2 bit 15 is Blitter-Finished-Disable.
        constexpr std::uint16_t copper_bfd_mask = 0x8000U;
    } // namespace

    chip_metadata agnus::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "8367",
            .family = "agnus",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void agnus::reset(reset_kind /*kind*/) {
        const bool was_pal = is_pal_;
        dmacon_ = 0U;
        blitter_busy_ = false;
        blitter_zero_ = false;
        is_pal_ = was_pal;
        scanline_ = 0U;
        color_clock_ = 0U;
        long_frame_ = false;
        vblank_active_ = false;
        hblank_active_ = false;
        bitplane_pointer_.fill(0U);
        modulo_even_ = 0;
        modulo_odd_ = 0;
        bplcon0_ = 0U;
        diwstrt_ = 0U;
        diwstop_ = 0U;
        ddfstrt_ = 0U;
        ddfstop_ = 0U;
        cop1lc_ = 0U;
        cop2lc_ = 0U;
        copper_pc_ = 0U;
        copper_running_ = false;
        copper_danger_ = false;
        frame_index_ = 0U;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void agnus::set_pal(bool is_pal) noexcept {
        is_pal_ = is_pal;
        // Snap the beam into range of the new region immediately.
        const std::uint32_t max_line = is_pal ? scanlines_pal : scanlines_ntsc;
        if (scanline_ >= max_line) {
            scanline_ = max_line - 1U;
        }
    }

    void agnus::tick(std::uint64_t cycles) {
        const std::uint32_t max_line = is_pal_ ? scanlines_pal : scanlines_ntsc;
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (color_clock_ == 0U) {
                if (scanline_ == active_height()) {
                    render_frame();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(scanline_);
                    }
                }
                if (scanline_cb_) {
                    scanline_cb_(scanline_);
                }
            }
            // The copper walks during the visible field, one slot per clock.
            run_copper();
            if (++color_clock_ == color_clocks_per_line) {
                color_clock_ = 0U;
                if (++scanline_ == max_line) {
                    scanline_ = 0U;
                    long_frame_ = !long_frame_;
                }
            }
            refresh_blank_flags();
        }
    }

    void agnus::refresh_blank_flags() noexcept {
        // OCS HBlank is the start/end color-clock band of each line; VBlank
        // covers the top scanlines. Approximations sufficient to gate copper
        // WAITs and sprite-DMA windows.
        hblank_active_ = (color_clock_ < 18U) || (color_clock_ >= 208U);
        const std::uint32_t vblank_end = is_pal_ ? 24U : 20U;
        vblank_active_ = scanline_ < vblank_end;
    }

    // --- DMA controller ----------------------------------------------------

    void agnus::write_dmacon(std::uint16_t value) noexcept {
        const std::uint16_t payload = value & dmacon_writable;
        if ((value & dmacon_set) != 0U) {
            dmacon_ = static_cast<std::uint16_t>(dmacon_ | payload);
        } else {
            dmacon_ = static_cast<std::uint16_t>(dmacon_ & ~payload);
        }
        // Master+copper enable starts the copper from a stopped state; losing
        // either parks it.
        const bool running = dma_copper();
        if (running && !copper_running_) {
            copper_pc_ = cop1lc_;
            copper_running_ = true;
        } else if (!running) {
            copper_running_ = false;
        }
    }

    std::uint16_t agnus::read_dmaconr() const noexcept {
        std::uint16_t value = dmacon_ & dmacon_writable;
        if (blitter_busy_) {
            value = static_cast<std::uint16_t>(value | dmacon_bbusy);
        }
        if (blitter_zero_) {
            value = static_cast<std::uint16_t>(value | dmacon_bzero);
        }
        return value;
    }

    std::uint16_t agnus::read_vposr() const noexcept {
        std::uint16_t value = 0U;
        if (long_frame_) {
            value |= 0x8000U;
        }
        value |= static_cast<std::uint16_t>((scanline_ >> 8U) & 0x0001U);
        return value;
    }

    std::uint16_t agnus::read_vhposr() const noexcept {
        auto value = static_cast<std::uint16_t>((scanline_ & 0x00FFU) << 8U);
        value = static_cast<std::uint16_t>(value | (color_clock_ & 0x00FFU));
        return value;
    }

    bool agnus::dma_master() const noexcept { return (dmacon_ & dmacon_dmaen) != 0U; }

    namespace {
        [[nodiscard]] bool dma_both(std::uint16_t dmacon, std::uint16_t channel_bit) noexcept {
            return (dmacon & agnus::dmacon_dmaen) != 0U && (dmacon & channel_bit) != 0U;
        }
    } // namespace

    bool agnus::dma_bitplane() const noexcept { return dma_both(dmacon_, dmacon_bplen); }
    bool agnus::dma_copper() const noexcept { return dma_both(dmacon_, dmacon_copen); }
    bool agnus::dma_blitter() const noexcept { return dma_both(dmacon_, dmacon_blten); }
    bool agnus::dma_sprite() const noexcept { return dma_both(dmacon_, dmacon_spren); }
    bool agnus::dma_disk() const noexcept { return dma_both(dmacon_, dmacon_dsken); }

    bool agnus::dma_audio(int channel) const noexcept {
        if (channel < 0 || channel > 3) {
            return false;
        }
        constexpr std::array<std::uint16_t, 4> channel_bits{dmacon_aud0en, dmacon_aud1en,
                                                            dmacon_aud2en, dmacon_aud3en};
        return dma_both(dmacon_, channel_bits[static_cast<std::size_t>(channel)]);
    }

    // --- display registers -------------------------------------------------

    void agnus::set_bitplane_pointer(std::uint32_t plane, std::uint32_t byte_address) noexcept {
        if (plane < max_bitplanes) {
            bitplane_pointer_[plane] = byte_address & 0x001FFFFEU;
        }
    }

    std::uint32_t agnus::bitplane_count() const noexcept {
        const std::uint32_t bpu = (bplcon0_ >> 12U) & 0x07U;
        return std::min<std::uint32_t>(bpu, max_bitplanes);
    }

    // --- copper ------------------------------------------------------------

    void agnus::strobe_copjmp1() noexcept {
        copper_pc_ = cop1lc_;
        copper_running_ = dma_copper();
    }

    void agnus::strobe_copjmp2() noexcept {
        copper_pc_ = cop2lc_;
        copper_running_ = dma_copper();
    }

    std::uint16_t agnus::chip_word(std::uint32_t byte_address) const noexcept {
        const std::size_t a = byte_address & 0x001FFFFEU;
        if (a + 1U >= chip_ram_.size()) {
            return 0U;
        }
        // Big-endian word.
        return static_cast<std::uint16_t>((chip_ram_[a] << 8U) | chip_ram_[a + 1U]);
    }

    void agnus::run_copper() noexcept {
        if (!copper_running_ || !dma_copper()) {
            copper_running_ = dma_copper() && copper_running_;
            return;
        }
        if (chip_ram_.empty()) {
            return;
        }
        // One instruction (two words) per invocation when not blocked. The
        // beam comparison uses the raw 8-bit VP and 7-bit HP of the current
        // position, exactly as the reference copper.
        const std::uint16_t beam_vp = static_cast<std::uint16_t>(scanline_ & 0x00FFU);
        const std::uint16_t beam_hp = static_cast<std::uint16_t>(color_clock_ & 0x007FU);

        const std::uint16_t ir1 = chip_word(copper_pc_);
        const std::uint16_t ir2 = chip_word(copper_pc_ + 2U);

        const bool is_move = (ir1 & 0x0001U) == 0U;
        if (is_move) {
            const std::uint16_t reg_addr = static_cast<std::uint16_t>(ir1 & 0x01FEU);
            const bool below_danger = reg_addr < 0x0080U;
            if (!(below_danger && !copper_danger_)) {
                apply_copper_move(reg_addr, ir2);
            }
            copper_pc_ = (copper_pc_ + 4U) & 0x001FFFFEU;
            return;
        }

        // WAIT / SKIP. Latch the comparator (VE bit 7 forced on per the
        // manual) and compare against the beam.
        const std::uint16_t wait_vp = static_cast<std::uint16_t>((ir1 >> 8U) & 0x00FFU);
        const std::uint16_t wait_hp = static_cast<std::uint16_t>((ir1 >> 1U) & 0x007FU);
        const std::uint16_t wait_ve = static_cast<std::uint16_t>(((ir2 >> 8U) & 0x007FU) | 0x0080U);
        const std::uint16_t wait_he = static_cast<std::uint16_t>((ir2 >> 1U) & 0x007FU);
        const bool wait_bfd = (ir2 & copper_bfd_mask) != 0U;
        const bool is_skip = (ir2 & 0x0001U) != 0U;

        const std::uint16_t vp_beam = static_cast<std::uint16_t>(beam_vp & wait_ve);
        const std::uint16_t vp_target = static_cast<std::uint16_t>(wait_vp & wait_ve);
        const std::uint16_t hp_beam = static_cast<std::uint16_t>(beam_hp & wait_he);
        const std::uint16_t hp_target = static_cast<std::uint16_t>(wait_hp & wait_he);

        bool past = true;
        if (vp_beam < vp_target) {
            past = false;
        } else if (vp_beam == vp_target && hp_beam < hp_target) {
            past = false;
        } else if (!wait_bfd && blitter_busy_) {
            past = false;
        }

        if (is_skip) {
            copper_pc_ = (copper_pc_ + 4U) & 0x001FFFFEU;
            if (past) {
                // Skip the next instruction's two words as well.
                copper_pc_ = (copper_pc_ + 4U) & 0x001FFFFEU;
            }
            return;
        }
        // WAIT: advance past the instruction only when the target is reached;
        // otherwise stall (re-evaluate on the next clock).
        if (past) {
            copper_pc_ = (copper_pc_ + 4U) & 0x001FFFFEU;
        }
    }

    void agnus::apply_copper_move(std::uint16_t reg_addr, std::uint16_t value) noexcept {
        switch (reg_addr) {
        case reg_dmacon:
            write_dmacon(value);
            return;
        case reg_bplcon0:
            bplcon0_ = value;
            return;
        case reg_bpl1mod:
            modulo_odd_ = static_cast<std::int16_t>(value);
            return;
        case reg_bpl2mod:
            modulo_even_ = static_cast<std::int16_t>(value);
            return;
        case reg_diwstrt:
            diwstrt_ = value;
            return;
        case reg_diwstop:
            diwstop_ = value;
            return;
        case reg_ddfstrt:
            ddfstrt_ = value;
            return;
        case reg_ddfstop:
            ddfstop_ = value;
            return;
        default:
            break;
        }
        // Bitplane pointers: BPL1PTH..BPL6PTL occupy a contiguous run of
        // high/low word pairs (4 bytes apart). The high word is the upper
        // bits, the low word the lower bits of the 20-bit chip-RAM pointer.
        if (reg_addr >= reg_bplpth_base && reg_addr < reg_bplpth_base + max_bitplanes * 4U) {
            const std::uint32_t idx = (reg_addr - reg_bplpth_base) / 4U;
            const bool is_high = ((reg_addr - reg_bplpth_base) & 0x02U) == 0U;
            if (is_high) {
                bitplane_pointer_[idx] = ((static_cast<std::uint32_t>(value) & 0x001FU) << 16U) |
                                         (bitplane_pointer_[idx] & 0xFFFEU);
            } else {
                bitplane_pointer_[idx] = (bitplane_pointer_[idx] & 0x001F0000U) |
                                         (static_cast<std::uint32_t>(value) & 0xFFFEU);
            }
            return;
        }
        // Colour palette: COLOR00..COLOR31, 2 bytes apart. Mirror into the
        // attached palette span only when writable storage is provided; the
        // render path reads the palette span directly, so a copper MOVE to a
        // colour register is honoured by the board mapping that span. When no
        // mutable palette is wired the MOVE is a no-op here (the board owns
        // the colour RAM cell). Nothing to do in this read-only model.
        (void)reg_color_base;
    }

    // --- rendering ---------------------------------------------------------

    std::uint16_t agnus::palette_word(std::uint32_t index) const noexcept {
        const std::size_t a = static_cast<std::size_t>(index) * 2U;
        if (a + 1U >= palette_.size()) {
            return 0U;
        }
        // Big-endian colour word.
        return static_cast<std::uint16_t>((palette_[a] << 8U) | palette_[a + 1U]);
    }

    std::uint32_t agnus::color_to_rgb(std::uint16_t color12) noexcept {
        const std::uint32_t r = expand4(static_cast<std::uint8_t>((color12 >> 8U) & 0x0FU));
        const std::uint32_t g = expand4(static_cast<std::uint8_t>((color12 >> 4U) & 0x0FU));
        const std::uint32_t b = expand4(static_cast<std::uint8_t>(color12 & 0x0FU));
        return (r << 16U) | (g << 8U) | b;
    }

    void agnus::render_frame() noexcept {
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
        // Background everywhere first (colour index 0).
        const std::uint32_t backdrop = color_to_rgb(palette_word(0U));
        for (std::uint32_t& px : pixels_) {
            px = backdrop;
        }

        const std::uint32_t planes = bitplane_count();
        // Bitplane DMA only contributes when master + bitplane DMA are on and
        // a non-empty chip RAM is attached.
        if (planes == 0U || !dma_bitplane() || chip_ram_.empty()) {
            return;
        }

        const std::uint32_t height = active_height();

        // Display window: DIWSTRT/DIWSTOP give the visible vertical band; the
        // horizontal start positions the first fetched word. The data-fetch
        // window (DDFSTRT/DDFSTOP) bounds the per-line word count: each step
        // of 8 color clocks fetches one 16-pixel low-resolution word.
        const std::uint32_t diw_v_start = (diwstrt_ >> 8U) & 0xFFU;
        const std::uint32_t diw_v_stop_raw = (diwstop_ >> 8U) & 0xFFU;
        // DIWSTOP V bit 8 is the complement of bit 7 (so stops >= 0x100 are
        // expressible); reconstruct the 9-bit stop.
        const std::uint32_t diw_v_stop =
            diw_v_stop_raw | (((diw_v_stop_raw & 0x80U) == 0U) ? 0x100U : 0U);

        const std::uint32_t ddf_start = ddfstrt_ & 0xFFU;
        const std::uint32_t ddf_stop = ddfstop_ & 0xFFU;
        // Words per line = ((stop - start) / 8) + 1, clamped to the visible
        // width.
        std::uint32_t words_per_line = 0U;
        if (ddf_stop >= ddf_start) {
            words_per_line = ((ddf_stop - ddf_start) / 8U) + 1U;
        }
        const std::uint32_t max_words = visible_width / 16U;
        words_per_line = std::min(words_per_line, max_words);

        // Live per-plane pointers walked across the frame.
        std::array<std::uint32_t, max_bitplanes> ptr = bitplane_pointer_;

        for (std::uint32_t line = 0; line < height; ++line) {
            const std::uint32_t beam_line = line + display_line_origin;
            const bool line_visible = beam_line >= diw_v_start && beam_line < diw_v_stop;

            for (std::uint32_t word = 0; word < words_per_line; ++word) {
                // Fetch one word per active plane for this cell.
                std::array<std::uint16_t, max_bitplanes> data{};
                for (std::uint32_t p = 0; p < planes; ++p) {
                    data[p] = chip_word(ptr[p]);
                    ptr[p] = (ptr[p] + 2U) & 0x001FFFFEU;
                }
                if (!line_visible) {
                    continue;
                }
                const std::uint32_t base_x = word * 16U;
                for (std::uint32_t bit = 0; bit < 16U; ++bit) {
                    const std::uint32_t x = base_x + (15U - bit);
                    if (x >= visible_width) {
                        continue;
                    }
                    std::uint32_t index = 0U;
                    for (std::uint32_t p = 0; p < planes; ++p) {
                        index |= ((data[p] >> bit) & 1U) << p;
                    }
                    // Colour index 0 already painted as the backdrop.
                    if (index == 0U) {
                        continue;
                    }
                    pixels_[static_cast<std::size_t>(line) * visible_width + x] =
                        color_to_rgb(palette_word(index));
                }
            }

            // End-of-line modulo: odd planes (1,3,5 => pointer indices 0,2,4)
            // add BPL1MOD; even planes add BPL2MOD.
            for (std::uint32_t p = 0; p < planes; ++p) {
                const std::int16_t modulo = ((p & 1U) == 0U) ? modulo_odd_ : modulo_even_;
                ptr[p] = (static_cast<std::uint32_t>(static_cast<std::int32_t>(ptr[p]) + modulo)) &
                         0x001FFFFEU;
            }
        }
    }

    frame_buffer_view agnus::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = active_height(),
                .stride = visible_width};
    }

    // --- state -------------------------------------------------------------

    void agnus::save_state(state_writer& writer) const {
        writer.u16(dmacon_);
        writer.boolean(blitter_busy_);
        writer.boolean(blitter_zero_);
        writer.boolean(is_pal_);
        writer.u32(scanline_);
        writer.u32(color_clock_);
        writer.boolean(long_frame_);
        writer.u64(frame_index_);
        for (std::uint32_t p : bitplane_pointer_) {
            writer.u32(p);
        }
        writer.u16(static_cast<std::uint16_t>(modulo_even_));
        writer.u16(static_cast<std::uint16_t>(modulo_odd_));
        writer.u16(bplcon0_);
        writer.u16(diwstrt_);
        writer.u16(diwstop_);
        writer.u16(ddfstrt_);
        writer.u16(ddfstop_);
        writer.u32(cop1lc_);
        writer.u32(cop2lc_);
        writer.u32(copper_pc_);
        writer.boolean(copper_running_);
        writer.boolean(copper_danger_);
    }

    void agnus::load_state(state_reader& reader) {
        dmacon_ = reader.u16();
        blitter_busy_ = reader.boolean();
        blitter_zero_ = reader.boolean();
        is_pal_ = reader.boolean();
        scanline_ = reader.u32();
        color_clock_ = reader.u32();
        long_frame_ = reader.boolean();
        frame_index_ = reader.u64();
        for (std::uint32_t& p : bitplane_pointer_) {
            p = reader.u32();
        }
        modulo_even_ = static_cast<std::int16_t>(reader.u16());
        modulo_odd_ = static_cast<std::int16_t>(reader.u16());
        bplcon0_ = reader.u16();
        diwstrt_ = reader.u16();
        diwstop_ = reader.u16();
        ddfstrt_ = reader.u16();
        ddfstop_ = reader.u16();
        cop1lc_ = reader.u32();
        cop2lc_ = reader.u32();
        copper_pc_ = reader.u32();
        copper_running_ = reader.boolean();
        copper_danger_ = reader.boolean();
        refresh_blank_flags();
    }

    instrumentation::ichip_introspection& agnus::introspection() noexcept { return introspection_; }

    frame_buffer_view agnus::introspection_surface::palette_layer::view() const {
        // Decode the 32 palette entries as a 16-per-row swatch grid, each
        // entry an 8x8 solid block. Rebuilt on each call.
        const agnus& a = *owner_;
        constexpr std::uint32_t per_row = 16U;
        constexpr std::uint32_t cell = 8U;
        constexpr std::uint32_t sheet_width = per_row * cell;
        constexpr std::uint32_t sheet_height = (palette_entries / per_row) * cell;
        a.palette_sheet_.assign(static_cast<std::size_t>(sheet_width) * sheet_height, 0U);

        for (std::uint32_t entry = 0; entry < palette_entries; ++entry) {
            const std::uint32_t rgb = color_to_rgb(a.palette_word(entry));
            const std::uint32_t base_x = (entry % per_row) * cell;
            const std::uint32_t base_y = (entry / per_row) * cell;
            for (std::uint32_t cy = 0; cy < cell; ++cy) {
                for (std::uint32_t cx = 0; cx < cell; ++cx) {
                    a.palette_sheet_[static_cast<std::size_t>(base_y + cy) * sheet_width + base_x +
                                     cx] = rgb;
                }
            }
        }
        return {.pixels = a.palette_sheet_.data(),
                .width = sheet_width,
                .height = sheet_height,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto agnus_registration =
            register_factory("commodore.agnus", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<agnus>(); });
    } // namespace

} // namespace mnemos::chips::video
