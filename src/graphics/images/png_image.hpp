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

} // namespace mnemos::graphics::images
