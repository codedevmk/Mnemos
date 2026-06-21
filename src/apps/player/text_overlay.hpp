#pragma once

// Minimal 8x8 monospaced ASCII bitmap-font blitter the player uses for its
// status overlay. The font covers the printable ASCII range (0x20..0x7E);
// anything outside that range renders as a hollow square (the conventional
// "missing glyph" tofu).
//
// Pixel format: the destination buffer is std::uint32_t packed 0x00RRGGBB
// (same as the adapters' framebuffers). draw_text() only writes pixels
// where the glyph bit is set; the caller pre-fills the background. The
// drawn area is clipped to the destination bounds.

#include <cstdint>
#include <string_view>

namespace mnemos::apps::player {

    constexpr int kGlyphWidth = 8;
    constexpr int kGlyphHeight = 8;

    // Pixel width needed to render `text` (kGlyphWidth per code unit;
    // unsupported bytes still take one cell).
    [[nodiscard]] inline constexpr int text_pixel_width(std::string_view text) noexcept {
        return static_cast<int>(text.size()) * kGlyphWidth;
    }

    // Blit `text` left-anchored at (x, y) into the dst_w x dst_h pixel
    // buffer. Pixels where the glyph row's bit is set are written to
    // `fg_color`; cleared bits are left untouched (caller-owned
    // background). Negative or oversize positions are clipped, not an
    // error.
    void draw_text(std::string_view text, std::uint32_t fg_color, std::uint32_t* dst, int dst_w,
                   int dst_h, int x, int y) noexcept;

    // Fill the rectangle [x, x+w) x [y, y+h) of dst with `color`,
    // clipped to the destination bounds.
    void fill_rect(std::uint32_t color, std::uint32_t* dst, int dst_w, int dst_h, int x, int y,
                   int w, int h) noexcept;

    // Draw a plus-shaped reticle centred at (cx, cy): a horizontal and a
    // vertical arm extending `arm` pixels in each direction (so each line
    // spans 2*arm+1 pixels), written in `color`. Clipped to the destination
    // bounds; a centre off the buffer draws nothing.
    void draw_crosshair(std::uint32_t color, std::uint32_t* dst, int dst_w, int dst_h, int cx,
                        int cy, int arm) noexcept;

} // namespace mnemos::apps::player
