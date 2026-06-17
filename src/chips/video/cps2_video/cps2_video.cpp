#include "cps2_video.hpp"

#include "state.hpp"

#include <algorithm>
#include <cstring>

namespace mnemos::chips::video {

    namespace {
        // The gfx mapper banks tile codes in 0x20000-tile units (the scroll layers
        // address the upper bank, shifted per layer).
        constexpr std::uint32_t gfx_bank_size_tiles = 0x20000U;
        constexpr std::uint32_t gfx_bank_mask = gfx_bank_size_tiles - 1U;

        // A 4-bit channel scaled by the 4-bit brightness: c*0x11 expands the nibble
        // to 8 bits, then the (0x0F + 2*brightness)/0x2D factor dims/brightens it.
        [[nodiscard]] std::uint8_t scale_channel(std::uint8_t channel,
                                                 std::uint8_t brightness) noexcept {
            const std::uint32_t intensity = 0x0FU + (static_cast<std::uint32_t>(brightness) << 1U);
            return static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(channel) * 0x11U * intensity) / 0x2DU);
        }
    } // namespace

    chip_metadata cps2_video::metadata() const noexcept {
        return {
            .manufacturer = "Capcom",
            .part_number = "CPS-2 video",
            .family = "CPS2",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void cps2_video::tick(std::uint64_t /*cycles*/) {
        // Frame-at-render for now (the board calls render()); no per-cycle work.
    }

    void cps2_video::reset(reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        palette_ram_.fill(0U);
        frame_index_ = 0U;
    }

    std::uint32_t cps2_video::decode_color(std::uint16_t value) noexcept {
        const auto brightness = static_cast<std::uint8_t>((value >> 12U) & 0x0FU);
        const auto r = scale_channel(static_cast<std::uint8_t>((value >> 8U) & 0x0FU), brightness);
        const auto g = scale_channel(static_cast<std::uint8_t>((value >> 4U) & 0x0FU), brightness);
        const auto b = scale_channel(static_cast<std::uint8_t>(value & 0x0FU), brightness);
        return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) | b;
    }

    std::uint16_t cps2_video::palette_color(std::uint16_t index) const noexcept {
        const std::size_t offset = static_cast<std::size_t>(index) * 2U;
        if (offset + 1U < palette_ram_.size()) {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(palette_ram_[offset]) << 8U) |
                palette_ram_[offset + 1U]);
        }
        return 0U;
    }

    void cps2_video::copy_palette(std::uint32_t source_base, std::uint16_t control) noexcept {
        if (video_ram_.empty() || source_base >= video_ram_.size()) {
            return;
        }
        // A zero control word means "all six pages" (the reference default).
        const std::uint16_t pages = control != 0U ? control : 0x003FU;
        std::uint32_t page_source = source_base;
        bool source_advanced = false;
        for (std::size_t page = 0U; page < palette_copy_pages; ++page) {
            if ((pages & static_cast<std::uint16_t>(1U << page)) != 0U) {
                if (page_source + palette_page_bytes <= video_ram_.size()) {
                    std::memcpy(palette_ram_.data() + page * palette_page_bytes,
                                video_ram_.data() + page_source, palette_page_bytes);
                }
                page_source += static_cast<std::uint32_t>(palette_page_bytes);
                source_advanced = true;
            } else if (source_advanced) {
                page_source += static_cast<std::uint32_t>(palette_page_bytes);
            }
        }
    }

    bool cps2_video::map_gfx_code(gfx_type type, std::uint32_t code,
                                  std::uint32_t& mapped) const noexcept {
        if (gfx_.empty()) {
            return false;
        }
        if (type == gfx_type::sprites) {
            mapped = code;
            return true;
        }
        int shift = 0;
        switch (type) {
        case gfx_type::scroll1:
            shift = 0;
            break;
        case gfx_type::scroll2:
            shift = 1;
            break;
        case gfx_type::scroll3:
            shift = 3;
            break;
        default:
            return false;
        }
        const std::uint32_t shifted = code << shift;
        if (shifted > gfx_bank_mask) {
            return false;
        }
        mapped = (gfx_bank_size_tiles + (shifted & gfx_bank_mask)) >> shift;
        return true;
    }

    std::uint8_t cps2_video::decode_packed_pixel(std::uint32_t tile_base, std::uint32_t row_stride,
                                                 int x, int y) const noexcept {
        const std::uint32_t group = static_cast<std::uint32_t>(x >> 3);
        const std::uint32_t bit = static_cast<std::uint32_t>(7 - (x & 7));
        const std::uint32_t offset =
            tile_base + static_cast<std::uint32_t>(y) * row_stride + group * 4U;
        if (gfx_.empty() || offset + 3U >= gfx_.size()) {
            return transparent_pen;
        }
        return static_cast<std::uint8_t>(
            (((gfx_[offset + 3U] >> bit) & 1U) << 0U) | (((gfx_[offset + 2U] >> bit) & 1U) << 1U) |
            (((gfx_[offset + 1U] >> bit) & 1U) << 2U) | (((gfx_[offset + 0U] >> bit) & 1U) << 3U));
    }

    std::uint8_t cps2_video::tile_pixel(gfx_type type, std::uint32_t code, int x, int y,
                                        int tile_size, int x_bias) const noexcept {
        std::uint32_t mapped = 0U;
        if (!map_gfx_code(type, code, mapped)) {
            return transparent_pen;
        }
        const int rom_x = x + x_bias;
        if (tile_size == 8 && x >= 0 && x < 8 && rom_x >= 0 && rom_x < 16 && y >= 0 && y < 8) {
            return decode_packed_pixel(mapped * 64U, 8U, rom_x, y);
        }
        if (tile_size == 16 && x >= 0 && x < 16 && y >= 0 && y < 16) {
            return decode_packed_pixel(mapped * 128U, 8U, x, y);
        }
        if (tile_size == 32 && x >= 0 && x < 32 && y >= 0 && y < 32) {
            return decode_packed_pixel(mapped * 512U, 16U, x, y);
        }
        return transparent_pen;
    }

    void cps2_video::render(std::uint32_t palette_source, std::uint16_t palette_control) noexcept {
        copy_palette(palette_source, palette_control);
        // Increment 1: a flat backdrop from palette index 0. Tilemaps + sprites
        // overwrite it in later increments.
        const std::uint32_t backdrop = decode_color(palette_color(0U));
        std::fill(pixels_.begin(), pixels_.end(), backdrop);
        ++frame_index_;
    }

    frame_buffer_view cps2_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    void cps2_video::save_state(state_writer& writer) const {
        writer.u64(frame_index_);
        for (const std::uint8_t b : palette_ram_) {
            writer.u8(b);
        }
    }

    void cps2_video::load_state(state_reader& reader) {
        frame_index_ = reader.u64();
        for (std::uint8_t& b : palette_ram_) {
            b = reader.u8();
        }
    }

    instrumentation::ichip_introspection& cps2_video::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> cps2_video::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FRAME", frame_index_ & 0xFFFFFFFFULL, 32U, fmt::unsigned_integer};
        register_view_[1] = {"BACKDROP", palette_color(0U), 16U, fmt::unsigned_integer};
        return register_view_;
    }

} // namespace mnemos::chips::video
