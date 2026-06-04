#include "multi_16k_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::multi_16k_mapper;

    // An N-page (16 KiB each) ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      multi_16k_mapper::rom_page_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * multi_16k_mapper::rom_page_size;
            for (int i = 0; i < multi_16k_mapper::rom_page_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, multi_16k_mapper>);

TEST_CASE("multi_16k_mapper reports identity and registers under korean.multi_16k_mapper") {
    const multi_16k_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Korean");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.multi_16k_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("multi_16k_mapper powers on with banks {0, 1, 0}") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 1U);
    CHECK(mapper.page(2) == 0U);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // slot 0 -> bank 0
    CHECK(mapper.cpu_read(0x4000U) == 1U); // slot 1 -> bank 1
    CHECK(mapper.cpu_read(0x8000U) == 0U); // slot 2 -> bank 0
}

TEST_CASE("multi_16k_mapper banks the three slots via $3FFE / $7FFF / $BFFF") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x3FFEU, 2U); // slot 0 -> bank 2
    mapper.cpu_write(0x7FFFU, 3U); // slot 1 -> bank 3
    mapper.cpu_write(0xBFFFU, 1U); // slot 2 -> (2 & 0x30) + 1 = bank 1

    CHECK(mapper.page(0) == 2U);
    CHECK(mapper.page(1) == 3U);
    CHECK(mapper.page(2) == 1U);

    CHECK(mapper.cpu_read(0x0000U) == 2U);
    CHECK(mapper.cpu_read(0x4000U) == 3U);
    CHECK(mapper.cpu_read(0x8000U) == 1U);
}

TEST_CASE("multi_16k_mapper takes slot-2's high bits from the slot-0 register at $BFFF") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    // slot-0 register with bits 4-5 set -> slot 2 inherits them.
    mapper.cpu_write(0x3FFEU, 0x30U);
    mapper.cpu_write(0xBFFFU, 0x05U);
    CHECK(mapper.page(2) == 0x35U); // (0x30 & 0x30) + 0x05

    // slot-0 register with those bits clear -> slot 2 is the plain value.
    mapper.cpu_write(0x3FFEU, 0x02U);
    mapper.cpu_write(0xBFFFU, 0x03U);
    CHECK(mapper.page(2) == 0x03U); // (0x02 & 0x30) + 0x03
}

TEST_CASE("multi_16k_mapper does not fix the first 1 KiB (it banks with slot 0)") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // reset: slot 0 = bank 0
    CHECK(mapper.cpu_read(0x03FFU) == 0U);

    mapper.cpu_write(0x3FFEU, 2U);         // slot 0 -> bank 2
    CHECK(mapper.cpu_read(0x0000U) == 2U); // first 1 KiB follows the bank...
    CHECK(mapper.cpu_read(0x03FFU) == 2U); // ...unlike the Sega mapper's fixed $0000-$03FF
}

TEST_CASE("multi_16k_mapper ignores writes outside its slot registers") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 7U); // slot start, not the $3FFE register
    mapper.cpu_write(0x4000U, 7U); // slot start, not the $7FFF register
    mapper.cpu_write(0x8000U, 7U); // slot start, not the $BFFF register
    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 1U);
    CHECK(mapper.page(2) == 0U);
}

TEST_CASE("multi_16k_mapper wraps the bank index modulo the 16 KiB page count") {
    auto rom = make_rom(4);
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x3FFEU, 5U); // 5 % 4 == bank 1
    CHECK(mapper.cpu_read(0x0000U) == 1U);
}

TEST_CASE("multi_16k_mapper round-trips its slot registers") {
    auto rom = make_rom();
    multi_16k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x3FFEU, 2U);
    mapper.cpu_write(0x7FFFU, 3U);
    mapper.cpu_write(0xBFFFU, 1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    multi_16k_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page(0) == 2U);
    CHECK(restored.page(1) == 3U);
    CHECK(restored.page(2) == 1U);
    CHECK(restored.cpu_read(0x4000U) == 3U);
}
