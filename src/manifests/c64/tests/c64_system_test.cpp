#include <mnemos/manifests/c64/c64_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::reset_kind;
    using mnemos::manifests::c64::assemble_c64;

    // Distinct fill bytes so a read tells us which overlay answered.
    constexpr std::uint8_t basic_fill = 0xAAU;   // $A000-$BFFF
    constexpr std::uint8_t kernal_fill = 0xBBU;  // $E000-$FFFF
    constexpr std::uint8_t chargen_fill = 0xCCU; // $D000-$DFFF

    auto make_c64() {
        return assemble_c64(std::vector<std::uint8_t>(0x2000U, basic_fill),
                            std::vector<std::uint8_t>(0x2000U, kernal_fill),
                            std::vector<std::uint8_t>(0x1000U, chargen_fill));
    }

} // namespace

TEST_CASE("assemble_c64 banks BASIC, KERNAL, and I/O by default", "[c64][banking]") {
    auto sys = make_c64();
    sys->cpu.reset(reset_kind::power_on);

    // After power-on the $00/$01 port DDR is all-input, so $01 reads $FF:
    // LORAM=HIRAM=CHAREN=1 — the standard power-up banking.
    REQUIRE(sys->cpu.read(0x0001U) == 0xFFU);
    CHECK(sys->bus.read8(0xA000U) == basic_fill);  // BASIC ROM
    CHECK(sys->bus.read8(0xE000U) == kernal_fill); // KERNAL ROM
}

TEST_CASE("the 6510 $01 port drives PLA banking", "[c64][banking]") {
    auto sys = make_c64();
    auto& cpu = sys->cpu;
    auto& bus = sys->bus;
    cpu.reset(reset_kind::power_on);

    // Make the three banking bits outputs so $01 writes take effect.
    cpu.write(0x0000U, 0x2FU);

    SECTION("all bits low maps RAM everywhere") {
        cpu.write(0x0001U, 0x30U); // LORAM=HIRAM=CHAREN=0
        bus.write8(0xA000U, 0x12U);
        bus.write8(0xE000U, 0x34U);
        CHECK(bus.read8(0xA000U) == 0x12U); // underlying RAM, not BASIC
        CHECK(bus.read8(0xE000U) == 0x34U); // underlying RAM, not KERNAL
    }

    SECTION("CHAREN low exposes the character ROM at $D000") {
        cpu.write(0x0001U, 0x33U); // LORAM=HIRAM=1, CHAREN=0
        CHECK(bus.read8(0xD000U) == chargen_fill);
        CHECK(bus.read8(0xA000U) == basic_fill);  // BASIC still banked in
        CHECK(bus.read8(0xE000U) == kernal_fill); // KERNAL still banked in
    }

    SECTION("CHAREN high exposes the I/O space at $D000") {
        cpu.write(0x0001U, 0x37U); // LORAM=HIRAM=CHAREN=1

        // VIC background-colour register ($D021) round-trips through the chip
        // rather than returning the char-ROM fill byte.
        bus.write8(0xD021U, 0x0EU);
        CHECK(bus.read8(0xD021U) == 0x0EU);

        // Colour RAM is plain RAM living inside the I/O window ($D800-$DBFF).
        bus.write8(0xD800U, 0x0AU);
        CHECK(bus.read8(0xD800U) == 0x0AU);
    }
}
