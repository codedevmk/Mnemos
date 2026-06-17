#include "capcom_cps2_system.hpp"

#include "cps2_crypto.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    namespace cps2 = mnemos::manifests::capcom_cps2;

    using mnemos::manifests::capcom_cps2::cps2_board_params;
    using mnemos::manifests::capcom_cps2::cps2_system;
    using mnemos::manifests::capcom_cps2::crypto_key_size;
    using mnemos::manifests::capcom_cps2::encrypt_opcodes;
    using mnemos::manifests::common::rom_set_image;

    std::array<std::uint8_t, crypto_key_size> sample_key() {
        std::array<std::uint8_t, crypto_key_size> k{};
        for (std::size_t i = 0; i < k.size(); ++i) {
            k[i] = static_cast<std::uint8_t>(i * 7U + 3U);
        }
        return k;
    }

    // A tiny 68000 program (big-endian): reset vector (SSP=$00FF0000, PC=$000008)
    // then one caller-selected instruction. Padded to an even length.
    std::vector<std::uint8_t> plain_program(std::uint16_t opcode = 0x707FU) {
        std::vector<std::uint8_t> p(0x40U, 0x00U);
        const auto w16 = [&](std::size_t a, std::uint16_t v) {
            p[a] = static_cast<std::uint8_t>(v >> 8U);
            p[a + 1U] = static_cast<std::uint8_t>(v);
        };
        w16(0x0U, 0x00FFU);
        w16(0x2U, 0x0000U); // SSP = 0x00FF0000
        w16(0x4U, 0x0000U);
        w16(0x6U, 0x0008U); // PC = 0x00000008
        w16(0x8U, opcode);
        return p;
    }

    // The encrypted program a real CPS-2 board ships (encrypt the plaintext with
    // the board key); the machine must decrypt it back to boot.
    std::vector<std::uint8_t> encrypted_program(const std::array<std::uint8_t, crypto_key_size>& k,
                                                std::uint16_t opcode = 0x707FU) {
        const std::vector<std::uint8_t> plain = plain_program(opcode);
        mnemos::manifests::capcom_cps2::cps2_crypto_key key{};
        REQUIRE(decode_key(k, key));
        std::vector<std::uint8_t> enc(plain.size());
        REQUIRE(encrypt_opcodes(plain, enc, key));
        return enc;
    }

} // namespace

TEST_CASE("cps2 system boots the 68000 from the decrypted opcode image", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    REQUIRE(sys.executable());

    auto r = sys.cpu().cpu_registers();
    CHECK(r.pc == 0x00000008U);   // reset PC from the decrypted vector
    CHECK(r.a[7] == 0x00FF0000U); // SSP from the decrypted vector

    // Data reads see the encrypted ROM; opcode fetches see the decrypted image.
    CHECK(sys.bus().read16_be(0x0008U) != 0x707FU);
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) == 0x707FU);

    sys.run_cycles(4); // executes the decrypted MOVEQ #$7F,D0
    r = sys.cpu().cpu_registers();
    CHECK((r.d[0] & 0xFFU) == 0x7FU);
}

TEST_CASE("cps2 system without a key is a non-executable blocker", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    // No key supplied: the program stays encrypted and the board is not runnable.
    cps2_system sys(std::move(image), cps2_board_params{});
    CHECK_FALSE(sys.executable());
    // The opcode overlay is the raw encrypted bytes, so a fetch != the plaintext.
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) != 0x707FU);
}

TEST_CASE("cps2 system reads the board key from a 'key' set region", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["key"].assign(k.begin(), k.end());

    cps2_system sys(std::move(image), cps2_board_params{}); // key resolved from the region
    REQUIRE(sys.executable());
    CHECK(sys.cpu().cpu_registers().pc == 0x00000008U);
}

TEST_CASE("cps2 system maps RAM, CPS registers, inputs, and the EEPROM port",
          "[capcom_cps2][system]") {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    SECTION("the RAM regions round-trip") {
        // (The QSound comm RAM at $618000 is odd-byte, tested separately.)
        const std::array<std::uint32_t, 5> ram_bases{cps2::main_ram_base, cps2::video_ram_base,
                                                     cps2::object_ram_base, cps2::extra_ram_base,
                                                     cps2::control_reg_base};
        std::uint16_t v = 0x1111U;
        for (const std::uint32_t base : ram_bases) {
            bus.write16_be(base, v);
            CHECK(bus.read16_be(base) == v);
            v = static_cast<std::uint16_t>(v + 0x1111U);
        }
    }

    SECTION("the CPS-A/B register file latches and the legacy mirror aliases it") {
        bus.write16_be(cps2::cps_a_base, 0xABCDU);
        CHECK(bus.read16_be(cps2::cps_a_base) == 0xABCDU);
        CHECK(bus.read16_be(cps2::cps_a_mirror_base) == 0xABCDU); // mirror sees the same latch
        bus.write16_be(cps2::cps_b_mirror_base, 0x1234U);
        CHECK(bus.read16_be(cps2::cps_b_base) == 0x1234U); // primary sees the mirror's write
        CHECK(sys.video().cps_b_reg(0U) == 0x1234U);
    }

    SECTION("the input ports read the active-low controls + QSound volume status") {
        sys.input0 = 0x4321U;
        sys.input1 = 0x8765U;
        CHECK(bus.read16_be(0x804000U) == 0x4321U);
        CHECK(bus.read16_be(0x804010U) == 0x8765U);
        CHECK(bus.read16_be(0x804030U) == cps2::qsound_volume_status);
    }

    SECTION("the EEPROM port is wired: data-out on input 2 bit 0, pins on $804040") {
        // CS low resets the serial state; an idle 93C46 drives DO high.
        bus.write8(0x804040U, 0x00U);
        CHECK((bus.read16_be(0x804020U) & 0x0001U) == 0x0001U);
        CHECK(((bus.read16_be(0x804020U) & 0x0001U) != 0U) == sys.eeprom().data_out());
    }
}

TEST_CASE("cps2 system decodes CPS-A latches into video while QSound advances",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping
    image.regions["gfx"].assign(0x10000U, 0xFFU);

    // A tiny Z80 sound program: write $24 to shared comm RAM, then spin. The
    // frame loop must run it when the 68K releases sound reset.
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> z80_prog{0x3EU, 0x24U,        // LD A,$24
                                               0x32U, 0x00U, 0xC0U, // LD ($C000),A
                                               0x18U, 0xFEU};       // JR $ (spin)
    std::copy(z80_prog.begin(), z80_prog.end(), audio.begin());
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    // The CPS-2 backdrop is the last palette entry (pal_num 0xBF, pen 0xF). Stage
    // the palette source in video RAM, write a full-red backdrop there (the 16-bit
    // brightness:R:G:B word 0xFF00 = full red), and point the CPS-A reg5 palette
    // base at it. cps2_video DMAs + decodes it at vblank.
    constexpr std::uint32_t palette_source = 0x20000U;
    constexpr std::uint32_t backdrop = 0xBFFU * 2U;
    bus.write16_be(cps2::video_ram_base + palette_source + backdrop, 0xFF00U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));
    bus.write8(cps2::sound_reset_port, 0x08U); // release sound CPU

    REQUIRE(sys.video().frame_index() == 0U);
    REQUIRE(sys.vblank_irq_raised() == 0U);

    sys.run_frame();

    CHECK(sys.video().frame_index() == 1U);
    CHECK(sys.vblank_irq_raised() == 1U);
    CHECK(sys.vblank_irq_acked() == 0U); // reset SR keeps IPM at 7 in this tiny program
    CHECK(sys.sound_bus().read8(0xC000U) == 0x24U);
    // The backdrop decodes through the CPS-A reg5 palette latch and reaches the
    // framebuffer end-to-end (the scroll layers + sprites render in later increments).
    const auto fb = sys.video().framebuffer();
    CHECK(fb.pixels[0] == 0xFF0000U);
    CHECK(fb.pixels[120U * fb.width + 200U] == 0xFF0000U);
}

TEST_CASE("cps2 system QSound: shared comm RAM, Z80 boot, reset gating", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    // A tiny Z80 sound program at $0000: write $42 to the comm RAM ($C000), loop.
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> z80_prog{0x3EU, 0x42U,        // LD A,$42
                                               0x32U, 0x00U, 0xC0U, // LD ($C000),A
                                               0x18U, 0xFEU};       // JR $ (spin)
    std::copy(z80_prog.begin(), z80_prog.end(), audio.begin());
    image.regions["qsound"].assign(0x1000U, 0x00U); // DL-1425 sample ROM

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    REQUIRE(sys.has_sound());
    auto& m68k = sys.bus();
    auto& z80 = sys.sound_bus();

    // The comm RAM is shared: the 68K sees the buffer on the ODD byte of $618000.
    z80.write8(0xC000U, 0x5AU);
    CHECK(m68k.read8(0x618001U) == 0x5AU); // 68K odd byte, index 0
    CHECK(m68k.read8(0x618000U) == 0xFFU); // even byte is open bus
    m68k.write8(0x618003U, 0xA5U);         // 68K writes index 1 (odd of $618002)
    CHECK(z80.read8(0xC001U) == 0xA5U);    // Z80 sees it flat

    // Held in reset, the Z80 does not run; once $804041 bit3 is set, it boots and
    // executes its program, writing $42 into the comm RAM.
    sys.run_cycles(100);
    CHECK(z80.read8(0xC000U) == 0x5AU); // unchanged: Z80 still in reset
    m68k.write8(0x804041U, 0x08U);      // release the sound-CPU reset
    sys.run_cycles(200);
    CHECK(z80.read8(0xC000U) == 0x42U);    // the Z80 ran its program
    CHECK(m68k.read8(0x618001U) == 0x42U); // and the 68K sees it

    // The DL-1425 ready flag is readable at $D007 (deterministic; not the scratch
    // RAM that backs the rest of the $D000 window).
    CHECK(z80.read8(0xD007U) == z80.read8(0xD007U));
}

TEST_CASE("cps2 gfx unshuffle de-interleaves 8-byte units recursively", "[capcom_cps2][gfx]") {
    // Eight 8-byte units, unit i tagged with the value i in every byte. The
    // recursive unshuffle is an inverse perfect-shuffle: the result orders the
    // units [0,2,4,6,1,3,5,7] (evens then odds, computed by hand from the
    // reference algorithm).
    std::vector<std::uint8_t> buf(8U * 8U, 0U);
    for (std::uint8_t unit = 0U; unit < 8U; ++unit) {
        std::fill_n(buf.begin() + unit * 8U, 8U, unit);
    }

    cps2_system::unshuffle_gfx_units(std::span<std::uint8_t>(buf));

    const std::array<std::uint8_t, 8> expected{0U, 2U, 4U, 6U, 1U, 3U, 5U, 7U};
    for (std::size_t pos = 0U; pos < expected.size(); ++pos) {
        // Every byte of the unit carries its tag, so checking the first suffices.
        CHECK(buf[pos * 8U] == expected[pos]);
    }

    // A run of two units (or any count not a multiple of four) is already ordered.
    std::vector<std::uint8_t> small{1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U};
    const std::vector<std::uint8_t> before = small;
    cps2_system::unshuffle_gfx_units(std::span<std::uint8_t>(small));
    CHECK(small == before);
}
