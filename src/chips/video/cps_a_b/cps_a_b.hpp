#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Capcom CPS-A / CPS-B custom video, modelled as one ivideo chip (the two
    // chips are physically separate but the renderer consumes their combined
    // register state every frame; an internal register-file/profile/renderer
    // split keeps the hardware boundary visible through introspection).
    //
    // CPS-A is the address/scroll/object register file plus the palette-DMA
    // engine; CPS-B carries layer enable, layer priority, palette control, a
    // 16x16 multiplier, a board-identity value, and a per-board-revision
    // scrambled register map. The board writes the raw CPS-B register file; the
    // renderer reads logical registers (layer control, priority masks) through
    // the active profile's offsets. The display is 384x224 over a 262-line frame.
    //
    // The board attaches non-owning spans: the graphics ROM (4bpp planar tile
    // and object pixels), the unified GFX RAM (tilemap name tables, row-scroll,
    // object list), the DMA-filled palette RAM, and the latched object table.
    // Colour is decoded from 16-bit big-endian palette words `iiii rrrr gggg
    // bbbb`: the high nibble is a global brightness that scales each 4-bit gun.
    //
    // The three playfields are 8x8 (scroll1), 16x16 (scroll2, with optional
    // per-line row-scroll), and 32x32 (scroll3) 4bpp tiles; an object list of
    // 16x16 cells layers on top. A name-table entry is two big-endian words: tile
    // code, then attribute (flip-x 0x20, flip-y 0x40, priority group 0x180,
    // palette bank 0x1F). Pen 15 is transparent. The CPS-B layer-control register
    // sets the layer stack order; a tile pixel whose (group, pen) is flagged in
    // the matching CPS-B priority register occludes the sprite immediately above
    // it. flip-screen mirrors the whole image.
    //
    // tick(cycles) advances one dot per cycle; crossing into the vblank region
    // renders the completed frame (frame-at-once; raster/row-scroll accuracy is a
    // later increment) and bumps frame_index(). The scanline callback fires at
    // the start of every line; the board derives its vblank and raster-compare
    // interrupts from it. The full per-board CPS-B profile/mapper data set lands
    // in a later increment; the default profile is the synthetic "legacy" one.
    class cps_a_b final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 384U;
        static constexpr std::uint32_t visible_height = 224U;
        static constexpr std::uint32_t frame_lines = 262U; // total scanlines
        static constexpr std::uint32_t line_pixels = 512U; // dots per line (board-driven)
        static constexpr std::uint32_t vblank_start = visible_height;
        // The visible window sits inside the larger scroll / object space.
        static constexpr std::uint32_t visible_x_start = 64U;
        static constexpr std::uint32_t visible_y_start = 16U;
        static constexpr std::uint8_t transparent_pen = 15U;
        // The hardware mixer's backdrop pen: palette page 0xBF, pen 0x0F (the
        // word at palette entry 0xBFF).
        static constexpr std::uint16_t backdrop_page = 0xBFU;
        static constexpr std::uint8_t backdrop_pen = 0x0FU;
        // CPS-B register-file sentinel: "this logical register is not mapped".
        static constexpr std::uint8_t reg_none = 0xFFU;
        static constexpr std::size_t cps_b_reg_count = 32U;

        // CPS1 graphics source class; the (eventual) per-board CPS-B mapper keys
        // its code remap on this. The identity mapping holds until the profile
        // mapper data lands in a later increment.
        enum class gfx_type : std::uint8_t { scroll1, scroll2, scroll3, sprites };

        // Object/sprite table: up to 256 entries of 8 bytes (4 big-endian words:
        // x, y, code, attr) snapshotted into an internal buffer by the board's
        // sprite DMA. Sprites render in a 512x256 full-screen space whose visible
        // window starts at (64, 16).
        static constexpr std::size_t object_table_bytes = 2048U;
        static constexpr std::size_t object_entry_bytes = 8U;
        static constexpr std::uint32_t full_screen_w = 512U;
        static constexpr std::uint32_t full_screen_h = 256U;

        enum class sprite_order : std::uint8_t { ascending, descending };

        // map_gfx_code returns this when the active board mapper rejects a code
        // (no range matches / invalid bank); callers skip that tile or sprite.
        static constexpr std::uint32_t gfx_code_absent = 0xFFFFFFFFU;

        // One (layer-type, code-range) -> bank route in a board's gfx-code mapper.
        // type_mask is a bitset over gfx_type bits (sprites=1, scroll1=2,
        // scroll2=4, scroll3=8); start/end are in the per-layer shifted element
        // space, and bank indexes bank_size.
        struct gfx_bank_range {
            std::uint8_t type_mask;
            std::uint32_t start;
            std::uint32_t end;
            std::uint8_t bank;
        };
        // Per-board graphics-code remap: up to four banks laid end-to-end plus an
        // ordered range list (first match wins). An empty range span is identity.
        struct gfx_mapper {
            std::array<std::uint32_t, 4> bank_size{};
            std::span<const gfx_bank_range> ranges{};
        };

        // The CPS-B per-board configuration: where each logical register sits in
        // the scrambled physical register file, the per-layer enable masks, the
        // board-identity / 68k-protection ports, and the graphics-code mapper.
        // Defaults are the synthetic "legacy" profile (no scramble, no priority,
        // identity mapper). The protection ports (id/multiplier) are carried as
        // data here; their bus read-decode is wired at the board/manifest layer.
        struct cps_b_profile {
            // The synthetic default: a zero layer-control latch falls back to the
            // canonical layer order. A real board profile clears this, so an
            // unprogrammed (zero) latch decodes literally instead.
            bool legacy{true};
            std::uint8_t layer_control_offset{0x04U};
            std::array<std::uint8_t, 4> priority_offset{0x08U, 0x0AU, 0x0CU, 0x0EU};
            std::uint8_t palette_control_offset{reg_none};
            std::array<std::uint16_t, 5> layer_enable_mask{};
            // Board identity (the numeric profile id, for traceability / save
            // state) and the 68k protection ports: the chip-ID register
            // (id_offset reads back id_value) and the 16x16 multiplier latch byte
            // offsets (factor1, factor2, result-lo, result-hi).
            std::uint16_t id{0U};
            std::uint8_t id_offset{reg_none};
            std::uint16_t id_value{0U};
            std::array<std::uint8_t, 4> mult_offset{reg_none, reg_none, reg_none, reg_none};
            // Bootleg-board behaviour selector (0 = none; real boards leave it 0,
            // so the engine is byte-for-byte unchanged for them). Low nibble 1
            // forces the object port to 0x9100 and nudges the scroll layers (the
            // sf2 hacks); bit 6 reverses sprite draw order.
            std::uint8_t bootleg_kludge{0U};
            // Set on boards that wire a serial 93C46 EEPROM to the CPS-B chip's
            // $80017A port (the CP1B1F board) instead of the QSound C-board's
            // $F1C006. The board reads the flag to route that port to the NVRAM
            // device; real CPS-B-only boards leave it false (no behaviour change).
            bool cps_b_eeprom{false};
            // 68k address the board decodes the CPS-B register file at. Almost every
            // board uses $800140; a few relocate the whole window (the CPS-B-18 /
            // sf2 World-Warrior rev-E board decodes it at $8001C0). The register
            // OFFSETS above stay window-relative; only this base moves.
            std::uint32_t cps_b_base{0x800140U};
            // Per-board graphics-code remap (non-owning; empty => identity).
            gfx_mapper mapper{};
        };

        using line_callback = std::function<void(std::uint32_t line)>;

        cps_a_b() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

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
        void attach_gfx(std::span<const std::uint8_t> gfx) noexcept { gfx_ = gfx; }
        void attach_palette(std::span<const std::uint8_t> palette) noexcept { palette_ = palette; }
        void attach_tile_ram(std::span<const std::uint8_t> ram) noexcept { tile_ram_ = ram; }
        void attach_object_ram(std::span<const std::uint8_t> ram) noexcept { object_ram_ = ram; }

        // CPS-A scroll registers (per playfield: scroll1 8x8, scroll2 16x16,
        // scroll3 32x32), signed pixel offsets in the larger scroll space.
        void set_scroll1(std::uint16_t x, std::uint16_t y) noexcept {
            scroll1_x_ = x;
            scroll1_y_ = y;
        }
        void set_scroll2(std::uint16_t x, std::uint16_t y) noexcept {
            scroll2_x_ = x;
            scroll2_y_ = y;
        }
        void set_scroll3(std::uint16_t x, std::uint16_t y) noexcept {
            scroll3_x_ = x;
            scroll3_y_ = y;
        }
        // CPS-A name-table base offsets into the unified GFX RAM (board-decoded,
        // already aligned).
        void set_scroll1_base(std::uint32_t offset) noexcept { scroll1_base_ = offset; }
        void set_scroll2_base(std::uint32_t offset) noexcept { scroll2_base_ = offset; }
        void set_scroll3_base(std::uint32_t offset) noexcept { scroll3_base_ = offset; }
        // Scroll2 per-line horizontal row-scroll: enable, the GFX-RAM table base,
        // and the line-index bias (CPS-A "other" offset).
        void set_rowscroll(bool enabled, std::uint32_t base, std::uint16_t line_offset) noexcept {
            rowscroll_enabled_ = enabled;
            rowscroll_base_ = base;
            rowscroll_offset_ = line_offset;
        }
        // Object-table source: a non-zero base reads the table from the unified
        // GFX RAM, zero reads it from the attached object RAM.
        void set_object_base(std::uint32_t offset) noexcept { object_base_ = offset; }
        void set_sprite_order(sprite_order order) noexcept { sprite_order_ = order; }
        // The board's sprite DMA: snapshot the object table into the holding
        // buffer the renderer reads.
        void latch_sprites() noexcept;

        // CPS-B register file (the board writes the 32-word window here) and the
        // active per-board profile that interprets it.
        void set_cps_b_reg(std::uint8_t index, std::uint16_t value) noexcept {
            if (index < cps_b_reg_count) {
                cps_b_regs_[index] = value;
            }
        }
        [[nodiscard]] std::uint16_t cps_b_reg(std::uint8_t index) const noexcept {
            return index < cps_b_reg_count ? cps_b_regs_[index] : 0U;
        }
        void set_cps_b_profile(const cps_b_profile& profile) noexcept { profile_ = profile; }
        // Convenience: write the layer-control register through the active
        // profile (the board may also write it as a raw register).
        void set_layer_control(std::uint16_t value) noexcept {
            const std::uint8_t offset = profile_.layer_control_offset;
            if (offset != reg_none && (offset & 1U) == 0U && (offset >> 1U) < cps_b_reg_count) {
                cps_b_regs_[offset >> 1U] = value;
            }
        }
        [[nodiscard]] std::uint16_t layer_control() const noexcept {
            return read_cps_b(profile_.layer_control_offset);
        }
        // CPS-A video-control register (bit 15 flip-screen, bits 2/3 layer enable).
        void set_video_control(std::uint16_t value) noexcept { video_control_ = value; }
        [[nodiscard]] bool flip_screen() const noexcept { return (video_control_ & 0x8000U) != 0U; }
        // Display disable (blanked frames render black).
        void set_display_enable(bool enabled) noexcept { display_enabled_ = enabled; }

        // Raster-compare beam line (board sets it de-biased); lines outside the
        // frame never match.
        void set_raster_compare(std::int32_t line) noexcept { raster_compare_ = line; }
        [[nodiscard]] bool raster_compare_matches(std::uint32_t line) const noexcept {
            return static_cast<std::int32_t>(line) == raster_compare_;
        }

        // Fired at the start of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired at the start of the first vblank line (after the frame renders).
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return beam_y_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return beam_x_; }

        // Decode a 16-bit CPS1 palette word (iiii rrrr gggg bbbb) to 0x00RRGGBB.
        // The high nibble is a global brightness scaling each 4-bit gun.
        [[nodiscard]] static std::uint32_t decode_color(std::uint16_t entry) noexcept;

        // Read a 16-bit big-endian palette word at (pal_num * 16 + pen).
        [[nodiscard]] std::uint16_t read_palette(std::uint16_t pal_num,
                                                 std::uint8_t pen) const noexcept;

        // Map a graphics code through the active profile's mapper (identity when
        // the profile carries no mapper; gfx_code_absent when a mapper rejects
        // it). Exposed for board tooling and conformance tests.
        [[nodiscard]] std::uint32_t mapped_gfx_code(gfx_type type,
                                                    std::uint32_t code) const noexcept {
            return map_gfx_code(type, code);
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        void render_frame() noexcept;
        void draw_scroll1(bool flip) noexcept;
        void draw_scroll2(bool flip) noexcept;
        void draw_scroll3(bool flip) noexcept;
        void draw_sprites(int layer_below, bool flip) noexcept;
        void draw_sprite_entry(std::uint32_t index, int layer_below, bool flip) noexcept;
        // Store a tile pixel (after flip-screen) plus its layer / pen / priority
        // group, which a later sprite pass consults.
        void plot_layer_pixel(int px, int py, bool flip, std::uint8_t layer, std::uint8_t pen,
                              std::uint8_t group, std::uint32_t rgb) noexcept;

        // Read one 8-byte object entry from the latched buffer (big-endian x, y,
        // code, attr). Returns false past the table.
        [[nodiscard]] bool read_sprite_entry(std::uint32_t index, std::uint16_t& x,
                                             std::uint16_t& y, std::uint16_t& code,
                                             std::uint16_t& attr) const noexcept;
        [[nodiscard]] std::uint32_t sprite_entry_count() const noexcept;
        [[nodiscard]] static std::uint32_t sprite_block_tile(std::uint32_t mapped, int blocks_x,
                                                             int blocks_y, int bx, int by,
                                                             bool flip_x, bool flip_y) noexcept;

        // Per-board CPS-B graphics-code remap driven by the active profile's
        // mapper (identity when the profile carries no mapper; gfx_code_absent
        // when a present mapper rejects the code).
        [[nodiscard]] std::uint32_t map_gfx_code(gfx_type type, std::uint32_t code) const noexcept;
        // gfx_type as a mapper bitset bit / its per-layer code-expansion shift.
        [[nodiscard]] static std::uint8_t gfx_type_bit(gfx_type type) noexcept;
        [[nodiscard]] static int gfx_type_shift(gfx_type type) noexcept;
        [[nodiscard]] std::uint8_t tile_pixel(gfx_type type, std::uint32_t code, int x, int y,
                                              int tile_size, int x_bias) const noexcept;
        [[nodiscard]] std::uint8_t decode_packed(std::uint32_t tile_base, std::uint32_t row_stride,
                                                 int x, int y) const noexcept;
        [[nodiscard]] std::uint16_t read_tile16(std::uint32_t offset) const noexcept;

        [[nodiscard]] static std::uint32_t scroll1_tile_index(std::uint32_t row,
                                                              std::uint32_t col) noexcept;
        [[nodiscard]] static std::uint32_t scroll2_tile_index(std::uint32_t row,
                                                              std::uint32_t col) noexcept;
        [[nodiscard]] static std::uint32_t scroll3_tile_index(std::uint32_t row,
                                                              std::uint32_t col) noexcept;

        // CPS-B logical-register reads through the active profile.
        [[nodiscard]] std::uint16_t read_cps_b(std::uint8_t offset) const noexcept;
        [[nodiscard]] std::uint16_t priority_mask(std::uint8_t group) const noexcept;
        // True when the tile pixel's (group, pen) is flagged to draw over sprites.
        [[nodiscard]] bool tile_pen_has_sprite_priority(std::uint8_t group,
                                                        std::uint8_t pen) const noexcept;
        [[nodiscard]] bool layer_enabled(std::uint16_t layercontrol,
                                         std::uint8_t layer) const noexcept;

        [[nodiscard]] std::uint32_t backdrop_rgb() const noexcept {
            return decode_color(read_palette(backdrop_page, backdrop_pen));
        }

        static constexpr std::size_t pixel_count =
            static_cast<std::size_t>(visible_width) * visible_height;

        std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(pixel_count);
        // Per-pixel priority bookkeeping the sprite pass consults.
        std::vector<std::uint8_t> pixel_layer_ = std::vector<std::uint8_t>(pixel_count, 0xFFU);
        std::vector<std::uint8_t> pixel_pen_ = std::vector<std::uint8_t>(pixel_count);
        std::vector<std::uint8_t> pixel_group_ = std::vector<std::uint8_t>(pixel_count);

        std::span<const std::uint8_t> gfx_{};
        std::span<const std::uint8_t> palette_{};
        std::span<const std::uint8_t> tile_ram_{};
        std::span<const std::uint8_t> object_ram_{};

        std::uint16_t scroll1_x_{};
        std::uint16_t scroll1_y_{};
        std::uint16_t scroll2_x_{};
        std::uint16_t scroll2_y_{};
        std::uint16_t scroll3_x_{};
        std::uint16_t scroll3_y_{};
        std::uint32_t scroll1_base_{};
        std::uint32_t scroll2_base_{};
        std::uint32_t scroll3_base_{};
        bool rowscroll_enabled_{};
        std::uint32_t rowscroll_base_{};
        std::uint16_t rowscroll_offset_{};
        std::uint32_t object_base_{};
        sprite_order sprite_order_{sprite_order::ascending};
        std::array<std::uint8_t, object_table_bytes> sprite_buffer_{};
        bool sprite_buffer_valid_{};
        std::array<std::uint16_t, cps_b_reg_count> cps_b_regs_{};
        cps_b_profile profile_{};
        std::uint16_t video_control_{};
        bool display_enabled_{true};
        std::int32_t raster_compare_{-1};

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        std::array<register_descriptor, 9> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video
