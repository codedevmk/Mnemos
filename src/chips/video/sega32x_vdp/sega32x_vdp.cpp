#include "sega32x_vdp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::video {

    namespace {
        // Expand a 5-bit channel to 8 bits (replicate the high bits).
        [[nodiscard]] constexpr std::uint32_t expand5(std::uint32_t ch5) noexcept {
            return (ch5 << 3U) | (ch5 >> 2U);
        }

        // 15bpp BGR (bit 14-10 = B, 9-5 = G, 4-0 = R) to 0x00RRGGBB.
        [[nodiscard]] constexpr std::uint32_t bgr15_to_rgb(std::uint16_t bgr15) noexcept {
            const std::uint32_t b = expand5((bgr15 >> 10U) & 0x1FU);
            const std::uint32_t g = expand5((bgr15 >> 5U) & 0x1FU);
            const std::uint32_t r = expand5(bgr15 & 0x1FU);
            return (r << 16U) | (g << 8U) | b;
        }
    } // namespace

    chip_metadata sega32x_vdp::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "315-5788",
            .family = "VDP",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void sega32x_vdp::reset(reset_kind /*kind*/) {
        palette_.fill(0U);
        bitmap_mode_ = 0U;
        screen_shift_ = 0U;
        autofill_length_ = 0U;
        autofill_addr_ = 0U;
        autofill_data_ = 0U;
        fb_control_ = 0U;
        fen_busy_reads_ = 0;
        pending_fs_ = 0U;
        prev_vblank_ = false;
    }

    std::uint16_t sega32x_vdp::read16(std::uint32_t off) const noexcept {
        switch (off & 0x0EU) {
        case reg_bitmap_mode:
            // Bit 15 is the read-only NTSC/PAL pin mirror: 1 = NTSC, 0 = PAL.
            // Retail region checks halt on the "for use only with NTSC
            // systems" screen when it reads 0 on an NTSC machine.
            return static_cast<std::uint16_t>(bitmap_mode_ | (pal_ ? 0x0000U : 0x8000U));
        case reg_screen_shift:
            return screen_shift_;
        case reg_autofill_length:
            return autofill_length_;
        case reg_autofill_addr:
            return autofill_addr_;
        case reg_autofill_data:
            return autofill_data_;
        case reg_fb_control:
            if (fen_busy_reads_ > 0) {
                --fen_busy_reads_;
                return static_cast<std::uint16_t>(fb_control_ | 0x0002U);
            }
            return fb_control_;
        default:
            return 0U;
        }
    }

    void sega32x_vdp::write16(std::uint32_t off, std::uint16_t val) noexcept {
        switch (off & 0x0EU) {
        case reg_bitmap_mode:
            // Bit 15 is the read-only PAL/NTSC mirror from the Genesis VDP.
            bitmap_mode_ = val & 0x7FFFU;
            break;
        case reg_screen_shift:
            // Only bit 0 means anything on hardware; hold the upper bits as
            // written for round-trip fidelity.
            screen_shift_ = val;
            break;
        case reg_autofill_length:
            autofill_length_ = val & 0x00FFU; // low byte only on hardware
            break;
        case reg_autofill_addr:
            autofill_addr_ = val;
            break;
        case reg_autofill_data:
            // Latch only; the bus glue runs autofill_execute against the
            // externally owned frame buffer.
            autofill_data_ = val;
            break;
        case reg_fb_control:
            // The only CPU-writable bit is FS (bit 0). During active display
            // it latches and commits at the next V-blank rising edge; written
            // DURING V-blank it takes effect immediately -- V-blank handlers
            // flip right after finishing a frame and expect the swap before
            // the next scanout. FEN (bit 1) is driven by the autofill engine;
            // HBLK/VBLK (14/15) by set_blanking.
            pending_fs_ = static_cast<std::uint8_t>(val & 0x0001U);
            if ((fb_control_ & 0x8000U) != 0U) { // VBLK active: commit now
                fb_control_ = static_cast<std::uint16_t>((fb_control_ & ~0x0001U) | pending_fs_);
            }
            break;
        default:
            break;
        }
    }

    std::uint16_t sega32x_vdp::palette_read16(std::uint32_t pal_off) const noexcept {
        return palette_[(pal_off & 0x1FEU) >> 1U];
    }

    void sega32x_vdp::palette_write16(std::uint32_t pal_off, std::uint16_t val) noexcept {
        palette_[(pal_off & 0x1FEU) >> 1U] = val;
    }

    std::uint32_t sega32x_vdp::autofill_execute(std::span<std::uint8_t> fb) noexcept {
        if (fb.size() < 2U) {
            return 0U;
        }

        // Word-aligned start; fill count = length + 1. The address-increment
        // quirk bumps the low byte only, so every written word shares the top
        // byte of the starting address (the fill sweeps one 256-word row).
        const std::uint16_t top = autofill_addr_ & 0xFF00U;
        auto low = static_cast<std::uint8_t>(autofill_addr_ & 0xFFU);
        const std::uint32_t count = (autofill_length_ & 0x00FFU) + 1U;

        std::uint32_t written = 0U;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t word_off = top | low;
            const std::uint32_t byte_off = word_off * 2U; // fb is byte-addressed
            if (byte_off + 1U < fb.size()) {
                fb[byte_off] = static_cast<std::uint8_t>(autofill_data_ >> 8U); // big-endian
                fb[byte_off + 1U] = static_cast<std::uint8_t>(autofill_data_);
                ++written;
            }
            low = static_cast<std::uint8_t>(low + 1U); // wraps within the row
        }

        // Hardware latches the final address back after the fill ends.
        autofill_addr_ = static_cast<std::uint16_t>(top | low);
        // FEN (bit 1) = fill in progress. The fill itself completes instantly
        // here, but pollers must still OBSERVE the busy edge: one waiter idiom
        // spins until FEN goes 1 (fill started) before a later not-busy wait,
        // the other spins until FEN reads 0 (fill done). Latch a few busy
        // reads so both sequences resolve deterministically.
        fen_busy_reads_ = 4;
        return written;
    }

    void sega32x_vdp::set_blanking(bool in_hblank, bool in_vblank) noexcept {
        std::uint16_t c = fb_control_ & static_cast<std::uint16_t>(~0xC000U);
        if (in_hblank) {
            c |= 0x4000U; // HBLK
        }
        if (in_vblank) {
            c |= 0x8000U; // VBLK
        }
        // The FS flip-flop commits on the V-blank leading edge so the bank
        // swap never tears the visible image.
        if (in_vblank && !prev_vblank_) {
            c = static_cast<std::uint16_t>((c & ~0x0001U) | (pending_fs_ & 0x0001U));
        }
        prev_vblank_ = in_vblank;
        fb_control_ = c;
    }

    void sega32x_vdp::compose_scanline(std::span<const std::uint8_t> fb,
                                       std::span<std::uint32_t> row, int y,
                                       const std::uint8_t* genesis_backdrop) const noexcept {
        if (row.empty() || y < 0 || y >= visible_h) {
            return;
        }
        const std::uint8_t m = mode();
        if (m == mode_off) {
            return; // display passthrough, the Genesis row is final
        }

        // A "behind" (priority-0) pixel draws only where the Genesis output is
        // the backdrop. With a real per-pixel backdrop row (the cartridge
        // connector's "transparent" signal) opaque-but-black Genesis pixels
        // correctly hide the 32X pixel -- games park work data in FB rows the
        // status bar covers. Without one, fall back to the colour heuristic
        // (black == backdrop).
        const auto genesis_has_content = [&row, genesis_backdrop](std::size_t x) {
            if (genesis_backdrop != nullptr) {
                return genesis_backdrop[x] == 0U;
            }
            return (row[x] & 0x00FFFFFFU) != 0U;
        };

        // Every bitmap mode locates a row through the line table at the start
        // of the displayed bank: one big-endian word per line giving the row
        // data's word offset within the bank.
        const std::size_t bank_base = static_cast<std::size_t>(display_bank()) * 0x20000U;
        const std::size_t table_off = bank_base + static_cast<std::size_t>(y) * 2U;
        if (table_off + 1U >= fb.size()) {
            return;
        }
        const std::size_t line_word_off =
            (static_cast<std::size_t>(fb[table_off]) << 8U) | fb[table_off + 1U];

        if (m == mode_packed) {
            // Packed 8bpp palette pixels, one byte each.
            const std::size_t row_base = bank_base + line_word_off * 2U;
            const std::size_t cols =
                row.size() < static_cast<std::size_t>(visible_w) ? row.size() : visible_w;
            for (std::size_t x = 0; x < cols; ++x) {
                const std::size_t bo = row_base + x;
                if (bo >= fb.size()) {
                    break;
                }
                const std::uint8_t idx = fb[bo];
                if (idx == 0U) {
                    continue; // palette index 0 is transparent
                }
                const std::uint16_t pal = palette_[idx];
                if ((pal & 0x8000U) == 0U && genesis_has_content(x)) {
                    continue;
                }
                row[x] = bgr15_to_rgb(pal & 0x7FFFU);
            }
            return;
        }

        if (m == mode_direct) {
            // Direct 15bpp colour, one big-endian word per pixel. 320 x 224 x 2
            // bytes exceeds one 128 KiB bank, so rows commonly spill past the
            // bank boundary -- index from the bank-relative row offset without
            // clamping to the bank.
            const std::size_t row_base = bank_base + line_word_off * 2U;
            const std::size_t cols =
                row.size() < static_cast<std::size_t>(visible_w) ? row.size() : visible_w;
            for (std::size_t x = 0; x < cols; ++x) {
                const std::size_t bo = row_base + x * 2U;
                if (bo + 1U >= fb.size()) {
                    break;
                }
                const auto pixel = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(fb[bo]) << 8U) | fb[bo + 1U]);
                if (pixel == 0U) {
                    continue; // word 0 is transparent
                }
                if ((pixel & 0x8000U) == 0U && genesis_has_content(x)) {
                    continue;
                }
                row[x] = bgr15_to_rgb(pixel & 0x7FFFU);
            }
            return;
        }

        // Run-length mode: rows are 16-bit entries of high byte = palette
        // index, low byte = run length - 1.
        std::size_t entry = bank_base + line_word_off * 2U;
        const std::size_t cols =
            row.size() < static_cast<std::size_t>(visible_w) ? row.size() : visible_w;
        std::size_t x = 0;
        while (x < cols && entry + 1U < fb.size()) {
            const std::uint8_t idx = fb[entry];
            const std::size_t run = static_cast<std::size_t>(fb[entry + 1U]) + 1U;
            entry += 2U;
            for (std::size_t k = 0; k < run && x < cols; ++k, ++x) {
                if (idx == 0U) {
                    continue;
                }
                const std::uint16_t pal = palette_[idx];
                if ((pal & 0x8000U) == 0U && genesis_has_content(x)) {
                    continue;
                }
                row[x] = bgr15_to_rgb(pal & 0x7FFFU);
            }
        }
    }

    void sega32x_vdp::save_state(state_writer& writer) const {
        for (const std::uint16_t entry : palette_) {
            writer.u16(entry);
        }
        writer.u16(bitmap_mode_);
        writer.u16(screen_shift_);
        writer.u16(autofill_length_);
        writer.u16(autofill_addr_);
        writer.u16(autofill_data_);
        writer.u16(fb_control_);
        writer.u8(pending_fs_);
        writer.u8(prev_vblank_ ? 1U : 0U);
        // FEN read-back window: a save taken inside the post-autofill transient
        // busy reads must replay the same number of busy reads after load.
        writer.u32(static_cast<std::uint32_t>(fen_busy_reads_));
    }

    void sega32x_vdp::load_state(state_reader& reader) {
        for (std::uint16_t& entry : palette_) {
            entry = reader.u16();
        }
        bitmap_mode_ = reader.u16();
        screen_shift_ = reader.u16();
        autofill_length_ = reader.u16();
        autofill_addr_ = reader.u16();
        autofill_data_ = reader.u16();
        fb_control_ = reader.u16();
        pending_fs_ = reader.u8();
        prev_vblank_ = reader.u8() != 0U;
        fen_busy_reads_ = static_cast<int>(reader.u32());
    }

    std::span<const register_descriptor> sega32x_vdp::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"BITMAP_MODE", bitmap_mode_, 16U, fmt::flags};
        register_view_[1] = {"SSCR", screen_shift_, 16U, fmt::flags};
        register_view_[2] = {"AFLR", autofill_length_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"AFAR", autofill_addr_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"AFDR", autofill_data_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"FBCR", fb_control_, 16U, fmt::flags};
        register_view_[6] = {"PENDING_FS", pending_fs_, 1U, fmt::flags};
        return register_view_;
    }

    instrumentation::ichip_introspection& sega32x_vdp::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto sega32x_vdp_registration =
            register_factory("sega.315_5788", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<sega32x_vdp>();
            });
    } // namespace

} // namespace mnemos::chips::video
