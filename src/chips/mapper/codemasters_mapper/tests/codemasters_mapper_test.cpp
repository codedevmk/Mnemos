#include "codemasters_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::codemasters_mapper;

    // An N-page ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      codemasters_mapper::rom_page_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * codemasters_mapper::rom_page_size;
            for (int i = 0; i < codemasters_mapper::rom_page_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, codemasters_mapper>);
static_assert(codemasters_mapper::static_class == mnemos::chips::chip_class::mapper);

TEST_CASE("codemasters_mapper reports identity and registers under codemasters.mapper") {
    const codemasters_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Codemasters");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("codemasters.mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("codemasters_mapper powers on with pages 0/1/0") {
    auto rom = make_rom();
    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 1U);
    CHECK(mapper.page(2) == 0U);
    CHECK(mapper.cpu_read(0x0000U) == 0U); // slot 0 -> page 0
    CHECK(mapper.cpu_read(0x4000U) == 1U); // slot 1 -> page 1
    CHECK(mapper.cpu_read(0x8000U) == 0U); // slot 2 -> page 0
}

TEST_CASE("codemasters_mapper banks all three slots via $0000/$4000/$8000") {
    auto rom = make_rom();
    rom[1U * codemasters_mapper::rom_page_size + 0x10U] = 0x99U; // page 1 marker

    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 3U); // slot 0 -> page 3
    CHECK(mapper.page(0) == 3U);
    CHECK(mapper.cpu_read(0x0000U) == 3U);

    mapper.cpu_write(0x4000U, 1U);            // slot 1 -> page 1
    CHECK(mapper.cpu_read(0x4010U) == 0x99U); // in-page offset preserved

    mapper.cpu_write(0x8000U, 5U); // slot 2 -> page 5
    CHECK(mapper.cpu_read(0x8000U) == 5U);
}

TEST_CASE("codemasters_mapper banks slot 0 whole (no fixed first 1 KiB)") {
    auto rom = make_rom();
    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    // Unlike the Sega mapper, $0000-$03FF follows the slot-0 page register.
    mapper.cpu_write(0x0000U, 4U);
    CHECK(mapper.cpu_read(0x0000U) == 4U);
    CHECK(mapper.cpu_read(0x0200U) == 4U);
}

TEST_CASE("codemasters_mapper wraps the page index modulo the ROM page count") {
    auto rom = make_rom(8);
    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 8U); // 8 % 8 == page 0
    CHECK(mapper.cpu_read(0x0000U) == 0U);
    mapper.cpu_write(0x0000U, 9U); // 9 % 8 == page 1
    CHECK(mapper.cpu_read(0x0000U) == 1U);
}

TEST_CASE("codemasters_mapper maps 8 KiB cart RAM at $A000-$BFFF via bit 7 of $4000") {
    auto rom = make_rom();
    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x8000U, 2U); // slot 2 -> page 2 (so ROM here reads 2)
    CHECK_FALSE(mapper.cart_ram_enabled());
    CHECK(mapper.cpu_read(0xA000U) == 2U); // slot-2 ROM, upper half

    mapper.cpu_write(0x4000U, codemasters_mapper::ram_enable_bit); // bit 7 set -> RAM on
    REQUIRE(mapper.cart_ram_enabled());
    mapper.cpu_write(0xA000U, 0x5AU);
    mapper.cpu_write(0xBFFFU, 0x3CU);
    CHECK(mapper.cpu_read(0xA000U) == 0x5AU); // RAM
    CHECK(mapper.cpu_read(0xBFFFU) == 0x3CU);
    CHECK(mapper.cpu_read(0x8000U) == 2U); // lower half stays slot-2 ROM
    CHECK(mapper.cpu_read(0x9FFFU) == 2U);

    mapper.cpu_write(0x4000U, 0x00U); // bit 7 clear -> RAM off
    CHECK_FALSE(mapper.cart_ram_enabled());
    CHECK(mapper.cpu_read(0xA000U) == 2U); // slot-2 ROM again
}

TEST_CASE("codemasters_mapper round-trips its banking + cart RAM state") {
    auto rom = make_rom();
    codemasters_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 6U);
    mapper.cpu_write(0x8000U, 3U);
    mapper.cpu_write(0x4000U, codemasters_mapper::ram_enable_bit); // RAM on, slot1 page = $80
    mapper.cpu_write(0xA100U, 0xC3U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    codemasters_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page(0) == 6U);
    CHECK(restored.page(2) == 3U);
    CHECK(restored.cart_ram_enabled());
    CHECK(restored.cpu_read(0xA100U) == 0xC3U);
}
