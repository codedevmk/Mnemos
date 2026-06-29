#include "msx_cartridge_mapper.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {
    using mnemos::manifests::common::detect_msx_cartridge_mapper;
    using mnemos::manifests::common::mirror_msx_konami_address;
    using mnemos::manifests::common::msx_ascii16_bank_register;
    using mnemos::manifests::common::msx_cartridge_mapper_kind;
    using mnemos::manifests::common::msx_cartridge_mapper_label;
    using mnemos::manifests::common::msx_mapper_has_scc_audio;
    using mnemos::manifests::common::msx_mapper_is_korean;
    using mnemos::manifests::common::msx_mapper_sram_size;
    using mnemos::manifests::common::msx_plain_32k_lower_handoff_required;
    using mnemos::manifests::common::msx_plain_32k_lower_rom_window;
    using mnemos::manifests::common::msx_plain_32k_upper_rom_window;
    using mnemos::manifests::common::msx_plain_rom_physical_offset;
    using mnemos::manifests::common::msx_plain_rom_payload_offset;
    using mnemos::manifests::common::parse_msx_cartridge_mapper_name;
    using mnemos::manifests::common::resolve_msx_cartridge_mapper;

    void add_signature(std::vector<std::uint8_t>& rom, std::size_t offset,
                       std::uint16_t address) {
        REQUIRE(offset + 2U < rom.size());
        rom[offset] = 0x32U; // LD (nn),A
        rom[offset + 1U] = static_cast<std::uint8_t>(address & 0xFFU);
        rom[offset + 2U] = static_cast<std::uint8_t>(address >> 8U);
    }

    std::vector<std::uint8_t> large_cart() {
        return std::vector<std::uint8_t>(0x2000U * 8U, 0x00U);
    }

    std::vector<std::uint8_t> known_korean_msx_crc_cart_with_misleading_payload_writes() {
        std::vector<std::uint8_t> rom(0x20000U, 0x00U);
        add_signature(rom, 0x9000U, 0x6000U);
        add_signature(rom, 0x9010U, 0x8000U);
        add_signature(rom, 0x9020U, 0xA000U);
        rom[rom.size() - 4U] = 0xA8U;
        rom[rom.size() - 3U] = 0x61U;
        rom[rom.size() - 2U] = 0x3FU;
        rom[rom.size() - 1U] = 0x60U;
        return rom;
    }

    std::vector<std::uint8_t> known_ascii8_crc_cart_with_misleading_konami_writes() {
        std::vector<std::uint8_t> rom(0x20000U, 0x00U);
        rom[0] = 'A';
        rom[1] = 'B';
        rom[2] = 0x1BU;
        rom[3] = 0x40U;
        add_signature(rom, 0x0060U, 0x7000U);
        add_signature(rom, 0x0100U, 0x8000U);
        rom[rom.size() - 4U] = 0xE5U;
        rom[rom.size() - 3U] = 0x70U;
        rom[rom.size() - 2U] = 0x8EU;
        rom[rom.size() - 1U] = 0x5FU;
        return rom;
    }

    std::vector<std::uint8_t> known_aliens_crc_cart_with_ascii16_converter_marker() {
        std::vector<std::uint8_t> rom(0x20000U, 0x00U);
        rom[0] = 'A';
        rom[1] = 'B';
        rom[2] = 'T';
        rom[3] = '@';
        constexpr std::string_view marker = "[cas2rom64ks]";
        std::copy(marker.begin(), marker.end(), rom.begin() + 0x10U);
        add_signature(rom, 0x00BCU, 0x6000U);
        add_signature(rom, 0x00CCU, 0x6000U);
        add_signature(rom, 0x00D0U, 0x7000U);
        add_signature(rom, 0x012CU, 0x6000U);

        add_signature(rom, 0x9000U, 0x6000U);
        add_signature(rom, 0x9010U, 0x7000U);
        add_signature(rom, 0x9020U, 0x5000U);
        add_signature(rom, 0x9030U, 0x9000U);
        add_signature(rom, 0x9040U, 0xB000U);
        rom[rom.size() - 4U] = 0xD3U;
        rom[rom.size() - 3U] = 0xE3U;
        rom[rom.size() - 2U] = 0xA7U;
        rom[rom.size() - 1U] = 0xEBU;
        return rom;
    }

    std::vector<std::uint8_t> known_3d_pool_cas2rom64ks_crc_cart() {
        std::vector<std::uint8_t> rom(0x20000U, 0x00U);
        rom[0] = 'A';
        rom[1] = 'B';
        constexpr std::string_view marker = "[cas2rom64ks]";
        std::copy(marker.begin(), marker.end(), rom.begin() + 0x10U);
        constexpr std::string_view kabish = "KABISH";
        std::copy(kabish.begin(), kabish.end(), rom.begin() + 0x155U);
        rom[rom.size() - 4U] = 0x8EU;
        rom[rom.size() - 3U] = 0x9FU;
        rom[rom.size() - 2U] = 0x08U;
        rom[rom.size() - 1U] = 0xA2U;
        return rom;
    }

    std::vector<std::uint8_t> jam_ascii16_converter_cart_with_misleading_loader_bytes() {
        std::vector<std::uint8_t> rom(0x10000U, 0x00U);
        rom[0] = 'A';
        rom[1] = 'B';
        rom[2] = 0x10U;
        rom[3] = 0x40U;
        constexpr std::string_view marker = "Adapted by JAM";
        std::copy(marker.begin(), marker.end(), rom.begin() + 0x31U);
        add_signature(rom, 0x0066U, 0x6000U);
        add_signature(rom, 0x0075U, 0x7000U);
        add_signature(rom, 0x0180U, 0x8000U);
        return rom;
    }

    std::vector<std::uint8_t> kabish_cas2rom64ks_ascii8_cart() {
        std::vector<std::uint8_t> rom(0x20000U, 0x00U);
        rom[0] = 'A';
        rom[1] = 'B';
        constexpr std::string_view marker = "[cas2rom64ks]";
        std::copy(marker.begin(), marker.end(), rom.begin() + 0x10U);
        constexpr std::string_view kabish = "KABISH";
        std::copy(kabish.begin(), kabish.end(), rom.begin() + 0x560U);
        add_signature(rom, 0x0060U, 0x6000U);
        add_signature(rom, 0x0070U, 0x7000U);
        return rom;
    }

    std::vector<std::uint8_t> padded_plain_cart_with_header_at_payload(std::size_t size) {
        std::vector<std::uint8_t> rom(size, 0x00U);
        rom[0] = 0x4BU;
        rom[1] = 0xFCU;
        rom[0x4000U] = 'A';
        rom[0x4001U] = 'B';
        rom[0x4002U] = 0x34U;
        rom[0x4003U] = 0x12U;
        return rom;
    }
} // namespace

TEST_CASE("msx cartridge mapper detection classifies shared MSX mapper signatures",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> plain(0x8000U, 0x00U);
    CHECK(detect_msx_cartridge_mapper(plain) == msx_cartridge_mapper_kind::plain);

    auto ascii8 = large_cart();
    add_signature(ascii8, 0x0100U, 0x6000U);
    add_signature(ascii8, 0x0110U, 0x6800U);
    add_signature(ascii8, 0x0120U, 0x7000U);
    CHECK(detect_msx_cartridge_mapper(ascii8) == msx_cartridge_mapper_kind::ascii8);

    auto ascii16 = large_cart();
    add_signature(ascii16, 0x0100U, 0x6000U);
    add_signature(ascii16, 0x0110U, 0x7000U);
    CHECK(detect_msx_cartridge_mapper(ascii16) == msx_cartridge_mapper_kind::ascii16);

    auto generic8 = large_cart();
    add_signature(generic8, 0x0100U, 0x4000U);
    add_signature(generic8, 0x0110U, 0x6000U);
    add_signature(generic8, 0x0120U, 0x8000U);
    add_signature(generic8, 0x0130U, 0xA000U);
    CHECK(detect_msx_cartridge_mapper(generic8) == msx_cartridge_mapper_kind::generic8);

    auto konami = large_cart();
    add_signature(konami, 0x0100U, 0x6000U);
    add_signature(konami, 0x0110U, 0x8000U);
    add_signature(konami, 0x0120U, 0xA000U);
    CHECK(detect_msx_cartridge_mapper(konami) == msx_cartridge_mapper_kind::konami);

    auto konami_scc = large_cart();
    add_signature(konami_scc, 0x0100U, 0x5000U);
    add_signature(konami_scc, 0x0110U, 0x7000U);
    add_signature(konami_scc, 0x0120U, 0x9000U);
    add_signature(konami_scc, 0x0130U, 0xB000U);
    CHECK(detect_msx_cartridge_mapper(konami_scc) == msx_cartridge_mapper_kind::konami_scc);
}

TEST_CASE("msx cartridge mapper detection prefers boot loader writes over payload bytes",
          "[manifests][common][msx][mapper]") {
    auto cas2rom_ascii16 = std::vector<std::uint8_t>(0x20000U, 0x00U);
    cas2rom_ascii16[0] = 'A';
    cas2rom_ascii16[1] = 'B';
    cas2rom_ascii16[2] = 'T';
    cas2rom_ascii16[3] = '@';
    constexpr std::string_view marker = "[cas2rom64ks]";
    std::copy(marker.begin(), marker.end(), cas2rom_ascii16.begin() + 0x10);
    add_signature(cas2rom_ascii16, 0x00BCU, 0x6000U);
    add_signature(cas2rom_ascii16, 0x00CCU, 0x6000U);
    add_signature(cas2rom_ascii16, 0x00D0U, 0x7000U);
    add_signature(cas2rom_ascii16, 0x012CU, 0x6000U);

    add_signature(cas2rom_ascii16, 0x9000U, 0x5000U);
    add_signature(cas2rom_ascii16, 0x9010U, 0x9000U);
    add_signature(cas2rom_ascii16, 0x9020U, 0xB000U);

    CHECK(detect_msx_cartridge_mapper(cas2rom_ascii16) == msx_cartridge_mapper_kind::ascii16);
}

TEST_CASE("msx cartridge mapper detection recognizes JAM ASCII16 converter stubs",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom =
        jam_ascii16_converter_cart_with_misleading_loader_bytes();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii16);
}

TEST_CASE("msx cartridge mapper detection recognizes Kabish CAS2ROM64KS ASCII8 conversions",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom = kabish_cas2rom64ks_ascii8_cart();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii8);
}

TEST_CASE("msx cartridge mapper detection prefers strong ASCII8 loader writes over "
          "self-modifying code hits",
          "[manifests][common][msx][mapper]") {
    std::vector<std::uint8_t> rom(0x40000U, 0x00U);
    rom[0] = 'A';
    rom[1] = 'B';
    rom[2] = 0xAEU;
    rom[3] = 0x40U;

    add_signature(rom, 0x020U, 0x7000U);
    add_signature(rom, 0x040U, 0x7800U);
    add_signature(rom, 0x060U, 0x6800U);
    add_signature(rom, 0x0ABU, 0x40C1U);

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii8);
}

TEST_CASE("msx cartridge mapper detection recognizes padded plain ROM payloads",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom = padded_plain_cart_with_header_at_payload(0x10000U);
    const std::vector<std::uint8_t> compact = padded_plain_cart_with_header_at_payload(0xC000U);

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::plain);
    CHECK(detect_msx_cartridge_mapper(compact) == msx_cartridge_mapper_kind::plain);
    CHECK(msx_plain_rom_payload_offset(rom) == 0x4000U);
    CHECK(msx_plain_rom_payload_offset(compact) == 0x4000U);
    CHECK(msx_plain_rom_physical_offset(compact, 0x4000U) == 0x4000U);
    CHECK(msx_plain_rom_physical_offset(compact, 0x7FFFU) == 0x7FFFU);
    CHECK(msx_plain_rom_payload_offset(std::vector<std::uint8_t>(0x8000U, 0x00U)) == 0U);
}

TEST_CASE("msx plain ROM physical offsets mirror 16 KiB payloads",
          "[manifests][common][msx][mapper]") {
    std::vector<std::uint8_t> rom(0x4000U, 0x00U);
    rom[0] = 'A';
    rom[1] = 'B';
    rom[2] = 0x35U;
    rom[3] = 0xBBU;
    const std::vector<std::uint8_t> larger(0x8000U, 0x00U);

    CHECK(msx_plain_rom_physical_offset(rom, 0x4000U) == 0x0000U);
    CHECK(msx_plain_rom_physical_offset(rom, 0x7FFFU) == 0x3FFFU);
    CHECK(msx_plain_rom_physical_offset(rom, 0x8000U) == 0x0000U);
    CHECK(msx_plain_rom_physical_offset(rom, 0xBFFFU) == 0x3FFFU);
    CHECK(msx_plain_rom_physical_offset(larger, 0x8000U) == 0x4000U);
}

TEST_CASE("msx plain ROM physical offsets keep lower-page 16 KiB payloads off page 2",
          "[manifests][common][msx][mapper]") {
    std::vector<std::uint8_t> rom(0x4000U, 0x00U);
    rom[0] = 'A';
    rom[1] = 'B';
    rom[2] = 0x0FU;
    rom[3] = 0x40U;

    CHECK(msx_plain_rom_physical_offset(rom, 0x4000U) == 0x0000U);
    CHECK(msx_plain_rom_physical_offset(rom, 0x7FFFU) == 0x3FFFU);
    CHECK_FALSE(msx_plain_rom_physical_offset(rom, 0x8000U) < rom.size());
    CHECK(msx_plain_rom_physical_offset(rom, 0x8000U, true) == 0x0000U);
    CHECK(msx_plain_rom_physical_offset(rom, 0xBFFFU, true) == 0x3FFFU);
}

TEST_CASE("msx cartridge mapper detection uses known Korean MSX CRC before payload signatures",
          "[manifests][common][msx][mapper][korean]") {
    const std::vector<std::uint8_t> rom =
        known_korean_msx_crc_cart_with_misleading_payload_writes();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::korean_msx);
}

TEST_CASE("msx cartridge mapper detection uses known ASCII8 CRC before Konami signatures",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom =
        known_ascii8_crc_cart_with_misleading_konami_writes();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii8);
}

TEST_CASE("msx cartridge mapper detection uses known Aliens CRC before converter signatures",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom =
        known_aliens_crc_cart_with_ascii16_converter_marker();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii8);
}

TEST_CASE("msx cartridge mapper detection uses known 3D Pool CAS2ROM64KS CRC",
          "[manifests][common][msx][mapper]") {
    const std::vector<std::uint8_t> rom = known_3d_pool_cas2rom64ks_crc_cart();

    CHECK(detect_msx_cartridge_mapper(rom) == msx_cartridge_mapper_kind::ascii16);
}

TEST_CASE("msx cartridge mapper helper resolves overrides, aliases and metadata",
          "[manifests][common][msx][mapper]") {
    CHECK(resolve_msx_cartridge_mapper(msx_cartridge_mapper_kind::automatic, large_cart()) ==
          msx_cartridge_mapper_kind::plain);
    CHECK(resolve_msx_cartridge_mapper(msx_cartridge_mapper_kind::ascii8, large_cart()) ==
          msx_cartridge_mapper_kind::ascii8);

    CHECK(parse_msx_cartridge_mapper_name("ASCII8_SRAM8") ==
          msx_cartridge_mapper_kind::ascii8_sram8);
    CHECK(parse_msx_cartridge_mapper_name("konamiscc") ==
          msx_cartridge_mapper_kind::konami_scc);
    CHECK(parse_msx_cartridge_mapper_name("konami4") == msx_cartridge_mapper_kind::konami);
    CHECK(parse_msx_cartridge_mapper_name("konami-generic") ==
          msx_cartridge_mapper_kind::generic8);
    CHECK(parse_msx_cartridge_mapper_name("nemesis") ==
          msx_cartridge_mapper_kind::korean_msx_nemesis);
    CHECK(parse_msx_cartridge_mapper_name("unknown") == msx_cartridge_mapper_kind::automatic);

    CHECK(msx_cartridge_mapper_label(msx_cartridge_mapper_kind::ascii16_sram2) ==
          "ASCII16 SRAM");
    CHECK(msx_cartridge_mapper_label(msx_cartridge_mapper_kind::generic8) == "Generic8");
    CHECK(msx_mapper_sram_size(msx_cartridge_mapper_kind::ascii8_sram8) == 0x2000U);
    CHECK(msx_mapper_sram_size(msx_cartridge_mapper_kind::ascii16_sram2) == 0x0800U);
    CHECK(msx_mapper_has_scc_audio(msx_cartridge_mapper_kind::konami_scc));
    CHECK(msx_mapper_is_korean(msx_cartridge_mapper_kind::korean_msx));
}

TEST_CASE("msx plain 32 KiB helper matches only the fixed ROM windows",
          "[manifests][common][msx][mapper]") {
    CHECK(msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x4000U));
    CHECK(msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x7FFFU));
    CHECK(msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x8000U));
    CHECK(msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0xBFFFU));

    CHECK_FALSE(
        msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x3FFFU));
    CHECK_FALSE(
        msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x8000U));
    CHECK_FALSE(
        msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x4000U, 0x4000U));
    CHECK_FALSE(
        msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::plain, 0x10000U, 0x4000U));
    CHECK_FALSE(
        msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind::ascii8, 0x8000U, 0x4000U));
    CHECK_FALSE(
        msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0x7FFFU));
    CHECK_FALSE(
        msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x8000U, 0xC000U));
    CHECK_FALSE(
        msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x4000U, 0x8000U));
    CHECK_FALSE(
        msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::plain, 0x10000U, 0x8000U));
    CHECK_FALSE(
        msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind::ascii8, 0x8000U, 0x8000U));
}

TEST_CASE("msx plain 32 KiB lower handoff is data-driven",
          "[manifests][common][msx][mapper]") {
    std::vector<std::uint8_t> rom(0x8000U, 0xFFU);
    CHECK_FALSE(msx_plain_32k_lower_handoff_required(rom));
    rom[0] = 0x41U;
    rom[1] = 0x42U;
    CHECK_FALSE(msx_plain_32k_lower_handoff_required(rom));
}

TEST_CASE("msx ASCII16 helper decodes only the two hardware bank-register windows",
          "[manifests][common][msx][mapper]") {
    CHECK(msx_ascii16_bank_register(0x6000U) == 0U);
    CHECK(msx_ascii16_bank_register(0x67FFU) == 0U);
    CHECK(msx_ascii16_bank_register(0x6800U) == 0xFFU);
    CHECK(msx_ascii16_bank_register(0x6FFFU) == 0xFFU);

    CHECK(msx_ascii16_bank_register(0x7000U) == 1U);
    CHECK(msx_ascii16_bank_register(0x77FFU) == 1U);
    CHECK(msx_ascii16_bank_register(0x7800U) == 0xFFU);
    CHECK(msx_ascii16_bank_register(0x7FFFU) == 0xFFU);
}

TEST_CASE("msx Konami mapper mirror helper covers page 0 and page 3 aliases",
          "[manifests][common][msx][mapper]") {
    CHECK(mirror_msx_konami_address(0x0123U) == 0x8123U);
    CHECK(mirror_msx_konami_address(0x2345U) == 0xA345U);
    CHECK(mirror_msx_konami_address(0xC456U) == 0x4456U);
    CHECK(mirror_msx_konami_address(0xE789U) == 0x6789U);
    CHECK(mirror_msx_konami_address(0x8000U) == 0x8000U);
}
