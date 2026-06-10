// Structural check of the VGM container: magic, version, clock/rate fields, the
// data offset, the appended command body, and the EoF offset.

#include "vgm.hpp"

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
    std::string tag(const std::vector<std::uint8_t>& b, std::size_t off) {
        return std::string(b.begin() + static_cast<std::ptrdiff_t>(off),
                           b.begin() + static_cast<std::ptrdiff_t>(off + 4));
    }
} // namespace

TEST_CASE("encode_vgm writes a valid v1.50 header + body", "[vgm]") {
    // A tiny PSG body: write 0x9F, wait one NTSC frame, end.
    const std::array<std::uint8_t, 4> body{0x50U, 0x9FU, 0x62U, 0x66U};
    mnemos::audio::vgm_header h{};
    h.sn76489_clock = 3579545U;
    h.rate = 60U;
    h.total_samples = 735U;
    const auto vgm = mnemos::audio::encode_vgm(h, body);

    REQUIRE(vgm.size() == 0x40U + body.size());
    CHECK(tag(vgm, 0) == "Vgm ");
    CHECK(le32(vgm, 0x08) == 0x150U);          // version 1.50
    CHECK(le32(vgm, 0x0C) == 3579545U);        // SN76489 clock
    CHECK(le32(vgm, 0x18) == 735U);            // total samples
    CHECK(le32(vgm, 0x24) == 60U);             // rate
    CHECK(vgm[0x28] == 0x09U);                 // SN76489 feedback low byte (default)
    CHECK(vgm[0x2A] == 16U);                   // shift register width
    CHECK(le32(vgm, 0x34) == 0x0CU);           // data offset (0x40 - 0x34)
    CHECK(le32(vgm, 0x04) == vgm.size() - 4U); // EoF offset

    // Body follows the 64-byte header verbatim.
    CHECK(vgm[0x40] == 0x50U);
    CHECK(vgm[0x41] == 0x9FU);
    CHECK(vgm[0x42] == 0x62U);
    CHECK(vgm[0x43] == 0x66U);
}

TEST_CASE("encode_vgm carries the YM2612 clock", "[vgm]") {
    mnemos::audio::vgm_header h{};
    h.ym2612_clock = 7670453U;
    const std::array<std::uint8_t, 1> body{0x66U};
    const auto vgm = mnemos::audio::encode_vgm(h, body);
    CHECK(le32(vgm, 0x2C) == 7670453U);
    CHECK(le32(vgm, 0x0C) == 0U); // no PSG
}
