#include "korean_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::korean_mapper;

    // An N-page ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      korean_mapper::rom_page_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * korean_mapper::rom_page_size;
            for (int i = 0; i < korean_mapper::rom_page_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, korean_mapper>);

TEST_CASE("korean_mapper reports identity and registers under korean.mapper") {
    const korean_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Korean");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("korean_mapper powers on linear with slot 2 = bank 2") {
    auto rom = make_rom();
    korean_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.page() == 2U);
    CHECK(mapper.cpu_read(0x0000U) == 0U); // slot 0 -> bank 0 (fixed)
    CHECK(mapper.cpu_read(0x4000U) == 1U); // slot 1 -> bank 1 (fixed)
    CHECK(mapper.cpu_read(0x8000U) == 2U); // slot 2 -> bank 2 (power-on)
}

TEST_CASE("korean_mapper banks slot 2 via a write to $A000") {
    auto rom = make_rom();
    rom[5U * korean_mapper::rom_page_size + 0x10U] = 0x99U; // page 5 marker

    korean_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xA000U, 5U); // slot 2 -> page 5
    CHECK(mapper.page() == 5U);
    CHECK(mapper.cpu_read(0x8000U) == 5U);
    CHECK(mapper.cpu_read(0x8010U) == 0x99U); // in-page offset preserved
    CHECK(mapper.cpu_read(0xBFFFU) == 5U);    // whole 16 KiB window follows the page
}

TEST_CASE("korean_mapper leaves slots 0 and 1 fixed when slot 2 banks") {
    auto rom = make_rom();
    korean_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xA000U, 7U);
    CHECK(mapper.cpu_read(0x0000U) == 0U); // slot 0 still bank 0
    CHECK(mapper.cpu_read(0x4000U) == 1U); // slot 1 still bank 1
    CHECK(mapper.cpu_read(0x8000U) == 7U); // only slot 2 moved
}

TEST_CASE("korean_mapper ignores writes outside the $A000 register") {
    auto rom = make_rom();
    korean_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 3U); // ROM is read-only -> dropped
    mapper.cpu_write(0x8000U, 4U); // slot-2 read window, not the register -> dropped
    mapper.cpu_write(0xBFFFU, 5U);
    CHECK(mapper.page() == 2U); // unchanged power-on page
    CHECK(mapper.cpu_read(0x8000U) == 2U);
}

TEST_CASE("korean_mapper wraps the page index modulo the ROM page count") {
    auto rom = make_rom(8);
    korean_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xA000U, 8U); // 8 % 8 == page 0
    CHECK(mapper.cpu_read(0x8000U) == 0U);
    mapper.cpu_write(0xA000U, 9U); // 9 % 8 == page 1
    CHECK(mapper.cpu_read(0x8000U) == 1U);
}

TEST_CASE("korean_mapper round-trips its slot-2 page") {
    auto rom = make_rom();
    korean_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xA000U, 6U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    korean_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page() == 6U);
    CHECK(restored.cpu_read(0x8000U) == 6U);
}
