#include "text_overlay.hpp"

#include <array>

namespace mnemos::apps::player {

    namespace {

        struct glyph final {
            std::array<std::uint8_t, kGlyphHeight> rows;
        };

        // Pack one row of an ASCII-art glyph into a byte (bit 7 = column 0).
        consteval std::uint8_t pack_row(const char* row) noexcept {
            std::uint8_t v = 0;
            for (int i = 0; i < kGlyphWidth; ++i) {
                if (row[i] != '.' && row[i] != ' ' && row[i] != 0) {
                    v |= static_cast<std::uint8_t>(1U << (7 - i));
                }
            }
            return v;
        }

        consteval glyph make_glyph(const char* r0, const char* r1, const char* r2, const char* r3,
                                   const char* r4, const char* r5, const char* r6,
                                   const char* r7) noexcept {
            return {{pack_row(r0), pack_row(r1), pack_row(r2), pack_row(r3), pack_row(r4),
                     pack_row(r5), pack_row(r6), pack_row(r7)}};
        }

        // Hand-designed 8x8 monospaced ASCII font (printable range 0x20..0x7E).
        // Letterforms are deliberately blocky and uniform-stroke; the goal is a
        // status overlay that stays legible at native 1:1 against an arbitrary
        // gameplay background, not a typeface.
        constexpr int kFirstGlyph = 0x20;
        constexpr int kLastGlyph = 0x7E;
        constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;
        // clang-format off
        constexpr std::array<glyph, kGlyphCount> kFont = {{
            // 0x20 ' '
            make_glyph("........","........","........","........","........","........","........","........"),
            // 0x21 '!'
            make_glyph("........","..XX....","..XX....","..XX....","..XX....","........","..XX....","........"),
            // 0x22 '"'
            make_glyph(".XX.XX..",".XX.XX..","........","........","........","........","........","........"),
            // 0x23 '#'
            make_glyph("..X.X...",".XXXXX..","..X.X...","..X.X...",".XXXXX..","..X.X...","........","........"),
            // 0x24 '$'
            make_glyph("..XX....",".X.XX...",".X.X....","..XX....","...X.X..","..XX.X..","..XX....","........"),
            // 0x25 '%'
            make_glyph(".XX..X..",".XX.X...","...X....","..X.....",".X.XX...","X..XX...","........","........"),
            // 0x26 '&'
            make_glyph("..XX....",".X..X...",".X.X....","..X.....",".X.X.X..",".X..X...","..XX.X..","........"),
            // 0x27 '''
            make_glyph("..XX....","..XX....","..X.....","........","........","........","........","........"),
            // 0x28 '('
            make_glyph("...X....","..X.....",".X......",".X......",".X......","..X.....","...X....","........"),
            // 0x29 ')'
            make_glyph(".X......","..X.....","...X....","...X....","...X....","..X.....",".X......","........"),
            // 0x2A '*'
            make_glyph("........","..X.X...","...X....",".XXXXX..","...X....","..X.X...","........","........"),
            // 0x2B '+'
            make_glyph("........","...X....","...X....",".XXXXX..","...X....","...X....","........","........"),
            // 0x2C ','
            make_glyph("........","........","........","........","........","..XX....","..XX....","..X....."),
            // 0x2D '-'
            make_glyph("........","........","........",".XXXXX..","........","........","........","........"),
            // 0x2E '.'
            make_glyph("........","........","........","........","........","..XX....","..XX....","........"),
            // 0x2F '/'
            make_glyph("........",".....X..","....X...","...X....","..X.....",".X......","X.......","........"),
            // 0x30 '0'
            make_glyph("..XXX...",".X...X..",".X..XX..",".X.X.X..",".XX..X..",".X...X..","..XXX...","........"),
            // 0x31 '1'
            make_glyph("...X....","..XX....","...X....","...X....","...X....","...X....","..XXX...","........"),
            // 0x32 '2'
            make_glyph("..XXX...",".X...X..","....X...","...X....","..X.....",".X......",".XXXXX..","........"),
            // 0x33 '3'
            make_glyph("..XXX...",".X...X..","....X...","...XX...","....X...",".X...X..","..XXX...","........"),
            // 0x34 '4'
            make_glyph("....X...","...XX...","..X.X...",".X..X...",".XXXXX..","....X...","....X...","........"),
            // 0x35 '5'
            make_glyph(".XXXXX..",".X......",".XXXX...","....X...","....X...",".X...X..","..XXX...","........"),
            // 0x36 '6'
            make_glyph("..XXX...",".X...X..",".X......",".XXXX...",".X...X..",".X...X..","..XXX...","........"),
            // 0x37 '7'
            make_glyph(".XXXXX..","....X...","....X...","...X....","..X.....","..X.....","..X.....","........"),
            // 0x38 '8'
            make_glyph("..XXX...",".X...X..",".X...X..","..XXX...",".X...X..",".X...X..","..XXX...","........"),
            // 0x39 '9'
            make_glyph("..XXX...",".X...X..",".X...X..","..XXXX..","....X...",".X...X..","..XXX...","........"),
            // 0x3A ':'
            make_glyph("........","..XX....","..XX....","........","..XX....","..XX....","........","........"),
            // 0x3B ';'
            make_glyph("........","..XX....","..XX....","........","..XX....","..XX....","..X.....","........"),
            // 0x3C '<'
            make_glyph("....X...","...X....","..X.....",".X......","..X.....","...X....","....X...","........"),
            // 0x3D '='
            make_glyph("........","........",".XXXXX..","........",".XXXXX..","........","........","........"),
            // 0x3E '>'
            make_glyph(".X......","..X.....","...X....","....X...","...X....","..X.....",".X......","........"),
            // 0x3F '?'
            make_glyph("..XXX...",".X...X..","....X...","...X....","...X....","........","...X....","........"),
            // 0x40 '@'
            make_glyph("..XXX...",".X...X..",".X.X.X..",".X.XXX..",".X......",".X......","..XXXX..","........"),
            // 0x41 'A'
            make_glyph("...X....","..X.X...",".X...X..",".X...X..",".XXXXX..",".X...X..",".X...X..","........"),
            // 0x42 'B'
            make_glyph(".XXXX...",".X...X..",".X...X..",".XXXX...",".X...X..",".X...X..",".XXXX...","........"),
            // 0x43 'C'
            make_glyph("..XXX...",".X...X..",".X......",".X......",".X......",".X...X..","..XXX...","........"),
            // 0x44 'D'
            make_glyph(".XXXX...",".X...X..",".X...X..",".X...X..",".X...X..",".X...X..",".XXXX...","........"),
            // 0x45 'E'
            make_glyph(".XXXXX..",".X......",".X......",".XXXX...",".X......",".X......",".XXXXX..","........"),
            // 0x46 'F'
            make_glyph(".XXXXX..",".X......",".X......",".XXXX...",".X......",".X......",".X......","........"),
            // 0x47 'G'
            make_glyph("..XXX...",".X...X..",".X......",".X.XXX..",".X...X..",".X...X..","..XXX...","........"),
            // 0x48 'H'
            make_glyph(".X...X..",".X...X..",".X...X..",".XXXXX..",".X...X..",".X...X..",".X...X..","........"),
            // 0x49 'I'
            make_glyph("..XXX...","...X....","...X....","...X....","...X....","...X....","..XXX...","........"),
            // 0x4A 'J'
            make_glyph("....XX..",".....X..",".....X..",".....X..","..X..X..","..X..X..","...XX...","........"),
            // 0x4B 'K'
            make_glyph(".X...X..",".X..X...",".X.X....",".XX.....",".X.X....",".X..X...",".X...X..","........"),
            // 0x4C 'L'
            make_glyph(".X......",".X......",".X......",".X......",".X......",".X......",".XXXXX..","........"),
            // 0x4D 'M'
            make_glyph(".X...X..",".XX.XX..",".X.X.X..",".X.X.X..",".X...X..",".X...X..",".X...X..","........"),
            // 0x4E 'N'
            make_glyph(".X...X..",".XX..X..",".X.X.X..",".X.X.X..",".X..XX..",".X...X..",".X...X..","........"),
            // 0x4F 'O'
            make_glyph("..XXX...",".X...X..",".X...X..",".X...X..",".X...X..",".X...X..","..XXX...","........"),
            // 0x50 'P'
            make_glyph(".XXXX...",".X...X..",".X...X..",".XXXX...",".X......",".X......",".X......","........"),
            // 0x51 'Q'
            make_glyph("..XXX...",".X...X..",".X...X..",".X...X..",".X.X.X..",".X..X...","..XX.X..","........"),
            // 0x52 'R'
            make_glyph(".XXXX...",".X...X..",".X...X..",".XXXX...",".X.X....",".X..X...",".X...X..","........"),
            // 0x53 'S'
            make_glyph("..XXX...",".X...X..",".X......","..XXX...","....X...",".X...X..","..XXX...","........"),
            // 0x54 'T'
            make_glyph(".XXXXX..","...X....","...X....","...X....","...X....","...X....","...X....","........"),
            // 0x55 'U'
            make_glyph(".X...X..",".X...X..",".X...X..",".X...X..",".X...X..",".X...X..","..XXX...","........"),
            // 0x56 'V'
            make_glyph(".X...X..",".X...X..",".X...X..",".X...X..",".X...X..","..X.X...","...X....","........"),
            // 0x57 'W'
            make_glyph(".X...X..",".X...X..",".X...X..",".X.X.X..",".X.X.X..",".XX.XX..",".X...X..","........"),
            // 0x58 'X'
            make_glyph(".X...X..",".X...X..","..X.X...","...X....","..X.X...",".X...X..",".X...X..","........"),
            // 0x59 'Y'
            make_glyph(".X...X..",".X...X..","..X.X...","...X....","...X....","...X....","...X....","........"),
            // 0x5A 'Z'
            make_glyph(".XXXXX..","....X...","...X....","..X.....",".X......",".X......",".XXXXX..","........"),
            // 0x5B '['
            make_glyph("..XXX...","..X.....","..X.....","..X.....","..X.....","..X.....","..XXX...","........"),
            // 0x5C '\'
            make_glyph("........","X.......",".X......","..X.....","...X....","....X...",".....X..","........"),
            // 0x5D ']'
            make_glyph("..XXX...","....X...","....X...","....X...","....X...","....X...","..XXX...","........"),
            // 0x5E '^'
            make_glyph("...X....","..X.X...",".X...X..","........","........","........","........","........"),
            // 0x5F '_'
            make_glyph("........","........","........","........","........","........","........",".XXXXX.."),
            // 0x60 '`'
            make_glyph(".XX.....","..X.....","........","........","........","........","........","........"),
            // 0x61 'a'
            make_glyph("........","........","..XXX...","....X...","..XXXX..",".X..X...","..XXXX..","........"),
            // 0x62 'b'
            make_glyph(".X......",".X......",".XXXX...",".X...X..",".X...X..",".X...X..",".XXXX...","........"),
            // 0x63 'c'
            make_glyph("........","........","..XXX...",".X...X..",".X......",".X...X..","..XXX...","........"),
            // 0x64 'd'
            make_glyph(".....X..",".....X..","..XXXX..",".X...X..",".X...X..",".X...X..","..XXXX..","........"),
            // 0x65 'e'
            make_glyph("........","........","..XXX...",".X...X..",".XXXXX..",".X......","..XXX...","........"),
            // 0x66 'f'
            make_glyph("...XX...","..X..X..","..X.....",".XXXX...","..X.....","..X.....","..X.....","........"),
            // 0x67 'g'
            make_glyph("........","........","..XXXX..",".X...X..",".X...X..","..XXXX..","....X...","..XXX..."),
            // 0x68 'h'
            make_glyph(".X......",".X......",".XXXX...",".X...X..",".X...X..",".X...X..",".X...X..","........"),
            // 0x69 'i'
            make_glyph("...X....","........","..XX....","...X....","...X....","...X....","..XXX...","........"),
            // 0x6A 'j'
            make_glyph("....X...","........","...XX...","....X...","....X...","....X...",".X..X...","..XX...."),
            // 0x6B 'k'
            make_glyph(".X......",".X......",".X..X...",".X.X....",".XX.....",".X.X....",".X..X...","........"),
            // 0x6C 'l'
            make_glyph("..XX....","...X....","...X....","...X....","...X....","...X....","..XXX...","........"),
            // 0x6D 'm'
            make_glyph("........","........",".XX.X...",".X.X.X..",".X.X.X..",".X...X..",".X...X..","........"),
            // 0x6E 'n'
            make_glyph("........","........",".XXXX...",".X...X..",".X...X..",".X...X..",".X...X..","........"),
            // 0x6F 'o'
            make_glyph("........","........","..XXX...",".X...X..",".X...X..",".X...X..","..XXX...","........"),
            // 0x70 'p'
            make_glyph("........","........",".XXXX...",".X...X..",".X...X..",".XXXX...",".X......",".X......"),
            // 0x71 'q'
            make_glyph("........","........","..XXXX..",".X...X..",".X...X..","..XXXX..","....X...","....XX.."),
            // 0x72 'r'
            make_glyph("........","........",".X.XX...",".XX..X..",".X......",".X......",".X......","........"),
            // 0x73 's'
            make_glyph("........","........","..XXXX..",".X......","..XXX...","....X...",".XXXX...","........"),
            // 0x74 't'
            make_glyph("..X.....","..X.....",".XXXX...","..X.....","..X.....","..X..X..","...XX...","........"),
            // 0x75 'u'
            make_glyph("........","........",".X...X..",".X...X..",".X...X..",".X...X..","..XXXX..","........"),
            // 0x76 'v'
            make_glyph("........","........",".X...X..",".X...X..",".X...X..","..X.X...","...X....","........"),
            // 0x77 'w'
            make_glyph("........","........",".X...X..",".X...X..",".X.X.X..",".X.X.X..","..X.X...","........"),
            // 0x78 'x'
            make_glyph("........","........",".X...X..","..X.X...","...X....","..X.X...",".X...X..","........"),
            // 0x79 'y'
            make_glyph("........","........",".X...X..",".X...X..",".X...X..","..XXXX..","....X...","..XXX..."),
            // 0x7A 'z'
            make_glyph("........","........",".XXXXX..","....X...","...X....","..X.....",".XXXXX..","........"),
            // 0x7B '{'
            make_glyph("...XX...","..X.....","..X.....",".X......","..X.....","..X.....","...XX...","........"),
            // 0x7C '|'
            make_glyph("...X....","...X....","...X....","...X....","...X....","...X....","...X....","........"),
            // 0x7D '}'
            make_glyph(".XX.....","...X....","...X....","....X...","...X....","...X....",".XX.....","........"),
            // 0x7E '~'
            make_glyph(".XX..X..","X..XX...","........","........","........","........","........","........"),
        }};
        // clang-format on

        // Hollow square ("tofu") for any byte the font doesn't cover.
        constexpr glyph kMissing =
            make_glyph(".XXXXXX.", ".X....X.", ".X....X.", ".X....X.", ".X....X.", ".X....X.",
                       ".XXXXXX.", "........");

        [[nodiscard]] const glyph& glyph_for(unsigned char c) noexcept {
            const int idx = static_cast<int>(c) - kFirstGlyph;
            if (idx < 0 || idx >= kGlyphCount) {
                return kMissing;
            }
            return kFont[static_cast<std::size_t>(idx)];
        }

    } // namespace

    void fill_rect(std::uint32_t color, std::uint32_t* dst, int dst_w, int dst_h, int x, int y,
                   int w, int h) noexcept {
        if (dst == nullptr || dst_w <= 0 || dst_h <= 0) {
            return;
        }
        const int x0 = x < 0 ? 0 : x;
        const int y0 = y < 0 ? 0 : y;
        const int x1 = x + w > dst_w ? dst_w : x + w;
        const int y1 = y + h > dst_h ? dst_h : y + h;
        for (int py = y0; py < y1; ++py) {
            std::uint32_t* row = dst + static_cast<std::ptrdiff_t>(py) * dst_w;
            for (int px = x0; px < x1; ++px) {
                row[px] = color;
            }
        }
    }

    void draw_text(std::string_view text, std::uint32_t fg_color, std::uint32_t* dst, int dst_w,
                   int dst_h, int x, int y) noexcept {
        if (dst == nullptr || dst_w <= 0 || dst_h <= 0) {
            return;
        }
        int cursor = x;
        for (unsigned char c : text) {
            const glyph& g = glyph_for(c);
            for (int row = 0; row < kGlyphHeight; ++row) {
                const int py = y + row;
                if (py < 0 || py >= dst_h) {
                    continue;
                }
                const std::uint8_t bits = g.rows[static_cast<std::size_t>(row)];
                if (bits == 0U) {
                    continue;
                }
                std::uint32_t* dst_row = dst + static_cast<std::ptrdiff_t>(py) * dst_w;
                for (int col = 0; col < kGlyphWidth; ++col) {
                    if ((bits & (1U << (7 - col))) == 0U) {
                        continue;
                    }
                    const int px = cursor + col;
                    if (px < 0 || px >= dst_w) {
                        continue;
                    }
                    dst_row[px] = fg_color;
                }
            }
            cursor += kGlyphWidth;
            if (cursor >= dst_w) {
                break;
            }
        }
    }

} // namespace mnemos::apps::player
