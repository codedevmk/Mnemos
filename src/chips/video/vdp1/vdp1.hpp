#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Saturn sprite/polygon framebuffer drawing processor (the "VDP1").
    //
    // A geometry rasteriser that walks a linked list of 32-byte command-table
    // entries in a 512 KiB internal VRAM and draws sprites, polygons,
    // polylines, and lines into one of two 256 KiB framebuffers. The two
    // framebuffers ping-pong: one is scanned out by the companion background
    // processor while the other is drawn into. Drawing terminates on the
    // first command whose control word carries the end-of-list bit, at which
    // point the draw-end status (EDSR) latches.
    //
    // Big-endian word VRAM layout. Each command table entry is 32 bytes
    // (0x20); the words this model consumes:
    //   +0x00 CTRL   end (15) | jump-mode (13:12) | skip (14) | type (3:0)
    //   +0x04 PMOD   pixel-op (2:0) | color-mode (5:3) | misc (clipping, MSB)
    //   +0x06 COLR   colour bank / palette base
    //   +0x08 SRCA   texture source address in VRAM, divided by 8
    //   +0x0A SIZE   width-in-8s (13:8) | height-in-pixels (7:0)
    //   +0x0C..0x1A  XA,YA..XD,YD signed screen coordinates
    //
    // Command types (CTRL bits 3:0): 0 normal sprite, 1 scaled sprite,
    // 2 distorted sprite, 4 polygon, 5 polyline, 6 line, 8 user clip,
    // 9 system clip, A local coordinates.
    //
    // The internal framebuffer stores native 16-bit 0BGR1555 words big-endian
    // (bit 15 set = opaque). framebuffer() converts the draw buffer to the
    // engine-wide 0x00RRGGBB view on demand.
    //
    // PORTING STATUS (partial). This is a deliberately reduced but correct and
    // self-consistent subset of the full chip:
    //   * Modelled: register window, command-list walk with all four jump
    //     modes + skip + call/return stack, local/system/user clipping,
    //     normal-sprite and polygon primitives, 4bpp/8bpp/16bpp texture
    //     sample fetch, replace pixel-op, EWDR/EWLR/EWRR framebuffer erase,
    //     EDSR draw-end latch, draw/display buffer ping-pong, save/load.
    //   * Stubbed / deferred: scaled & distorted sprites, polyline & line
    //     primitives, the shadow / half-luminance / half-transparent / mesh
    //     pixel-ops, Gouraud shading, high-speed-shrink, end-code handling,
    //     and cycle-accurate per-command timing. tick() runs the armed list
    //     to completion in one go rather than metering it across cycles.
    class vdp1 final : public ivideo {
      public:
        static constexpr std::size_t vram_size = 512U * 1024U;
        static constexpr std::size_t framebuffer_size = 256U * 1024U;
        static constexpr std::size_t cmd_table_bytes = 0x20U;
        static constexpr std::uint32_t fb_width = 512U; // 16bpp pixels per line
        static constexpr std::uint32_t fb_height = 256U;
        static constexpr std::uint32_t fb_pitch_bytes = 1024U; // every mode

        // Register byte offsets within the $00..$1F window (word-aligned).
        static constexpr std::uint8_t reg_tvmr = 0x00U; // TV mode
        static constexpr std::uint8_t reg_fbcr = 0x02U; // frame-buffer change
        static constexpr std::uint8_t reg_ptmr = 0x04U; // plot trigger mode
        static constexpr std::uint8_t reg_ewdr = 0x06U; // erase/write data
        static constexpr std::uint8_t reg_ewlr = 0x08U; // erase upper-left
        static constexpr std::uint8_t reg_ewrr = 0x0AU; // erase lower-right
        static constexpr std::uint8_t reg_endr = 0x0CU; // end-draw strobe
        static constexpr std::uint8_t reg_edsr = 0x10U; // end-draw status (ro)
        static constexpr std::uint8_t reg_lopr = 0x12U; // last op address (ro)
        static constexpr std::uint8_t reg_copr = 0x14U; // current op address (ro)
        static constexpr std::uint8_t reg_modr = 0x16U; // mode status (ro)

        // Plot-trigger-mode values (PTMR bits 1:0).
        static constexpr std::uint8_t ptmr_idle = 0U;
        static constexpr std::uint8_t ptmr_start = 1U; // begin on register write
        static constexpr std::uint8_t ptmr_auto = 2U;  // begin each frame change

        // CTRL word fields.
        static constexpr std::uint16_t ctrl_end = 0x8000U;
        static constexpr std::uint16_t ctrl_skip = 0x4000U;
        static constexpr std::uint16_t ctrl_jump_mask = 0x3000U;
        static constexpr std::uint16_t jump_next = 0x0000U;
        static constexpr std::uint16_t jump_assign = 0x1000U;
        static constexpr std::uint16_t jump_call = 0x2000U;
        static constexpr std::uint16_t jump_return = 0x3000U;

        // CTRL command types (bits 3:0).
        static constexpr std::uint8_t cmd_normal_sprite = 0x0U;
        static constexpr std::uint8_t cmd_scaled_sprite = 0x1U;
        static constexpr std::uint8_t cmd_distorted_sprite = 0x2U;
        static constexpr std::uint8_t cmd_polygon = 0x4U;
        static constexpr std::uint8_t cmd_polyline = 0x5U;
        static constexpr std::uint8_t cmd_line = 0x6U;
        static constexpr std::uint8_t cmd_user_clip = 0x8U;
        static constexpr std::uint8_t cmd_system_clip = 0x9U;
        static constexpr std::uint8_t cmd_local_coord = 0xAU;

        enum class draw_state : std::uint8_t { idle, plotting, done };

        // Host-supplied palette reader: resolve a colour-bank index to a native
        // 0BGR1555 word. When unset, colour-bank texels fall back to a
        // monochrome ramp so the engine still produces non-zero output without
        // a bound companion CRAM.
        using cram_reader = std::function<std::uint16_t(std::uint16_t palette_index)>;

        vdp1() { reset(reset_kind::power_on); }

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

        // Register access (offset is the byte index within $00..$1F).
        [[nodiscard]] std::uint16_t read_register(std::uint8_t offset) const noexcept;
        void write_register(std::uint8_t offset, std::uint16_t value) noexcept;

        // Raw VRAM access so a host/test can stage a command list + textures.
        [[nodiscard]] std::span<std::uint8_t> vram() noexcept { return vram_; }
        [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept { return vram_; }
        void write_vram_word(std::uint32_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t read_vram_word(std::uint32_t offset) const noexcept;

        // Framebuffer pixel access (native 0BGR1555, draw buffer).
        [[nodiscard]] std::uint16_t fb_read(std::int32_t x, std::int32_t y) const noexcept;
        void fb_write(std::int32_t x, std::int32_t y, std::uint16_t pixel) noexcept;

        // Drawing lifecycle. begin_plot arms the engine from the top of VRAM;
        // step_command processes one entry; run_to_end bounds the walk.
        void begin_plot() noexcept;
        bool step_command() noexcept;
        int run_to_end(int max_commands) noexcept;
        [[nodiscard]] draw_state state() const noexcept { return state_; }

        // Frame-change event (companion VBlank): rolls draw-end status, swaps
        // ping-pong ownership intent, and re-arms in AUTO mode.
        void on_frame_change() noexcept;

        void erase_draw_buffer() noexcept;
        void erase_display_buffer() noexcept;
        void swap_framebuffers() noexcept { draw_buffer_index_ ^= 1U; }
        [[nodiscard]] std::uint8_t draw_buffer_index() const noexcept { return draw_buffer_index_; }
        [[nodiscard]] std::uint8_t display_buffer_index() const noexcept {
            return static_cast<std::uint8_t>(draw_buffer_index_ ^ 1U);
        }
        [[nodiscard]] bool is_8bpp_framebuffer() const noexcept { return (tvmr_ & 0x0001U) != 0U; }

        void set_cram_reader(cram_reader reader) noexcept { cram_reader_ = std::move(reader); }

      private:
        struct sample {
            std::uint16_t color{}; // native 0BGR1555
            bool transparent{true};
        };

        [[nodiscard]] std::uint32_t framebuffer_width_pixels() const noexcept {
            return is_8bpp_framebuffer() ? 1024U : fb_width;
        }
        void erase_buffer(std::uint8_t buffer_index) noexcept;

        // Command handlers operate on the entry at cmd_ip_.
        void draw_normal_sprite(std::uint32_t cmd_base, std::uint16_t pmod,
                                std::uint16_t colr) noexcept;
        void draw_polygon(std::uint32_t cmd_base, std::uint16_t pmod, std::uint16_t colr) noexcept;
        void exec_local_coord(std::uint32_t cmd_base) noexcept;
        void exec_system_clip(std::uint32_t cmd_base) noexcept;
        void exec_user_clip(std::uint32_t cmd_base) noexcept;
        void advance_ip(std::uint16_t ctrl) noexcept;

        [[nodiscard]] bool pixel_passes_clip(std::int32_t x, std::int32_t y,
                                             std::uint16_t pmod) const noexcept;
        // Resolve one texel; transparent==true means "do not draw".
        [[nodiscard]] sample fetch_texel(std::uint32_t src_addr, std::uint16_t color_bank,
                                         std::uint8_t color_mode, std::uint16_t pmod,
                                         std::int32_t tx, std::int32_t ty,
                                         std::int32_t tw) const noexcept;

        std::vector<std::uint8_t> vram_ = std::vector<std::uint8_t>(vram_size);
        std::array<std::vector<std::uint8_t>, 2> framebuffers_{
            std::vector<std::uint8_t>(framebuffer_size),
            std::vector<std::uint8_t>(framebuffer_size)};
        std::uint8_t draw_buffer_index_{};

        // Register shadow (word-indexed by offset/2 over $00..$1F).
        std::array<std::uint16_t, 0x10> regs_{};

        std::uint16_t tvmr_{};
        std::uint16_t fbcr_{};
        std::uint8_t ptmr_{ptmr_idle};
        std::uint16_t ewdr_{};
        std::uint16_t ewlr_{};
        std::uint16_t ewrr_{};
        std::uint16_t edsr_{};
        std::uint16_t lopr_{};
        std::uint16_t copr_{};
        std::uint8_t pending_ptmr_{ptmr_idle};
        bool pending_ptmr_valid_{};

        draw_state state_{draw_state::idle};
        std::uint32_t cmd_ip_{};
        std::array<std::uint32_t, 8> call_stack_{};
        int call_depth_{};

        std::int16_t local_x_{};
        std::int16_t local_y_{};
        std::int16_t sys_clip_x1_{};
        std::int16_t sys_clip_y1_{};
        std::int16_t sys_clip_x2_{static_cast<std::int16_t>(fb_width - 1U)};
        std::int16_t sys_clip_y2_{static_cast<std::int16_t>(fb_height - 1U)};
        std::int16_t user_clip_x1_{};
        std::int16_t user_clip_y1_{};
        std::int16_t user_clip_x2_{static_cast<std::int16_t>(fb_width - 1U)};
        std::int16_t user_clip_y2_{static_cast<std::int16_t>(fb_height - 1U)};

        std::uint64_t frame_index_{};

        cram_reader cram_reader_{};

        // The 0x00RRGGBB view, rebuilt by framebuffer() from the draw buffer.
        mutable std::vector<std::uint32_t> rgb_view_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(fb_width) * fb_height);

        class introspection_surface final : public instrumentation::ichip_introspection {};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::video
