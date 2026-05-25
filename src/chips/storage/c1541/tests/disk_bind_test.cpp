#include <mnemos/chips/storage/c1541/disk_bind.hpp>

#include <mnemos/chips/storage/c1541/d64_image.hpp>
#include <mnemos/chips/storage/c1541/gcr.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::storage::c1541::bind_gcr;
    using mnemos::chips::storage::c1541::d64_image;
    using mnemos::chips::storage::c1541::gcr_decode_5to4;
    using mnemos::chips::storage::c1541::gcr_density_zone;

    std::vector<std::uint8_t> make_disk() {
        std::vector<std::uint8_t> img(d64_image::size_35_tracks, 0U);
        const auto bam = static_cast<std::size_t>(18U - 1U) * 21U * 256U; // track 18 sector 0
        img[bam + 0xA2U] = '4';
        img[bam + 0xA3U] = '2';
        // Track 1 sector 0 lives at offset 0; write a recognisable pattern.
        for (std::size_t i = 0; i < 256U; ++i) {
            img[i] = static_cast<std::uint8_t>(i);
        }
        return img;
    }

} // namespace

TEST_CASE("gcr_density_zone follows the 1541 zoning") {
    CHECK(gcr_density_zone(1U) == 3U);
    CHECK(gcr_density_zone(17U) == 3U);
    CHECK(gcr_density_zone(18U) == 2U);
    CHECK(gcr_density_zone(25U) == 1U);
    CHECK(gcr_density_zone(35U) == 0U);
}

TEST_CASE("bind_gcr lays out tracks and the data block round-trips through GCR") {
    d64_image img;
    REQUIRE(img.load(make_disk()));

    const auto tracks = bind_gcr(img);
    REQUIRE(tracks.size() == 35U);
    CHECK(tracks[0].density_zone == 3U);
    CHECK_FALSE(tracks[0].bytes.empty());

    // Sector 0 layout: 5 sync + 10 header-GCR + 9 gap + 5 sync + 325 data-GCR + ...
    const std::size_t data_gcr = 5U + 10U + 9U + 5U;
    const auto& stream = tracks[0].bytes;
    REQUIRE(stream.size() >= data_gcr + 325U);

    std::array<std::uint8_t, 260> block{};
    for (std::size_t g = 0; g < 65U; ++g) {
        std::array<std::uint8_t, 5> code{};
        for (std::size_t b = 0; b < 5U; ++b) {
            code[b] = stream[data_gcr + g * 5U + b];
        }
        std::array<std::uint8_t, 4> out{};
        REQUIRE(gcr_decode_5to4(code, out));
        for (std::size_t b = 0; b < 4U; ++b) {
            block[g * 4U + b] = out[b];
        }
    }

    CHECK(block[0] == 0x07U); // data block marker
    for (std::size_t i = 0; i < 256U; ++i) {
        CHECK(block[1U + i] == static_cast<std::uint8_t>(i)); // sector contents survive
    }
    std::uint8_t chk = 0U;
    for (std::size_t i = 0; i < 256U; ++i) {
        chk = static_cast<std::uint8_t>(chk ^ block[1U + i]);
    }
    CHECK(block[257] == chk); // data checksum
}
