#include "janggun_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::janggun_mapper;

    // An N-page (8 KiB each) ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      janggun_mapper::rom_bank_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * janggun_mapper::rom_bank_size;
            for (int i = 0; i < janggun_mapper::rom_bank_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, janggun_mapper>);

TEST_CASE("janggun_mapper reports identity and registers under korean.janggun_mapper") {
    const janggun_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Korean");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.janggun_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("janggun_mapper powers on with a fixed first 16 KiB and zeroed banks") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.fcr(0) == 0U);
    CHECK(mapper.fcr(1) == 0U);
    CHECK(mapper.fcr(2) == 0U);
    CHECK(mapper.fcr(3) == 0U);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // fixed: first 8 KiB of bank 0
    CHECK(mapper.cpu_read(0x2000U) == 1U); // fixed: bank 1
    CHECK(mapper.cpu_read(0x4000U) == 0U); // FCR2 -> bank 0
    CHECK(mapper.cpu_read(0x6000U) == 0U); // FCR3 -> bank 0
    CHECK(mapper.cpu_read(0x8000U) == 0U); // FCR0 -> bank 0
    CHECK(mapper.cpu_read(0xA000U) == 0U); // FCR1 -> bank 0
}

TEST_CASE("janggun_mapper banks the four windows via the direct 8 KiB registers") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x8000U, 3U); // FCR0 -> $8000-$9FFF = bank 3
    mapper.cpu_write(0xA000U, 4U); // FCR1 -> $A000-$BFFF = bank 4
    mapper.cpu_write(0x4000U, 5U); // FCR2 -> $4000-$5FFF = bank 5
    mapper.cpu_write(0x6000U, 6U); // FCR3 -> $6000-$7FFF = bank 6

    CHECK(mapper.fcr(0) == 3U);
    CHECK(mapper.fcr(1) == 4U);
    CHECK(mapper.fcr(2) == 5U);
    CHECK(mapper.fcr(3) == 6U);

    CHECK(mapper.cpu_read(0x8000U) == 3U);
    CHECK(mapper.cpu_read(0xA000U) == 4U);
    CHECK(mapper.cpu_read(0x4000U) == 5U);
    CHECK(mapper.cpu_read(0x6000U) == 6U);

    // The fixed first 16 KiB is unaffected by banking.
    CHECK(mapper.cpu_read(0x0000U) == 0U);
    CHECK(mapper.cpu_read(0x2000U) == 1U);
}

TEST_CASE("janggun_mapper banks 16 KiB pairs via the Sega-style $FFFE/$FFFF registers") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0xFFFFU, 3U); // $8000-$BFFF pair: FCR0=6, FCR1=7
    CHECK(mapper.fcr(0) == 6U);
    CHECK(mapper.fcr(1) == 7U);
    CHECK(mapper.cpu_read(0x8000U) == 6U);
    CHECK(mapper.cpu_read(0xA000U) == 7U);

    mapper.cpu_write(0xFFFEU, 2U); // $4000-$7FFF pair: FCR2=4, FCR3=5
    CHECK(mapper.fcr(2) == 4U);
    CHECK(mapper.fcr(3) == 5U);
    CHECK(mapper.cpu_read(0x4000U) == 4U);
    CHECK(mapper.cpu_read(0x6000U) == 5U);
}

TEST_CASE("janggun_mapper bit-reverses a page when its low-window FCR has bit 7 set") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    // FCR2 selects bank 5 with the reverse flag clear: read is the raw byte.
    mapper.cpu_write(0x4000U, 5U);
    CHECK(mapper.cpu_read(0x4000U) == 0x05U);

    // Set bit 7 on FCR2 (bank 5 | $80): the whole $4000-$7FFF page reverses.
    mapper.cpu_write(0x4000U, 0x85U);
    CHECK(mapper.fcr(2) == 0x85U);
    CHECK(mapper.cpu_read(0x4000U) == 0xA0U); // reverse(0x05)

    // $6000-$7FFF (FCR3) reverses off FCR2's flag, not its own register.
    mapper.cpu_write(0x6000U, 1U);            // FCR3 -> bank 1, no bit 7 of its own
    CHECK(mapper.cpu_read(0x6000U) == 0x80U); // reverse(0x01)

    // The fixed first 16 KiB is never reversed.
    CHECK(mapper.cpu_read(0x0000U) == 0x00U);
    CHECK(mapper.cpu_read(0x2000U) == 0x01U);

    // The $8000-$BFFF page is independent: reverses only on FCR0 bit 7.
    mapper.cpu_write(0x8000U, 3U);
    CHECK(mapper.cpu_read(0x8000U) == 0x03U);
    mapper.cpu_write(0x8000U, 0x83U);         // bank 3 | reverse
    CHECK(mapper.cpu_read(0x8000U) == 0xC0U); // reverse(0x03)
    mapper.cpu_write(0xA000U, 2U);            // FCR1 -> bank 2
    CHECK(mapper.cpu_read(0xA000U) == 0x40U); // reverse(0x02), off FCR0 bit 7
}

TEST_CASE("janggun_mapper ignores writes outside its bank/pair registers") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 7U); // fixed region, not a register
    mapper.cpu_write(0x2000U, 7U); // fixed region, not a register
    mapper.cpu_write(0xC000U, 7U); // work-RAM area, not a register
    CHECK(mapper.fcr(0) == 0U);
    CHECK(mapper.fcr(1) == 0U);
    CHECK(mapper.fcr(2) == 0U);
    CHECK(mapper.fcr(3) == 0U);
}

TEST_CASE("janggun_mapper wraps the bank index modulo the 8 KiB page count") {
    auto rom = make_rom(8);
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x8000U, 8U); // 8 % 8 == bank 0
    CHECK(mapper.cpu_read(0x8000U) == 0U);
    mapper.cpu_write(0x8000U, 9U); // 9 % 8 == bank 1
    CHECK(mapper.cpu_read(0x8000U) == 1U);
}

TEST_CASE("janggun_mapper round-trips its bank registers") {
    auto rom = make_rom();
    janggun_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x8000U, 1U);
    mapper.cpu_write(0xA000U, 2U);
    mapper.cpu_write(0x4000U, 3U);
    mapper.cpu_write(0x6000U, 4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    janggun_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.fcr(0) == 1U);
    CHECK(restored.fcr(1) == 2U);
    CHECK(restored.fcr(2) == 3U);
    CHECK(restored.fcr(3) == 4U);
    CHECK(restored.cpu_read(0x8000U) == 1U);
}
