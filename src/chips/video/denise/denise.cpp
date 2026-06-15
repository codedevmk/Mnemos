#include "denise.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::video {

    chip_metadata denise::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "8362",
            .family = "denise",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void denise::reset(reset_kind /*kind*/) {
        // Hardware /RESET clears BPLCON* and the bitplane latches. The colour
        // registers are RAM cells without a reset input, but the boot ROM
        // initialises them explicitly; clearing them here matches what the
        // guest observes after a cold start.
        beam_x_ = 0U;
        beam_y_ = 0U;
        frame_index_ = 0U;
        palette_.fill(0U);
        bplcon0_ = 0U;
        bplcon1_ = 0U;
        bplcon2_ = 0U;
        bplcon3_ = 0U;
        decoded_ = bplcon0_decoded{};
        display_enabled_ = true;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void denise::write_bplcon0(std::uint16_t value) noexcept {
        bplcon0_ = value;
        decoded_.bitplane_count =
            static_cast<std::uint8_t>((value & bplcon0_bpu_mask) >> bplcon0_bpu_shift);
        decoded_.hires = (value & bplcon0_hires) != 0U;
        decoded_.ham = (value & bplcon0_ham) != 0U;
        decoded_.dual_playfield = (value & bplcon0_dpf) != 0U;
        decoded_.color_enable = (value & bplcon0_color) != 0U;
        decoded_.genlock_audio = (value & bplcon0_gaud) != 0U;
        decoded_.light_pen = (value & bplcon0_lpen) != 0U;
        decoded_.interlace = (value & bplcon0_lace) != 0U;
        decoded_.external_resync = (value & bplcon0_ersy) != 0U;
        decoded_.ecs_enabled = (value & bplcon0_ecsena) != 0U;
    }

    std::uint32_t denise::palette_rgb888(std::size_t index) const noexcept {
        if (index >= palette_size) {
            return 0U;
        }
        const std::uint16_t raw = palette_[index];
        const std::uint32_t r = expand4(static_cast<std::uint8_t>((raw >> 8U) & 0x0FU));
        const std::uint32_t g = expand4(static_cast<std::uint8_t>((raw >> 4U) & 0x0FU));
        const std::uint32_t b = expand4(static_cast<std::uint8_t>(raw & 0x0FU));
        return (r << 16U) | (g << 8U) | b;
    }

    void denise::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (beam_x_ == 0U) {
                if (beam_y_ == visible_height) {
                    render_frame();
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(beam_y_);
                    }
                }
                if (scanline_cb_) {
                    scanline_cb_(beam_y_);
                }
            }
            if (++beam_x_ == line_pixels) {
                beam_x_ = 0U;
                if (++beam_y_ == frame_lines) {
                    beam_y_ = 0U;
                }
            }
        }
    }

    frame_buffer_view denise::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = 0U};
    }

    std::uint32_t denise::plane_bit_at(std::size_t plane, std::uint32_t x,
                                       std::uint32_t y) const noexcept {
        const std::span<const std::uint8_t>& bits = planes_[plane];
        const std::size_t byte_index =
            static_cast<std::size_t>(y) * plane_stride_bytes_ + (x >> 3U);
        if (byte_index >= bits.size()) {
            return 0U;
        }
        const std::uint32_t bit = 7U - (x & 7U); // MSB = leftmost pixel
        return (bits[byte_index] >> bit) & 1U;
    }

    std::uint32_t denise::plane_index_at(std::uint32_t x, std::uint32_t y) const noexcept {
        const std::uint32_t enabled =
            decoded_.bitplane_count <= bitplane_count ? decoded_.bitplane_count : bitplane_count;
        std::uint32_t index = 0U;
        for (std::uint32_t plane = 0; plane < enabled; ++plane) {
            index |= plane_bit_at(plane, x, y) << plane;
        }
        return index;
    }

    void denise::render_frame() noexcept {
        if (!display_enabled_) {
            for (std::uint32_t& px : pixels_) {
                px = 0U;
            }
            return;
        }

        const bool ham = decoded_.ham && decoded_.bitplane_count >= 5U;
        const bool dual = decoded_.dual_playfield && !ham;
        // EHB: six planes, neither HAM nor dual-playfield (OCS extra-half-brite).
        const bool ehb = !ham && !dual && decoded_.bitplane_count == 6U;

        for (std::uint32_t y = 0; y < visible_height; ++y) {
            // HAM holds the previous pixel across the row; the row starts from
            // the backdrop colour (register 0).
            std::uint32_t held = palette_rgb888(0U);
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                std::uint32_t rgb = 0U;
                if (dual) {
                    // Odd planes (0,2,4) -> playfield 1; even planes (1,3,5) ->
                    // playfield 2 whose colours live at register +8. A
                    // transparent (index 0) front pixel shows the back.
                    std::uint32_t pf1 = 0U;
                    std::uint32_t pf2 = 0U;
                    const std::uint32_t enabled = decoded_.bitplane_count;
                    for (std::uint32_t plane = 0; plane < enabled; ++plane) {
                        const std::uint32_t b = plane_bit_at(plane, x, y);
                        if ((plane & 1U) == 0U) {
                            pf1 |= b << (plane >> 1U);
                        } else {
                            pf2 |= b << (plane >> 1U);
                        }
                    }
                    if (pf1 != 0U) {
                        rgb = palette_rgb888(pf1);
                    } else if (pf2 != 0U) {
                        rgb = palette_rgb888(8U + pf2);
                    } else {
                        rgb = palette_rgb888(0U);
                    }
                } else if (ham) {
                    const std::uint32_t index = plane_index_at(x, y);
                    const std::uint32_t command = (index >> 4U) & 0x3U;
                    const std::uint32_t data = index & 0x0FU;
                    const std::uint8_t nib = static_cast<std::uint8_t>(data);
                    const std::uint8_t channel = expand4(nib);
                    switch (command) {
                    case 0U: // load colour from the register file (bits 3:0)
                        held = palette_rgb888(data);
                        break;
                    case 1U: // modify blue
                        held = (held & 0x00FFFF00U) | channel;
                        break;
                    case 2U: // modify red
                        held = (held & 0x0000FFFFU) | (static_cast<std::uint32_t>(channel) << 16U);
                        break;
                    case 3U: // modify green
                        held = (held & 0x00FF00FFU) | (static_cast<std::uint32_t>(channel) << 8U);
                        break;
                    default:
                        break;
                    }
                    rgb = held;
                } else if (ehb) {
                    const std::uint32_t index = plane_index_at(x, y);
                    if ((index & 0x20U) != 0U) {
                        // Half-brightness of colours 0-31: halve each channel.
                        const std::uint32_t base = palette_rgb888(index & 0x1FU);
                        rgb = ((base >> 1U) & 0x007F7F7FU);
                    } else {
                        rgb = palette_rgb888(index);
                    }
                } else {
                    rgb = palette_rgb888(plane_index_at(x, y));
                }
                pixels_[static_cast<std::size_t>(y) * visible_width + x] = rgb;
            }
        }
    }

    void denise::save_state(state_writer& writer) const {
        writer.u32(beam_x_);
        writer.u32(beam_y_);
        writer.u64(frame_index_);
        for (std::uint16_t color : palette_) {
            writer.u16(color);
        }
        writer.u16(bplcon0_);
        writer.u16(bplcon1_);
        writer.u16(bplcon2_);
        writer.u16(bplcon3_);
        writer.u32(plane_stride_bytes_);
        writer.boolean(display_enabled_);
    }

    void denise::load_state(state_reader& reader) {
        beam_x_ = reader.u32();
        beam_y_ = reader.u32();
        frame_index_ = reader.u64();
        for (std::uint16_t& color : palette_) {
            color = reader.u16();
        }
        // Re-decode BPLCON0 so the decoded view matches the restored raw word.
        write_bplcon0(reader.u16());
        bplcon1_ = reader.u16();
        bplcon2_ = reader.u16();
        bplcon3_ = reader.u16();
        plane_stride_bytes_ = reader.u32();
        display_enabled_ = reader.boolean();
    }

    instrumentation::ichip_introspection& denise::introspection() noexcept {
        return introspection_;
    }

    frame_buffer_view denise::introspection_surface::palette_layer::view() const {
        // Decode the 32 colour registers as a 16-swatches-per-row grid of
        // 8x8 blocks. Rebuilt on each call.
        const denise& d = *owner_;
        constexpr std::uint32_t swatches_per_row = 16U;
        constexpr std::uint32_t swatch = 8U;
        constexpr std::uint32_t sheet_width = swatches_per_row * swatch;
        constexpr std::uint32_t rows =
            (static_cast<std::uint32_t>(denise::palette_size) + swatches_per_row - 1U) /
            swatches_per_row;
        constexpr std::uint32_t sheet_height = rows * swatch;

        d.palette_sheet_.assign(static_cast<std::size_t>(sheet_width) * sheet_height, 0U);
        for (std::size_t entry = 0; entry < denise::palette_size; ++entry) {
            const std::uint32_t rgb = d.palette_rgb888(entry);
            const std::uint32_t base_x =
                static_cast<std::uint32_t>(entry % swatches_per_row) * swatch;
            const std::uint32_t base_y =
                static_cast<std::uint32_t>(entry / swatches_per_row) * swatch;
            for (std::uint32_t sy = 0; sy < swatch; ++sy) {
                for (std::uint32_t sx = 0; sx < swatch; ++sx) {
                    d.palette_sheet_[static_cast<std::size_t>(base_y + sy) * sheet_width + base_x +
                                     sx] = rgb;
                }
            }
        }
        return {.pixels = d.palette_sheet_.data(),
                .width = sheet_width,
                .height = sheet_height,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto denise_registration =
            register_factory("commodore.denise", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<denise>(); });
    } // namespace

} // namespace mnemos::chips::video
