#include "wd1793.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {
    using mnemos::chips::storage::wd1793;

    std::vector<std::uint8_t> make_dsk() {
        std::vector<std::uint8_t> disk(
            static_cast<std::size_t>(80U) * 2U * 9U * wd1793::sector_size, 0xE5U);
        disk[0] = 0x11U;
        disk[1] = 0x22U;
        const std::size_t sector_two = wd1793::sector_size;
        disk[sector_two] = 0x33U;
        disk[sector_two + 1U] = 0x44U;
        const std::size_t side_one_sector_one = 9U * wd1793::sector_size;
        disk[side_one_sector_one] = 0x55U;
        return disk;
    }

    void append_repeat(std::vector<std::uint8_t>& out, std::uint8_t value, std::size_t count) {
        out.insert(out.end(), count, value);
    }

    std::vector<std::uint8_t> make_mfm_format_track(std::uint8_t track, std::uint8_t side,
                                                    std::uint8_t sectors) {
        std::vector<std::uint8_t> out;
        out.reserve(8192U);
        append_repeat(out, 0x4EU, 80U);
        append_repeat(out, 0x00U, 12U);
        append_repeat(out, 0xF6U, 3U);
        out.push_back(0xFCU);
        append_repeat(out, 0x4EU, 50U);

        for (std::uint8_t sector = 1U; sector <= sectors; ++sector) {
            append_repeat(out, 0x00U, 12U);
            append_repeat(out, 0xF5U, 3U);
            out.push_back(0xFEU);
            out.push_back(track);
            out.push_back(side);
            out.push_back(sector);
            out.push_back(0x02U); // 512-byte sector
            out.push_back(0xF7U);
            append_repeat(out, 0x4EU, 22U);
            append_repeat(out, 0x00U, 12U);
            append_repeat(out, 0xF5U, 3U);
            out.push_back(0xFBU);
            for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
                out.push_back(static_cast<std::uint8_t>((0x80U | sector) + (i & 0x0FU)));
            }
            out.push_back(0xF7U);
            append_repeat(out, 0x4EU, 54U);
        }
        append_repeat(out, 0x4EU, 160U);
        return out;
    }

    [[nodiscard]] std::size_t find_mfm_id_mark(std::span<const std::uint8_t> stream,
                                               std::uint8_t track, std::uint8_t side,
                                               std::uint8_t sector) noexcept {
        const std::array<std::uint8_t, 5> pattern{0xFEU, track, side, sector, 0x02U};
        const auto found =
            std::search(stream.begin(), stream.end(), pattern.begin(), pattern.end());
        return found == stream.end() ? stream.size()
                                     : static_cast<std::size_t>(found - stream.begin());
    }

    [[nodiscard]] std::size_t find_data_mark_after(std::span<const std::uint8_t> stream,
                                                   std::size_t offset) noexcept {
        for (std::size_t i = offset; i < stream.size(); ++i) {
            if (stream[i] == 0xFBU || stream[i] == 0xF8U) {
                return i;
            }
        }
        return stream.size();
    }
} // namespace

TEST_CASE("wd1793 registers in the chip factory", "[chips][storage][wd1793]") {
    const mnemos::chips::chip_factory_descriptor* descriptor =
        mnemos::chips::find_factory("western_digital.wd1793");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::storage);

    std::unique_ptr<mnemos::chips::ichip> chip =
        mnemos::chips::create_chip("western_digital.wd1793");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string_view{"WD1793"});
}

TEST_CASE("wd1793 detects standard MSX DSK geometry", "[chips][storage][wd1793]") {
    wd1793 fdc;
    const auto disk = make_dsk();
    REQUIRE(fdc.mount(disk));

    CHECK(fdc.disk_loaded());
    CHECK(fdc.geometry().tracks == 80U);
    CHECK(fdc.geometry().sides == 2U);
    CHECK(fdc.geometry().sectors_per_track == 9U);
    CHECK_FALSE(fdc.mount(std::span<const std::uint8_t>{disk.data(), disk.size() - 1U}));
}

TEST_CASE("wd1793 reads a 512-byte sector through DRQ", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 0U); // track
    fdc.write_register(2, 1U); // sector
    fdc.write_register(0, 0x80U);

    CHECK((fdc.status() & 0x03U) == 0x03U);
    CHECK((fdc.read_io_status() & 0x40U) != 0U);
    CHECK(fdc.read_register(3) == 0x11U);
    CHECK(fdc.read_register(3) == 0x22U);
    for (std::uint16_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3);
    }

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x03U) == 0x00U);
}

TEST_CASE("wd1793 reads multiple sectors and advances the sector register",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 0U); // track
    fdc.write_register(2, 1U); // first sector
    fdc.write_register(0, 0x90U);

    CHECK((fdc.status() & 0x03U) == 0x03U);
    CHECK(fdc.read_register(3) == 0x11U);
    CHECK(fdc.read_register(3) == 0x22U);
    for (std::uint16_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3);
    }

    CHECK(fdc.drq());
    CHECK(fdc.sector() == 2U);
    CHECK(fdc.read_register(3) == 0x33U);
    CHECK(fdc.read_register(3) == 0x44U);
    for (std::uint16_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3);
    }

    for (std::uint8_t sector = 3U; sector <= 9U; ++sector) {
        CHECK(fdc.sector() == sector);
        for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
            (void)fdc.read_register(3);
        }
    }

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.sector() == 10U);
    CHECK((fdc.status() & 0x10U) != 0U);
}

TEST_CASE("wd1793 honors Type II side compare flags", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.set_side(0U);
    fdc.write_register(1, 0U);
    fdc.write_register(2, 1U);
    fdc.write_register(0, 0x8AU); // read sector, C=1, S=1

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x10U) != 0U);

    fdc.set_side(1U);
    fdc.write_register(2, 1U);
    fdc.write_register(0, 0x8AU); // selected side now matches S=1

    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3) == 0x55U);

    fdc.write_register(0, 0xD0U);
    fdc.write_register(2, 1U);
    fdc.write_register(0, 0xA2U); // write sector, C=1, S=0 mismatch on side 1

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x10U) != 0U);
    CHECK(fdc.disk_image()[9U * wd1793::sector_size] == 0x55U);
}

TEST_CASE("wd1793 force interrupt terminates a multiple sector read cleanly",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(2, 1U);
    fdc.write_register(0, 0x90U);
    for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3);
    }
    REQUIRE(fdc.sector() == 2U);

    fdc.write_register(0, 0xD0U);

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.sector() == 2U);
    CHECK((fdc.status() & 0x11U) == 0x00U);
}

TEST_CASE("wd1793 writes a sector back into the mounted DSK", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 0U);
    fdc.write_register(2, 2U);
    fdc.write_register(0, 0xA0U);
    REQUIRE(fdc.drq());
    for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3, static_cast<std::uint8_t>(i));
    }

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.disk_image()[wd1793::sector_size] == 0x00U);
    CHECK(fdc.disk_image()[wd1793::sector_size + 1U] == 0x01U);
    CHECK(fdc.disk_image()[wd1793::sector_size + 255U] == 0xFFU);
}

TEST_CASE("wd1793 writes completed multiple sectors before force interrupt",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 0U);
    fdc.write_register(2, 1U);
    fdc.write_register(0, 0xB0U);
    REQUIRE(fdc.drq());
    for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3, i == 0U ? 0xAAU : 0x00U);
    }
    CHECK(fdc.disk_image()[0] == 0xAAU);
    CHECK(fdc.sector() == 2U);

    for (std::uint16_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3, i == 0U ? 0xBBU : 0x00U);
    }
    CHECK(fdc.disk_image()[wd1793::sector_size] == 0xBBU);
    CHECK(fdc.sector() == 3U);

    fdc.write_register(0, 0xD0U);
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x11U) == 0x00U);
}

TEST_CASE("wd1793 write-protected media rejects write commands", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk(), true));
    REQUIRE(fdc.write_protected());

    fdc.write_register(1, 0U);
    fdc.write_register(2, 2U);
    fdc.write_register(0, 0xA0U);

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x40U) != 0U);
    CHECK(fdc.disk_image()[wd1793::sector_size] == 0x33U);

    fdc.write_register(0, 0xF0U);
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x40U) != 0U);
}

TEST_CASE("wd1793 formats a standard MFM track stream into the mounted DSK",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 2U);
    fdc.set_side(1U);
    const std::vector<std::uint8_t> stream = make_mfm_format_track(2U, 1U, 9U);

    fdc.write_register(0, 0xF0U);
    REQUIRE(fdc.drq());
    for (const std::uint8_t value : stream) {
        fdc.write_register(3, value);
        if (!fdc.drq()) {
            break;
        }
    }

    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x45U) == 0x00U);

    const std::size_t track_side_offset =
        ((static_cast<std::size_t>(2U) * 2U + 1U) * 9U) * wd1793::sector_size;
    CHECK(fdc.disk_image()[track_side_offset] == 0x81U);
    CHECK(fdc.disk_image()[track_side_offset + 8U * wd1793::sector_size] == 0x89U);
}

TEST_CASE("wd1793 reads a synthetic MFM track stream from the mounted DSK",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(1, 0U);
    fdc.set_side(1U);
    fdc.write_register(0, 0xE0U);
    REQUIRE(fdc.drq());

    std::vector<std::uint8_t> stream;
    while (fdc.drq()) {
        stream.push_back(fdc.read_register(3));
    }

    CHECK(fdc.intrq());
    CHECK((fdc.status() & 0x11U) == 0x00U);
    REQUIRE_FALSE(stream.empty());
    CHECK(stream[0] == 0x4EU);

    const std::size_t id_mark = find_mfm_id_mark(stream, 0U, 1U, 1U);
    REQUIRE(id_mark < stream.size());
    const std::size_t data_mark = find_data_mark_after(stream, id_mark + 5U);
    REQUIRE(data_mark + wd1793::sector_size < stream.size());
    CHECK(stream[data_mark] == 0xFBU);
    CHECK(stream[data_mark + 1U] == 0x55U);
    CHECK(stream[data_mark + wd1793::sector_size] == 0xE5U);
}

TEST_CASE("wd1793 seeks and reports the current address mark", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount(make_dsk()));

    fdc.write_register(3, 5U); // seek destination in the data register
    fdc.write_register(0, 0x10U);
    CHECK(fdc.track() == 5U);
    fdc.write_register(2, 3U);
    fdc.set_side(1U);
    fdc.write_register(0, 0xC0U);

    CHECK(fdc.read_register(3) == 5U);
    CHECK(fdc.read_register(3) == 1U);
    CHECK(fdc.read_register(3) == 3U);
    CHECK(fdc.read_register(3) == 0x02U);
}

TEST_CASE("wd1793 save_state/load_state preserves an in-flight sector read",
          "[chips][storage][wd1793]") {
    wd1793 live;
    REQUIRE(live.mount(make_dsk()));
    live.write_register(2, 1U);
    live.write_register(0, 0x80U);
    CHECK(live.read_register(3) == 0x11U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    live.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.drq());
    CHECK(restored.read_register(3) == 0x22U);
    for (std::uint16_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)restored.read_register(3);
    }
    CHECK(restored.intrq());
}

TEST_CASE("wd1793 save_state/load_state preserves write-protect state",
          "[chips][storage][wd1793]") {
    wd1793 live;
    REQUIRE(live.mount(make_dsk(), true));

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    live.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.disk_loaded());
    CHECK(restored.write_protected());
    restored.write_register(2, 1U);
    restored.write_register(0, 0xA0U);
    CHECK((restored.status() & 0x40U) != 0U);
}
