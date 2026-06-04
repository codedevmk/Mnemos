#include "hicom_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::hicom_mapper;

    // An N-page (32 KiB each) ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      hicom_mapper::rom_page_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * hicom_mapper::rom_page_size;
            for (int i = 0; i < hicom_mapper::rom_page_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, hicom_mapper>);

TEST_CASE("hicom_mapper reports identity and registers under korean.hicom_mapper") {
    const hicom_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "HiCom");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.hicom_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("hicom_mapper powers on at page 0 across the whole window") {
    auto rom = make_rom();
    hicom_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.page() == 0U);
    CHECK(mapper.cpu_read(0x0000U) == 0U); // 32 KiB page window
    CHECK(mapper.cpu_read(0x7FFFU) == 0U);
    CHECK(mapper.cpu_read(0x8000U) == 0U); // mirror of the lower 16 KiB
    CHECK(mapper.cpu_read(0xBFFFU) == 0U);
}

TEST_CASE("hicom_mapper selects a 32 KiB page via a write to $FFFF") {
    auto rom = make_rom();
    rom[3U * hicom_mapper::rom_page_size + 0x0010U] = 0x99U; // page 3 marker
    rom[3U * hicom_mapper::rom_page_size + 0x4010U] = 0x77U; // page 3, upper-half marker

    hicom_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xFFFFU, 3U);
    CHECK(mapper.page() == 3U);
    CHECK(mapper.cpu_read(0x0000U) == 3U);    // page 3 across the window
    CHECK(mapper.cpu_read(0x0010U) == 0x99U); // in-page offset preserved
    CHECK(mapper.cpu_read(0x4010U) == 0x77U); // upper half of the 32 KiB page
    CHECK(mapper.cpu_read(0x8010U) == 0x99U); // $8000 mirrors $0000 (lower 16 KiB)
    CHECK(mapper.cpu_read(0x7FFFU) == 3U);
}

TEST_CASE("hicom_mapper drops writes that are not the $FFFF register") {
    auto rom = make_rom();
    hicom_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 5U);
    mapper.cpu_write(0x8000U, 5U);
    mapper.cpu_write(0xFFFEU, 5U); // adjacent address, not the register
    CHECK(mapper.page() == 0U);
    CHECK(mapper.cpu_read(0x0000U) == 0U);
}

TEST_CASE("hicom_mapper wraps the page index modulo the 32 KiB page count") {
    auto rom = make_rom(8);
    hicom_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xFFFFU, 8U); // 8 % 8 == page 0
    CHECK(mapper.cpu_read(0x0000U) == 0U);
    mapper.cpu_write(0xFFFFU, 9U); // 9 % 8 == page 1
    CHECK(mapper.cpu_read(0x0000U) == 1U);
}

TEST_CASE("hicom_mapper round-trips its page register") {
    auto rom = make_rom();
    hicom_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xFFFFU, 6U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    hicom_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page() == 6U);
    CHECK(restored.cpu_read(0x0000U) == 6U);
}
