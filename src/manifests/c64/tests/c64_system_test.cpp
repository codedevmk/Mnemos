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

TEST_CASE("assemble_c64 wires the VIC raster IRQ into the 6510", "[c64][irq]") {
    auto sys = make_c64();
    auto& cpu = sys->cpu;
    auto& vic = sys->vic;
    cpu.reset(reset_kind::power_on);

    // All-RAM banking so we control the IRQ vector at $FFFE/$FFFF.
    cpu.write(0x0000U, 0x2FU);
    cpu.write(0x0001U, 0x30U);

    // Main loop JMP $1000; IRQ handler JMP $4000; IRQ vector -> $4000.
    sys->ram[0x1000] = 0x4CU;
    sys->ram[0x1001] = 0x00U;
    sys->ram[0x1002] = 0x10U;
    sys->ram[0x4000] = 0x4CU;
    sys->ram[0x4001] = 0x00U;
    sys->ram[0x4002] = 0x40U;
    sys->ram[0xFFFE] = 0x00U;
    sys->ram[0xFFFF] = 0x40U;

    mnemos::chips::cpu::m6510::registers regs{};
    regs.pc = 0x1000U;
    regs.sp = 0xFFU;
    regs.p = 0x20U; // I flag clear: IRQs enabled
    cpu.set_registers(regs);

    vic.write(0x1AU, 0x01U); // enable raster IRQ
    vic.write(0x12U, 0x05U); // compare at line 5

    for (int i = 0; i < 4000 && cpu.cpu_registers().pc != 0x4000U; ++i) {
        vic.tick(1U);
        cpu.tick(1U);
    }

    const auto& r = cpu.cpu_registers();
    CHECK(r.pc == 0x4000U);     // vectored into the IRQ handler
    CHECK((r.p & 0x04U) != 0U); // I set while servicing
    CHECK(r.sp == 0xFCU);       // pushed PCH, PCL, P
}

TEST_CASE("assemble_c64 tracks the VIC bank from CIA2 port A", "[c64][vic]") {
    auto sys = make_c64();
    CHECK(sys->vic.bank() == 0U); // power-up default (port A floats high)

    sys->cia2.write(0x02U, 0x03U); // DDRA: bits 0-1 are outputs
    sys->cia2.write(0x00U, 0x02U); // PRA = %10 -> bank = ~%10 & 3 = 1
    CHECK(sys->vic.bank() == 1U);

    sys->cia2.write(0x00U, 0x00U); // PRA = %00 -> bank 3
    CHECK(sys->vic.bank() == 3U);
}

TEST_CASE("c64_input resolves the keyboard matrix and joysticks", "[c64][input]") {
    using key = mnemos::manifests::c64::c64_input::key;
    mnemos::manifests::c64::c64_input input;

    input.press(key::a); // column 1, row 2
    // Column 1 selected (its strobe bit low) reveals row 2 low on PRB.
    CHECK(input.read_rows(0xFDU) == 0xFBU); // ~(1<<1) strobe -> bit2 clear
    CHECK(input.read_rows(0xFFU) == 0xFFU); // no column selected -> nothing
    input.release(key::a);
    CHECK(input.read_rows(0xFDU) == 0xFFU);

    // Joystick 2 (PRA bits 0-4) overlays the column read.
    input.set_joystick(2U, mnemos::manifests::c64::c64_input::joy_fire |
                               mnemos::manifests::c64::c64_input::joy_left);
    CHECK(input.read_columns(0xFFU) == 0xEBU); // ~(0x10 | 0x04)
}

TEST_CASE("assemble_c64 reads the keyboard through CIA1", "[c64][input]") {
    using key = mnemos::manifests::c64::c64_input::key;
    auto sys = make_c64();
    sys->input.press(key::a); // column 1, row 2

    // Drive PRA as the column strobe, read the rows on PRB.
    sys->cia1.write(0x02U, 0xFFU);         // DDRA = output (columns)
    sys->cia1.write(0x03U, 0x00U);         // DDRB = input (rows)
    sys->cia1.write(0x00U, 0xFDU);         // PRA: pull column 1 low
    CHECK(sys->cia1.read(0x01U) == 0xFBU); // PRB row 2 reads low

    sys->cia1.write(0x00U, 0xFBU);         // select a different column (2)
    CHECK(sys->cia1.read(0x01U) == 0xFFU); // key not in that column
}
