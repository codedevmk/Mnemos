#include "png_image.hpp"

#include "crc32.hpp"
#include "deflate.hpp"
#include "file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace mnemos::graphics::images {

    namespace {

        void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
            out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        }

        // length | type | data | CRC-32(type+data) -- the PNG chunk layout.
        void append_chunk(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 4>& type,
                          std::span<const std::uint8_t> data) {
            append_be32(out, static_cast<std::uint32_t>(data.size()));
            out.insert(out.end(), type.begin(), type.end());
            out.insert(out.end(), data.begin(), data.end());

            std::uint32_t crc = security::cryptography::crc32(type);
            crc = security::cryptography::crc32(data, crc);
            append_be32(out, crc);
        }

    } // namespace

    std::vector<std::uint8_t> png_image::encode() const {
        // Filtered raw scanlines: one filter-type byte (0 = None) then RGB.
        std::vector<std::uint8_t> raw;
        raw.reserve((static_cast<std::size_t>(width_) * 3U + 1U) * height_);
        for (std::uint32_t y = 0; y < height_; ++y) {
            raw.push_back(0x00U); // None filter
            const std::size_t row = static_cast<std::size_t>(y) * width_;
            for (std::uint32_t x = 0; x < width_; ++x) {
                const std::uint32_t p = pixels_[row + x];
                raw.push_back(static_cast<std::uint8_t>((p >> 16U) & 0xFFU));
                raw.push_back(static_cast<std::uint8_t>((p >> 8U) & 0xFFU));
                raw.push_back(static_cast<std::uint8_t>(p & 0xFFU));
            }
        }

        const std::vector<std::uint8_t> idat = compression::deflate_zlib(raw);

        std::vector<std::uint8_t> png;
        png.reserve(8U + 25U + (12U + idat.size()) + 12U);

        constexpr std::array<std::uint8_t, 8> signature = {0x89U, 0x50U, 0x4EU, 0x47U,
                                                           0x0DU, 0x0AU, 0x1AU, 0x0AU};
        png.insert(png.end(), signature.begin(), signature.end());

        std::array<std::uint8_t, 13> ihdr{};
        ihdr[0] = static_cast<std::uint8_t>((width_ >> 24U) & 0xFFU);
        ihdr[1] = static_cast<std::uint8_t>((width_ >> 16U) & 0xFFU);
        ihdr[2] = static_cast<std::uint8_t>((width_ >> 8U) & 0xFFU);
        ihdr[3] = static_cast<std::uint8_t>(width_ & 0xFFU);
        ihdr[4] = static_cast<std::uint8_t>((height_ >> 24U) & 0xFFU);
        ihdr[5] = static_cast<std::uint8_t>((height_ >> 16U) & 0xFFU);
        ihdr[6] = static_cast<std::uint8_t>((height_ >> 8U) & 0xFFU);
        ihdr[7] = static_cast<std::uint8_t>(height_ & 0xFFU);
        ihdr[8] = 0x08U;  // bit depth
        ihdr[9] = 0x02U;  // colour type 2 (truecolour RGB)
        ihdr[10] = 0x00U; // compression method (deflate)
        ihdr[11] = 0x00U; // filter method
        ihdr[12] = 0x00U; // interlace method (none)

        constexpr std::array<std::uint8_t, 4> kIHDR = {0x49U, 0x48U, 0x44U, 0x52U}; // "IHDR"
        constexpr std::array<std::uint8_t, 4> kIDAT = {0x49U, 0x44U, 0x41U, 0x54U}; // "IDAT"
        constexpr std::array<std::uint8_t, 4> kIEND = {0x49U, 0x45U, 0x4EU, 0x44U}; // "IEND"
        append_chunk(png, kIHDR, ihdr);
        append_chunk(png, kIDAT, idat);
        append_chunk(png, kIEND, {});
        return png;
    }

    std::vector<std::uint8_t> indexed_png_image::encode() const {
        // PNG requires every pixel index to be in PLTE range; size the palette to
        // cover the largest index used (and the transparent index), padding with
        // black, and cap at the 256-entry PLTE maximum.
        std::size_t needed = palette_.size();
        for (std::uint8_t v : indices_) {
            needed = std::max(needed, static_cast<std::size_t>(v) + 1U);
        }
        if (transparent_index_ >= 0) {
            needed = std::max(needed, static_cast<std::size_t>(transparent_index_) + 1U);
        }
        needed = std::min<std::size_t>(needed, 256U);

        std::vector<std::uint8_t> plte;
        plte.reserve(needed * 3U);
        for (std::size_t i = 0; i < needed; ++i) {
            const std::uint32_t c = i < palette_.size() ? palette_[i] : 0U;
            plte.push_back(static_cast<std::uint8_t>((c >> 16U) & 0xFFU));
            plte.push_back(static_cast<std::uint8_t>((c >> 8U) & 0xFFU));
            plte.push_back(static_cast<std::uint8_t>(c & 0xFFU));
        }

        // tRNS: alpha for palette entries up to and including the transparent one
        // (entries past the chunk default to opaque). Only the marked index is 0.
        std::vector<std::uint8_t> trns;
        if (transparent_index_ >= 0 && static_cast<std::size_t>(transparent_index_) < needed) {
            trns.assign(static_cast<std::size_t>(transparent_index_) + 1U, 0xFFU);
            trns[static_cast<std::size_t>(transparent_index_)] = 0x00U;
        }

        // Filtered raw scanlines: one filter-type byte (0 = None) then index bytes.
        std::vector<std::uint8_t> raw;
        raw.reserve((static_cast<std::size_t>(width_) + 1U) * height_);
        for (std::uint32_t y = 0; y < height_; ++y) {
            raw.push_back(0x00U); // None filter
            const std::size_t row = static_cast<std::size_t>(y) * width_;
            for (std::uint32_t x = 0; x < width_; ++x) {
                const std::size_t idx = row + x;
                raw.push_back(idx < indices_.size() ? indices_[idx] : 0U);
            }
        }

        const std::vector<std::uint8_t> idat = compression::deflate_zlib(raw);

        std::vector<std::uint8_t> png;
        png.reserve(8U + 25U + (12U + plte.size()) + (12U + trns.size()) + (12U + idat.size()) +
                    12U);

        constexpr std::array<std::uint8_t, 8> signature = {0x89U, 0x50U, 0x4EU, 0x47U,
                                                           0x0DU, 0x0AU, 0x1AU, 0x0AU};
        png.insert(png.end(), signature.begin(), signature.end());

        std::array<std::uint8_t, 13> ihdr{};
        ihdr[0] = static_cast<std::uint8_t>((width_ >> 24U) & 0xFFU);
        ihdr[1] = static_cast<std::uint8_t>((width_ >> 16U) & 0xFFU);
        ihdr[2] = static_cast<std::uint8_t>((width_ >> 8U) & 0xFFU);
        ihdr[3] = static_cast<std::uint8_t>(width_ & 0xFFU);
        ihdr[4] = static_cast<std::uint8_t>((height_ >> 24U) & 0xFFU);
        ihdr[5] = static_cast<std::uint8_t>((height_ >> 16U) & 0xFFU);
        ihdr[6] = static_cast<std::uint8_t>((height_ >> 8U) & 0xFFU);
        ihdr[7] = static_cast<std::uint8_t>(height_ & 0xFFU);
        ihdr[8] = 0x08U;  // bit depth
        ihdr[9] = 0x03U;  // colour type 3 (indexed)
        ihdr[10] = 0x00U; // compression method (deflate)
        ihdr[11] = 0x00U; // filter method
        ihdr[12] = 0x00U; // interlace method (none)

        constexpr std::array<std::uint8_t, 4> kIHDR = {0x49U, 0x48U, 0x44U, 0x52U};
        constexpr std::array<std::uint8_t, 4> kPLTE = {0x50U, 0x4CU, 0x54U, 0x45U};
        constexpr std::array<std::uint8_t, 4> kTRNS = {0x74U, 0x52U, 0x4EU, 0x53U};
        constexpr std::array<std::uint8_t, 4> kIDAT = {0x49U, 0x44U, 0x41U, 0x54U};
        constexpr std::array<std::uint8_t, 4> kIEND = {0x49U, 0x45U, 0x4EU, 0x44U};
        append_chunk(png, kIHDR, ihdr);
        append_chunk(png, kPLTE, plte);
        if (!trns.empty()) {
            append_chunk(png, kTRNS, trns);
        }
        append_chunk(png, kIDAT, idat);
        append_chunk(png, kIEND, {});
        return png;
    }

    bool indexed_png_image::write(const std::string& path) const {
        return mnemos::io::write_file(path, encode());
    }

} // namespace mnemos::graphics::images
