// Structural check of the RIFF/WAVE container: header tags, fmt fields, the data
// chunk size, and the little-endian sample bytes.

#include "wav.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {
    std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t off) {
        return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8U) |
               (static_cast<std::uint32_t>(b[off + 2]) << 16U) |
               (static_cast<std::uint32_t>(b[off + 3]) << 24U);
    }
    std::uint16_t le16(const std::vector<std::uint8_t>& b, std::size_t off) {
        return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8U));
    }
    std::string tag(const std::vector<std::uint8_t>& b, std::size_t off) {
        return std::string(b.begin() + static_cast<std::ptrdiff_t>(off),
                           b.begin() + static_cast<std::ptrdiff_t>(off + 4));
    }
} // namespace

TEST_CASE("encode_wav writes a valid mono 16-bit PCM header + samples", "[wav]") {
    const std::array<std::int16_t, 3> frames{0, 1000, -1000};
    const auto wav = mnemos::audio::encode_wav(frames, 8000U, 1);

    REQUIRE(wav.size() == 44U + frames.size() * 2U);
    CHECK(tag(wav, 0) == "RIFF");
    CHECK(le32(wav, 4) == 36U + frames.size() * 2U);
    CHECK(tag(wav, 8) == "WAVE");
    CHECK(tag(wav, 12) == "fmt ");
    CHECK(le32(wav, 16) == 16U); // fmt size
    CHECK(le16(wav, 20) == 1U);  // PCM
    CHECK(le16(wav, 22) == 1U);  // channels
    CHECK(le32(wav, 24) == 8000U);
    CHECK(le32(wav, 28) == 8000U * 2U); // byte rate = rate * channels * 2
    CHECK(le16(wav, 32) == 2U);         // block align
    CHECK(le16(wav, 34) == 16U);        // bits per sample
    CHECK(tag(wav, 36) == "data");
    CHECK(le32(wav, 40) == frames.size() * 2U);
    // First sample little-endian; 1000 = 0x03E8.
    CHECK(wav[44] == 0x00U);
    CHECK(wav[46] == 0xE8U);
    CHECK(wav[47] == 0x03U);
}

TEST_CASE("encode_wav stereo sets channels and block align", "[wav]") {
    const std::array<std::int16_t, 4> frames{1, 2, 3, 4}; // 2 L/R frames
    const auto wav = mnemos::audio::encode_wav(frames, 32000U, 2);
    CHECK(le16(wav, 22) == 2U);          // channels
    CHECK(le16(wav, 32) == 4U);          // block align = channels * 2
    CHECK(le32(wav, 28) == 32000U * 4U); // byte rate
    CHECK(le32(wav, 40) == 8U);          // data bytes
}
