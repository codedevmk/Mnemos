#include "multi_4x8k_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::multi_4x8k_mapper;

    // An N-page (8 KiB each) ROM where every byte of page p equals p.
    std::vector<std::uint8_t> make_rom(int pages = 32) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) *
                                      multi_4x8k_mapper::rom_bank_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * multi_4x8k_mapper::rom_bank_size;
            for (int i = 0; i < multi_4x8k_mapper::rom_bank_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, multi_4x8k_mapper>);

TEST_CASE("multi_4x8k_mapper reports identity and registers under korean.multi_4x8k_mapper") {
    const multi_4x8k_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Korean");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("korean.multi_4x8k_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("multi_4x8k_mapper powers on with a fixed first 16 KiB and zeroed windows") {
    auto rom = make_rom();
    multi_4x8k_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 0U);
    CHECK(mapper.page(2) == 0U);
    CHECK(mapper.page(3) == 0U);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // fixed: bank 0
    CHECK(mapper.cpu_read(0x2000U) == 1U); // fixed: bank 1
    CHECK(mapper.cpu_read(0x4000U) == 0U); // windows power on at bank 0
    CHECK(mapper.cpu_read(0x6000U) == 0U);
    CHECK(mapper.cpu_read(0x8000U) == 0U);
    CHECK(mapper.cpu_read(0xA000U) == 0U);
}

TEST_CASE("multi_4x8k_mapper banks all four windows from one XOR-masked $2000 write") {
    auto rom = make_rom();
    multi_4x8k_mapper mapper;
    mapper.attach_rom(rom);

    // value 0 -> windows = 0 ^ {0x1F, 0x1E, 0x1D, 0x1C}.
    mapper.cpu_write(0x2000U, 0x00U);
    CHECK(mapper.page(2) == 0x1FU); // $4000-$5FFF
    CHECK(mapper.page(3) == 0x1EU); // $6000-$7FFF
    CHECK(mapper.page(0) == 0x1DU); // $8000-$9FFF
    CHECK(mapper.page(1) == 0x1CU); // $A000-$BFFF

    CHECK(mapper.cpu_read(0x4000U) == 0x1FU);
    CHECK(mapper.cpu_read(0x6000U) == 0x1EU);
    CHECK(mapper.cpu_read(0x8000U) == 0x1DU);
    CHECK(mapper.cpu_read(0xA000U) == 0x1CU);

    // The fixed first 16 KiB is unaffected.
    CHECK(mapper.cpu_read(0x0000U) == 0U);
    CHECK(mapper.cpu_read(0x2000U) == 1U);

    // A different value shifts every window by the same XOR.
    mapper.cpu_write(0x2000U, 0x03U);
    CHECK(mapper.page(2) == 0x1CU); // 3 ^ 0x1F
    CHECK(mapper.cpu_read(0x4000U) == 0x1CU);
    CHECK(mapper.cpu_read(0x8000U) == 0x1EU); // 3 ^ 0x1D
}

TEST_CASE("multi_4x8k_mapper ignores writes outside the $2000 register") {
    auto rom = make_rom();
    multi_4x8k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x0000U, 7U); // fixed region, not the register
    mapper.cpu_write(0x4000U, 7U); // ROM read window, not the register
    mapper.cpu_write(0xC000U, 7U); // work-RAM area, not the register
    CHECK(mapper.page(0) == 0U);
    CHECK(mapper.page(1) == 0U);
    CHECK(mapper.page(2) == 0U);
    CHECK(mapper.page(3) == 0U);
}

TEST_CASE("multi_4x8k_mapper wraps the bank index modulo the 8 KiB page count") {
    auto rom = make_rom(4); // banks wrap mod 4
    multi_4x8k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x2000U, 0x00U);         // $4000 window -> bank 0x1F (31)
    CHECK(mapper.cpu_read(0x4000U) == 0x03U); // 31 % 4 == page 3
}

TEST_CASE("multi_4x8k_mapper round-trips its bank registers") {
    auto rom = make_rom();
    multi_4x8k_mapper mapper;
    mapper.attach_rom(rom);

    mapper.cpu_write(0x2000U, 0x05U); // windows = 5 ^ {0x1F,0x1E,0x1D,0x1C}

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    multi_4x8k_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.page(2) == 0x1AU); // 5 ^ 0x1F
    CHECK(restored.page(3) == 0x1BU); // 5 ^ 0x1E
    CHECK(restored.page(0) == 0x18U); // 5 ^ 0x1D
    CHECK(restored.page(1) == 0x19U); // 5 ^ 0x1C
    CHECK(restored.cpu_read(0x8000U) == 0x18U);
}
