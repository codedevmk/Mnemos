// make_prg_disk authors a .d64 around a single PRG. These tests prove the
// image round-trips through the same d64_image reader the synthetic 1541
// serves from: the directory resolves to one closed PRG and its block chain
// reproduces the original bytes exactly, across sizes that span one block,
// many blocks, and an exact 254-byte multiple.

#include "prg_disk.hpp"

#include "d64_image.hpp"
#include "synthetic_drive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <numeric>
#include <vector>

namespace {

    using mnemos::chips::storage::c1541::d64_image;
    using mnemos::chips::storage::c1541::make_prg_disk;
    using mnemos::chips::storage::c1541::synthetic_drive;

    // A PRG of `payload` data bytes after the 2-byte $0801 load address, filled
    // with a walking pattern so any mis-ordered block shows up.
    std::vector<std::uint8_t> sample_prg(std::size_t payload) {
        std::vector<std::uint8_t> prg;
        prg.reserve(payload + 2U);
        prg.push_back(0x01U);
        prg.push_back(0x08U);
        for (std::size_t i = 0; i < payload; ++i) {
            prg.push_back(static_cast<std::uint8_t>((i * 7U + 3U) & 0xFFU));
        }
        return prg;
    }

    void check_roundtrip(std::size_t payload) {
        const auto prg = sample_prg(payload);
        const auto disk = make_prg_disk(prg);
        REQUIRE(disk.size() == d64_image::size_35_tracks);

        d64_image img;
        REQUIRE(img.load(disk));
        const auto entry = img.find_first_prg();
        REQUIRE(entry.has_value());
        CHECK(entry->is_prg_closed());

        const auto got = img.read_chain(entry->first_track, entry->first_sector);
        CHECK(got == prg);
    }

} // namespace

TEST_CASE("make_prg_disk round-trips a single-block program") { check_roundtrip(10U); }

TEST_CASE("make_prg_disk round-trips a multi-block program") { check_roundtrip(1000U); }

TEST_CASE("make_prg_disk round-trips an exact 254-byte multiple") {
    // 254 payload + 2-byte load addr = 256: the data spans whole blocks with no
    // partial tail, exercising the (next_track == 0, used + 1) terminator math.
    check_roundtrip(254U * 3U - 2U);
}

TEST_CASE("make_prg_disk names the directory entry") {
    const auto prg = sample_prg(20U);
    const std::vector<std::uint8_t> name = {'G', 'A', 'M', 'E'};
    const auto disk = make_prg_disk(prg, name);
    d64_image img;
    REQUIRE(img.load(disk));
    const auto entry = img.find_first_prg();
    REQUIRE(entry.has_value());
    CHECK(entry->name[0] == 'G');
    CHECK(entry->name[3] == 'E');
    CHECK(entry->name[4] == 0xA0U); // padded
}

TEST_CASE("make_prg_disk rejects an empty program") { CHECK(make_prg_disk({}).empty()); }

TEST_CASE("make_prg_disk image mounts and serves through the synthetic drive") {
    const auto prg = sample_prg(600U);
    const auto disk = make_prg_disk(prg);

    synthetic_drive drv(8U);
    REQUIRE(drv.mount(disk));

    // Drive the protocol surface: LISTEN 8 / OPEN 0 / wildcard name / UNLISTEN
    // commits the load, then TALK 8 / channel 0 streams the file back.
    drv.debug_command(0x28U); // LISTEN device 8
    drv.debug_command(0xF0U); // OPEN channel 0
    drv.debug_filename_byte('*');
    drv.debug_unlisten();
    drv.debug_command(0x48U); // TALK device 8
    drv.debug_command(0x60U); // talk channel 0

    std::vector<std::uint8_t> served;
    while (auto byte = drv.debug_pull_byte()) {
        served.push_back(*byte);
    }
    CHECK(served == prg);
}
