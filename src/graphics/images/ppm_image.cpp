#include "ppm_image.hpp"

#include <string>

namespace mnemos::graphics::images {

    std::vector<std::uint8_t> ppm_image::encode() const {
        const std::string header =
            "P6\n" + std::to_string(width_) + " " + std::to_string(height_) + "\n255\n";

        std::vector<std::uint8_t> out;
        out.reserve(header.size() + pixels_.size() * 3U);
        out.insert(out.end(), header.begin(), header.end());
        for (std::uint32_t p : pixels_) {
            out.push_back(static_cast<std::uint8_t>((p >> 16U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((p >> 8U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>(p & 0xFFU));
        }
        return out;
    }

} // namespace mnemos::graphics::images
