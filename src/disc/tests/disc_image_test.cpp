// disc_image: in-memory BIN/ISO sector reads, format validation, a CUE
// integration test (real open() over temp files), and MSF/LBA helpers.

#include "circ_ecc.hpp"
#include "disc_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

    using mnemos::disc::disc_format;
    using mnemos::disc::disc_image;
    using mnemos::disc::lba_from_msf;
    using mnemos::disc::msf_from_lba;
    using mnemos::disc::sector_mode;

    std::uint8_t bcd(std::uint8_t v) {
        return static_cast<std::uint8_t>(((v / 10U) << 4U) | (v % 10U));
    }

    // A valid raw Mode-1 sector with user data[k] = seed + 16 + k and real ECC.
    std::array<std::uint8_t, 2352> make_bin_sector(std::uint32_t lba, std::uint8_t seed) {
        std::array<std::uint8_t, 2352> s{};
        s[0] = 0x00;
        for (std::size_t i = 1; i <= 10; ++i) {
            s[i] = 0xFF;
        }
        s[11] = 0x00;
        std::uint8_t m = 0;
        std::uint8_t sec = 0;
        std::uint8_t f = 0;
        msf_from_lba(lba + 150U, m, sec, f);
        s[12] = bcd(m);
        s[13] = bcd(sec);
        s[14] = bcd(f);
        s[15] = 0x01;
        for (std::size_t i = 16; i < 2064; ++i) {
            s[i] = static_cast<std::uint8_t>(seed + i);
        }
        mnemos::disc::circ_ecc_regen_sector(std::span<std::uint8_t, 2352>{s});
        return s;
    }

    std::array<std::uint8_t, 2048> expected_user(std::uint8_t seed) {
        std::array<std::uint8_t, 2048> u{};
        for (std::size_t k = 0; k < 2048; ++k) {
            u[k] = static_cast<std::uint8_t>(seed + 16U + k);
        }
        return u;
    }

    std::vector<std::uint8_t> make_bin_image(int sectors) {
        std::vector<std::uint8_t> data;
        for (int lba = 0; lba < sectors; ++lba) {
            const auto s = make_bin_sector(static_cast<std::uint32_t>(lba),
                                           static_cast<std::uint8_t>(lba * 7 + 1));
            data.insert(data.end(), s.begin(), s.end());
        }
        return data;
    }

    // RAII temp directory for the CUE integration test.
    struct temp_dir {
        std::filesystem::path path;
        temp_dir() {
            path = std::filesystem::temp_directory_path() / "mnemos_disc_image_test";
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path, ec);
        }
        ~temp_dir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    void write_file(const std::filesystem::path& p, std::span<const std::uint8_t> bytes) {
        std::ofstream os(p, std::ios::binary);
        os.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    }

} // namespace

TEST_CASE("disc_image opens a raw BIN image and reads user-data + raw sectors", "[disc][image]") {
    const auto data = make_bin_image(3);
    auto img = disc_image::open_bin(data);
    REQUIRE(img.has_value());
    REQUIRE(img->format() == disc_format::bin_2352);
    REQUIRE(img->total_sectors() == 3);
    REQUIRE(img->track_count() == 1);

    for (std::uint32_t lba = 0; lba < 3; ++lba) {
        std::array<std::uint8_t, 2048> user{};
        REQUIRE(img->read_sector(lba, std::span<std::uint8_t, 2048>{user}));
        REQUIRE(user == expected_user(static_cast<std::uint8_t>(lba * 7 + 1)));

        std::array<std::uint8_t, 2352> raw{};
        REQUIRE(img->read_raw_sector(lba, std::span<std::uint8_t, 2352>{raw}));
        const auto golden = make_bin_sector(lba, static_cast<std::uint8_t>(lba * 7 + 1));
        REQUIRE(raw == golden); // verbatim, ECC included

        REQUIRE(img->mode_at(lba) == sector_mode::mode1);
    }
    REQUIRE(img->find_track(0) != nullptr);
    REQUIRE(img->find_track(3) == nullptr); // out of range
    std::array<std::uint8_t, 2048> oob{};
    REQUIRE_FALSE(img->read_sector(3, std::span<std::uint8_t, 2048>{oob}));
}

TEST_CASE("disc_image opens an ISO image and synthesises raw headers", "[disc][image]") {
    std::vector<std::uint8_t> iso(3U * 2048U);
    for (std::size_t i = 0; i < iso.size(); ++i) {
        iso[i] = static_cast<std::uint8_t>(i * 5 + 3);
    }
    auto img = disc_image::open_iso(iso);
    REQUIRE(img.has_value());
    REQUIRE(img->format() == disc_format::iso_2048);
    REQUIRE(img->total_sectors() == 3);

    std::array<std::uint8_t, 2048> user{};
    REQUIRE(img->read_sector(1, std::span<std::uint8_t, 2048>{user}));
    for (std::size_t k = 0; k < 2048; ++k) {
        REQUIRE(user[k] == static_cast<std::uint8_t>((2048 + k) * 5 + 3));
    }

    std::array<std::uint8_t, 2352> raw{};
    REQUIRE(img->read_raw_sector(1, std::span<std::uint8_t, 2352>{raw}));
    REQUIRE(raw[0] == 0x00);
    REQUIRE(raw[1] == 0xFF);
    REQUIRE(raw[15] == 0x01); // Mode 1
    std::uint8_t m = 0;
    std::uint8_t s = 0;
    std::uint8_t f = 0;
    msf_from_lba(1U + 150U, m, s, f);
    REQUIRE(raw[12] == bcd(m));
    REQUIRE(raw[13] == bcd(s));
    REQUIRE(raw[14] == bcd(f));
    REQUIRE(raw[16] == user[0]); // user data follows the header
    REQUIRE(raw[2064] == 0x00);  // EDC/ECC left zeroed for ISO
    REQUIRE(img->mode_at(2) == sector_mode::mode1);
}

TEST_CASE("disc_image rejects malformed images", "[disc][image]") {
    REQUIRE_FALSE(disc_image::open_bin(std::vector<std::uint8_t>(100)).has_value());  // bad size
    REQUIRE_FALSE(disc_image::open_bin(std::vector<std::uint8_t>(2352)).has_value()); // no sync
    REQUIRE_FALSE(disc_image::open_iso(std::vector<std::uint8_t>(1000)).has_value()); // bad size
    REQUIRE_FALSE(disc_image::open_iso({}).has_value());
}

TEST_CASE("disc_image opens a CUE sheet referencing a BIN track", "[disc][image]") {
    const auto bin = make_bin_image(4);
    temp_dir td;
    if (td.path.empty()) {
        SUCCEED("temp dir unavailable");
        return;
    }
    write_file(td.path / "game.bin", bin);
    const std::string cue =
        "FILE \"game.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n";
    write_file(td.path / "game.cue",
               std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(cue.data()),
                                             cue.size()});

    auto img = disc_image::open((td.path / "game.cue").string());
    REQUIRE(img.has_value());
    REQUIRE(img->format() == disc_format::cue);
    REQUIRE(img->total_sectors() == 4);
    REQUIRE(img->track_count() == 1);

    for (std::uint32_t lba = 0; lba < 4; ++lba) {
        std::array<std::uint8_t, 2048> user{};
        REQUIRE(img->read_sector(lba, std::span<std::uint8_t, 2048>{user}));
        REQUIRE(user == expected_user(static_cast<std::uint8_t>(lba * 7 + 1)));
    }
}

TEST_CASE("disc_image MSF/LBA helpers round-trip", "[disc][image]") {
    REQUIRE(lba_from_msf(0, 0, 0) == 0U);
    REQUIRE(lba_from_msf(0, 1, 0) == 75U);
    REQUIRE(lba_from_msf(1, 0, 0) == 4500U);
    for (std::uint32_t lba : {0U, 1U, 74U, 75U, 4499U, 4500U, 123456U}) {
        std::uint8_t m = 0;
        std::uint8_t s = 0;
        std::uint8_t f = 0;
        msf_from_lba(lba, m, s, f);
        REQUIRE(lba_from_msf(m, s, f) == lba);
    }
}
