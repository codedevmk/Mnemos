#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::video {

    // Sega 32X VDP -- the secondary video chip on the Mars adapter, separate
    // from the Genesis VDP. It owns a 256-entry 16-bit palette CRAM, a register
    // file selecting how the (externally stored) 256 KiB frame buffer decodes
    // into pixels, and an autofill engine that fires fills into that buffer.
    //
    // The pixel byte store itself lives on the 32X board (the SH-2s write it as
    // plain memory at $04000000), so the buffer is passed in by span where
    // needed. The register window sits at SH-2 $4100 (and the 68000's $A15180
    // mirror), the palette at SH-2 $4200.
    //
    // Frame output is composition: compose_scanline overlays the 32X pixel for
    // each x over an already-rendered Genesis row, honouring the per-pixel
    // priority bit -- priority gates layer ORDER, not visibility, so a
    // priority-0 pixel still shows wherever the Genesis row is at backdrop.
    class sega32x_vdp final : public ichip {
      public:
        // Register-file byte offsets inside the 16-byte window.
        static constexpr std::uint32_t reg_bitmap_mode = 0x00U;
        static constexpr std::uint32_t reg_screen_shift = 0x02U;
        static constexpr std::uint32_t reg_autofill_length = 0x04U;
        static constexpr std::uint32_t reg_autofill_addr = 0x06U;
        static constexpr std::uint32_t reg_autofill_data = 0x08U;
        static constexpr std::uint32_t reg_fb_control = 0x0AU;

        // Bitmap-mode field (low two bits of the bitmap-mode register).
        static constexpr std::uint8_t mode_off = 0U;    // Genesis passes through
        static constexpr std::uint8_t mode_packed = 1U; // 8bpp palette-indexed
        static constexpr std::uint8_t mode_direct = 2U; // 15bpp direct colour
        static constexpr std::uint8_t mode_rle = 3U;    // run-length packed

        static constexpr std::size_t palette_entries = 256U;
        static constexpr int visible_w = 320;
        static constexpr int visible_h = 224;

        sega32x_vdp() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        // The chip has no free-running state; it is slaved to the Genesis VDP's
        // beam through set_blanking and the composition calls.
        void tick(std::uint64_t /*cycles*/) override {}
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Register-file access (off = byte offset in the window, 16-bit cells
        // mirrored across it). Reads are side-effect-free; an AUTOFILL_DATA
        // write only latches -- the bus glue triggers autofill_execute, since
        // the frame buffer lives outside the chip.
        [[nodiscard]] std::uint16_t read16(std::uint32_t off) const noexcept;
        void write16(std::uint32_t off, std::uint16_t val) noexcept;

        // Palette CRAM access (pal_off = byte offset in the 512-byte window).
        [[nodiscard]] std::uint16_t palette_read16(std::uint32_t pal_off) const noexcept;
        void palette_write16(std::uint32_t pal_off, std::uint16_t val) noexcept;

        // Execute the autofill armed by the last AUTOFILL_DATA write: store
        // (length + 1) big-endian copies of the data word into `fb` starting at
        // the word offset in AUTOFILL_ADDR. The hardware quirk: only address
        // bits 0-7 increment (bits 8-15 hold), so the fill sweeps one 256-word
        // row and wraps. The final address latches back into AUTOFILL_ADDR.
        // Returns the number of words written.
        std::uint32_t autofill_execute(std::span<std::uint8_t> fb) noexcept;

        // Drive the FBCR status bits from the Genesis VDP's blanking state:
        // HBLK = bit 14, VBLK = bit 15 (read-only from the CPUs). The V-blank
        // rising edge also commits the pending frame-select write into FBCR
        // bit 0 -- the FS flip-flop that keeps double-buffered flips tear-free.
        void set_blanking(bool in_hblank, bool in_vblank) noexcept;

        // Overlay the 32X pixels for row `y` onto an already-rendered Genesis
        // row of 0x00RRGGBB pixels. Transparent 32X pixels (palette index 0 /
        // direct-colour word 0) leave the Genesis pixel; the per-pixel priority
        // bit selects over/behind, where "behind" still shows wherever the
        // Genesis pixel is black (the backdrop approximation).
        void compose_scanline(std::span<const std::uint8_t> fb, std::span<std::uint32_t> row, int y,
                              const std::uint8_t* genesis_backdrop = nullptr) const noexcept;

        // Video-standard pin: drives the read-only bit 15 of BITMAP_MODE
        // (1 = NTSC, 0 = PAL). The machine sets it from the Genesis config.
        void set_pal(bool pal) noexcept { pal_ = pal; }

        // Introspection / glue accessors.
        [[nodiscard]] std::uint8_t mode() const noexcept {
            return static_cast<std::uint8_t>(bitmap_mode_ & 0x03U);
        }
        // FBCR FS (bit 0) selects the frame-buffer bank the CPUs ACCESS through
        // the $04000000 window; the scanout displays the OTHER bank. Both flip
        // together at the V-blank FS commit, which is what makes the
        // double-buffer swap tear-free.
        [[nodiscard]] int access_bank() const noexcept {
            return static_cast<int>(fb_control_ & 0x1U);
        }
        [[nodiscard]] int display_bank() const noexcept { return access_bank() ^ 1; }
        [[nodiscard]] std::uint16_t fb_control() const noexcept { return fb_control_; }
        [[nodiscard]] std::uint16_t palette(std::size_t idx) const noexcept {
            return idx < palette_entries ? palette_[idx] : 0U;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(sega32x_vdp& owner) noexcept : registers_(owner) {}
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_;
            }

          private:
            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(sega32x_vdp& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override {
                    return owner_->register_snapshot();
                }

              private:
                sega32x_vdp* owner_;
            };

            registers_impl registers_;
        };

        std::array<std::uint16_t, palette_entries> palette_{}; // 15bpp BGR, bit 15 = priority

        std::uint16_t bitmap_mode_{};     // mode bits + PAL mirror (bit 15, read-only)
        std::uint16_t screen_shift_{};    // bit 0 = shift output one cell
        std::uint16_t autofill_length_{}; // fill word count - 1 (low byte)
        std::uint16_t autofill_addr_{};   // fill start, frame-buffer word offset
        std::uint16_t autofill_data_{};   // fill value
        std::uint16_t fb_control_{};      // FS (bit 0) + FEN (bit 1) + HBLK/VBLK (14/15)

        // FS writes latch here and commit to fb_control_ bit 0 at the V-blank
        // rising edge; reads meanwhile keep returning the displayed bank, so a
        // poll after writing FS correctly sees "still on the old bank".
        std::uint8_t pending_fs_{};
        bool pal_{};         // NTSC/PAL pin, mirrored into BITMAP_MODE bit 15
        bool prev_vblank_{}; // V-blank edge detector for the FS commit

        std::array<register_descriptor, 7> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
