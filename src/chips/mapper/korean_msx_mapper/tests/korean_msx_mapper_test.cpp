#include "korean_msx_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::korean_msx_mapper;

    // An N-page (8 KiB each) ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 8) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      korean_msx_mapper::rom_bank_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * korean_msx_mapper::rom_bank_size;
            for (int i = 0; i < korean_msx_mapper::rom_bank_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, korean_msx_mapper>);

TEST_CASE("korean_msx_mapper reports identity and registers under korean.msx_mapper") {
    const korean_msx_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Korean");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.msx_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("korean_msx_mapper powers on with a fixed bank 0 and zeroed pages") {
    auto rom = make_rom();
    korean_msx_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // fixed: first 8 KiB of bank 0
    CHECK(mapper.cpu_read(0x2000U) == 1U); // fixed: second 8 KiB of bank 0
    CHECK(mapper.cpu_read(0x4000U) == 0U); // reg 2 -> page 0
    CHECK(mapper.cpu_read(0x6000U) == 0U); // reg 3 -> page 0
    CHECK(mapper.cpu_read(0x8000U) == 0U); // reg 0 -> page 0
    CHECK(mapper.cpu_read(0xA000U) == 0U); // reg 1 -> page 0
}

TEST_CASE("korean_msx_mapper banks the four windows in register order 0/1/2/3") {
    auto rom = make_rom();
    rom[5U * korean_msx_mapper::rom_bank_size + 0x10U] = 0x99U; // page 5 marker

    korean_msx_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 3U); // reg 0 -> $8000-$9FFF = page 3
    mapper.cpu_write(0x0001U, 4U); // reg 1 -> $A000-$BFFF = page 4
    mapper.cpu_write(0x0002U, 5U); // reg 2 -> $4000-$5FFF = page 5
    mapper.cpu_write(0x0003U, 6U); // reg 3 -> $6000-$7FFF = page 6

    CHECK(mapper.page(0) == 3U);
    CHECK(mapper.page(1) == 4U);
    CHECK(mapper.page(2) == 5U);
    CHECK(mapper.page(3) == 6U);

    CHECK(mapper.cpu_read(0x8000U) == 3U);
    CHECK(mapper.cpu_read(0xA000U) == 4U);
    CHECK(mapper.cpu_read(0x4000U) == 5U);
    CHECK(mapper.cpu_read(0x4010U) == 0x99U); // in-page offset preserved
    CHECK(mapper.cpu_read(0x6000U) == 6U);

    // The fixed window is unaffected by banking.
    CHECK(mapper.cpu_read(0x0000U) == 0U);
    CHECK(mapper.cpu_read(0x2000U) == 1U);
}

TEST_CASE("korean_msx_mapper ignores writes outside the $0000-$0003 registers") {
    auto rom = make_rom();
    korean_msx_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0004U, 7U); // just past the register file
    mapper.cpu_write(0x8000U, 7U); // ROM read window, not a register
    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 0U);
    CHECK(mapper.page(2) == 0U);
    CHECK(mapper.page(3) == 0U);
}

TEST_CASE("korean_msx_mapper wraps the page index modulo the 8 KiB page count") {
    auto rom = make_rom(8);
    korean_msx_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 8U); // 8 % 8 == page 0
    CHECK(mapper.cpu_read(0x8000U) == 0U);
    mapper.cpu_write(0x0000U, 9U); // 9 % 8 == page 1
    CHECK(mapper.cpu_read(0x8000U) == 1U);
}

TEST_CASE("korean_msx_mapper Nemesis variant maps the last 8 KiB at $0000-$1FFF") {
    auto rom = make_rom(8); // last page index 7
    korean_msx_mapper mapper;
    mapper.set_variant(korean_msx_mapper::variant::nemesis);
    mapper.attach_rom(rom);

    CHECK(mapper.chip_variant() == korean_msx_mapper::variant::nemesis);
    CHECK(mapper.cpu_read(0x0000U) == 7U); // last 8 KiB bank
    CHECK(mapper.cpu_read(0x1FFFU) == 7U);
    CHECK(mapper.cpu_read(0x2000U) == 1U); // $2000-$3FFF stays bank 0's second half
    CHECK(mapper.cpu_read(0x8000U) == 0U); // banked windows unchanged from msx
}

TEST_CASE("korean_msx_mapper round-trips its page registers") {
    auto rom = make_rom();
    korean_msx_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 1U);
    mapper.cpu_write(0x0001U, 2U);
    mapper.cpu_write(0x0002U, 3U);
    mapper.cpu_write(0x0003U, 4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    korean_msx_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page(0) == 1U);
    CHECK(restored.page(1) == 2U);
    CHECK(restored.page(2) == 3U);
    CHECK(restored.page(3) == 4U);
    CHECK(restored.cpu_read(0x8000U) == 1U);
}
