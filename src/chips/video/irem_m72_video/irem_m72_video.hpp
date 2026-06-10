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
    // work), bumps frame_index(), and fires the vblank callback. A
    // scanline-compare callback fires at the start of the matching line for
    // the board's programmable raster IRQ.
    //
    // The board attaches non-owning spans for VRAM / palette RAM / tile ROM;
    // layer formats are first-cut (validated against R-Type in plan phase C):
    //   * VRAM entry, 4 bytes per tile: code low, code high, color (low 4
    //     bits), attributes (bit0 flip-x, bit1 flip-y).
    //   * Tile ROM is 4 sequential bitplane banks (each region.size()/4
    //     bytes); tile N row R is byte N*8+R of each plane, MSB leftmost.
    //   * Palette RAM holds 5-bit gun planes at +0x000 (R), +0x400 (G),
    //     +0x800 (B); both playfields read palette A, pixel value 0 of the
    //     front layer is transparent over the back layer.
    //
    // Sprites (palette B) and flip-screen land in the next phase-C increment.
    class irem_m72_video final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 384U;
        static constexpr std::uint32_t visible_height = 256U;
        static constexpr std::uint32_t line_pixels = 512U; // total per line
        static constexpr std::uint32_t frame_lines = 284U; // total per frame
        static constexpr std::uint32_t map_tiles = 64U;    // 64x64 tilemap
        static constexpr std::size_t vram_entry_bytes = 4U;

        // Fired with the line number: vblank at visible_height, raster at the
        // programmed compare line.
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
        void attach_tiles_a(std::span<const std::uint8_t> tiles) noexcept { tiles_a_ = tiles; }
        void attach_tiles_b(std::span<const std::uint8_t> tiles) noexcept { tiles_b_ = tiles; }

        // Registers (driven by the board's I/O ports).
        void set_scroll_a(std::uint16_t x, std::uint16_t y) noexcept {
            scroll_a_x_ = x;
            scroll_a_y_ = y;
        }
        void set_scroll_b(std::uint16_t x, std::uint16_t y) noexcept {
            scroll_b_x_ = x;
            scroll_b_y_ = y;
        }
        // Raster-compare line; values >= frame_lines disable the callback.
        void set_raster_compare(std::uint32_t line) noexcept { raster_compare_ = line; }

        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }
        void set_raster_callback(line_callback cb) noexcept { raster_cb_ = std::move(cb); }

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
        void render_layer(std::span<const std::uint8_t> vram, std::span<const std::uint8_t> tiles,
                          std::uint16_t scroll_x, std::uint16_t scroll_y,
                          bool transparent_pixel0) noexcept;
        [[nodiscard]] std::uint32_t palette_rgb(std::size_t index) const noexcept;

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(visible_width) * visible_height);
        // The decoded tile-sheet debug view, rebuilt on demand.
        mutable std::vector<std::uint32_t> tile_sheet_;
        mutable std::uint32_t tile_sheet_height_{};

        std::span<const std::uint8_t> vram_a_{};
        std::span<const std::uint8_t> vram_b_{};
        std::span<const std::uint8_t> palette_a_{};
        std::span<const std::uint8_t> tiles_a_{};
        std::span<const std::uint8_t> tiles_b_{};

        std::uint16_t scroll_a_x_{};
        std::uint16_t scroll_a_y_{};
        std::uint16_t scroll_b_x_{};
        std::uint16_t scroll_b_y_{};
        std::uint32_t raster_compare_{frame_lines}; // disabled

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback vblank_cb_{};
        line_callback raster_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
