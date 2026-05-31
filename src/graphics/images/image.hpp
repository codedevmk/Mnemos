#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnemos::graphics::images {

    // Abstract base for a packed RGB raster. Subclasses own a single output
    // format and implement `encode()`; the base provides the pixel store and a
    // format-agnostic `write()`. Deliberately chips-free -- callers convert
    // their framebuffer (which may be strided) into a packed 0x00RRGGBB,
    // row-major buffer before constructing a concrete image.
    class image {
      public:
        image(const image&) = delete;
        image& operator=(const image&) = delete;
        virtual ~image() = default;

        [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
        [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
        [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept { return pixels_; }

        // Serialize to the subclass's container format (PPM, PNG, ...).
        [[nodiscard]] virtual std::vector<std::uint8_t> encode() const = 0;

        // Encode and write to `path`. Returns false if the file can't be written.
        [[nodiscard]] bool write(const std::string& path) const;

      protected:
        image(std::uint32_t w, std::uint32_t h, std::vector<std::uint32_t> pixels)
            : width_(w), height_(h), pixels_(std::move(pixels)) {}

        std::uint32_t width_;
        std::uint32_t height_;
        std::vector<std::uint32_t> pixels_; // 0x00RRGGBB, row-major, packed (stride == width)
    };

} // namespace mnemos::graphics::images
