#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mnemos::graphics::images {

    struct gif_frame final {
        std::vector<std::uint32_t> pixels; // 0x00RRGGBB, row-major, packed
        std::uint16_t delay_centiseconds{2U};
    };

    // GIF89a animation encoder with a fixed RGB332 global palette. This keeps
    // animation dumping dependency-free and small enough for debugger/tooling
    // use; lossless frame export stays available through PNG sequences.
    class gif_animation final {
      public:
        gif_animation(std::uint32_t w, std::uint32_t h, std::vector<gif_frame> frames,
                      std::uint16_t loop_count = 0U)
            : width_(w), height_(h), frames_(std::move(frames)), loop_count_(loop_count) {}

        [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
        [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
        [[nodiscard]] const std::vector<gif_frame>& frames() const noexcept { return frames_; }

        [[nodiscard]] std::vector<std::uint8_t> encode() const;
        [[nodiscard]] bool write(const std::string& path) const;

      private:
        std::uint32_t width_{};
        std::uint32_t height_{};
        std::vector<gif_frame> frames_{};
        std::uint16_t loop_count_{};
    };

} // namespace mnemos::graphics::images
