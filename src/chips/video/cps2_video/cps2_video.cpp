#include "cps2_video.hpp"

#include "state.hpp"

#include <algorithm>
#include <cstring>

namespace mnemos::chips::video {

    namespace {
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
