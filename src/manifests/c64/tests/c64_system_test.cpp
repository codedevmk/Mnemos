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

    // After power-on the port DDR is all-input: bits 0-5 read the pull-up (high),
    // and the unconnected bits 6,7 read their floating-gate charge (0). So $01
    // reads $3F with LORAM=HIRAM=CHAREN=1 — the standard power-up banking.
    REQUIRE(sys->cpu.read(0x0001U) == 0x3FU);
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

TEST_CASE("assemble_c64 routes the paddle mux to the SID POTs", "[c64][input]") {
    auto sys = make_c64();
    sys->input.set_paddle(1U, 0x11U, 0x22U); // control port 1
    sys->input.set_paddle(2U, 0x33U, 0x44U); // control port 2

    sys->cia1.write(0x02U, 0xC0U); // DDRA: bits 6,7 output (mux select)

    sys->cia1.write(0x00U, 0x80U);        // PRA bit 7 -> control port 1
    CHECK(sys->sid.read(0x19U) == 0x11U); // POTX = port-1 X
    CHECK(sys->sid.read(0x1AU) == 0x22U); // POTY = port-1 Y

    sys->cia1.write(0x00U, 0x40U); // PRA bit 6 -> control port 2
    CHECK(sys->sid.read(0x19U) == 0x33U);
    CHECK(sys->sid.read(0x1AU) == 0x44U);
}

TEST_CASE("assemble_c64 maps an inserted cartridge's ROML at $8000", "[c64][cart]") {
    auto sys = make_c64();

    // Minimal 8K generic .crt: EXROM low (asserted), GAME high; ROML[0] = 0x4C.
    std::vector<std::uint8_t> crt;
    const char* magic = "C64 CARTRIDGE   ";
    crt.insert(crt.end(), magic, magic + 16);
    const auto be32 = [&](std::uint32_t x) {
        crt.push_back(static_cast<std::uint8_t>(x >> 24U));
        crt.push_back(static_cast<std::uint8_t>((x >> 16U) & 0xFFU));
        crt.push_back(static_cast<std::uint8_t>((x >> 8U) & 0xFFU));
        crt.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    };
    const auto be16 = [&](std::uint16_t x) {
        crt.push_back(static_cast<std::uint8_t>(x >> 8U));
        crt.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    };
    be32(0x40U);
    be16(0x0100U);
    be16(0U);          // generic
    crt.push_back(0U); // EXROM low
    crt.push_back(1U); // GAME high
    crt.insert(crt.end(), 6U + 32U, 0U);
    const char* cm = "CHIP";
    crt.insert(crt.end(), cm, cm + 4);
    be32(0x10U + 0x2000U);
    be16(0U);
    be16(0U);
    be16(0x8000U);
    be16(0x2000U);
    std::vector<std::uint8_t> rom(0x2000U, 0U);
    rom[0] = 0x4CU;
    crt.insert(crt.end(), rom.begin(), rom.end());

    REQUIRE(sys->cart.load_crt(crt));
    sys->cpu.reset(reset_kind::power_on);

    CHECK(sys->bus.read8(0x8000U) == 0x4CU); // cartridge ROML, decoded by the PLA
    CHECK(sys->bus.read8(0x8001U) == 0x00U);
}

TEST_CASE("assemble_c64 honours the NTSC region config", "[c64][region]") {
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_config;
    auto sys = assemble_c64(
        std::vector<std::uint8_t>(0x2000U, 0U), std::vector<std::uint8_t>(0x2000U, 0U),
        std::vector<std::uint8_t>(0x1000U, 0U), {.video_region = c64_config::region::ntsc});
    CHECK_FALSE(sys->vic.is_pal());
    CHECK(sys->vic.cycles_per_line() == 65U);
    CHECK(sys->vic.total_lines() == 263U);
}

TEST_CASE("assemble_c64 selects the SID variant", "[c64][sid]") {
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_config;
    using variant = mnemos::chips::audio::sid_6581::variant;
    auto sys =
        assemble_c64(std::vector<std::uint8_t>(0x2000U, 0U), std::vector<std::uint8_t>(0x2000U, 0U),
                     std::vector<std::uint8_t>(0x1000U, 0U), {.sid_variant = variant::mos_8580});
    CHECK(sys->sid.chip_variant() == variant::mos_8580);
    CHECK(sys->sid2.chip_variant() == variant::mos_8580);
}

TEST_CASE("assemble_c64 maps a second SID at $D420 when dual", "[c64][sid]") {
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_config;
    auto sys =
        assemble_c64(std::vector<std::uint8_t>(0x2000U, 0U), std::vector<std::uint8_t>(0x2000U, 0U),
                     std::vector<std::uint8_t>(0x1000U, 0U), {.dual_sid = true});
    sys->cpu.reset(reset_kind::power_on); // $01 = $FF -> I/O visible

    // Distinct voice-1 frequencies routed to each SID via the bus.
    sys->bus.write8(0xD400U, 0x00U); // SID 1 freq lo
    sys->bus.write8(0xD401U, 0x10U); // SID 1 freq hi -> $1000
    sys->bus.write8(0xD420U, 0x00U); // SID 2 freq lo
    sys->bus.write8(0xD421U, 0x20U); // SID 2 freq hi -> $2000
    sys->sid.tick(1U);
    sys->sid2.tick(1U);
    CHECK(sys->sid.voice_phase(0U) == 0x1000U);
    CHECK(sys->sid2.voice_phase(0U) == 0x2000U); // $D420 reached SID 2, not SID 1
}

TEST_CASE("assemble_c64 maps the REU at $DF00 and DMAs through the bus", "[c64][reu]") {
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_config;
    auto sys =
        assemble_c64(std::vector<std::uint8_t>(0x2000U, 0U), std::vector<std::uint8_t>(0x2000U, 0U),
                     std::vector<std::uint8_t>(0x1000U, 0U), {.reu = true});
    sys->cpu.reset(reset_kind::power_on); // $01 = $3F -> I/O (so $DF00) visible

    // A marker byte in screen RAM, then stash one byte C64 $0400 -> REU $000000.
    sys->bus.write8(0x0400U, 0x5AU);
    sys->bus.write8(0xDF02U, 0x00U); // C64 address lo
    sys->bus.write8(0xDF03U, 0x04U); // C64 address hi -> $0400
    sys->bus.write8(0xDF04U, 0x00U); // REU address lo
    sys->bus.write8(0xDF05U, 0x00U); // REU address mid
    sys->bus.write8(0xDF06U, 0x00U); // REU address bank
    sys->bus.write8(0xDF07U, 0x01U); // length lo
    sys->bus.write8(0xDF08U, 0x00U); // length hi -> 1 byte
    sys->bus.write8(0xDF01U, 0x90U); // command: execute | stash (type 0)
    CHECK(sys->reu_unit.peek(0U) == 0x5AU);

    // Clobber the C64 byte, then fetch it back from the REU (type 1).
    sys->bus.write8(0x0400U, 0x00U);
    sys->bus.write8(0xDF02U, 0x00U);
    sys->bus.write8(0xDF03U, 0x04U);
    sys->bus.write8(0xDF04U, 0x00U);
    sys->bus.write8(0xDF05U, 0x00U);
    sys->bus.write8(0xDF06U, 0x00U);
    sys->bus.write8(0xDF07U, 0x01U);
    sys->bus.write8(0xDF08U, 0x00U);
    sys->bus.write8(0xDF01U, 0x91U); // command: execute | fetch (type 1)
    CHECK(sys->bus.read8(0x0400U) == 0x5AU);
}

TEST_CASE("assemble_c64 leaves $DF00 unmapped without --reu", "[c64][reu]") {
    auto sys = make_c64();
    sys->cpu.reset(reset_kind::power_on);
    // No REU and no cartridge: $DF00 is open I/O, not the REU status register.
    CHECK(sys->reu_unit.ram_size() == 128U * 1024U); // member default, never resized
    sys->bus.write8(0xDF01U, 0x90U);                 // would trigger a DMA if it were mapped
    CHECK(sys->reu_unit.peek(0U) == 0x00U);          // nothing was stashed
}

TEST_CASE("assemble_c64 returns the VIC floating-bus byte for open I/O-2", "[c64][openbus]") {
    auto sys = make_c64();
    sys->cpu.reset(reset_kind::power_on); // $01 = $3F -> I/O ($DE00-$DFFF) visible

    // Distinct bytes in the RAM that physically underlies $DE00 / $DF00.
    sys->ram[0xDE00] = 0x42U;
    sys->ram[0xDF00] = 0x99U;

    // No cartridge, no REU: an open expansion-port read returns the VIC's last
    // fetch (0xFF before any render), not the PLA-deselected RAM underneath.
    CHECK(sys->bus.read8(0xDE00U) == sys->vic.last_fetched_byte());
    CHECK(sys->bus.read8(0xDF00U) == sys->vic.last_fetched_byte());
    CHECK(sys->bus.read8(0xDE00U) == 0xFFU); // latch default
    CHECK(sys->bus.read8(0xDE00U) != 0x42U); // RAM is shadowed by the open I/O window
    CHECK(sys->bus.read8(0xDF00U) != 0x99U);
}

TEST_CASE("assemble_c64 open I/O-2 mirrors the live VIC fetch latch", "[c64][openbus]") {
    auto sys = make_c64();
    sys->ram.fill(0xAAU);                 // everything the VIC fetches off the bus
    sys->cpu.reset(reset_kind::power_on); // $01 = $3F -> I/O visible
    sys->vic.write(0x11U, 0x1BU);         // DEN=1: arm the display fetches
    sys->vic.write(0x18U, 0x14U);         // screen $0400, char gen $1000
    sys->vic.tick(63U * 312U);            // a full PAL frame drives the fetches

    REQUIRE(sys->vic.last_fetched_byte() != 0xFFU); // the latch went live
    CHECK(sys->bus.read8(0xDE00U) == sys->vic.last_fetched_byte());
    CHECK(sys->bus.read8(0xDF00U) == sys->vic.last_fetched_byte());
}

TEST_CASE("assemble_c64 open I/O-2 yields to the cartridge and REU", "[c64][openbus]") {
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_config;

    SECTION("an inserted cartridge drives I/O-2 (0xFF) over the open bus") {
        auto sys = make_c64();
        // Minimal 8K generic .crt (EXROM low, GAME high) so cart.inserted() is true.
        std::vector<std::uint8_t> crt;
        const char* magic = "C64 CARTRIDGE   ";
        crt.insert(crt.end(), magic, magic + 16);
        const auto be32 = [&](std::uint32_t x) {
            crt.push_back(static_cast<std::uint8_t>(x >> 24U));
            crt.push_back(static_cast<std::uint8_t>((x >> 16U) & 0xFFU));
            crt.push_back(static_cast<std::uint8_t>((x >> 8U) & 0xFFU));
            crt.push_back(static_cast<std::uint8_t>(x & 0xFFU));
        };
        const auto be16 = [&](std::uint16_t x) {
            crt.push_back(static_cast<std::uint8_t>(x >> 8U));
            crt.push_back(static_cast<std::uint8_t>(x & 0xFFU));
        };
        be32(0x40U);
        be16(0x0100U);
        be16(0U);
        crt.push_back(0U);
        crt.push_back(1U);
        crt.insert(crt.end(), 6U + 32U, 0U);
        const char* cm = "CHIP";
        crt.insert(crt.end(), cm, cm + 4);
        be32(0x10U + 0x2000U);
        be16(0U);
        be16(0U);
        be16(0x8000U);
        be16(0x2000U);
        crt.insert(crt.end(), 0x2000U, 0U);
        REQUIRE(sys->cart.load_crt(crt));
        sys->cpu.reset(reset_kind::power_on);

        // The cartridge claims the whole $DE00-$DFFF window; a generic cart that
        // does not decode the address answers 0xFF, not the floating-bus byte.
        CHECK(sys->bus.read8(0xDF00U) == 0xFFU);
    }

    SECTION("the REU drives I/O-2 over the open bus") {
        auto sys = assemble_c64(std::vector<std::uint8_t>(0x2000U, 0U),
                                std::vector<std::uint8_t>(0x2000U, 0U),
                                std::vector<std::uint8_t>(0x1000U, 0U), {.reu = true});
        sys->cpu.reset(reset_kind::power_on);
        // $DF00 is the REU status register, which reports the size bit (512K -> 0x10),
        // proving the REU answers $DF00 ahead of the open-bus overlay.
        CHECK(sys->bus.read8(0xDF00U) == 0x10U);
    }
}

TEST_CASE("assemble_c64 drives the cassette sense from the datasette", "[c64][tape]") {
    auto sys = make_c64();
    sys->cpu.reset(reset_kind::power_on); // DDR all input -> $01 bit 4 reads the pin

    sys->tape.set_play(true);
    CHECK((sys->cpu.read(0x0001U) & 0x10U) == 0U); // PLAY held -> sense low
    sys->tape.set_play(false);
    CHECK((sys->cpu.read(0x0001U) & 0x10U) != 0U); // released -> sense high
}
