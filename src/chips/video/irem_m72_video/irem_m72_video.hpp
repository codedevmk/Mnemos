#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Irem M72 tilemap video unit (discrete board logic, modelled as one chip).
    //
    // Two 64x64-tile scrolling playfields of 8x8 4bpp planar tiles over a
    // 384x256 visible raster (512x284 total) at the 8 MHz pixel clock
    // (32 MHz board crystal / 4), ~55 Hz. tick(cycles) advances one pixel per
    // cycle; entering the vblank region renders the completed frame
    // (frame-at-once; a scanline renderer arrives with raster-effect parity
    // work) and bumps frame_index(). The scanline callback fires at the start
    // of EVERY line with its number -- the board derives its one-line vblank
    // and raster-compare interrupt pulses from it.
    //
    // The board attaches non-owning spans for VRAM / sprite RAM / palette RAM
    // / graphics ROM. Formats per the real hardware:
    //   * VRAM entry, 2 little-endian words per tile: word 0 = tile code
    //     (13:0) + flip-x (14) + flip-y (15); word 1 = color (3:0) + priority
    //     group (7:6). The group selects which pens render above sprites.
    //   * Tile ROM is 4 sequential bitplane banks (each region.size()/4
    //     bytes); tile N row R is byte N*8+R of each plane, MSB leftmost,
    //     bank order LSB plane first.
    //   * Sprite RAM entry, 4 little-endian words: w0 = Y (8:0, screen y =
    //     384 - Y - 16*height); w1 = cell code; w2 = color (3:0) + flip-y
    //     (10) + flip-x (11) + log2 height-in-cells (13:12) + log2 width
    //     (15:14); w3 = X (9:0, screen x = X - 256, in the 512-wide beam
    //     space whose visible window starts at 64). A multi-cell sprite's
    //     column step is 8 codes, its row step 1; an entry of width W
    //     occupies W slots, and earlier entries draw on top.
    //   * Sprite ROM is 4 bitplane banks of 16x16 cells, 32 bytes per cell
    //     per plane: bytes 0-15 = rows 0-15 of the left 8x16 half, bytes
    //     16-31 = the right half, MSB leftmost.
    //   * Palette RAM holds 5-bit gun planes at +0x000 (R), +0x400 (G),
    //     +0x800 (B), entry mirroring per the disconnected A9. Sprites read
    //     palette A (board 0xC8000), the playfields read palette B (0xCC000).
    //     Pixel value 0 is transparent for sprites and per the group masks
    //     for tiles.
    //
    // Sprites render from an internal holding buffer the board fills via
    // latch_sprites() (the sprite-DMA port), never from live sprite RAM.
    // Flip-screen lands with the cabinet pass.
    class irem_m72_video final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 384U;
        static constexpr std::uint32_t visible_height = 256U;
        static constexpr std::uint32_t line_pixels = 512U; // total per line
        static constexpr std::uint32_t frame_lines = 284U; // total per frame
        static constexpr std::uint32_t map_tiles = 64U;    // 64x64 tilemap
        // The visible window starts 64 beam pixels into the line; sprite X
        // and scroll X are programmed in beam space.
        static constexpr std::uint32_t beam_x_origin = 64U;
        // Scroll Y and the raster-compare line live in a 128-biased space.
        static constexpr std::int32_t line_bias = 128;
        static constexpr std::size_t vram_entry_bytes = 4U;
        static constexpr std::size_t sprite_entry_bytes = 8U;
        // Bytes per 16x16 sprite cell within one bitplane bank.
        static constexpr std::size_t sprite_cell_bytes = 32U;

        using line_callback = std::function<void(std::uint32_t line)>;

        irem_m72_video() { reset(reset_kind::power_on); }

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

        // Board attachment (non-owning; the board's RAM/ROM outlives the chip).
        void attach_vram_a(std::span<const std::uint8_t> vram) noexcept { vram_a_ = vram; }
        void attach_vram_b(std::span<const std::uint8_t> vram) noexcept { vram_b_ = vram; }
        void attach_palette_a(std::span<const std::uint8_t> palette) noexcept {
            palette_a_ = palette;
        }
        void attach_palette_b(std::span<const std::uint8_t> palette) noexcept {
            palette_b_ = palette;
        }
        void attach_tiles_a(std::span<const std::uint8_t> tiles) noexcept { tiles_a_ = tiles; }
        void attach_tiles_b(std::span<const std::uint8_t> tiles) noexcept { tiles_b_ = tiles; }
        void attach_sprite_ram(std::span<const std::uint8_t> ram) noexcept { sprite_ram_ = ram; }
        void attach_sprites(std::span<const std::uint8_t> sprites) noexcept { sprites_ = sprites; }

        // Registers (driven by the board's I/O ports).
        void set_scroll_a(std::uint16_t x, std::uint16_t y) noexcept {
            scroll_a_x_ = x;
            scroll_a_y_ = y;
        }
        void set_scroll_b(std::uint16_t x, std::uint16_t y) noexcept {
            scroll_b_x_ = x;
            scroll_b_y_ = y;
        }
        // Raster-compare beam line (already de-biased by the board); lines
        // outside [0, frame_lines) never match.
        void set_raster_compare(std::int32_t line) noexcept { raster_compare_ = line; }
        [[nodiscard]] bool raster_compare_matches(std::uint32_t line) const noexcept {
            return static_cast<std::int32_t>(line) == raster_compare_;
        }
        // Display disable (board control register bit): blanked frames render
        // black.
        void set_display_enable(bool enabled) noexcept { display_enabled_ = enabled; }
        // The board's sprite DMA: copy live sprite RAM into the holding
        // buffer rendering reads.
        void latch_sprites() noexcept;

        // Fired at the start of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired at the start of the first vblank line (after the frame
        // renders); kept alongside the scanline callback for tests/tools.
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return beam_y_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return beam_x_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(irem_m72_video& owner) noexcept : tiles_(owner) {}
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                layer_ptr_[0] = &tiles_;
                return layer_ptr_;
            }

          private:
            class tile_sheet_layer final : public instrumentation::debug_layer {
              public:
                explicit tile_sheet_layer(irem_m72_video& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::string_view name() const noexcept override { return "tiles_a"; }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                irem_m72_video* owner_;
            };

            tile_sheet_layer tiles_;
            std::array<instrumentation::debug_layer*, 1> layer_ptr_{};
        };

        void render_frame() noexcept;
        // One playfield pass: draws the pens the group's mask admits for this
        // pass (masks index by group; a set bit means the pen is skipped).
        void render_layer(std::span<const std::uint8_t> vram, std::span<const std::uint8_t> tiles,
                          std::uint16_t scroll_x, std::uint16_t scroll_y,
                          std::span<const std::uint16_t, 4> skip_masks) noexcept;
        void render_sprites() noexcept;
        [[nodiscard]] static std::uint32_t lookup_rgb(std::span<const std::uint8_t> palette,
                                                      std::size_t index) noexcept;

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(visible_width) * visible_height);
        // The decoded tile-sheet debug view, rebuilt on demand.
        mutable std::vector<std::uint32_t> tile_sheet_;
        mutable std::uint32_t tile_sheet_height_{};

        std::span<const std::uint8_t> vram_a_{};
        std::span<const std::uint8_t> vram_b_{};
        std::span<const std::uint8_t> palette_a_{};
        std::span<const std::uint8_t> palette_b_{};
        std::span<const std::uint8_t> tiles_a_{};
        std::span<const std::uint8_t> tiles_b_{};
        std::span<const std::uint8_t> sprite_ram_{};
        std::span<const std::uint8_t> sprites_{};

        // The sprite-DMA holding buffer (board pushes via latch_sprites()).
        std::array<std::uint8_t, 0x400> sprite_buffer_{};

        std::uint16_t scroll_a_x_{};
        std::uint16_t scroll_a_y_{};
        std::uint16_t scroll_b_x_{};
        std::uint16_t scroll_b_y_{};
        std::int32_t raster_compare_{-1}; // de-biased beam line; -1 = never
        bool display_enabled_{true};

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
