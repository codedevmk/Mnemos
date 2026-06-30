#include "msx_kanji_rom.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::chip_class;
    using mnemos::chips::create_chip;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::peripheral::msx_kanji_rom;

    [[nodiscard]] std::vector<std::uint8_t> make_kanji_rom() {
        std::vector<std::uint8_t> rom(msx_kanji_rom::level_size * msx_kanji_rom::level_count,
                                      0xFFU);
        const std::size_t level1_char = 0x0123U;
        const std::size_t level2_char = 0x0045U;
        for (std::size_t i = 0; i < msx_kanji_rom::bytes_per_character; ++i) {
            rom[(level1_char * msx_kanji_rom::bytes_per_character) + i] =
                static_cast<std::uint8_t>(0x40U + i);
            rom[msx_kanji_rom::level_size + (level2_char * msx_kanji_rom::bytes_per_character) +
                i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        return rom;
    }
} // namespace

TEST_CASE("msx_kanji_rom registers as a peripheral", "[msx][kanji]") {
    auto chip = create_chip("msx.kanji_rom");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == chip_class::peripheral);
}

TEST_CASE("msx_kanji_rom streams 32-byte level glyphs", "[msx][kanji]") {
    msx_kanji_rom kanji;
    kanji.attach_rom(make_kanji_rom());
    CHECK(kanji.complete_level_count() == 2U);
    CHECK_FALSE(kanji.has_partial_level());
    CHECK(kanji.level_loaded(0U));
    CHECK(kanji.level_loaded(1U));
    CHECK_FALSE(kanji.level_loaded(2U));

    kanji.write_address(0U, false, 0x23U);
    kanji.write_address(0U, true, 0x04U);
    CHECK(kanji.character_address(0U) == 0x0123U);
    CHECK(kanji.read_data(0U) == 0x40U);
    CHECK(kanji.read_data(0U) == 0x41U);
    for (std::size_t i = 0; i < 30U; ++i) {
        (void)kanji.read_data(0U);
    }
    CHECK(kanji.read_data(0U) == 0x40U);

    kanji.write_address(1U, false, 0x05U);
    kanji.write_address(1U, true, 0x01U);
    CHECK(kanji.character_address(1U) == 0x0045U);
    CHECK(kanji.read_data(1U) == 0xA0U);
    CHECK(kanji.read_data(1U) == 0xA1U);
}

TEST_CASE("msx_kanji_rom returns open bus without matching ROM data", "[msx][kanji]") {
    msx_kanji_rom kanji;
    kanji.write_address(0U, false, 0x23U);
    kanji.write_address(0U, true, 0x04U);
    CHECK(kanji.read_data(0U) == 0xFFU);
    CHECK(kanji.complete_level_count() == 0U);
    CHECK_FALSE(kanji.has_partial_level());

    std::vector<std::uint8_t> level1_only(msx_kanji_rom::level_size, 0x00U);
    kanji.attach_rom(level1_only);
    CHECK(kanji.complete_level_count() == 1U);
    CHECK_FALSE(kanji.has_partial_level());
    CHECK(kanji.level_loaded(0U));
    CHECK_FALSE(kanji.level_loaded(1U));
    kanji.write_address(1U, false, 0x01U);
    CHECK(kanji.read_data(1U) == 0xFFU);

    std::vector<std::uint8_t> truncated(msx_kanji_rom::level_size + 16U, 0x00U);
    kanji.attach_rom(truncated);
    CHECK(kanji.complete_level_count() == 1U);
    CHECK(kanji.has_partial_level());
}

TEST_CASE("msx_kanji_rom cursor state round-trips", "[msx][kanji][state]") {
    const auto rom = make_kanji_rom();
    msx_kanji_rom a;
    a.attach_rom(rom);
    a.write_address(0U, false, 0x23U);
    a.write_address(0U, true, 0x04U);
    CHECK(a.read_data(0U) == 0x40U);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    msx_kanji_rom b;
    b.attach_rom(rom);
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    CHECK(b.character_address(0U) == 0x0123U);
    CHECK(b.byte_counter(0U) == 1U);
    CHECK(b.read_data(0U) == 0x41U);
}
