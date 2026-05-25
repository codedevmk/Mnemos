#include <mnemos/chips/storage/c1541/synthetic_drive.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::storage::c1541::d64_image;
    using mnemos::chips::storage::c1541::synthetic_drive;

    std::int32_t offset(std::uint8_t track, std::uint8_t sector) {
        std::int32_t blocks = 0;
        for (std::uint8_t t = 1U; t < track; ++t) {
            blocks += d64_image::sectors_per_track(t);
        }
        return (blocks + sector) * 256;
    }

    // A 35-track image holding one 4-byte PRG "TEST" -> {01,08,AA,BB}.
    std::vector<std::uint8_t> build_disk() {
        std::vector<std::uint8_t> img(d64_image::size_35_tracks, 0U);
        const auto bam = static_cast<std::size_t>(offset(18U, 0U));
        img[bam + 0U] = 18U;
        img[bam + 1U] = 1U;
        const char* name = "TEST";
        for (std::size_t i = 0; i < 16U; ++i) {
            img[bam + 0x90U + i] = i < 4U ? static_cast<std::uint8_t>(name[i]) : 0xA0U;
        }
        const auto dir = static_cast<std::size_t>(offset(18U, 1U));
        img[dir + 0U] = 0U;
        img[dir + 1U] = 0xFFU;
        img[dir + 0x02U] = 0x82U; // closed PRG
        img[dir + 0x03U] = 1U;
        img[dir + 0x04U] = 0U;
        for (std::size_t i = 0; i < 16U; ++i) {
            img[dir + 0x05U + i] = i < 4U ? static_cast<std::uint8_t>(name[i]) : 0xA0U;
        }
        img[dir + 0x1EU] = 1U;
        const auto file = static_cast<std::size_t>(offset(1U, 0U));
        img[file + 0U] = 0U;
        img[file + 1U] = 5U;
        img[file + 2U] = 0x01U;
        img[file + 3U] = 0x08U;
        img[file + 4U] = 0xAAU;
        img[file + 5U] = 0xBBU;
        return img;
    }

    std::vector<std::uint8_t> drain(synthetic_drive& drv) {
        std::vector<std::uint8_t> out;
        while (const auto b = drv.debug_pull_byte()) {
            out.push_back(*b);
        }
        return out;
    }

} // namespace

TEST_CASE("synthetic_drive registers under commodore.c1541") {
    REQUIRE(mnemos::chips::find_factory("commodore.c1541") != nullptr);
    REQUIRE(mnemos::chips::create_chip("commodore.c1541") != nullptr);
}

TEST_CASE("synthetic_drive serves a named PRG via the IEC command FSM") {
    synthetic_drive drv(8U);
    REQUIRE(drv.mount(build_disk()));

    drv.debug_command(0x28U); // LISTEN device 8
    drv.debug_command(0xF0U); // OPEN channel 0
    for (const char c : std::string("TEST")) {
        drv.debug_filename_byte(static_cast<std::uint8_t>(c));
    }
    drv.debug_unlisten();     // commit the OPEN -> load channel 0
    drv.debug_command(0x48U); // TALK device 8
    drv.debug_command(0x60U); // talk channel 0

    const std::vector<std::uint8_t> prg = drain(drv);
    REQUIRE(prg.size() == 4U);
    CHECK(prg[0] == 0x01U);
    CHECK(prg[1] == 0x08U);
    CHECK(prg[2] == 0xAAU);
    CHECK(prg[3] == 0xBBU);
}

TEST_CASE("synthetic_drive serves the first PRG for an empty/wildcard name") {
    synthetic_drive drv(8U);
    REQUIRE(drv.mount(build_disk()));
    drv.debug_command(0x28U);
    drv.debug_command(0xF0U);
    // no filename -> LOAD"*" behaviour
    drv.debug_unlisten();
    drv.debug_command(0x48U);
    drv.debug_command(0x60U);
    CHECK(drain(drv).size() == 4U);
}

TEST_CASE("synthetic_drive serves the directory for a $ name") {
    synthetic_drive drv(8U);
    REQUIRE(drv.mount(build_disk()));
    drv.debug_command(0x28U);
    drv.debug_command(0xF0U);
    drv.debug_filename_byte(static_cast<std::uint8_t>('$'));
    drv.debug_unlisten();
    drv.debug_command(0x48U);
    drv.debug_command(0x60U);
    const std::vector<std::uint8_t> listing = drain(drv);
    REQUIRE(listing.size() > 2U);
    CHECK(listing[0] == 0x01U); // BASIC load address $0801
    CHECK(listing[1] == 0x08U);
}

TEST_CASE("synthetic_drive ignores commands not addressed to its device") {
    synthetic_drive drv(8U);
    REQUIRE(drv.mount(build_disk()));
    drv.debug_command(0x29U); // LISTEN device 9 (not us)
    drv.debug_command(0xF0U); // OPEN ignored (we are not listen-addressed)
    drv.debug_filename_byte(static_cast<std::uint8_t>('T'));
    drv.debug_unlisten();
    drv.debug_command(0x48U); // TALK device 8 (us)
    drv.debug_command(0x60U);
    CHECK(drain(drv).empty()); // nothing was opened
}

TEST_CASE("synthetic_drive save/load round-trips a loaded channel") {
    synthetic_drive a(8U);
    REQUIRE(a.mount(build_disk()));
    a.debug_command(0x28U);
    a.debug_command(0xF0U);
    for (const char c : std::string("TEST")) {
        a.debug_filename_byte(static_cast<std::uint8_t>(c));
    }
    a.debug_unlisten();

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    synthetic_drive b(8U);
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
