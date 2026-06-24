#ifndef MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP
#define MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP

// CPS-2 video (a sibling of the CPS-1 cps_a_b, NOT a variant -- CPS-2 has its own
// object/tile/sprite format). Full pipeline: the colour pipeline (palette DMA +
// brightness:R:G:B decode + backdrop), the gfx-code mapper + 4bpp tile decode,
// the system-facing register contract (the cps_a_b-shaped setters), the three
// scroll playfields (8x8/16x16+rowscroll/32x32), the object/sprite layer, and the
// CPS-B layer-control priority compositor -- all drawn through the per-pixel
// compositor buffers.

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    class cps2_video final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 384U;
        static constexpr std::uint32_t visible_height = 224U;
        // The palette DMA copies up to six 0x400-byte pages from video RAM.
        static constexpr std::size_t palette_page_bytes = 0x400U;
        static constexpr std::size_t palette_copy_pages = 6U;
        static constexpr std::size_t palette_bytes = palette_page_bytes * palette_copy_pages;
        // The pen the gfx decode returns for a transparent / out-of-range texel.
        static constexpr std::uint8_t transparent_pen = 15U;

        // Graphics-code categories. CPS-2 packs all tile/sprite art in one ROM; a
        // layer's code is mapped into it differently per category (sprites use the
        // code directly, the scroll layers shift + bank it).
        enum class gfx_type : std::uint8_t {
            sprites = 1U,
            scroll1 = 2U,
            scroll2 = 4U,
            scroll3 = 8U,
        };

        cps2_video() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        // ichip / ivideo
        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // Video RAM is the palette DMA source (and, in later increments, the
        // tilemap/attribute RAM). Non-owning; the board keeps the storage alive.
        void attach_video_ram(std::span<const std::uint8_t> video_ram) noexcept {
            video_ram_ = video_ram;
        }
        // The tile/sprite graphics ROM (the source for tile_pixel).
        void attach_gfx(std::span<const std::uint8_t> gfx) noexcept { gfx_ = gfx; }
        // The object/sprite RAM (the board's $700000 window); the active bank is
        // selected by set_object_base. Non-owning. Sprites render from the vblank
        // latch, not this live RAM span.
        void attach_object_ram(std::span<const std::uint8_t> object_ram) noexcept {
            object_ram_ = object_ram;
        }
        // The CPS-2 control-register words used to bias sprite coordinates
        // (control reg 0x08 = x base, 0x0A = y base).
        void set_sprite_offsets(std::uint16_t x_base, std::uint16_t y_base) noexcept {
            sprite_x_base_ = x_base;
            sprite_y_base_ = y_base;
        }
        // The object priority-control word (CPS-2 control reg 0x04) -- drives the
        // sprite-vs-layer priority masks.
        void set_object_priority(std::uint16_t value) noexcept { object_priority_ = value; }
        // Latch the selected object-RAM bank and priority-control word for the next
        // sprite draw. CPS-2 hardware snapshots this table at vblank; live writes
        // after the latch are not visible until the following latch.
        void latch_objects() noexcept;

        // Map a layer's graphics code to a tile index in the gfx ROM. Returns false
        // (no tile) for an out-of-bank code or when no gfx is attached.
        [[nodiscard]] bool map_gfx_code(gfx_type type, std::uint32_t code,
                                        std::uint32_t& mapped) const noexcept;

        // Decode one 4bpp texel of a tile. `tile_size` is 8, 16, or 32; `x_bias`
        // selects the half of a 16-wide storage row for 8x8 tiles (0 or 8). Returns
        // transparent_pen for an out-of-range texel / unmapped code.
        [[nodiscard]] std::uint8_t tile_pixel(gfx_type type, std::uint32_t code, int x, int y,
                                              int tile_size, int x_bias) const noexcept;

        // Copy the active palette pages from video RAM at `source_base` into the
        // palette buffer; `control` is the CPS-B palette-control word (one bit per
        // page; a zero word defaults to 0x3F = all six pages).
        void copy_palette(std::uint32_t source_base, std::uint16_t control) noexcept;

        // Render one frame: refresh the palette from video RAM, then fill the
        // framebuffer with the decoded backdrop (palette index 0). Bumps frame_index.
        void render(std::uint32_t palette_source, std::uint16_t palette_control) noexcept;

        // The raw 16-bit palette entry at a colour index (pal_num*16 + colour).
        [[nodiscard]] std::uint16_t palette_color(std::uint16_t index) const noexcept;

        // Decode a CPS-2 16-bit colour (brightness:4 R:4 G:4 B:4) to 0x00RRGGBB.
        [[nodiscard]] static std::uint32_t decode_color(std::uint16_t value) noexcept;

        // --- CPS-B layer-control priority compositor (pure helpers, exposed for
        // testing) ---
        // Decode the per-slot scroll-layer ids (raw, pre-collapse) from the control
        // word; a zero control word uses the default order.
        static void decode_layer_control(std::uint16_t layer_control,
                                         std::array<int, 4>& raw_layers,
                                         bool default_when_zero = true) noexcept;
        // Collapse the raw layers to the 3 drawn slots and build the per-priority
        // sprite-vs-layer masks (transcribed from the reference).
        static void build_sprite_priority_masks(
            std::uint16_t priority_control, const std::array<int, 4>& raw_layers,
            std::array<int, 3>& scroll_layers, std::array<std::uint16_t, 8>& sprite_masks) noexcept;

        // --- system-facing register contract ---
        // These mirror the CPS1 cps_a_b setters so the board can drive either video
        // chip. The board latches already-resolved video-RAM offsets (the *_base
        // setters) and the raw CPS-A register words (scroll offsets / control).
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
        void set_scroll1_base(std::uint32_t offset) noexcept { scroll1_base_ = offset; }
        void set_scroll2_base(std::uint32_t offset) noexcept { scroll2_base_ = offset; }
        void set_scroll3_base(std::uint32_t offset) noexcept { scroll3_base_ = offset; }
        // Scroll2 per-line horizontal row-scroll: enable, the video-RAM table base,
        // and the line-index bias.
        void set_rowscroll(bool enabled, std::uint32_t base, std::uint16_t line_offset) noexcept {
            rowscroll_enabled_ = enabled;
            rowscroll_base_ = base;
            rowscroll_line_offset_ = line_offset;
        }
        void set_object_base(std::uint32_t offset) noexcept { object_base_ = offset; }
        // CPS-B register file (the board writes the register window here); the
        // layer/palette/priority-control words are read back from it during render.
        void set_cps_b_reg(std::uint8_t index, std::uint16_t value) noexcept {
            if (index < cps_b_regs_.size()) {
                cps_b_regs_[index] = value;
            }
        }
        [[nodiscard]] std::uint16_t cps_b_reg(std::uint8_t index) const noexcept {
            return index < cps_b_regs_.size() ? cps_b_regs_[index] : 0U;
        }
        // CPS-A video-control register (bit 15 = flip screen).
        void set_video_control(std::uint16_t value) noexcept { video_control_ = value; }
        void set_display_enable(bool enabled) noexcept { display_enabled_ = enabled; }
        // Bare video-chip tests have no ROM-backed CPS-B register stream and use
        // the reference's synthetic no-ROM default order; real CPS-2 boards do not.
        void set_zero_layer_control_defaults(bool enabled) noexcept {
            zero_layer_control_defaults_ = enabled;
        }
        [[nodiscard]] bool flip_screen() const noexcept { return (video_control_ & 0x8000U) != 0U; }

      private:
        // The visible window sits inside the larger scroll space at this origin.
        static constexpr int visible_x_start = 64;
        static constexpr int visible_y_start = 16;
        // Per-layer palette page bases (each pal_num is 16 colours).
        static constexpr std::uint16_t scroll1_palette_base = 32U;
        static constexpr std::uint16_t scroll2_palette_base = 64U;
        static constexpr std::uint16_t scroll3_palette_base = 96U;
        // The backdrop is palette colour (pal_num 0xBF, pen 0x0F) = last entry.
        static constexpr std::uint16_t backdrop_color_index = 0xBFU * 16U + 0x0FU;
        // Tile-layer priority bits (one per draw pass, by draw order back->front;
        // ORed into pixel_priority_).
        static constexpr std::uint8_t tile_priority_0 = 1U;
        static constexpr std::uint8_t tile_priority_1 = 2U;
        static constexpr std::uint8_t tile_priority_2 = 4U;
        // Sprite-vs-layer priority masks (per scroll layer slot).
        static constexpr std::uint16_t priority_mask_layer0 = 0x00AAU;
        static constexpr std::uint16_t priority_mask_layer1 = 0x00CCU;
        static constexpr std::uint16_t priority_mask_layer2 = 0x00F0U;
        // The CPS-B register file the board mirrors here (32 words covers it).
        static constexpr std::size_t cps_b_reg_count = 32U;
        // The CPS-B layer-control word (reg byte 0x26 -> word index 0x13) + its
        // per-scroll enable bits.
        static constexpr std::size_t cps_b_layer_control_word = 0x13U;
        static constexpr std::uint16_t layer_enable_scroll1 = 0x02U;
        static constexpr std::uint16_t layer_enable_scroll2 = 0x04U;
        static constexpr std::uint16_t layer_enable_scroll3 = 0x08U;
        static constexpr std::uint16_t video_enable_scroll2 = 0x0004U;
        static constexpr std::uint16_t video_enable_scroll3 = 0x0008U;
        // Object/sprite RAM: one bank is 0x2000 bytes of 8-byte entries; sprites
        // are positioned in a 512x256 full-screen space clipped to the visible
        // window.
        static constexpr std::uint32_t object_bank_bytes = 0x2000U;
        static constexpr std::uint32_t object_entry_bytes = 8U;
        static constexpr int full_screen_width = 512;
        static constexpr int full_screen_height = 256;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;
        // One 4bpp texel from the gfx ROM at `tile_base`, rows `row_stride` bytes
        // apart (16 pixels = 8 bytes for the 8x8/16x16 layouts; 32 pixels = 16
        // bytes for 32x32). Transparent if out of range.
        [[nodiscard]] std::uint8_t decode_packed_pixel(std::uint32_t tile_base,
                                                       std::uint32_t row_stride, int x,
                                                       int y) const noexcept;

        // The two-byte big-endian word at `offset` in the attached video RAM.
        [[nodiscard]] std::uint16_t video_read16(std::uint32_t offset) const noexcept;
        // Clear the framebuffer + per-pixel compositor buffers before a frame.
        void reset_pixel_buffers(std::uint32_t backdrop) noexcept;
        // Plot one opaque layer texel: write its colour to the framebuffer and
        // record the layer/pen/group + OR the priority into the per-pixel buffers
        // (so later layers + sprites can composite against it). `x`/`y` are visible
        // coordinates; flip_screen mirrors them.
        void plot_layer_pixel(int x, int y, std::uint8_t layer, std::uint8_t pen,
                              std::uint8_t group, std::uint8_t priority,
                              std::uint32_t color) noexcept;
        // Draw a scroll playfield at the given priority. scroll1 = 8x8 tiles,
        // scroll2 = 16x16 (with optional per-line row-scroll), scroll3 = 32x32.
        void draw_scroll1(std::uint8_t priority) noexcept;
        void draw_scroll2(std::uint8_t priority) noexcept;
        void draw_scroll3(std::uint8_t priority) noexcept;

        // The big-endian word at `offset` in the latched object-RAM bank.
        [[nodiscard]] std::uint16_t object_read16(std::uint32_t offset) const noexcept;
        // Count the live sprite entries (stop at a terminator / past the bank).
        [[nodiscard]] std::uint32_t find_sprite_count() const noexcept;
        // The gfx tile for one 16x16 block of a multi-block sprite.
        [[nodiscard]] static std::uint32_t sprite_block_tile(std::uint32_t tile_code, int blocks_x,
                                                             int blocks_y, int block_x, int block_y,
                                                             bool flip_x, bool flip_y) noexcept;
        // Draw one object-RAM sprite entry (composites against the per-pixel
        // priority + sprite-priority buffers; `sprite_masks` puts the sprite behind
        // higher-priority scroll layers).
        void draw_sprite_entry(std::uint32_t index, bool flip_screen,
                               const std::array<std::uint16_t, 8>& sprite_masks) noexcept;
        // Draw all live sprites (back-to-front by entry order).
        void draw_sprites(const std::array<std::uint16_t, 8>& sprite_masks) noexcept;

        // The CPS-B layer-control word (drives the scroll draw order + enables).
        [[nodiscard]] std::uint16_t cps_b_layer_control() const noexcept;
        // Whether a scroll layer (1/2/3) is enabled for this frame.
        [[nodiscard]] bool scroll_layer_enabled(int layer,
                                                std::uint16_t layer_control) const noexcept;

        std::span<const std::uint8_t> video_ram_{};
        std::span<const std::uint8_t> gfx_{};
        std::span<const std::uint8_t> object_ram_{};
        std::vector<std::uint8_t> object_render_ram_ =
            std::vector<std::uint8_t>(object_bank_bytes, 0xFFU);
        std::array<std::uint8_t, palette_bytes> palette_ram_{};
        static constexpr std::size_t pixel_count =
            static_cast<std::size_t>(visible_width) * visible_height;
        std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(pixel_count, 0U);

        // Per-pixel compositor buffers (Emu pixel_layer/pen/group/priority +
        // sprite-priority): the metadata later scroll layers + sprites read to
        // resolve priority. pixel_layer 0 = a sprite owns the pixel.
        std::vector<std::uint8_t> pixel_layer_ = std::vector<std::uint8_t>(pixel_count, 0xFFU);
        std::vector<std::uint8_t> pixel_pen_ = std::vector<std::uint8_t>(pixel_count, 0U);
        std::vector<std::uint8_t> pixel_group_ = std::vector<std::uint8_t>(pixel_count, 0U);
        std::vector<std::uint8_t> pixel_priority_ = std::vector<std::uint8_t>(pixel_count, 0U);
        std::vector<std::uint8_t> pixel_sprite_priority_ =
            std::vector<std::uint8_t>(pixel_count, 0U);

        // CPS-A scroll state (raw register words; signed when used).
        std::uint16_t scroll1_x_{}, scroll1_y_{};
        std::uint16_t scroll2_x_{}, scroll2_y_{};
        std::uint16_t scroll3_x_{}, scroll3_y_{};
        std::uint32_t scroll1_base_{}, scroll2_base_{}, scroll3_base_{};
        std::uint32_t object_base_{};
        std::uint32_t rowscroll_base_{};
        std::uint16_t rowscroll_line_offset_{};
        bool rowscroll_enabled_{};
        std::uint16_t video_control_{};
        bool display_enabled_{true};
        bool zero_layer_control_defaults_{true};
        std::uint16_t sprite_x_base_{}, sprite_y_base_{};
        std::uint16_t object_priority_{};
        std::uint16_t object_priority_latch_{};
        std::array<std::uint16_t, cps_b_reg_count> cps_b_regs_{};

        std::uint64_t frame_index_{};
        std::array<register_descriptor, 2> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video

#endif // MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP
