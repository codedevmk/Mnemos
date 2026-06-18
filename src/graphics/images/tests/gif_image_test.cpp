// GIF has no in-tree decoder, so the test checks the GIF89a structure and
// decodes the fixed-width LZW literal stream used by the encoder.

#include "gif_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

    [[nodiscard]] std::uint16_t read_le16(std::span<const std::uint8_t> b, std::size_t off) {
        return static_cast<std::uint16_t>(static_cast<unsigned>(b[off]) |
                                          (static_cast<unsigned>(b[off + 1U]) << 8U));
    }

    [[nodiscard]] std::optional<std::size_t> find_bytes(std::span<const std::uint8_t> bytes,
                                                        std::span<const std::uint8_t> pattern) {
        if (pattern.empty() || pattern.size() > bytes.size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < pattern.size(); ++j) {
                if (bytes[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t count_bytes(std::span<const std::uint8_t> bytes,
                                          std::span<const std::uint8_t> pattern) {
        std::size_t count = 0;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto found = find_bytes(bytes.subspan(offset), pattern);
            if (!found) {
                break;
            }
            ++count;
            offset += *found + pattern.size();
        }
        return count;
    }

    [[nodiscard]] bool contains_ascii(std::span<const std::uint8_t> bytes, std::string_view text) {
        std::vector<std::uint8_t> pattern;
        pattern.reserve(text.size());
        for (char c : text) {
            pattern.push_back(static_cast<std::uint8_t>(c));
        }
        return find_bytes(bytes, pattern).has_value();
    }

    [[nodiscard]] std::vector<std::uint8_t> read_all(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] std::vector<std::uint8_t> first_image_data(std::span<const std::uint8_t> gif) {
        const std::vector<std::uint8_t> descriptor = {0x2CU, 0x00U, 0x00U, 0x00U, 0x00U,
                                                      0x02U, 0x00U, 0x02U, 0x00U, 0x00U};
        const auto image = find_bytes(gif, descriptor);
        REQUIRE(image.has_value());
        std::size_t off = *image + descriptor.size();
        REQUIRE(off < gif.size());
        CHECK(gif[off] == 0x08U); // LZW minimum code size
        ++off;

        std::vector<std::uint8_t> data;
        while (off < gif.size()) {
            const std::uint8_t n = gif[off++];
            if (n == 0U) {
                break;
            }
            REQUIRE(off + n <= gif.size());
            data.insert(data.end(), gif.begin() + static_cast<std::ptrdiff_t>(off),
                        gif.begin() + static_cast<std::ptrdiff_t>(off + n));
            off += n;
        }
        return data;
    }

    [[nodiscard]] std::vector<std::uint16_t> read_fixed9_codes(std::span<const std::uint8_t> data) {
        std::vector<std::uint16_t> codes;
        std::uint32_t bit_buffer = 0;
        unsigned bit_count = 0;
        for (std::uint8_t byte : data) {
            bit_buffer |= static_cast<std::uint32_t>(byte) << bit_count;
            bit_count += 8U;
            while (bit_count >= 9U) {
                codes.push_back(static_cast<std::uint16_t>(bit_buffer & 0x1FFU));
                bit_buffer >>= 9U;
                bit_count -= 9U;
            }
        }
        return codes;
    }

} // namespace

TEST_CASE("gif_animation emits GIF89a frames with RGB332 palette", "[gif]") {
    using mnemos::graphics::images::gif_animation;
    using mnemos::graphics::images::gif_frame;

    std::vector<gif_frame> frames;
    frames.push_back({{0xFF0000U, 0x00FF00U, 0x0000FFU, 0xFFFFFFU}, 3U});
    frames.push_back({{0x000000U, 0x123456U, 0x654321U, 0xABCDEFU}, 4U});
    const gif_animation gif(2U, 2U, frames);
    const std::vector<std::uint8_t> bytes = gif.encode();

    REQUIRE(bytes.size() > 13U + 768U);
    CHECK(bytes[0] == static_cast<std::uint8_t>('G'));
    CHECK(bytes[1] == static_cast<std::uint8_t>('I'));
    CHECK(bytes[2] == static_cast<std::uint8_t>('F'));
    CHECK(bytes[3] == static_cast<std::uint8_t>('8'));
    CHECK(bytes[4] == static_cast<std::uint8_t>('9'));
    CHECK(bytes[5] == static_cast<std::uint8_t>('a'));
    CHECK(read_le16(bytes, 6U) == 2U);
    CHECK(read_le16(bytes, 8U) == 2U);
    CHECK(bytes[10] == 0xF7U); // global colour table, 256 entries
    CHECK(bytes.back() == 0x3BU);
    CHECK(contains_ascii(bytes, "NETSCAPE2.0"));

    const std::vector<std::uint8_t> gce = {0x21U, 0xF9U, 0x04U};
    CHECK(count_bytes(bytes, gce) == 2U);

    const std::vector<std::uint16_t> codes = read_fixed9_codes(first_image_data(bytes));
    REQUIRE(codes.size() >= 6U);
    CHECK(codes[0] == 256U); // Clear
    CHECK(codes[1] == 224U); // red -> RGB332
    CHECK(codes[2] == 28U);  // green -> RGB332
    CHECK(codes[3] == 3U);   // blue -> RGB332
    CHECK(codes[4] == 255U); // white -> RGB332
    CHECK(codes[5] == 257U); // End
}

TEST_CASE("gif_animation write round-trips the encoded bytes to disk", "[gif]") {
    using mnemos::graphics::images::gif_animation;
    using mnemos::graphics::images::gif_frame;

    const gif_animation gif(1U, 1U, std::vector<gif_frame>{{{0x112233U}, 2U}});
    const auto tmp = std::filesystem::temp_directory_path() / "mnemos_gif_image_test.gif";

    REQUIRE(gif.write(tmp.string()));
    CHECK(read_all(tmp) == gif.encode());

    std::filesystem::remove(tmp);
}

TEST_CASE("gif_animation rejects empty or mismatched frame data", "[gif]") {
    using mnemos::graphics::images::gif_animation;
    using mnemos::graphics::images::gif_frame;

    CHECK(gif_animation(2U, 2U, {}).encode().empty());
    CHECK(gif_animation(2U, 2U, std::vector<gif_frame>{{{0x000000U}, 2U}}).encode().empty());
}
