#include "d64_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::storage::c1541::d64_image;

    std::int32_t offset(std::uint8_t track, std::uint8_t sector) {
        std::int32_t blocks = 0;
        for (std::uint8_t t = 1U; t < track; ++t) {
            blocks += d64_image::sectors_per_track(t);
        }
        return (blocks + sector) * 256;
    }

    // A minimal 35-track image: BAM + one directory slot for a 4-byte PRG "TEST".
    std::vector<std::uint8_t> make_image() {
        std::vector<std::uint8_t> img(d64_image::size_35_tracks, 0U);

        const std::int32_t bam = offset(18U, 0U);
        img[static_cast<std::size_t>(bam) + 0U] = 18U; // link to first dir sector
        img[static_cast<std::size_t>(bam) + 1U] = 1U;
        const char* name = "TEST";
        for (std::size_t i = 0; i < 16U; ++i) {
            img[static_cast<std::size_t>(bam) + 0x90U + i] =
                i < 4U ? static_cast<std::uint8_t>(name[i]) : 0xA0U;
        }
        img[static_cast<std::size_t>(bam) + 0xA2U] = '4';
        img[static_cast<std::size_t>(bam) + 0xA3U] = '2';
        // BAM free counts: give track 1 one free block so BLOCKS FREE > 0.
        img[static_cast<std::size_t>(bam) + 0x04U] = 1U;

        const std::int32_t dir = offset(18U, 1U);
        img[static_cast<std::size_t>(dir) + 0U] = 0U; // no next dir sector
        img[static_cast<std::size_t>(dir) + 1U] = 0xFFU;
        const std::size_t slot = static_cast<std::size_t>(dir);
        img[slot + 0x02U] = 0x82U; // closed PRG
        img[slot + 0x03U] = 1U;    // first track
        img[slot + 0x04U] = 0U;    // first sector
        for (std::size_t i = 0; i < 16U; ++i) {
            img[slot + 0x05U + i] = i < 4U ? static_cast<std::uint8_t>(name[i]) : 0xA0U;
        }
        img[slot + 0x1EU] = 1U; // blocks

        const std::int32_t file = offset(1U, 0U);
        img[static_cast<std::size_t>(file) + 0U] = 0U;    // last sector
        img[static_cast<std::size_t>(file) + 1U] = 5U;    // 4 used bytes (5 - 1)
        img[static_cast<std::size_t>(file) + 2U] = 0x01U; // load addr lo ($0801)
        img[static_cast<std::size_t>(file) + 3U] = 0x08U; // load addr hi
        img[static_cast<std::size_t>(file) + 4U] = 0xAAU; // data
        img[static_cast<std::size_t>(file) + 5U] = 0xBBU;
        return img;
    }

} // namespace

TEST_CASE("d64_image validates geometry on load") {
    d64_image img;
    CHECK_FALSE(img.load(std::vector<std::uint8_t>(1000U, 0U)));
    CHECK(img.load(make_image()));
    CHECK(img.loaded());
    CHECK(img.track_count() == 35U);
}

TEST_CASE("d64_image sectors-per-track zoning and offsets") {
    CHECK(d64_image::sectors_per_track(1U) == 21U);
    CHECK(d64_image::sectors_per_track(18U) == 19U);
    CHECK(d64_image::sectors_per_track(25U) == 18U);
    CHECK(d64_image::sectors_per_track(35U) == 17U);

    d64_image img;
    REQUIRE(img.load(make_image()));
    CHECK(img.sector_offset(1U, 0U) == 0);
    CHECK(img.sector_offset(18U, 0U) == offset(18U, 0U));
    CHECK(img.sector_offset(1U, 21U) == -1); // out of range
}

TEST_CASE("d64_image reads the directory and finds files") {
    d64_image img;
    REQUIRE(img.load(make_image()));

    const auto dir = img.directory();
    REQUIRE(dir.size() == 1U);
    CHECK(dir[0].is_prg_closed());
    CHECK(dir[0].blocks == 1U);
    CHECK(dir[0].name[0] == 'T');

    REQUIRE(img.find_first_prg().has_value());
    const std::array<std::uint8_t, 2> pattern = {'T', 0x2AU}; // "T*"
    REQUIRE(img.find_by_name(pattern).has_value());
    const std::array<std::uint8_t, 4> nomatch = {'Z', 'Z', 'Z', 'Z'};
    CHECK_FALSE(img.find_by_name(nomatch).has_value());
}

TEST_CASE("d64_image reads a PRG block chain including the load address") {
    d64_image img;
    REQUIRE(img.load(make_image()));
    const auto prg = img.read_chain(1U, 0U);
    REQUIRE(prg.size() == 4U);
    CHECK(prg[0] == 0x01U); // load addr $0801
    CHECK(prg[1] == 0x08U);
    CHECK(prg[2] == 0xAAU);
    CHECK(prg[3] == 0xBBU);
}

TEST_CASE("d64_image renders a BASIC directory listing") {
    d64_image img;
    REQUIRE(img.load(make_image()));
    const auto listing = img.render_directory_listing();
    REQUIRE(listing.size() > 4U);
    CHECK(listing[0] == 0x01U); // load address $0801
    CHECK(listing[1] == 0x08U);
    CHECK(listing.back() == 0x00U); // terminated
}
