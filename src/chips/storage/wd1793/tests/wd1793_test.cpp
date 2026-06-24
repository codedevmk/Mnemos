#include "wd1793.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::storage::wd1793;

    constexpr std::uint8_t k_status_record_not_found = 0x10U;

    [[nodiscard]] std::vector<std::uint8_t> make_dsk(std::uint16_t tracks = 40U,
                                                     std::uint8_t sides = 2U) {
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(tracks) * sides *
                                           wd1793::standard_sectors_per_track * wd1793::sector_size,
                                       0xE5U);
        for (std::size_t i = 0; i < disk.size(); ++i) {
            disk[i] = static_cast<std::uint8_t>(i & 0xFFU);
        }
        return disk;
    }

    [[nodiscard]] std::size_t sector_offset(std::uint8_t track, std::uint8_t side,
                                            std::uint8_t sector, std::uint8_t sides = 2U) {
        return (((static_cast<std::size_t>(track) * sides) + side) *
                    wd1793::standard_sectors_per_track +
                (sector - 1U)) *
               wd1793::sector_size;
    }

    void append_bytes(std::vector<std::uint8_t>& out, std::size_t count, std::uint8_t value) {
        out.insert(out.end(), count, value);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_write_track_stream(std::uint8_t track, std::uint8_t side, std::uint8_t sector,
                            std::uint8_t first_byte, std::uint8_t second_byte) {
        std::vector<std::uint8_t> stream;
        stream.reserve(wd1793::standard_mfm_track_size);

        append_bytes(stream, 80U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF6U);
        stream.push_back(0xFCU);
        append_bytes(stream, 50U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF5U);
        stream.push_back(0xFEU);
        stream.push_back(track);
        stream.push_back(side);
        stream.push_back(sector);
        stream.push_back(0x02U);
        stream.push_back(0xF7U);
        append_bytes(stream, 22U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF5U);
        stream.push_back(0xFBU);
        stream.push_back(first_byte);
        stream.push_back(second_byte);
        append_bytes(stream, wd1793::sector_size - 2U, 0xE5U);
        stream.push_back(0xF7U);
        append_bytes(stream, wd1793::standard_mfm_track_size - stream.size(), 0x4EU);
        return stream;
    }
} // namespace

TEST_CASE("wd1793 infers common flat MSX DSK geometries", "[chips][storage][wd1793]") {
    const auto single = make_dsk(40U, 1U);
    const auto dual = make_dsk(40U, 2U);
    const auto double_density = make_dsk(80U, 2U);

    REQUIRE(wd1793::infer_dsk_geometry(single).has_value());
    CHECK(wd1793::infer_dsk_geometry(single)->tracks == 40U);
    CHECK(wd1793::infer_dsk_geometry(single)->sides == 1U);
    REQUIRE(wd1793::infer_dsk_geometry(dual).has_value());
    CHECK(wd1793::infer_dsk_geometry(dual)->tracks == 40U);
    CHECK(wd1793::infer_dsk_geometry(dual)->sides == 2U);
    REQUIRE(wd1793::infer_dsk_geometry(double_density).has_value());
    CHECK(wd1793::infer_dsk_geometry(double_density)->tracks == 80U);
    CHECK(wd1793::infer_dsk_geometry(double_density)->sides == 2U);

    CHECK_FALSE(wd1793::is_supported_dsk(std::vector<std::uint8_t>(0x4000U, 0x00U)));
}

TEST_CASE("wd1793 streams a selected DSK sector through the data register",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t off = sector_offset(2U, 1U, 3U);
    disk[off + 0U] = 0x42U;
    disk[off + 1U] = 0x99U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(2U, 3U);
    fdc.write_register(0U, 0x80U);

    CHECK(fdc.busy());
    CHECK(fdc.drq());
    CHECK((fdc.read_control_register() & 0x80U) != 0U);
    CHECK(fdc.read_register(3U) == 0x42U);
    CHECK(fdc.read_register(3U) == 0x99U);
    for (std::size_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3U);
    }
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
}

TEST_CASE("wd1793 read-sector multiple streams ascending sectors", "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t sector8 = sector_offset(2U, 1U, 8U);
    const std::size_t sector9 = sector_offset(2U, 1U, 9U);
    disk[sector8] = 0xA8U;
    disk[sector9] = 0xB9U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(2U, 8U);
    fdc.write_register(0U, 0x90U);

    CHECK(fdc.read_register(3U) == 0xA8U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3U);
    }
    CHECK(fdc.busy());
    CHECK(fdc.drq());
    CHECK_FALSE(fdc.intrq());
    CHECK(fdc.sector_register() == 9U);

    CHECK(fdc.read_register(3U) == 0xB9U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)fdc.read_register(3U);
    }
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.sector_register() == 10U);
    CHECK((fdc.read_register(0U) & k_status_record_not_found) != 0U);
}

TEST_CASE("wd1793 Type II side compare rejects mismatched ID side", "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t side1_sector = sector_offset(1U, 1U, 4U);
    disk[side1_sector] = 0x4DU;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 1U);
    fdc.write_register(2U, 4U);
    fdc.write_register(0U, 0x82U);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK((fdc.read_register(0U) & k_status_record_not_found) != 0U);

    fdc.write_register(0U, 0x8AU);
    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3U) == 0x4DU);
}

TEST_CASE("wd1793 read address streams a flat DSK ID field", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 6U);
    fdc.write_register(2U, 7U);
    fdc.write_register(0U, 0xC0U);

    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3U) == 6U);
    CHECK(fdc.read_register(3U) == 1U);
    CHECK(fdc.read_register(3U) == 7U);
    CHECK(fdc.read_register(3U) == 0x02U);
    CHECK(fdc.read_register(3U) == 0x00U);
    CHECK(fdc.read_register(3U) == 0x00U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
}

TEST_CASE("wd1793 read-track synthesizes a full MFM track stream from a flat DSK",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t sector3 = sector_offset(2U, 1U, 3U);
    disk[sector3] = 0x42U;
    disk[sector3 + 1U] = 0x99U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xE0U);

    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    std::vector<std::uint8_t> stream;
    stream.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        stream.push_back(fdc.read_register(3U));
    }

    CHECK(stream.size() == wd1793::standard_mfm_track_size);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());

    const std::vector<std::uint8_t> id_field{
        0xA1U, 0xA1U, 0xA1U, 0xFEU, 0x02U, 0x01U, 0x03U, 0x02U,
    };
    const auto id = std::search(stream.begin(), stream.end(), id_field.begin(), id_field.end());
    CHECK(id != stream.end());

    const std::vector<std::uint8_t> data_field{
        0xA1U, 0xA1U, 0xA1U, 0xFBU, 0x42U, 0x99U,
    };
    const auto data =
        std::search(stream.begin(), stream.end(), data_field.begin(), data_field.end());
    CHECK(data != stream.end());
    CHECK(data > id);
}

TEST_CASE("wd1793 force interrupt can terminate without raising INTRQ",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    fdc.write_register(0U, 0xD0U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK_FALSE(fdc.intrq());

    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.busy());
    fdc.write_register(0U, 0xD8U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    (void)fdc.read_register(0U);
    CHECK_FALSE(fdc.intrq());
}

TEST_CASE("wd1793 writes a DSK sector deterministically", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 4U);
    fdc.write_register(2U, 5U);
    fdc.write_register(0U, 0xA0U);
    REQUIRE(fdc.drq());

    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3U, static_cast<std::uint8_t>(0x80U + i));
    }

    const auto image = fdc.disk_image();
    const std::size_t off = sector_offset(4U, 0U, 5U);
    CHECK(image[off + 0U] == 0x80U);
    CHECK(image[off + 1U] == 0x81U);
    CHECK_FALSE(fdc.busy());
    CHECK(fdc.intrq());
}

TEST_CASE("wd1793 write-sector multiple commits ascending sectors", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 3U);
    fdc.write_register(2U, 8U);
    fdc.write_register(0U, 0xB0U);
    REQUIRE(fdc.drq());

    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3U, 0xC8U);
    }
    CHECK(fdc.busy());
    CHECK(fdc.drq());
    CHECK_FALSE(fdc.intrq());
    CHECK(fdc.sector_register() == 9U);

    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        fdc.write_register(3U, 0xD9U);
    }

    const auto image = fdc.disk_image();
    CHECK(image[sector_offset(3U, 1U, 8U)] == 0xC8U);
    CHECK(image[sector_offset(3U, 1U, 9U)] == 0xD9U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.sector_register() == 10U);
    CHECK((fdc.read_register(0U) & k_status_record_not_found) != 0U);
}

TEST_CASE("wd1793 write-track commits formatted MFM sector data to a flat DSK",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    const auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    for (const std::uint8_t value : stream) {
        fdc.write_register(3U, value);
    }

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    const std::size_t offset = sector_offset(2U, 1U, 4U);
    CHECK(image[offset] == 0x42U);
    CHECK(image[offset + 1U] == 0x99U);
    CHECK(image[offset + 2U] == 0xE5U);
}

TEST_CASE("wd1793 state preserves mounted disk and in-flight register state",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 6U);
    fdc.write_register(2U, 7U);
    fdc.write_register(0U, 0x80U);
    CHECK(fdc.read_register(3U) == fdc.disk_image()[sector_offset(6U, 0U, 7U)]);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.mounted());
    CHECK(restored.track_register() == 6U);
    CHECK(restored.sector_register() == 7U);
    CHECK(restored.drq());
    CHECK(restored.disk_image().size() == fdc.disk_image().size());
    CHECK(restored.read_register(3U) == fdc.disk_image()[sector_offset(6U, 0U, 7U) + 1U]);
}

TEST_CASE("wd1793 state preserves in-flight read-track streams", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 1U);
    fdc.write_register(0U, 0xE0U);
    CHECK(fdc.read_register(3U) == 0x4EU);
    CHECK(fdc.read_register(3U) == 0x4EU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored.drq());
    CHECK(restored.read_register(3U) == 0x4EU);
    CHECK(restored.read_register(3U) == 0x4EU);
}
