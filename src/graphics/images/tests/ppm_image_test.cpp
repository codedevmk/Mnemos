// Pins the binary PPM encoder: exact P6 header + R,G,B byte order from each
// packed 0x00RRGGBB pixel, and a write() round-trip through the filesystem.

#include "ppm_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using mnemos::graphics::images::ppm_image;

namespace {
    [[nodiscard]] std::vector<std::uint8_t> read_all(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("ppm_image: encode emits the P6 header and RGB bytes in order") {
    // 2x1: pixel 0 = 0x00112233 (R=0x11,G=0x22,B=0x33), pixel 1 = 0x00445566.
    const ppm_image img(2U, 1U, {0x00112233U, 0x00445566U});

    const std::string header = "P6\n2 1\n255\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());
    expected.insert(expected.end(), {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U});

    CHECK(img.encode() == expected);
}

TEST_CASE("ppm_image: write round-trips the encoded bytes to disk") {
    namespace fs = std::filesystem;
    const ppm_image img(2U, 1U, {0x00112233U, 0x00445566U});
    const auto tmp = fs::temp_directory_path() / "mnemos_ppm_image_test.ppm";

    REQUIRE(img.write(tmp.string()));
    CHECK(read_all(tmp) == img.encode());

    fs::remove(tmp);
}
