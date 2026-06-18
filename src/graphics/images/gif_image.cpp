#include "gif_image.hpp"

#include "file.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace mnemos::graphics::images {

    namespace {

        constexpr std::uint16_t kClearCode = 256U;
        constexpr std::uint16_t kEndCode = 257U;
        constexpr std::uint8_t kMinLzwCodeSize = 8U;
        constexpr std::uint8_t kPackedGlobalColorTable256 = 0xF7U;
        constexpr std::uint8_t kPackedNoLocalColorTable = 0x00U;

        void append_le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
            out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        }

        void append_ascii(std::vector<std::uint8_t>& out, const char* text, std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(static_cast<std::uint8_t>(text[i]));
            }
        }

        std::uint8_t quantize_rgb332(std::uint32_t pixel) {
            const auto r = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
            const auto g = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
            const auto b = static_cast<std::uint8_t>(pixel & 0xFFU);
            return static_cast<std::uint8_t>(((r >> 5U) << 5U) | ((g >> 5U) << 2U) | (b >> 6U));
        }

        void append_rgb332_palette(std::vector<std::uint8_t>& out) {
            for (std::uint16_t i = 0; i < 256U; ++i) {
                const std::uint8_t r3 = static_cast<std::uint8_t>((i >> 5U) & 0x07U);
                const std::uint8_t g3 = static_cast<std::uint8_t>((i >> 2U) & 0x07U);
                const std::uint8_t b2 = static_cast<std::uint8_t>(i & 0x03U);
                out.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(r3) * 255U) / 7U));
                out.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(g3) * 255U) / 7U));
                out.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(b2) * 255U) / 3U));
            }
        }

        class fixed_9_bit_writer final {
          public:
            void write(std::uint16_t code) {
                bit_buffer_ |= static_cast<std::uint32_t>(code) << bit_count_;
                bit_count_ += 9U;
                while (bit_count_ >= 8U) {
                    bytes_.push_back(static_cast<std::uint8_t>(bit_buffer_ & 0xFFU));
                    bit_buffer_ >>= 8U;
                    bit_count_ -= 8U;
                }
            }

            std::vector<std::uint8_t> finish() {
                if (bit_count_ > 0U) {
                    bytes_.push_back(static_cast<std::uint8_t>(bit_buffer_ & 0xFFU));
                    bit_buffer_ = 0U;
                    bit_count_ = 0U;
                }
                return std::move(bytes_);
            }

          private:
            std::vector<std::uint8_t> bytes_{};
            std::uint32_t bit_buffer_{};
            unsigned bit_count_{};
        };

        std::vector<std::uint8_t> encode_lzw_fixed9(std::span<const std::uint32_t> pixels) {
            fixed_9_bit_writer bits;
            // Keep each post-clear literal run under the 512-code-size threshold.
            constexpr std::size_t kMaxLiteralRunBeforeClear = 254U;
            std::size_t offset = 0;
            while (offset < pixels.size()) {
                bits.write(kClearCode);
                const std::size_t n =
                    std::min(kMaxLiteralRunBeforeClear, pixels.size() - offset);
                for (std::size_t i = 0; i < n; ++i) {
                    bits.write(quantize_rgb332(pixels[offset + i]));
                }
                offset += n;
            }
            bits.write(kEndCode);
            return bits.finish();
        }

        void append_data_subblocks(std::vector<std::uint8_t>& out,
                                   std::span<const std::uint8_t> data) {
            std::size_t offset = 0;
            while (offset < data.size()) {
                const std::size_t n = std::min<std::size_t>(255U, data.size() - offset);
                out.push_back(static_cast<std::uint8_t>(n));
                out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                           data.begin() + static_cast<std::ptrdiff_t>(offset + n));
                offset += n;
            }
            out.push_back(0x00U);
        }

        bool valid_image_dimensions(std::uint32_t width, std::uint32_t height) {
            return width > 0U && height > 0U && width <= std::numeric_limits<std::uint16_t>::max() &&
                   height <= std::numeric_limits<std::uint16_t>::max();
        }

    } // namespace

    std::vector<std::uint8_t> gif_animation::encode() const {
        if (!valid_image_dimensions(width_, height_) || frames_.empty()) {
            return {};
        }
        const std::size_t expected_pixels = static_cast<std::size_t>(width_) * height_;
        for (const gif_frame& frame : frames_) {
            if (frame.pixels.size() != expected_pixels) {
                return {};
            }
        }

        const auto w = static_cast<std::uint16_t>(width_);
        const auto h = static_cast<std::uint16_t>(height_);
        std::vector<std::uint8_t> gif;
        gif.reserve(13U + 768U + frames_.size() * (32U + expected_pixels));

        append_ascii(gif, "GIF89a", 6U);
        append_le16(gif, w);
        append_le16(gif, h);
        gif.push_back(kPackedGlobalColorTable256);
        gif.push_back(0x00U); // background colour index
        gif.push_back(0x00U); // pixel aspect ratio
        append_rgb332_palette(gif);

        gif.push_back(0x21U);
        gif.push_back(0xFFU);
        gif.push_back(0x0BU);
        append_ascii(gif, "NETSCAPE2.0", 11U);
        gif.push_back(0x03U);
        gif.push_back(0x01U);
        append_le16(gif, loop_count_);
        gif.push_back(0x00U);

        for (const gif_frame& frame : frames_) {
            gif.push_back(0x21U);
            gif.push_back(0xF9U);
            gif.push_back(0x04U);
            gif.push_back(0x00U); // no disposal hint, no transparency
            append_le16(gif, frame.delay_centiseconds);
            gif.push_back(0x00U); // transparent index unused
            gif.push_back(0x00U); // block terminator

            gif.push_back(0x2CU);
            append_le16(gif, static_cast<std::uint16_t>(0U)); // left
            append_le16(gif, static_cast<std::uint16_t>(0U)); // top
            append_le16(gif, w);
            append_le16(gif, h);
            gif.push_back(kPackedNoLocalColorTable);

            gif.push_back(kMinLzwCodeSize);
            const std::vector<std::uint8_t> lzw = encode_lzw_fixed9(frame.pixels);
            append_data_subblocks(gif, lzw);
        }

        gif.push_back(0x3BU);
        return gif;
    }

    bool gif_animation::write(const std::string& path) const {
        const std::vector<std::uint8_t> bytes = encode();
        if (bytes.empty()) {
            return false;
        }
        return mnemos::io::write_file(path, bytes);
    }

} // namespace mnemos::graphics::images
