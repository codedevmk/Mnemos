#include "sms_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::sms_mapper;

    // A 4-page (64 KiB) ROM where every byte of page p equals p, with a couple of
    // distinct markers so the in-page offset arithmetic is observable too.
    std::vector<std::uint8_t> make_rom(int pages = 4) {
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(pages) * sms_mapper::rom_page_size);
        for (int p = 0; p < pages; ++p) {
            const auto base = static_cast<std::size_t>(p) * sms_mapper::rom_page_size;
            for (int i = 0; i < sms_mapper::rom_page_size; ++i) {
                rom[base + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
            }
        }
        return rom;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, sms_mapper>);
static_assert(sms_mapper::static_class == mnemos::chips::chip_class::mapper);

TEST_CASE("sms_mapper reports identity and registers under sega.sms_mapper") {
    const sms_mapper mapper;
    const auto md = mapper.metadata();
    CHECK(md.manufacturer == "Sega");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    auto chip = mnemos::chips::create_chip("sega.sms_mapper");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("Mapper"));
}

TEST_CASE("sms_mapper pages the three ROM slots to 0/1/2 by default") {
    auto rom = make_rom();
    sms_mapper mapper;
    mapper.attach_rom(rom);

    CHECK(mapper.cpu_read(0x0000U) == 0U); // first 1 KiB, fixed page 0
    CHECK(mapper.cpu_read(0x0400U) == 0U); // slot 0 -> page 0
    CHECK(mapper.cpu_read(0x4000U) == 1U); // slot 1 -> page 1
    CHECK(mapper.cpu_read(0x8000U) == 2U); // slot 2 -> page 2
}

TEST_CASE("sms_mapper switches pages through the $FFFD-$FFFF registers") {
    auto rom = make_rom();
    // Page 1 marker to confirm the in-page offset is preserved.
    rom[1U * sms_mapper::rom_page_size + 0x0010U] = 0x77U;

    sms_mapper mapper;
    mapper.attach_rom(rom);

    mapper.write_register(0xFFFDU, 3U); // slot 0 -> page 3
    CHECK(mapper.page(0) == 3U);
    CHECK(mapper.cpu_read(0x0400U) == 3U);
    CHECK(mapper.cpu_read(0x0000U) == 0U); // first 1 KiB stays on page 0

    mapper.write_register(0xFFFEU, 1U); // slot 1 -> page 1
    CHECK(mapper.cpu_read(0x4010U) == 0x77U);

    mapper.write_register(0xFFFFU, 0U); // slot 2 -> page 0
    CHECK(mapper.cpu_read(0x8000U) == 0U);
}

TEST_CASE("sms_mapper wraps the page index modulo the ROM page count") {
    auto rom = make_rom(4);
    sms_mapper mapper;
    mapper.attach_rom(rom);

    mapper.write_register(0xFFFDU, 4U); // 4 % 4 == page 0
    CHECK(mapper.cpu_read(0x0400U) == 0U);
    mapper.write_register(0xFFFDU, 5U); // 5 % 4 == page 1
    CHECK(mapper.cpu_read(0x0400U) == 1U);
}

TEST_CASE("sms_mapper maps cart RAM into slot 2 when enabled") {
    auto rom = make_rom();
    sms_mapper mapper;
    mapper.attach_rom(rom);

    CHECK_FALSE(mapper.cart_ram_enabled());
    mapper.cpu_write(0x8000U, 0x12U); // ignored: slot 2 is ROM here
    CHECK(mapper.cpu_read(0x8000U) == 2U);

    mapper.write_register(0xFFFCU, sms_mapper::ram_enable_bit); // enable cart RAM
    REQUIRE(mapper.cart_ram_enabled());
    mapper.cpu_write(0x8000U, 0x55U);
    CHECK(mapper.cpu_read(0x8000U) == 0x55U);

    // Bank bit selects the second 16 KiB cart-RAM bank.
    mapper.write_register(
        0xFFFCU, static_cast<std::uint8_t>(sms_mapper::ram_enable_bit | sms_mapper::ram_bank_bit));
    mapper.cpu_write(0x8000U, 0xAAU);
    CHECK(mapper.cpu_read(0x8000U) == 0xAAU);

    mapper.write_register(0xFFFCU, sms_mapper::ram_enable_bit); // back to bank 0
    CHECK(mapper.cpu_read(0x8000U) == 0x55U);
}

TEST_CASE("sms_mapper ignores register writes outside $FFFC-$FFFF") {
    sms_mapper mapper;
    mapper.write_register(0xDFFCU, 0xFFU); // the RAM-mirror copy games use as work RAM
    CHECK(mapper.control() == 0U);
    CHECK(mapper.page(0) == 0U);
}

TEST_CASE("sms_mapper returns open bus with no ROM attached") {
    const sms_mapper mapper;
    CHECK(mapper.cpu_read(0x0000U) == 0xFFU);
    CHECK(mapper.cpu_read(0x4000U) == 0xFFU);
    CHECK(mapper.cpu_read(0x8000U) == 0xFFU);
}

TEST_CASE("sms_mapper round-trips its banking + cart RAM state") {
    auto rom = make_rom();
    sms_mapper mapper;
    mapper.attach_rom(rom);

    mapper.write_register(0xFFFCU, sms_mapper::ram_enable_bit);
    mapper.write_register(0xFFFDU, 3U);
    mapper.write_register(0xFFFEU, 2U);
    mapper.write_register(0xFFFFU, 1U);
    mapper.cpu_write(0x9000U, 0xC3U); // cart RAM byte

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    mapper.save_state(writer);

    sms_mapper restored;
    restored.attach_rom(rom);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.control() == sms_mapper::ram_enable_bit);
    CHECK(restored.page(0) == 3U);
    CHECK(restored.page(1) == 2U);
    CHECK(restored.page(2) == 1U);
    CHECK(restored.cpu_read(0x9000U) == 0xC3U);
}
