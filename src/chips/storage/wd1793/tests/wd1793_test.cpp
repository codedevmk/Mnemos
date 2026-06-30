#include "wd1793.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace {
    using mnemos::chips::storage::wd1793;

    constexpr std::uint8_t k_status_index = 0x02U;
    constexpr std::uint8_t k_status_lost_data = 0x04U;
    constexpr std::uint8_t k_status_record_not_found = 0x10U;
    constexpr std::uint8_t k_status_record_type = 0x20U;
    constexpr std::uint8_t k_status_not_ready = 0x80U;

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

    void put_le16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_msx_bpb_dsk(std::uint8_t sides,
                                                             std::uint8_t sectors_per_track) {
        const std::uint16_t tracks = 80U;
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(tracks) * sides *
                                           sectors_per_track * wd1793::sector_size,
                                       0xE5U);
        for (std::size_t i = 0; i < disk.size(); ++i) {
            disk[i] = static_cast<std::uint8_t>(i & 0xFFU);
        }

        disk[0] = 0xEBU;
        disk[1] = 0xFEU;
        disk[2] = 0x90U;
        put_le16(disk, 0x0BU, static_cast<std::uint16_t>(wd1793::sector_size));
        disk[0x0DU] = 2U; // sectors per cluster
        put_le16(disk, 0x0EU, 1U);
        disk[0x10U] = 2U;
        put_le16(disk, 0x11U, 112U);
        put_le16(disk, 0x13U, static_cast<std::uint16_t>(disk.size() / wd1793::sector_size));
        disk[0x15U] =
            static_cast<std::uint8_t>(0xF7U + ((sectors_per_track == 8U) ? 2U : 0U) + sides);
        const std::uint16_t fat_sectors = sectors_per_track == 8U
                                              ? static_cast<std::uint16_t>(sides)
                                              : static_cast<std::uint16_t>(sides + 1U);
        put_le16(disk, 0x16U, fat_sectors);
        put_le16(disk, 0x18U, sectors_per_track);
        put_le16(disk, 0x1AU, sides);
        return disk;
    }

    [[nodiscard]] std::size_t
    sector_offset(std::uint8_t track, std::uint8_t side, std::uint8_t sector,
                  std::uint8_t sides = 2U,
                  std::uint8_t sectors_per_track = wd1793::standard_sectors_per_track) {
        return (((static_cast<std::size_t>(track) * sides) + side) * sectors_per_track +
                (sector - 1U)) *
               wd1793::sector_size;
    }

    void append_bytes(std::vector<std::uint8_t>& out, std::size_t count, std::uint8_t value) {
        out.insert(out.end(), count, value);
    }

    void wait_for_drq(wd1793& fdc) {
        for (std::uint64_t i = 0; i <= wd1793::nominal_mfm_byte_cycles; ++i) {
            if (fdc.drq()) {
                return;
            }
            fdc.tick(1U);
        }
        REQUIRE(fdc.drq());
    }

    std::uint8_t read_data_register(wd1793& fdc) {
        wait_for_drq(fdc);
        return fdc.read_register(3U);
    }

    void write_data_register(wd1793& fdc, std::uint8_t value) {
        wait_for_drq(fdc);
        fdc.write_register(3U, value);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_write_track_stream(std::uint8_t track, std::uint8_t side, std::uint8_t sector,
                            std::uint8_t first_byte, std::uint8_t second_byte,
                            bool deleted_data_mark = false) {
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
        stream.push_back(deleted_data_mark ? 0xF8U : 0xFBU);
        stream.push_back(first_byte);
        stream.push_back(second_byte);
        append_bytes(stream, wd1793::sector_size - 2U, 0xE5U);
        stream.push_back(0xF7U);
        append_bytes(stream, wd1793::standard_mfm_track_size - stream.size(), 0x4EU);
        return stream;
    }

    [[nodiscard]] std::uint8_t read_address_track(wd1793& fdc) {
        fdc.write_register(0U, 0xC0U);
        REQUIRE(fdc.drq());
        const std::uint8_t track = read_data_register(fdc);
        for (int i = 1; i < 6; ++i) {
            (void)read_data_register(fdc);
        }
        return track;
    }

    [[nodiscard]] std::size_t find_marker(const std::vector<std::uint8_t>& stream,
                                          std::uint8_t marker, std::size_t start = 0U) {
        REQUIRE(start <= stream.size());
        const auto it =
            std::find(stream.begin() + static_cast<std::ptrdiff_t>(start), stream.end(), marker);
        REQUIRE(it != stream.end());
        return static_cast<std::size_t>(std::distance(stream.begin(), it));
    }

    [[nodiscard]] std::size_t find_synced_marker(const std::vector<std::uint8_t>& stream,
                                                 std::uint8_t marker,
                                                 std::size_t start = 0U) {
        REQUIRE(start <= stream.size());
        for (std::size_t i = std::max<std::size_t>(start, 3U); i < stream.size(); ++i) {
            if (stream[i] != marker) {
                continue;
            }
            const std::uint8_t sync = marker == 0xFCU ? std::uint8_t{0xC2U} : std::uint8_t{0xA1U};
            if (stream[i - 3U] == sync && stream[i - 2U] == sync && stream[i - 1U] == sync) {
                return i;
            }
        }
        REQUIRE(false);
        return stream.size();
    }

    void write_track_stream(wd1793& fdc, const std::vector<std::uint8_t>& stream) {
        for (const std::uint8_t value : stream) {
            write_data_register(fdc, value);
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> read_track_stream(wd1793& fdc) {
        fdc.write_register(0U, 0xE0U);
        REQUIRE(fdc.drq());
        std::vector<std::uint8_t> stream;
        stream.reserve(wd1793::standard_mfm_track_size);
        for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
            stream.push_back(read_data_register(fdc));
        }
        return stream;
    }

    [[nodiscard]] std::uint16_t crc16_ccitt(std::span<const std::uint8_t> bytes) noexcept {
        std::uint16_t crc = 0xFFFFU;
        for (const std::uint8_t value : bytes) {
            crc ^= static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 8U);
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                            : static_cast<std::uint16_t>(crc << 1U);
            }
        }
        return crc;
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

TEST_CASE("wd1793 uses MSX-DOS BPB geometry for one-sided 80-track disks",
          "[chips][storage][wd1793]") {
    auto disk = make_msx_bpb_dsk(1U, 9U);
    const std::size_t track1 = sector_offset(1U, 0U, 1U, 1U, 9U);
    disk[track1] = 0x7AU;

    const auto geometry = wd1793::infer_dsk_geometry(disk);
    REQUIRE(geometry.has_value());
    CHECK(geometry->tracks == 80U);
    CHECK(geometry->sides == 1U);
    CHECK(geometry->sectors_per_track == 9U);

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_register(1U, 1U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0x7AU);

    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 1U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    CHECK_FALSE(fdc.drq());
    CHECK((fdc.read_register(0U) & k_status_record_not_found) != 0U);
}

TEST_CASE("wd1793 mounts MSX-DOS 8-sector media geometries", "[chips][storage][wd1793]") {
    auto single = make_msx_bpb_dsk(1U, 8U);
    auto dual = make_msx_bpb_dsk(2U, 8U);
    single[sector_offset(2U, 0U, 8U, 1U, 8U)] = 0x38U;
    dual[sector_offset(2U, 1U, 8U, 2U, 8U)] = 0x68U;

    REQUIRE(wd1793::infer_dsk_geometry(single).has_value());
    CHECK(wd1793::infer_dsk_geometry(single)->tracks == 80U);
    CHECK(wd1793::infer_dsk_geometry(single)->sides == 1U);
    CHECK(wd1793::infer_dsk_geometry(single)->sectors_per_track == 8U);
    REQUIRE(wd1793::infer_dsk_geometry(dual).has_value());
    CHECK(wd1793::infer_dsk_geometry(dual)->tracks == 80U);
    CHECK(wd1793::infer_dsk_geometry(dual)->sides == 2U);
    CHECK(wd1793::infer_dsk_geometry(dual)->sectors_per_track == 8U);

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(dual));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(2U, 8U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0x68U);
}

TEST_CASE("wd1793 write-track accepts MSX-DOS 8-sector media", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_msx_bpb_dsk(2U, 8U)));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    const auto stream = make_write_track_stream(2U, 1U, 8U, 0x83U, 0x84U);
    write_track_stream(fdc, stream);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    const std::size_t offset = sector_offset(2U, 1U, 8U, 2U, 8U);
    CHECK(image[offset] == 0x83U);
    CHECK(image[offset + 1U] == 0x84U);
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
    CHECK(read_data_register(fdc) == 0x42U);
    CHECK(read_data_register(fdc) == 0x99U);
    for (std::size_t i = 2U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(fdc);
    }
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
}

TEST_CASE("wd1793 reports not-ready when a non-mounted drive is selected",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[0] = 0x42U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));

    fdc.write_memory_register(5U, 1U); // drive B has no mounted image in the single-drive model
    fdc.write_register(1U, 0U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK((fdc.read_register(0U) & k_status_not_ready) != 0U);

    fdc.write_memory_register(5U, 0U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0x42U);
}

TEST_CASE("wd1793 reports lost data when DRQ is not serviced in time", "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[0] = 0x35U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_register(1U, 0U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());

    fdc.tick(511U);
    CHECK((fdc.composed_status() & k_status_lost_data) == 0U);
    fdc.tick(1U);
    CHECK((fdc.composed_status() & k_status_lost_data) != 0U);
    CHECK(read_data_register(fdc) == 0x35U);

    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(fdc);
    }
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK((fdc.read_register(0U) & k_status_lost_data) != 0U);
}

TEST_CASE("wd1793 paces DRQ between transfer bytes", "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[0] = 0x12U;
    disk[1] = 0x34U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);

    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3U) == 0x12U);
    CHECK_FALSE(fdc.drq());
    fdc.tick(wd1793::nominal_mfm_byte_cycles - 1U);
    CHECK_FALSE(fdc.drq());
    fdc.tick(1U);
    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3U) == 0x34U);
}

TEST_CASE("wd1793 DRQ service resets lost-data timing and save state preserves DRQ age",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[0] = 0x12U;
    disk[1] = 0x34U;

    wd1793 serviced;
    REQUIRE(serviced.mount_dsk(disk));
    serviced.write_register(1U, 0U);
    serviced.write_register(2U, 1U);
    serviced.write_register(0U, 0x80U);
    REQUIRE(serviced.drq());
    serviced.tick(511U);
    CHECK((serviced.composed_status() & k_status_lost_data) == 0U);
    CHECK(read_data_register(serviced) == 0x12U);
    serviced.tick(1U);
    CHECK((serviced.composed_status() & k_status_lost_data) == 0U);
    wait_for_drq(serviced);
    serviced.tick(511U);
    CHECK((serviced.composed_status() & k_status_lost_data) == 0U);
    serviced.tick(1U);
    CHECK((serviced.composed_status() & k_status_lost_data) != 0U);
    CHECK(read_data_register(serviced) == 0x34U);

    wd1793 saved;
    REQUIRE(saved.mount_dsk(disk));
    saved.write_register(1U, 0U);
    saved.write_register(2U, 1U);
    saved.write_register(0U, 0x80U);
    REQUIRE(saved.drq());
    saved.tick(300U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    saved.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored.drq());
    restored.tick(211U);
    CHECK((restored.composed_status() & k_status_lost_data) == 0U);
    restored.tick(1U);
    CHECK((restored.composed_status() & k_status_lost_data) != 0U);
}

TEST_CASE("wd1793 state preserves in-flight byte pacing", "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[0] = 0x21U;
    disk[1] = 0x43U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(fdc.read_register(3U) == 0x21U);
    fdc.tick(50U);
    REQUIRE_FALSE(fdc.drq());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    restored.tick(wd1793::nominal_mfm_byte_cycles - 51U);
    CHECK_FALSE(restored.drq());
    restored.tick(1U);
    REQUIRE(restored.drq());
    CHECK(restored.read_register(3U) == 0x43U);
}

TEST_CASE("wd1793 Type I status exposes deterministic index pulses", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));

    CHECK((fdc.composed_status() & k_status_index) != 0U);

    fdc.tick(wd1793::nominal_index_pulse_cycles);
    CHECK((fdc.composed_status() & k_status_index) == 0U);

    fdc.tick(wd1793::nominal_index_revolution_cycles - wd1793::nominal_index_pulse_cycles);
    CHECK((fdc.composed_status() & k_status_index) != 0U);
}

TEST_CASE("wd1793 index pulse phase round-trips through save state", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.tick(wd1793::nominal_index_pulse_cycles + 32U);
    REQUIRE((fdc.composed_status() & k_status_index) == 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK((restored.composed_status() & k_status_index) == 0U);

    restored.tick(wd1793::nominal_index_revolution_cycles - wd1793::nominal_index_pulse_cycles -
                  32U);
    CHECK((restored.composed_status() & k_status_index) != 0U);
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

    CHECK(read_data_register(fdc) == 0xA8U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(fdc);
    }
    CHECK(fdc.busy());
    CHECK(fdc.drq());
    CHECK_FALSE(fdc.intrq());
    CHECK(fdc.sector_register() == 9U);

    CHECK(read_data_register(fdc) == 0xB9U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(fdc);
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
    CHECK(read_data_register(fdc) == 0x4DU);
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
    CHECK(read_data_register(fdc) == 6U);
    CHECK(read_data_register(fdc) == 1U);
    CHECK(read_data_register(fdc) == 7U);
    CHECK(read_data_register(fdc) == 0x02U);
    CHECK(read_data_register(fdc) == 0x70U);
    CHECK(read_data_register(fdc) == 0x60U);
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    CHECK(fdc.sector_register() == 6U);
}

TEST_CASE("wd1793 Type I STEP repeats the last seek direction", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 10U);

    fdc.write_register(0U, 0x50U);
    CHECK(fdc.track_register() == 11U);
    fdc.write_register(0U, 0x30U);
    CHECK(fdc.track_register() == 12U);
    fdc.write_register(0U, 0x70U);
    CHECK(fdc.track_register() == 11U);
    fdc.write_register(0U, 0x30U);
    CHECK(fdc.track_register() == 10U);
}

TEST_CASE("wd1793 Type I STEP no-update moves the head without changing TR",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 10U);

    fdc.write_register(0U, 0x40U);
    CHECK(fdc.track_register() == 10U);
    CHECK(read_address_track(fdc) == 11U);

    fdc.write_register(0U, 0x50U);
    CHECK(fdc.track_register() == 11U);
    CHECK(read_address_track(fdc) == 12U);

    fdc.write_register(0U, 0x60U);
    CHECK(fdc.track_register() == 11U);
    CHECK(read_address_track(fdc) == 11U);
}

TEST_CASE("wd1793 sector reads require the track register to match the stepped head",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    disk[sector_offset(10U, 0U, 1U)] = 0xA0U;
    disk[sector_offset(11U, 0U, 1U)] = 0xB1U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_register(1U, 10U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x40U);
    fdc.write_register(0U, 0x80U);
    CHECK_FALSE(fdc.drq());
    CHECK((fdc.read_register(0U) & k_status_record_not_found) != 0U);

    fdc.write_register(1U, 11U);
    fdc.write_register(2U, 1U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0xB1U);
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
        stream.push_back(read_data_register(fdc));
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

TEST_CASE("wd1793 read-track starts at the current index phase", "[chips][storage][wd1793]") {
    wd1793 canonical_fdc;
    REQUIRE(canonical_fdc.mount_dsk(make_dsk()));
    const auto canonical = read_track_stream(canonical_fdc);

    wd1793 phased_fdc;
    REQUIRE(phased_fdc.mount_dsk(make_dsk()));
    const std::uint64_t phase_cycles = wd1793::nominal_index_revolution_cycles / 3U;
    const std::size_t byte_phase = static_cast<std::size_t>(
        (phase_cycles * static_cast<std::uint64_t>(wd1793::standard_mfm_track_size)) /
        wd1793::nominal_index_revolution_cycles);
    REQUIRE(byte_phase > 0U);
    REQUIRE(byte_phase < canonical.size());

    phased_fdc.tick(phase_cycles);
    const auto rotated = read_track_stream(phased_fdc);

    auto expected = canonical;
    std::rotate(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(byte_phase),
                expected.end());
    CHECK(rotated == expected);
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
        write_data_register(fdc, static_cast<std::uint8_t>(0x80U + i));
    }

    const auto image = fdc.disk_image();
    const std::size_t off = sector_offset(4U, 0U, 5U);
    CHECK(image[off + 0U] == 0x80U);
    CHECK(image[off + 1U] == 0x81U);
    CHECK_FALSE(fdc.busy());
    CHECK(fdc.intrq());
}

TEST_CASE("wd1793 write-sector deleted data mark survives in-flight save state",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 4U);
    fdc.write_register(2U, 5U);
    fdc.write_register(0U, 0xA1U);
    REQUIRE(fdc.drq());

    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        write_data_register(fdc, static_cast<std::uint8_t>(0xD0U + i));
    }
    REQUIRE_FALSE(fdc.busy());

    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0xD0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(restored);
    }
    CHECK_FALSE(restored.busy());
    CHECK((restored.read_register(0U) & k_status_record_type) != 0U);
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
        write_data_register(fdc, 0xC8U);
    }
    CHECK(fdc.busy());
    CHECK(fdc.drq());
    CHECK_FALSE(fdc.intrq());
    CHECK(fdc.sector_register() == 9U);

    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        write_data_register(fdc, 0xD9U);
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
        write_data_register(fdc, value);
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

TEST_CASE("wd1793 write-track deleted data marks drive record-type status and track reads",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    const auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U, true);
    write_track_stream(fdc, stream);
    REQUIRE_FALSE(fdc.busy());

    fdc.write_register(2U, 4U);
    fdc.write_register(0U, 0x80U);
    REQUIRE(fdc.drq());
    CHECK(read_data_register(fdc) == 0x42U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_data_register(fdc);
    }
    CHECK((fdc.read_register(0U) & k_status_record_type) != 0U);

    fdc.write_register(0U, 0xE0U);
    REQUIRE(fdc.drq());
    std::vector<std::uint8_t> read_track;
    read_track.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        read_track.push_back(read_data_register(fdc));
    }

    const std::vector<std::uint8_t> data_field{
        0xA1U, 0xA1U, 0xA1U, 0xF8U, 0x42U, 0x99U,
    };
    const auto data =
        std::search(read_track.begin(), read_track.end(), data_field.begin(), data_field.end());
    CHECK(data != read_track.end());
}

TEST_CASE("wd1793 write-track requires generated ID CRC markers before committing flat DSK data",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t offset = sector_offset(2U, 1U, 4U);
    disk[offset] = 0x11U;
    disk[offset + 1U] = 0x22U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    const std::size_t id_mark = find_marker(stream, 0xFEU);
    stream[id_mark + 5U] = 0x4EU;
    write_track_stream(fdc, stream);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    CHECK(image[offset] == 0x11U);
    CHECK(image[offset + 1U] == 0x22U);
}

TEST_CASE("wd1793 write-track requires generated data CRC markers before committing flat DSK data",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t offset = sector_offset(2U, 1U, 4U);
    disk[offset] = 0x11U;
    disk[offset + 1U] = 0x22U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    const std::size_t data_mark = find_marker(stream, 0xFBU);
    stream[data_mark + 1U + wd1793::sector_size] = 0x4EU;
    write_track_stream(fdc, stream);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    CHECK(image[offset] == 0x11U);
    CHECK(image[offset + 1U] == 0x22U);
}

TEST_CASE("wd1793 write-track rejects reserved control bytes inside flat DSK payloads",
          "[chips][storage][wd1793]") {
    auto disk = make_dsk();
    const std::size_t offset = sector_offset(2U, 1U, 4U);
    disk[offset] = 0x11U;
    disk[offset + 1U] = 0x22U;

    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(disk));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0xF7U);
    write_track_stream(fdc, stream);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    CHECK(image[offset] == 0x11U);
    CHECK(image[offset + 1U] == 0x22U);
}

TEST_CASE("wd1793 write-track commits to the stepped head after no-update STEP",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 10U);
    fdc.write_register(0U, 0x40U);
    REQUIRE(fdc.track_register() == 10U);
    REQUIRE(read_address_track(fdc) == 11U);

    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());

    const auto stream = make_write_track_stream(11U, 0U, 3U, 0xD1U, 0xD2U);
    write_track_stream(fdc, stream);

    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK(fdc.intrq());
    const auto image = fdc.disk_image();
    const std::size_t stepped_offset = sector_offset(11U, 0U, 3U);
    const std::size_t original_offset = sector_offset(10U, 0U, 3U);
    CHECK(image[stepped_offset] == 0xD1U);
    CHECK(image[stepped_offset + 1U] == 0xD2U);
    CHECK(image[original_offset] != 0xD1U);
}

TEST_CASE("wd1793 write-track preserves raw track gaps and generated CRC bytes",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    const std::size_t id_mark = find_marker(stream, 0xFEU);
    const std::size_t custom_gap = id_mark - 20U;
    stream[custom_gap] = 0x33U;

    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.busy());
    REQUIRE(fdc.drq());
    write_track_stream(fdc, stream);
    REQUIRE_FALSE(fdc.busy());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    const auto raw = read_track_stream(restored);
    CHECK(std::find(raw.begin(), raw.end(), static_cast<std::uint8_t>(0x33U)) != raw.end());

    const std::size_t restored_id = find_synced_marker(raw, 0xFEU);
    REQUIRE(restored_id >= 3U);
    CHECK(raw[restored_id - 3U] == 0xA1U);
    CHECK(raw[restored_id - 2U] == 0xA1U);
    CHECK(raw[restored_id - 1U] == 0xA1U);

    const std::array<std::uint8_t, 8U> id_crc_bytes{
        0xA1U, 0xA1U, 0xA1U, 0xFEU, 2U, 1U, 4U, 0x02U,
    };
    const std::uint16_t id_crc = crc16_ccitt(id_crc_bytes);
    CHECK(raw[restored_id + 5U] == static_cast<std::uint8_t>((id_crc >> 8U) & 0xFFU));
    CHECK(raw[restored_id + 6U] == static_cast<std::uint8_t>(id_crc & 0xFFU));
    CHECK(raw[restored_id + 5U] != 0xF7U);

    const std::size_t data_mark = find_synced_marker(raw, 0xFBU, restored_id + 7U);
    REQUIRE(data_mark + 1U + wd1793::sector_size + 1U < raw.size());
    std::vector<std::uint8_t> data_crc_bytes{0xA1U, 0xA1U, 0xA1U, 0xFBU};
    data_crc_bytes.insert(
        data_crc_bytes.end(), raw.begin() + static_cast<std::ptrdiff_t>(data_mark + 1U),
        raw.begin() + static_cast<std::ptrdiff_t>(data_mark + 1U + wd1793::sector_size));
    const std::uint16_t data_crc = crc16_ccitt(data_crc_bytes);
    CHECK(raw[data_mark + 1U + wd1793::sector_size] ==
          static_cast<std::uint8_t>((data_crc >> 8U) & 0xFFU));
    CHECK(raw[data_mark + 1U + wd1793::sector_size + 1U] ==
          static_cast<std::uint8_t>(data_crc & 0xFFU));
}

TEST_CASE("wd1793 sector writes invalidate preserved raw track streams",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    const std::size_t id_mark = find_marker(stream, 0xFEU);
    const std::size_t custom_gap = id_mark - 20U;
    stream[custom_gap] = 0x33U;

    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.drq());
    write_track_stream(fdc, stream);
    const auto preserved = read_track_stream(fdc);
    REQUIRE(std::find(preserved.begin(), preserved.end(), static_cast<std::uint8_t>(0x33U)) !=
            preserved.end());

    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);
    fdc.write_register(2U, 4U);
    fdc.write_register(0U, 0xA0U);
    REQUIRE(fdc.drq());
    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        write_data_register(fdc, static_cast<std::uint8_t>(0x55U + i));
    }

    const auto regenerated = read_track_stream(fdc);
    const std::size_t regenerated_id = find_synced_marker(regenerated, 0xFEU);
    REQUIRE(regenerated_id >= 20U);
    CHECK(regenerated[regenerated_id - 20U] == 0x4EU);
    const std::vector<std::uint8_t> data_field{0xA1U, 0xA1U, 0xA1U, 0xFBU, 0x55U};
    const auto data =
        std::search(regenerated.begin(), regenerated.end(), data_field.begin(), data_field.end());
    CHECK(data != regenerated.end());
}

TEST_CASE("wd1793 preserved raw track streams rotate from the current index phase",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_memory_register(4U, 1U);
    fdc.write_register(1U, 2U);

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x42U, 0x99U);
    const std::size_t id_mark = find_marker(stream, 0xFEU);
    const std::size_t custom_gap = id_mark - 20U;
    stream[custom_gap] = 0x33U;

    fdc.write_register(0U, 0xF0U);
    REQUIRE(fdc.drq());
    write_track_stream(fdc, stream);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 canonical_fdc;
    mnemos::chips::state_reader canonical_reader(blob);
    canonical_fdc.load_state(canonical_reader);
    REQUIRE(canonical_reader.ok());
    canonical_fdc.write_memory_register(4U, 1U);
    canonical_fdc.write_register(1U, 2U);
    const auto canonical = read_track_stream(canonical_fdc);
    REQUIRE(std::find(canonical.begin(), canonical.end(), static_cast<std::uint8_t>(0x33U)) !=
            canonical.end());

    const std::uint64_t phase_cycles = wd1793::nominal_index_revolution_cycles / 5U;
    const std::size_t byte_phase = static_cast<std::size_t>(
        (phase_cycles * static_cast<std::uint64_t>(wd1793::standard_mfm_track_size)) /
        wd1793::nominal_index_revolution_cycles);
    REQUIRE(byte_phase > 0U);
    REQUIRE(byte_phase < canonical.size());

    wd1793 phased_fdc;
    mnemos::chips::state_reader phased_reader(blob);
    phased_fdc.load_state(phased_reader);
    REQUIRE(phased_reader.ok());
    phased_fdc.tick(phase_cycles);
    phased_fdc.write_memory_register(4U, 1U);
    phased_fdc.write_register(1U, 2U);
    const auto rotated = read_track_stream(phased_fdc);

    const std::size_t canonical_id = find_synced_marker(canonical, 0xFEU);
    const std::size_t rotated_id = find_synced_marker(rotated, 0xFEU);
    const std::size_t observed_phase =
        (canonical_id + canonical.size() - rotated_id) % canonical.size();
    CHECK((observed_phase == byte_phase || observed_phase == byte_phase + 1U));

    auto expected = canonical;
    std::rotate(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(observed_phase),
                expected.end());
    CHECK(rotated == expected);
}

TEST_CASE("wd1793 state preserves mounted disk and in-flight register state",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 6U);
    fdc.write_register(2U, 7U);
    fdc.write_register(0U, 0x80U);
    CHECK(read_data_register(fdc) == fdc.disk_image()[sector_offset(6U, 0U, 7U)]);

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
    CHECK(restored.disk_image().size() == fdc.disk_image().size());
    CHECK(read_data_register(restored) == fdc.disk_image()[sector_offset(6U, 0U, 7U) + 1U]);
}

TEST_CASE("wd1793 state preserves Type I STEP direction", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 10U);
    fdc.write_register(0U, 0x70U);
    CHECK(fdc.track_register() == 9U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    restored.write_register(0U, 0x30U);
    CHECK(restored.track_register() == 8U);
}

TEST_CASE("wd1793 state preserves physical head position after no-update STEP",
          "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 10U);
    fdc.write_register(0U, 0x40U);
    REQUIRE(fdc.track_register() == 10U);
    REQUIRE(read_address_track(fdc) == 11U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.track_register() == 10U);
    CHECK(read_address_track(restored) == 11U);
}

TEST_CASE("wd1793 state preserves in-flight read-track streams", "[chips][storage][wd1793]") {
    wd1793 fdc;
    REQUIRE(fdc.mount_dsk(make_dsk()));
    fdc.write_register(1U, 1U);
    fdc.write_register(0U, 0xE0U);
    CHECK(read_data_register(fdc) == 0x4EU);
    CHECK(read_data_register(fdc) == 0x4EU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fdc.save_state(writer);

    wd1793 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(read_data_register(restored) == 0x4EU);
    CHECK(read_data_register(restored) == 0x4EU);
}
