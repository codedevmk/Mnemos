#ifndef MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP
#define MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP

// CPS-2 video (a sibling of the CPS-1 cps_a_b, NOT a variant -- CPS-2 has its own
// object/tile/sprite format). Increment 1 of phase 7: the colour pipeline -- the
// reg5 palette DMA out of video RAM (gated per page by the CPS-B palette-control
// register), the 16-bit brightness:R:G:B colour decode, and the 384x224
// framebuffer (backdrop fill). The scroll tilemaps, sprites/object bank, layer
// priority, and the vblank IRQ land in later increments.

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

      private:
        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;
        // One 4bpp texel from the gfx ROM at `tile_base`, rows `row_stride` bytes
        // apart (16 pixels = 8 bytes for the 8x8/16x16 layouts; 32 pixels = 16
        // bytes for 32x32). Transparent if out of range.
        [[nodiscard]] std::uint8_t decode_packed_pixel(std::uint32_t tile_base,
                                                       std::uint32_t row_stride, int x,
                                                       int y) const noexcept;

        std::span<const std::uint8_t> video_ram_{};
        std::span<const std::uint8_t> gfx_{};
        std::array<std::uint8_t, palette_bytes> palette_ram_{};
        static constexpr std::size_t pixel_count =
            static_cast<std::size_t>(visible_width) * visible_height;
        std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(pixel_count, 0U);
        std::uint64_t frame_index_{};
        std::array<register_descriptor, 2> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video

#endif // MNEMOS_CHIPS_VIDEO_CPS2_VIDEO_CPS2_VIDEO_HPP
