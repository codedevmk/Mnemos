#pragma once

#include "image.hpp"

#include <cstdint>
#include <vector>

namespace mnemos::graphics::images {

    // Binary (P6) Portable Pixmap. Header "P6\n<w> <h>\n255\n" followed by three
    // bytes (R, G, B) per pixel, drawn from each packed 0x00RRGGBB value.
    class ppm_image final : public image {
      public:
        ppm_image(std::uint32_t w, std::uint32_t h, std::vector<std::uint32_t> pixels)
            : image(w, h, std::move(pixels)) {}

        [[nodiscard]] std::vector<std::uint8_t> encode() const override;
    };

} // namespace mnemos::graphics::images
