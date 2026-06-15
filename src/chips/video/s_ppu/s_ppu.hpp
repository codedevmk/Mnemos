#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Sony S-PPU (SNES picture-processing unit; the 5C77 + 5C78 pair sharing a
    // single $2100..$213F register window, modelled here as one chip).
    //
    // Ported from the Emu reference (chips/s_ppu); clean-room per public SNES
    // S-PPU documentation. The reference contributes the register-facing side
    // verbatim (VRAM / CGRAM / OAM memories + the CPU-side auto-increment and
    // double-write latches every game drives); the renderer below is the
    // clean-room character/colour path the reference left as a follow-up.
    //
    // A 256x224 visible raster over a 340x262 dot grid (~60 Hz NTSC); tick(dots)
    // advances the beam one dot per cycle and renders the completed frame the
    // moment the beam enters the first vblank line (frame-at-once, like the
    // sibling tilemap unit), bumping frame_index(). The scanline callback fires
    // at the start of EVERY line with its number so the board can derive its
    // V-blank / H/V-IRQ pulses; the vblank callback fires once at the top of the
    // vblank region.
    //
    // Memory model:
    //   * VRAM is 32 K little-endian words. A character ("tile") is 8x8; a 4bpp
    //     character is 16 words: words 0..7 carry bitplanes 0 and 1 (low byte =
    //     plane 0, high byte = plane 1) for rows 0..7, words 8..15 carry planes
    //     2 and 3. MSB is the leftmost pixel.
    //   * The tilemap is 32x32 entries of one word each: tile number (9:0),
    //     palette group (12:10), priority (13), flip-x (14), flip-y (15).
    //   * CGRAM is 256 little-endian 15-bit colour words, bbbbbgggggrrrrr;
    //     each 5-bit gun expands to 8 bits (top bits replicated low) and is then
    //     scaled by the INIDISP brightness (0..15, where 15 = full). Palette
    //     entry 0 of every group is the transparent backdrop pen.
    //   * OAM is 512 + 32 bytes (sprite evaluation lands with a follow-up).
    //
    // The board may attach non-owning spans to inject raw VRAM / CGRAM the
    // renderer reads in place of the internally-written memories (attach_vram /
    // attach_cgram); with nothing attached the renderer reads the memories the
    // CPU register port fills. Registers are driven through the $2100..$213F
    // window via write_register() / read_register(), exactly as the reference
    // models them.
    class s_ppu final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 256U;
        static constexpr std::uint32_t visible_height = 224U;
        static constexpr std::uint32_t line_dots = 340U;   // total dots per line
        static constexpr std::uint32_t frame_lines = 262U; // total lines per frame
        static constexpr std::uint32_t map_tiles = 32U;    // 32x32 tilemap
        static constexpr std::uint32_t tile_pixels = 8U;   // 8x8 character

        static constexpr std::size_t vram_words = 0x8000U; // 64 KiB as words
        static constexpr std::size_t cgram_words = 0x100U; // 256 colour entries
        static constexpr std::size_t oam_bytes = 0x220U;   // 512 + 32 bytes

        // $2100..$213F register offsets (low 6 bits of the window).
        static constexpr std::uint8_t reg_inidisp = 0x00U;
        static constexpr std::uint8_t reg_objsel = 0x01U;
        static constexpr std::uint8_t reg_oamaddl = 0x02U;
        static constexpr std::uint8_t reg_oamaddh = 0x03U;
        static constexpr std::uint8_t reg_oamdata = 0x04U;
        static constexpr std::uint8_t reg_bgmode = 0x05U;
        static constexpr std::uint8_t reg_mosaic = 0x06U;
        static constexpr std::uint8_t reg_bg1sc = 0x07U;
        static constexpr std::uint8_t reg_bg2sc = 0x08U;
        static constexpr std::uint8_t reg_bg3sc = 0x09U;
        static constexpr std::uint8_t reg_bg4sc = 0x0AU;
        static constexpr std::uint8_t reg_bg12nba = 0x0BU;
        static constexpr std::uint8_t reg_bg34nba = 0x0CU;
        static constexpr std::uint8_t reg_bg1hofs = 0x0DU;
        static constexpr std::uint8_t reg_bg1vofs = 0x0EU;
        static constexpr std::uint8_t reg_bg2hofs = 0x0FU;
        static constexpr std::uint8_t reg_bg2vofs = 0x10U;
        static constexpr std::uint8_t reg_bg3hofs = 0x11U;
        static constexpr std::uint8_t reg_bg3vofs = 0x12U;
        static constexpr std::uint8_t reg_bg4hofs = 0x13U;
        static constexpr std::uint8_t reg_bg4vofs = 0x14U;
        static constexpr std::uint8_t reg_vmain = 0x15U;
        static constexpr std::uint8_t reg_vmaddl = 0x16U;
        static constexpr std::uint8_t reg_vmaddh = 0x17U;
        static constexpr std::uint8_t reg_vmdatal = 0x18U;
        static constexpr std::uint8_t reg_vmdatah = 0x19U;
        static constexpr std::uint8_t reg_cgadd = 0x21U;
        static constexpr std::uint8_t reg_cgdata = 0x22U;
        static constexpr std::uint8_t reg_setini = 0x33U;

        // INIDISP bits.
        static constexpr std::uint8_t inidisp_force_blank = 0x80U;
        static constexpr std::uint8_t inidisp_brightness = 0x0FU;

        // VMAIN bits.
        static constexpr std::uint8_t vmain_step_mask = 0x03U;   // 00=+1 01=+32 1x=+128
        static constexpr std::uint8_t vmain_inc_on_high = 0x80U; // 0=inc on $2118, 1=on $2119

        using line_callback = std::function<void(std::uint32_t line)>;

        s_ppu() { reset(reset_kind::power_on); }

        // ichip
        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // ivideo
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // Board attachment (non-owning; the board's memory outlives the chip).
        // When a span is attached the renderer reads it in place of the
        // CPU-written internal memory, letting the board inject raw frames.
        void attach_vram(std::span<const std::uint8_t> vram) noexcept { vram_ext_ = vram; }
        void attach_cgram(std::span<const std::uint8_t> cgram) noexcept { cgram_ext_ = cgram; }

        // $2100..$213F register port (`reg` = low 6 bits of the window).
        void write_register(std::uint8_t reg, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_register(std::uint8_t reg) const noexcept;

        // Fired at the start of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired at the start of the first vblank line (after the frame renders).
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return beam_y_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return beam_x_; }

        // Decoded-register peeks, for tests and tools.
        [[nodiscard]] bool force_blank() const noexcept { return force_blank_; }
        [[nodiscard]] std::uint8_t brightness() const noexcept { return brightness_; }
        [[nodiscard]] std::uint16_t vram_address() const noexcept { return vram_addr_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(s_ppu& owner) noexcept : sheet_(owner) {}
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                layer_ptr_[0] = &sheet_;
                return layer_ptr_;
            }

          private:
            class char_sheet_layer final : public instrumentation::debug_layer {
              public:
                explicit char_sheet_layer(s_ppu& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::string_view name() const noexcept override { return "vram"; }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                s_ppu* owner_;
            };

            char_sheet_layer sheet_;
            std::array<instrumentation::debug_layer*, 1> layer_ptr_{};
        };

        void render_frame() noexcept;
        void render_bg1() noexcept;

        // VRAM word access, preferring an attached span over internal memory.
        [[nodiscard]] std::uint16_t vram_word(std::size_t word_index) const noexcept;
        // 15-bit CGRAM colour -> 0x00RRGGBB with brightness scaling.
        [[nodiscard]] std::uint32_t lookup_rgb(std::size_t index) const noexcept;

        [[nodiscard]] static std::uint16_t vram_increment(std::uint8_t vmain) noexcept;

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(visible_width) * visible_height);
        // The decoded character-sheet debug view, rebuilt on demand.
        mutable std::vector<std::uint32_t> char_sheet_;
        mutable std::uint32_t char_sheet_height_{};

        // Internal memories the CPU register port fills.
        std::array<std::uint16_t, vram_words> vram_{};
        std::array<std::uint16_t, cgram_words> cgram_{};
        std::array<std::uint8_t, oam_bytes> oam_{};
        std::array<std::uint8_t, 0x40> regs_{};

        // Optional board-injected memory the renderer reads in place of the above.
        std::span<const std::uint8_t> vram_ext_{};
        std::span<const std::uint8_t> cgram_ext_{};

        // CPU-side addressing state (ported integer-exact from the reference).
        std::uint16_t vram_addr_{};  // word address
        std::uint8_t vmain_{};       // raw VMAIN register
        std::uint16_t cgram_addr_{}; // byte address (wraps at 512)
        std::uint8_t cgram_latch_{}; // first-byte buffer for word writes
        bool cgram_latch_high_{};    // next write completes the colour word
        std::uint16_t oam_addr_{};   // 10-bit OAM address
        std::uint8_t oam_latch_{};   // low-byte latch for word writes
        bool oam_latch_high_{};
        std::uint8_t bg_scroll_latch_{}; // BG scroll double-write latch

        // Decoded view.
        std::uint8_t inidisp_{};
        std::uint8_t bgmode_{};
        bool force_blank_{true};
        std::uint8_t brightness_{};
        std::array<std::uint16_t, 4> bg_hofs_{};
        std::array<std::uint16_t, 4> bg_vofs_{};
        std::array<std::uint16_t, 4> bg_sc_base_{};   // tilemap word base per BG
        std::array<std::uint16_t, 4> bg_char_base_{}; // character word base per BG

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
