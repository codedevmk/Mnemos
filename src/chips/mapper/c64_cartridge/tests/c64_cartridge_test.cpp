#include <mnemos/chips/mapper/c64_cartridge.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

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
