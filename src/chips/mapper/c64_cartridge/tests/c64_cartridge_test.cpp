#include "c64_cartridge.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::mapper::c64_cartridge;

    struct chip_packet final {
        std::uint16_t bank;
        std::uint16_t load;
        std::vector<std::uint8_t> data;
    };

    void push_be16(std::vector<std::uint8_t>& v, std::uint16_t x) {
        v.push_back(static_cast<std::uint8_t>(x >> 8U));
        v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    }
    void push_be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(static_cast<std::uint8_t>(x >> 24U));
        v.push_back(static_cast<std::uint8_t>((x >> 16U) & 0xFFU));
        v.push_back(static_cast<std::uint8_t>((x >> 8U) & 0xFFU));
        v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    }

    // Build a .crt: hw type, EXROM/GAME header bytes (0 = line asserted), chips.
    std::vector<std::uint8_t> build_crt(std::uint16_t hw, std::uint8_t exrom, std::uint8_t game,
                                        const std::vector<chip_packet>& chips) {
        std::vector<std::uint8_t> crt;
        const std::string_view magic = "C64 CARTRIDGE   ";
        crt.insert(crt.end(), magic.begin(), magic.end());
        push_be32(crt, 0x40U);   // header length
        push_be16(crt, 0x0100U); // version
        push_be16(crt, hw);
        crt.push_back(exrom);
        crt.push_back(game);
        crt.insert(crt.end(), 6U, 0U);  // reserved
        crt.insert(crt.end(), 32U, 0U); // name
        for (const chip_packet& c : chips) {
            const std::string_view cm = "CHIP";
            crt.insert(crt.end(), cm.begin(), cm.end());
            push_be32(crt, static_cast<std::uint32_t>(0x10U + c.data.size()));
            push_be16(crt, 0U); // ROM
            push_be16(crt, c.bank);
            push_be16(crt, c.load);
            push_be16(crt, static_cast<std::uint16_t>(c.data.size()));
            crt.insert(crt.end(), c.data.begin(), c.data.end());
        }
        return crt;
    }

    std::vector<std::uint8_t> filled(std::size_t n, std::uint8_t value) {
        return std::vector<std::uint8_t>(n, value);
    }

} // namespace

TEST_CASE("c64_cartridge registers and rejects junk") {
    REQUIRE(mnemos::chips::find_factory("commodore.cartridge") != nullptr);
    REQUIRE(mnemos::chips::create_chip("commodore.cartridge") != nullptr);
    c64_cartridge cart;
    CHECK_FALSE(cart.load_crt(std::vector<std::uint8_t>(10U, 0U)));
    CHECK_FALSE(cart.inserted());
}

TEST_CASE("c64_cartridge loads a generic 8K cart and sets the lines") {
    std::vector<std::uint8_t> roml(0x2000U);
    for (std::size_t i = 0; i < roml.size(); ++i) {
        roml[i] = static_cast<std::uint8_t>(i & 0xFFU);
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(0U, 0U, 1U, {{0U, 0x8000U, roml}}))); // EXROM low, GAME high
    CHECK(cart.inserted());
    CHECK(cart.type() == c64_cartridge::hardware::generic);
    CHECK_FALSE(cart.exrom()); // 8K: /EXROM asserted
    CHECK(cart.game());        // /GAME released
    CHECK(cart.read_roml(0x0007U) == 0x07U);
    CHECK(cart.read_roml(0x0100U) == 0x00U);
}

TEST_CASE("c64_cartridge loads a 16K cart into ROML + ROMH") {
    std::vector<std::uint8_t> rom(0x4000U);
    rom[0x0000U] = 0xA1U; // ROML byte 0
    rom[0x2000U] = 0xB2U; // ROMH byte 0
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(0U, 0U, 0U, {{0U, 0x8000U, rom}}))); // both lines low
    CHECK_FALSE(cart.exrom());
    CHECK_FALSE(cart.game());
    CHECK(cart.read_roml(0U) == 0xA1U);
    CHECK(cart.read_romh(0U) == 0xB2U);
}

TEST_CASE("c64_cartridge Ocean banks via $DE00") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 4U; ++b) {
        chips.push_back({b, 0x8000U, filled(0x2000U, static_cast<std::uint8_t>(b))});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(5U, 0U, 1U, chips)));
    CHECK(cart.type() == c64_cartridge::hardware::ocean);
    CHECK(cart.bank_count() == 4U);
    CHECK(cart.read_roml(0U) == 0U);
    cart.mmio_write(0x00U, 2U);
    CHECK(cart.read_roml(0U) == 2U);
    cart.mmio_write(0x00U, 5U); // 5 % 4 = bank 1
    CHECK(cart.read_roml(0U) == 1U);
}

TEST_CASE("c64_cartridge System 3 / C64GS banks on the I/O-1 write address") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 4U; ++b) {
        chips.push_back({b, 0x8000U, filled(0x2000U, static_cast<std::uint8_t>(0x10U + b))});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(15U, 0U, 1U, chips))); // 8K mode: EXROM low, GAME high
    CHECK(cart.type() == c64_cartridge::hardware::system_3);
    CHECK(cart.bank_count() == 4U);
    CHECK(cart.read_roml(0U) == 0x10U); // power-on bank 0
    cart.mmio_write(0x03U, 0x00U);      // write $DE03 selects bank 3 (value ignored)
    CHECK(cart.bank() == 3U);
    CHECK(cart.read_roml(0U) == 0x13U);
    cart.mmio_write(0x06U, 0xFFU); // $DE06 -> 6 % 4 = bank 2
    CHECK(cart.read_roml(0U) == 0x12U);
}

TEST_CASE("c64_cartridge Dinamic banks on the I/O-1 read address") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 8U; ++b) {
        chips.push_back({b, 0x8000U, filled(0x2000U, static_cast<std::uint8_t>(0x20U + b))});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(17U, 0U, 1U, chips)));
    CHECK(cart.type() == c64_cartridge::hardware::dinamic);
    CHECK(cart.read_roml(0U) == 0x20U);    // power-on bank 0
    CHECK(cart.mmio_read(0x05U) == 0xFFU); // read $DE05 selects bank 5, bus floats high
    CHECK(cart.bank() == 5U);
    CHECK(cart.read_roml(0U) == 0x25U);
    (void)cart.mmio_read(0x00U); // $DE00 returns to bank 0
    CHECK(cart.read_roml(0U) == 0x20U);
}

TEST_CASE("c64_cartridge Fun Play scrambles the bank and releases on $86") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 16U; ++b) {
        chips.push_back({b, 0x8000U, filled(0x2000U, static_cast<std::uint8_t>(b))});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(7U, 0U, 1U, chips))); // 8K mode: EXROM low, GAME high
    CHECK(cart.type() == c64_cartridge::hardware::fun_play);
    CHECK(cart.bank_count() == 16U);
    CHECK(cart.read_roml(0U) == 0U);
    cart.mmio_write(0x00U, 0x08U); // ((8>>3)&7)|((8&1)<<3) = 1
    CHECK(cart.bank() == 1U);
    CHECK(cart.read_roml(0U) == 1U);
    cart.mmio_write(0x00U, 0x01U); // ((1>>3)&7)|((1&1)<<3) = 8
    CHECK(cart.bank() == 8U);
    CHECK(cart.read_roml(0U) == 8U);
    cart.mmio_write(0x00U, 0x86U); // ROMs off: both lines released
    CHECK(cart.exrom());
    CHECK(cart.game());
    cart.mmio_write(0x00U, 0x00U); // back to 8K config, bank 0
    CHECK_FALSE(cart.exrom());
    CHECK(cart.game());
    CHECK(cart.bank() == 0U);
}

TEST_CASE("c64_cartridge Super Games banks 16K via $DF00 and disables on bit 2") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 4U; ++b) {
        std::vector<std::uint8_t> rom(0x4000U, static_cast<std::uint8_t>(0x40U + b));
        rom[0x2000U] = static_cast<std::uint8_t>(0x50U + b); // ROMH byte 0
        chips.push_back({b, 0x8000U, rom});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(8U, 0U, 0U, chips))); // 16K mode: both lines low
    CHECK(cart.type() == c64_cartridge::hardware::super_games);
    CHECK(cart.bank_count() == 4U);
    CHECK(cart.read_roml(0U) == 0x40U);
    CHECK(cart.read_romh(0U) == 0x50U);
    cart.mmio_write(0x100U, 0x02U); // $DF00: bank 2, bit 2 clear (16K config)
    CHECK(cart.bank() == 2U);
    CHECK(cart.read_roml(0U) == 0x42U);
    CHECK(cart.read_romh(0U) == 0x52U);
    CHECK_FALSE(cart.exrom());
    CHECK_FALSE(cart.game());
    cart.mmio_write(0x100U, 0x04U); // bit 2 set: cartridge released
    CHECK(cart.exrom());
    CHECK(cart.game());
}

TEST_CASE("c64_cartridge Comal-80 banks 16K on $DE00 values $80-$83") {
    std::vector<chip_packet> chips;
    for (std::uint16_t b = 0; b < 4U; ++b) {
        std::vector<std::uint8_t> rom(0x4000U, static_cast<std::uint8_t>(0x60U + b));
        rom[0x2000U] = static_cast<std::uint8_t>(0x70U + b);
        chips.push_back({b, 0x8000U, rom});
    }
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(21U, 0U, 0U, chips))); // 16K mode
    CHECK(cart.type() == c64_cartridge::hardware::comal_80);
    CHECK(cart.read_roml(0U) == 0x60U);
    CHECK(cart.read_romh(0U) == 0x70U);
    cart.mmio_write(0x00U, 0x83U); // bit 7 validates, bank 3
    CHECK(cart.bank() == 3U);
    CHECK(cart.read_roml(0U) == 0x63U);
    CHECK(cart.read_romh(0U) == 0x73U);
    cart.mmio_write(0x00U, 0x02U); // bit 7 clear: ignored, bank unchanged
    CHECK(cart.bank() == 3U);
}

TEST_CASE("c64_cartridge Magic Desk disables via bit 7") {
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(19U, 0U, 1U, {{0U, 0x8000U, filled(0x2000U, 0x42U)}})));
    CHECK(cart.read_roml(0U) == 0x42U);
    cart.mmio_write(0x00U, 0x80U); // disable
    CHECK(cart.read_roml(0U) == 0xFFU);
    CHECK(cart.exrom());           // released -> RAM visible
    cart.mmio_write(0x00U, 0x00U); // re-enable
    CHECK(cart.read_roml(0U) == 0x42U);
    CHECK_FALSE(cart.exrom());
}

TEST_CASE("c64_cartridge EasyFlash control register and RAM") {
    std::vector<chip_packet> chips = {{0U, 0x8000U, filled(0x2000U, 0xC0U)},
                                      {0U, 0xA000U, filled(0x2000U, 0xD0U)}};
    c64_cartridge cart;
    REQUIRE(cart.load_crt(build_crt(32U, 0U, 0U, chips)));
    CHECK(cart.type() == c64_cartridge::hardware::easyflash);
    CHECK(cart.read_roml(0U) == 0xC0U);
    CHECK(cart.read_romh(0U) == 0xD0U);

    // $DF00 256-byte RAM.
    cart.mmio_write(0x100U, 0xABU);
    CHECK(cart.mmio_read(0x100U) == 0xABU);

    // $DE02 control: bit1 -> /EXROM, bit2 = GAME mode, bit0 = GAME state.
    cart.mmio_write(0x02U, 0x07U); // mode + GAME=1, EXROM bit set
    CHECK(cart.game());
    CHECK_FALSE(cart.exrom());
    cart.mmio_write(0x02U, 0x04U); // mode + GAME=0
    CHECK_FALSE(cart.game());
}

TEST_CASE("c64_cartridge save/load round-trips") {
    c64_cartridge a;
    REQUIRE(a.load_crt(build_crt(
        5U, 0U, 1U, {{0U, 0x8000U, filled(0x2000U, 0U)}, {1U, 0x8000U, filled(0x2000U, 1U)}})));
    a.mmio_write(0x00U, 1U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    c64_cartridge b;
    REQUIRE(b.load_crt(build_crt(
        5U, 0U, 1U, {{0U, 0x8000U, filled(0x2000U, 0U)}, {1U, 0x8000U, filled(0x2000U, 1U)}})));
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());
    CHECK(b.bank() == 1U);

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
