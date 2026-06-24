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

    // Taito F2 custom video pair, modelled as one ivideo chip for the same reason
    // CPS-A/B is modelled together: the board writes TC0100SCN tilemap state and
    // TC0200OBJ object RAM, while the renderer consumes the combined register/RAM
    // snapshot at vblank. The chip borrows board-owned RAM/ROM spans.
    //
    // Increment 1 implements the common F2 shape: two 64x64 8x8 4bpp
    // ROM-backed background tilemaps, one 64x64 2bpp RAM-generated text
    // tilemap, 16x16 sprite cells with a vblank-latched object buffer,
    // 15-bit RGB palette decode, scroll/control registers, beam callbacks,
    // and deterministic save/load.
    class taito_f2_video final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 320U;
        static constexpr std::uint32_t visible_height = 224U;
        static constexpr std::uint32_t line_pixels = 512U;
        static constexpr std::uint32_t frame_lines = 262U;
        static constexpr std::uint32_t vblank_start = visible_height;

        static constexpr std::uint32_t map_tiles = 64U;
        static constexpr std::uint32_t tile_size = 8U;
        static constexpr std::size_t tile_entry_bytes = 4U;
        static constexpr std::size_t sprite_entry_bytes = 16U;
        static constexpr std::size_t sprite_area_bytes = 0x4000U;
        static constexpr std::size_t sprite_active_area_stride = 0x8000U;
        static constexpr std::size_t sprite_buffer_bytes = 0x10000U;
        static constexpr std::size_t sprite_bank_count = 8U;
        static constexpr std::size_t priority_reg_count = 10U;
        static constexpr std::size_t roz_ram_bytes = 0x2000U;
        static constexpr std::size_t roz_control_reg_count = 8U;
        static constexpr std::size_t sprite_priority_group_count = 4U;
        static constexpr std::uint32_t sprite_screen_x_bias = 0x60U;
        static constexpr std::uint8_t transparent_pen = 0U;

        static constexpr std::uint32_t bg0_tilemap_base = 0x0000U;
        static constexpr std::uint32_t text_tilemap_base = 0x4000U;
        static constexpr std::uint32_t text_gfx_base = 0x6000U;
        static constexpr std::uint32_t bg1_tilemap_base = 0x8000U;
        static constexpr std::uint32_t bg0_rowscroll_base = 0xC000U;
        static constexpr std::uint32_t bg1_rowscroll_base = 0xC400U;
        static constexpr std::uint32_t bg1_colscroll_base = 0xE000U;
        static constexpr std::uint32_t text_char_bytes = 16U;

        static constexpr std::size_t tc0480scp_control_reg_count = 0x18U;
        static constexpr std::uint32_t tc0480scp_bg_tile_size = 16U;
        static constexpr std::size_t tc0480scp_bg_entry_bytes = 4U;
        static constexpr std::uint32_t tc0480scp_text_tilemap_base = 0xC000U;
        static constexpr std::uint32_t tc0480scp_text_gfx_base = 0xE000U;
        static constexpr std::uint32_t tc0480scp_text_char_bytes = 32U;
        static constexpr std::uint32_t tc0480scp_rowscroll_rows = 512U;
        static constexpr std::uint16_t tc0480scp_control_row_zoom_bg2 = 0x0001U;
        static constexpr std::uint16_t tc0480scp_control_row_zoom_bg3 = 0x0002U;
        static constexpr std::uint16_t tc0480scp_control_flip_screen = 0x0040U;
        static constexpr std::uint16_t tc0480scp_control_double_width = 0x0080U;

        static constexpr std::uint16_t control_bg0_disable = 0x0001U;
        static constexpr std::uint16_t control_bg1_disable = 0x0002U;
        static constexpr std::uint16_t control_text_disable = 0x0004U;
        static constexpr std::uint16_t control_priority_swap = 0x0008U;

        using line_callback = std::function<void(std::uint32_t line)>;

        enum class sprite_mode : std::uint8_t {
            standard,
            banked,
            extension_low,
            extension_high,
            extension_low_as_high
        };

        enum class sprite_active_area_source : std::uint8_t {
            mode_default,
            none,
            control_word_bit0,
            y_word_bit0
        };

        enum class sprite_buffer_policy : std::uint8_t {
            immediate,
            full_delayed,
            partial_delayed,
            partial_delayed_thundfox,
            partial_delayed_qzchikyu
        };

        enum class palette_format : std::uint8_t {
            xbgr_555,
            rgbx_444,
            xrgb_555
        };

        enum class roz_variant : std::uint8_t {
            tc0280grd,
            tc0430grw
        };

        enum class tilemap_variant : std::uint8_t {
            tc0100scn,
            dual_tc0100scn,
            tc0480scp
        };

        enum class tc0480scp_priority_model : std::uint8_t {
            metalb,
            deadconx_footchmp
        };

        taito_f2_video() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        void attach_tile_ram(std::span<const std::uint8_t> ram) noexcept { tile_ram_ = ram; }
        void attach_secondary_tile_ram(std::span<const std::uint8_t> ram) noexcept {
            tile_ram_secondary_ = ram;
        }
        void attach_sprite_ram(std::span<const std::uint8_t> ram) noexcept { sprite_ram_ = ram; }
        void attach_sprite_extension_ram(std::span<const std::uint8_t> ram) noexcept {
            sprite_extension_ram_ = ram;
        }
        void attach_palette(std::span<const std::uint8_t> ram) noexcept { palette_ = ram; }
        void attach_tile_gfx(std::span<const std::uint8_t> rom) noexcept { tile_gfx_ = rom; }
        void attach_secondary_tile_gfx(std::span<const std::uint8_t> rom) noexcept {
            tile_gfx_secondary_ = rom;
        }
        void attach_sprite_gfx(std::span<const std::uint8_t> rom) noexcept { sprite_gfx_ = rom; }
        void attach_roz_ram(std::span<const std::uint8_t> ram) noexcept { roz_ram_ = ram; }
        void attach_roz_gfx(std::span<const std::uint8_t> rom) noexcept { roz_gfx_ = rom; }

        void set_scroll0(std::uint16_t x, std::uint16_t y) noexcept {
            scroll0_x_ = x;
            scroll0_y_ = y;
        }
        void set_scroll1(std::uint16_t x, std::uint16_t y) noexcept {
            scroll1_x_ = x;
            scroll1_y_ = y;
        }
        void set_scroll2(std::uint16_t x, std::uint16_t y) noexcept {
            scroll2_x_ = x;
            scroll2_y_ = y;
        }
        void set_layer0_base(std::uint32_t offset) noexcept { layer0_base_ = offset; }
        void set_layer1_base(std::uint32_t offset) noexcept { layer1_base_ = offset; }
        void set_text_base(std::uint32_t tilemap_offset, std::uint32_t gfx_offset) noexcept {
            text_base_ = tilemap_offset;
            text_gfx_base_ = gfx_offset;
        }
        void set_layer_control(std::uint16_t value) noexcept { layer_control_ = value; }
        [[nodiscard]] std::uint16_t layer_control() const noexcept { return layer_control_; }
        void set_secondary_scroll0(std::uint16_t x, std::uint16_t y) noexcept {
            scroll0_secondary_x_ = x;
            scroll0_secondary_y_ = y;
        }
        void set_secondary_scroll1(std::uint16_t x, std::uint16_t y) noexcept {
            scroll1_secondary_x_ = x;
            scroll1_secondary_y_ = y;
        }
        void set_secondary_scroll2(std::uint16_t x, std::uint16_t y) noexcept {
            scroll2_secondary_x_ = x;
            scroll2_secondary_y_ = y;
        }
        void set_secondary_layer_control(std::uint16_t value) noexcept {
            layer_control_secondary_ = value;
        }
        [[nodiscard]] std::uint16_t secondary_layer_control() const noexcept {
            return layer_control_secondary_;
        }
        void set_display_enable(bool enabled) noexcept { display_enabled_ = enabled; }
        [[nodiscard]] bool display_enabled() const noexcept { return display_enabled_; }
        void set_palette_format(palette_format format) noexcept {
            palette_format_ = format;
        }
        [[nodiscard]] palette_format current_palette_format() const noexcept {
            return palette_format_;
        }
        void set_tilemap_variant(tilemap_variant variant) noexcept {
            tilemap_variant_ = variant;
        }
        [[nodiscard]] tilemap_variant current_tilemap_variant() const noexcept {
            return tilemap_variant_;
        }
        void set_tc0480scp_palette_bank_base(std::uint16_t palette_bank) noexcept {
            tc0480scp_palette_bank_base_ = palette_bank;
        }
        [[nodiscard]] std::uint16_t tc0480scp_palette_bank_base() const noexcept {
            return tc0480scp_palette_bank_base_;
        }
        void set_tc0480scp_priority_model(tc0480scp_priority_model model) noexcept {
            tc0480scp_priority_model_ = model;
        }
        [[nodiscard]] tc0480scp_priority_model current_tc0480scp_priority_model()
            const noexcept {
            return tc0480scp_priority_model_;
        }
        void set_tc0480scp_offsets(int bg_x, int bg_y, int text_x, int text_y,
                                   int flip_x, int flip_y) noexcept {
            tc0480scp_bg_x_offset_ = bg_x;
            tc0480scp_bg_y_offset_ = bg_y;
            tc0480scp_text_x_offset_ = text_x;
            tc0480scp_text_y_offset_ = text_y;
            tc0480scp_flip_x_offset_ = flip_x;
            tc0480scp_flip_y_offset_ = flip_y;
        }
        void set_sprite_mode(sprite_mode mode) noexcept { sprite_mode_ = mode; }
        [[nodiscard]] sprite_mode current_sprite_mode() const noexcept { return sprite_mode_; }
        void set_sprite_active_area_source(sprite_active_area_source source) noexcept {
            sprite_active_area_source_ = source;
        }
        [[nodiscard]] sprite_active_area_source current_sprite_active_area_source()
            const noexcept {
            return sprite_active_area_source_;
        }
        void set_sprite_hide_pixels(int normal, int flipped) noexcept {
            hide_pixels_ = normal;
            flip_hide_pixels_ = flipped;
        }
        [[nodiscard]] int sprite_hide_pixels() const noexcept { return hide_pixels_; }
        [[nodiscard]] int sprite_flip_hide_pixels() const noexcept {
            return flip_hide_pixels_;
        }
        void set_sprite_buffer_policy(sprite_buffer_policy policy) noexcept {
            sprite_buffer_policy_ = policy;
        }
        [[nodiscard]] sprite_buffer_policy current_sprite_buffer_policy() const noexcept {
            return sprite_buffer_policy_;
        }
        void write_sprite_bank_register(std::uint32_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t sprite_bank(std::uint8_t index) const noexcept {
            return sprite_banks_[index & 0x07U];
        }
        void write_priority_register(std::uint32_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t priority_register(std::uint8_t index) const noexcept {
            return index < priority_regs_.size() ? priority_regs_[index] : 0U;
        }
        void write_roz_control_register(std::uint32_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t roz_control_register(std::uint8_t index) const noexcept {
            return index < roz_control_regs_.size() ? roz_control_regs_[index] : 0U;
        }
        void write_tc0480scp_control_register(std::uint32_t offset,
                                              std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t
        tc0480scp_control_register(std::uint8_t index) const noexcept {
            return index < tc0480scp_control_regs_.size() ? tc0480scp_control_regs_[index] : 0U;
        }
        void set_roz_variant(roz_variant variant) noexcept { roz_variant_ = variant; }
        [[nodiscard]] roz_variant current_roz_variant() const noexcept {
            return roz_variant_;
        }
        void set_roz_offsets(int x, int y) noexcept {
            roz_x_offset_ = x;
            roz_y_offset_ = y;
        }

        // The board snapshots TC0200OBJ object RAM at vblank; rendering consumes
        // this latched buffer so CPU writes during a frame affect the next frame.
        void latch_sprites() noexcept;

        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        [[nodiscard]] std::uint32_t beam_line() const noexcept { return beam_y_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return beam_x_; }

        // Taito palette words are decoded as xBBBBBGGGGGRRRRR in board RAM.
        [[nodiscard]] static std::uint32_t decode_color(std::uint16_t entry) noexcept;
        [[nodiscard]] static std::uint32_t decode_color(palette_format format,
                                                        std::uint16_t entry) noexcept;
        [[nodiscard]] std::uint16_t read_palette(std::uint16_t bank,
                                                 std::uint8_t pen) const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct frame_priority_state final {
            std::array<std::uint8_t, 2> layer{};
            std::uint8_t roz{};
            std::uint8_t text{};
            std::array<std::uint8_t, sprite_priority_group_count> sprite{};
            std::uint8_t sprite_blend_mode{};
        };

        void render_frame() noexcept;
        void draw_background_layer(int layer, bool opaque, std::uint8_t priority) noexcept;
        void draw_background_layer_from(std::span<const std::uint8_t> ram,
                                        std::span<const std::uint8_t> gfx, int layer,
                                        bool opaque, std::uint8_t priority,
                                        std::uint16_t control,
                                        std::uint16_t scroll0_x,
                                        std::uint16_t scroll0_y,
                                        std::uint16_t scroll1_x,
                                        std::uint16_t scroll1_y,
                                        std::uint32_t layer0_base,
                                        std::uint32_t layer1_base) noexcept;
        void draw_roz_layer(std::uint8_t priority) noexcept;
        void draw_text_layer(std::uint8_t priority) noexcept;
        void draw_text_layer_from(std::span<const std::uint8_t> ram,
                                  std::uint8_t priority, std::uint16_t control,
                                  std::uint16_t scroll_x, std::uint16_t scroll_y,
                                  std::uint32_t tilemap_base,
                                  std::uint32_t gfx_base) noexcept;
        void draw_dual_tc0100scn_layers() noexcept;
        void draw_tc0480scp_layers() noexcept;
        void draw_tc0480scp_bg_layer(std::uint8_t layer, std::uint8_t priority) noexcept;
        void draw_tc0480scp_text_layer(std::uint8_t priority) noexcept;
        void draw_sprites(const frame_priority_state& priorities) noexcept;
        void draw_sprite_cell(std::uint32_t code, int sx, int sy, std::uint32_t width,
                              std::uint32_t height, bool flip_x, bool flip_y,
                              std::uint16_t palette_bank,
                              std::uint8_t sprite_priority,
                              std::uint8_t sprite_blend_mode) noexcept;

        [[nodiscard]] std::uint16_t read16(std::span<const std::uint8_t> bytes,
                                           std::uint32_t offset) const noexcept;
        [[nodiscard]] static std::int32_t sign_extend_24(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint8_t tile_pixel(std::span<const std::uint8_t> gfx,
                                              std::uint32_t code, int x, int y) const noexcept;
        [[nodiscard]] std::uint8_t sprite_pixel(std::uint32_t code, int x,
                                                int y) const noexcept;
        [[nodiscard]] std::uint16_t sprite_extension_word(std::uint32_t entry) const noexcept;
        [[nodiscard]] std::uint8_t text_pixel(std::span<const std::uint8_t> ram,
                                              std::uint32_t gfx_base, std::uint32_t code,
                                              int x, int y) const noexcept;
        [[nodiscard]] std::uint8_t tc0480scp_tile_pixel(std::uint32_t code, int x,
                                                        int y) const noexcept;
        [[nodiscard]] std::uint8_t tc0480scp_text_pixel(std::uint32_t code, int x,
                                                        int y) const noexcept;
        [[nodiscard]] std::uint16_t tc0480scp_bg_priority_order() const noexcept;
        [[nodiscard]] std::uint32_t tc0480scp_bg_layer_base(std::uint8_t layer) const noexcept;
        [[nodiscard]] std::uint32_t tc0480scp_rowscroll_base(std::uint8_t layer) const noexcept;
        [[nodiscard]] std::uint32_t
        tc0480scp_rowscroll_low_base(std::uint8_t layer) const noexcept;
        [[nodiscard]] std::uint32_t tc0480scp_rowzoom_base(std::uint8_t layer) const noexcept;
        [[nodiscard]] std::uint32_t tc0480scp_colscroll_base(std::uint8_t layer) const noexcept;
        [[nodiscard]] bool tc0480scp_double_width() const noexcept;
        [[nodiscard]] static std::int32_t fixed_floor_shift_16(std::int64_t value) noexcept;
        [[nodiscard]] static std::uint32_t tc0480scp_zoom_x_step(std::uint16_t word) noexcept;
        [[nodiscard]] static std::uint32_t tc0480scp_zoom_y_step(std::uint16_t word) noexcept;
        [[nodiscard]] std::uint32_t palette_rgb(std::uint16_t bank,
                                                std::uint8_t pen) const noexcept;
        [[nodiscard]] std::uint16_t palette_index(std::uint16_t bank,
                                                  std::uint8_t pen) const noexcept;
        [[nodiscard]] std::uint32_t palette_index_rgb(std::uint16_t index) const noexcept;
        [[nodiscard]] static int roz_x_multiplier(roz_variant variant) noexcept;
        [[nodiscard]] static bool layer_enabled(std::uint16_t control, int layer) noexcept;
        [[nodiscard]] static int sign_extend_12(std::uint16_t value) noexcept;
        [[nodiscard]] sprite_active_area_source resolved_sprite_active_area_source()
            const noexcept;
        [[nodiscard]] std::uint32_t sprite_active_area_from_marker(
            std::uint16_t y_word, std::uint16_t ctrl, std::uint32_t fallback) const noexcept;
        [[nodiscard]] int sprite_x_offset(bool flip_screen) const noexcept;
        [[nodiscard]] frame_priority_state priority_state() const noexcept;
        void update_sprite_control_state() noexcept;
        void copy_current_sprite_ram(std::array<std::uint8_t, sprite_buffer_bytes>& target)
            const noexcept;
        void overlay_current_sprite_word(std::uint32_t word_index) noexcept;
        void overlay_current_sprite_words(std::span<const std::uint8_t> word_offsets)
            noexcept;

        static constexpr std::size_t pixel_count =
            static_cast<std::size_t>(visible_width) * visible_height;

        std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(pixel_count);
        std::vector<std::uint16_t> pixel_palette_index_ =
            std::vector<std::uint16_t>(pixel_count, 0U);
        std::vector<std::uint8_t> pixel_priority_ =
            std::vector<std::uint8_t>(pixel_count, 0U);
        std::vector<std::uint8_t> pixel_sprite_priority_ =
            std::vector<std::uint8_t>(pixel_count, 0U);

        std::span<const std::uint8_t> tile_ram_{};
        std::span<const std::uint8_t> tile_ram_secondary_{};
        std::span<const std::uint8_t> sprite_ram_{};
        std::span<const std::uint8_t> sprite_extension_ram_{};
        std::span<const std::uint8_t> palette_{};
        std::span<const std::uint8_t> tile_gfx_{};
        std::span<const std::uint8_t> tile_gfx_secondary_{};
        std::span<const std::uint8_t> sprite_gfx_{};
        std::span<const std::uint8_t> roz_ram_{};
        std::span<const std::uint8_t> roz_gfx_{};

        std::uint16_t scroll0_x_{};
        std::uint16_t scroll0_y_{};
        std::uint16_t scroll1_x_{};
        std::uint16_t scroll1_y_{};
        std::uint16_t scroll2_x_{};
        std::uint16_t scroll2_y_{};
        std::uint16_t scroll0_secondary_x_{};
        std::uint16_t scroll0_secondary_y_{};
        std::uint16_t scroll1_secondary_x_{};
        std::uint16_t scroll1_secondary_y_{};
        std::uint16_t scroll2_secondary_x_{};
        std::uint16_t scroll2_secondary_y_{};
        std::uint32_t layer0_base_{bg0_tilemap_base};
        std::uint32_t layer1_base_{bg1_tilemap_base};
        std::uint32_t text_base_{text_tilemap_base};
        std::uint32_t text_gfx_base_{text_gfx_base};
        std::uint16_t layer_control_{};
        std::uint16_t layer_control_secondary_{};
        bool display_enabled_{true};
        palette_format palette_format_{palette_format::xbgr_555};
        tilemap_variant tilemap_variant_{tilemap_variant::tc0100scn};

        std::array<std::uint8_t, sprite_buffer_bytes> sprite_buffer_{};
        std::array<std::uint8_t, sprite_buffer_bytes> sprite_delay_buffer_{};
        bool sprite_buffer_valid_{};
        std::array<std::uint16_t, sprite_bank_count> sprite_banks_{};
        std::array<std::uint16_t, priority_reg_count> priority_regs_{};
        std::array<std::uint16_t, roz_control_reg_count> roz_control_regs_{};
        std::array<std::uint16_t, tc0480scp_control_reg_count> tc0480scp_control_regs_{};
        std::array<int, 4> tc0480scp_scroll_x_{};
        std::array<int, 4> tc0480scp_scroll_y_{};
        std::uint16_t tc0480scp_priority_reg_{};
        std::uint16_t tc0480scp_palette_bank_base_{};
        tc0480scp_priority_model tc0480scp_priority_model_{
            tc0480scp_priority_model::metalb};
        int tc0480scp_bg_x_offset_{};
        int tc0480scp_bg_y_offset_{};
        int tc0480scp_text_x_offset_{};
        int tc0480scp_text_y_offset_{};
        int tc0480scp_flip_x_offset_{};
        int tc0480scp_flip_y_offset_{};
        roz_variant roz_variant_{roz_variant::tc0280grd};
        int roz_x_offset_{};
        int roz_y_offset_{};
        sprite_mode sprite_mode_{sprite_mode::standard};
        sprite_active_area_source sprite_active_area_source_{
            sprite_active_area_source::mode_default};
        sprite_buffer_policy sprite_buffer_policy_{sprite_buffer_policy::immediate};
        int hide_pixels_{};
        int flip_hide_pixels_{};
        std::uint32_t sprite_active_area_{};
        bool sprites_disabled_{};
        bool sprites_flip_screen_{};
        int sprite_master_scroll_x_{};
        int sprite_master_scroll_y_{};

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        std::array<register_descriptor, 24> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video
