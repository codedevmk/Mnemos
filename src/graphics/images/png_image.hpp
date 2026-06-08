#pragma once

#include "image.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace mnemos::graphics::images {

    // PNG, 8-bit truecolour RGB (colour type 2). Each packed 0x00RRGGBB pixel
    // becomes three bytes; alpha is not represented. Scanlines use the None
    // filter and the image data is zlib-compressed (mnemos::compression).
    class png_image final : public image {
      public:
        png_image(std::uint32_t w, std::uint32_t h, std::vector<std::uint32_t> pixels)
            : image(w, h, std::move(pixels)) {}

        [[nodiscard]] std::vector<std::uint8_t> encode() const override;
    };

    // PNG, 8-bit palette-indexed (colour type 3). Each pixel is a palette index
    // (one byte) resolved through a PLTE chunk built from `palette` (0x00RRGGBB
    // entries). When `transparent_index >= 0` a tRNS chunk marks that entry fully
    // transparent. This is the lossless companion to png_image for tools that
    // want the raw indices + palette rather than flattened RGB. The palette is
    // padded with black if an index exceeds its size (PNG requires every index
    // to be in-range); a palette longer than 256 entries is truncated.
    class indexed_png_image final {
      public:
        indexed_png_image(std::uint32_t w, std::uint32_t h, std::vector<std::uint8_t> indices,
                          std::vector<std::uint32_t> palette, int transparent_index = -1)
            : width_(w), height_(h), indices_(std::move(indices)), palette_(std::move(palette)),
              transparent_index_(transparent_index) {}

        [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
        [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

        [[nodiscard]] std::vector<std::uint8_t> encode() const;
        [[nodiscard]] bool write(const std::string& path) const;

      private:
        std::uint32_t width_;
        std::uint32_t height_;
        std::vector<std::uint8_t> indices_;  // width*height palette indices, row-major
        std::vector<std::uint32_t> palette_; // 0x00RRGGBB entries
        int transparent_index_;
    };

} // namespace mnemos::graphics::images
