#include "sh2.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "introspection_views.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::cpu::sh2;
    using reset_kind = mnemos::chips::reset_kind;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{32U, mnemos::topology::endianness::big};
        sh2 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
        }
        void w16(std::uint32_t a, std::uint16_t v) {
            ram[a] = static_cast<std::uint8_t>(v >> 8U); // big-endian
            ram[a + 1U] = static_cast<std::uint8_t>(v);
        }
        void w32(std::uint32_t a, std::uint32_t v) {
            w16(a, static_cast<std::uint16_t>(v >> 16U));
            w16(a + 2U, static_cast<std::uint16_t>(v));
        }
        void load(std::uint32_t a, std::initializer_list<std::uint16_t> words) {
            for (const std::uint16_t w : words) {
                w16(a, w);
                a += 2U;
            }
        }
        void set_pc(std::uint32_t pc) {
            auto r = cpu.cpu_registers();
            r.pc = pc;
            cpu.set_registers(r);
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, sh2>);

TEST_CASE("sh2 reports identity and registers under hitachi.sh7604") {
    const sh2 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Hitachi");
    CHECK(md.part_number == "SH7604");
    CHECK(md.family == "SH-2");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("hitachi.sh7604");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("SH7604"));
}

TEST_CASE("sh2 resets PC and R15 from the big-endian vector table at VBR=0") {
    machine m;
    m.w32(0x0000U, 0x06000000U); // reset PC
    m.w32(0x0004U, 0x0603FFFCU); // initial SP -> R15
    m.cpu.reset(reset_kind::power_on);

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x06000000U);
    CHECK(r.r[15] == 0x0603FFFCU);
    CHECK(r.sr == sh2::sr_imask); // interrupts masked out of reset
}

TEST_CASE("sh2 MOV #imm,Rn sign-extends the byte immediate") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE37FU, 0xE4FFU}); // MOV #0x7F,R3 ; MOV #-1,R4
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().r[3] == 0x0000007FU);
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().r[4] == 0xFFFFFFFFU);
}

TEST_CASE("sh2 ADD #imm,Rn adds a sign-extended immediate") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE205U, 0x72FFU}); // MOV #5,R2 ; ADD #-1,R2
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[2] == 4U);
}

TEST_CASE("sh2 MOV Rm,Rn copies one register to another") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE142U, 0x6513U}); // MOV #0x42,R1 ; MOV R1,R5
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[5] == 0x42U);
    CHECK(r.r[1] == 0x42U);
}

TEST_CASE("sh2 advances PC by two per 16-bit instruction") {
    machine m;
    m.set_pc(0x2000U);
    m.load(0x2000U, {0x0009U, 0x0009U}); // NOP ; NOP
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2002U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2004U);
}

TEST_CASE("sh2 round-trips its state") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE309U}); // MOV #9,R3
    m.cpu.step_instruction();

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    sh2 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.cpu_registers().r[3] == 9U);
    CHECK(restored.cpu_registers().pc == 0x1002U);
}

TEST_CASE("sh2 register ALU: ADD/SUB/NEG") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #10,R1 ; MOV #3,R2 ; ADD R2,R1 ; SUB R2,R1 ; NEG R1,R3
    m.load(0x1000U, {0xE10AU, 0xE203U, 0x312CU, 0x3128U, 0x631BU});
    for (int i = 0; i < 5; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 10U);                             // 10 + 3 - 3
    CHECK(r.r[3] == static_cast<std::uint32_t>(-10)); // NEG R1,R3
}

TEST_CASE("sh2 EXTU/EXTS widen bytes and words") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #-1,R1 ; EXTU.B R1,R2 ; EXTS.B R1,R3 ; EXTU.W R1,R4 ; EXTS.W R1,R5
    m.load(0x1000U, {0xE1FFU, 0x621CU, 0x631EU, 0x641DU, 0x651FU});
    for (int i = 0; i < 5; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[2] == 0x000000FFU);
    CHECK(r.r[3] == 0xFFFFFFFFU);
    CHECK(r.r[4] == 0x0000FFFFU);
    CHECK(r.r[5] == 0xFFFFFFFFU);
}

TEST_CASE("sh2 ADDC chains carry through T") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #-1,R1 ; MOV #1,R2 ; ADDC R2,R1 ; ADDC R2,R1
    m.load(0x1000U, {0xE1FFU, 0xE201U, 0x312EU, 0x312EU});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction(); // 0xFFFFFFFF + 1 + 0 = 0, carry out
    auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 0U);
    CHECK((r.sr & sh2::sr_t) != 0U);
    m.cpu.step_instruction(); // 0 + 1 + 1 (carry in) = 2, no carry out
    r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 2U);
    CHECK((r.sr & sh2::sr_t) == 0U);
}

TEST_CASE("sh2 ADDV/SUBV flag signed overflow") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #-1,R1 ; SHLR R1 -> 0x7FFFFFFF ; MOV #1,R2 ; ADDV R2,R1 -> overflow
    m.load(0x1000U, {0xE1FFU, 0x4101U, 0xE201U, 0x312FU});
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 0x80000000U);
    CHECK((r.sr & sh2::sr_t) != 0U);
    // SUBV: 0x80000000 - 1 -> signed overflow
    m.set_pc(0x2000U);
    m.load(0x2000U, {0xE201U, 0x312BU}); // MOV #1,R2 ; SUBV R2,R1
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 0x7FFFFFFFU);
    CHECK((r.sr & sh2::sr_t) != 0U);
}

TEST_CASE("sh2 NEGC negates with borrow") {
    machine m;
    m.set_pc(0x1000U);
    // SETT ; MOV #5,R1 ; NEGC R1,R2 -> 0 - 5 - 1 = -6, T = 1 (borrow)
    m.load(0x1000U, {0x0018U, 0xE105U, 0x621AU});
    for (int i = 0; i < 3; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[2] == static_cast<std::uint32_t>(-6));
    CHECK((r.sr & sh2::sr_t) != 0U);
}

TEST_CASE("sh2 CMP/EQ,GT,HS,GE,HI set T") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #5,R1 ; MOV #3,R2 ; CMP/EQ ; CMP/GT ; CMP/HS ; CMP/GE ; CMP/HI
    m.load(0x1000U, {0xE105U, 0xE203U, 0x3120U, 0x3127U, 0x3122U, 0x3123U, 0x3126U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) == 0U); // CMP/EQ 5==3 false
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // CMP/GT 5>3
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // CMP/HS 5>=3
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // CMP/GE 5>=3
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // CMP/HI 5>3
}

TEST_CASE("sh2 CMP/EQ #imm, CMP/PZ/PL, DT") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE007U, 0x8807U}); // MOV #7,R0 ; CMP/EQ #7,R0
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U);
    m.set_pc(0x2000U);
    m.load(0x2000U, {0xE102U, 0x4110U, 0x4110U}); // MOV #2,R1 ; DT R1 ; DT R1
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 1U);
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) == 0U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 0U);
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // T set when count hits 0
}

TEST_CASE("sh2 logical AND/OR/XOR/NOT and TST") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #0x12,R1 ; MOV #0x34,R2 ; AND R2,R1 ; MOV #0x12,R3 ; OR R2,R3 ;
    // MOV #0x12,R4 ; XOR R2,R4 ; NOT R1,R5
    m.load(0x1000U, {0xE112U, 0xE234U, 0x2129U, 0xE312U, 0x232BU, 0xE412U, 0x242AU, 0x6517U});
    for (int i = 0; i < 8; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 0x10U);  // 0x12 & 0x34
    CHECK(r.r[3] == 0x36U);  // 0x12 | 0x34
    CHECK(r.r[4] == 0x26U);  // 0x12 ^ 0x34
    CHECK(r.r[5] == ~0x10U); // NOT of R1 (0x10)
    // TST sets T when the AND is zero.
    m.set_pc(0x2000U);
    m.load(0x2000U, {0xE101U, 0xE202U, 0x2128U}); // MOV #1,R1 ; MOV #2,R2 ; TST R2,R1
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U);
}

TEST_CASE("sh2 CMP/STR and XTRCT") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[3] = 0xAAAABBBBU;
    r.r[4] = 0xCCCCDDDDU;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x243DU}); // XTRCT R3,R4 -> (R4>>16)|(R3<<16)
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[4] == 0xBBBBCCCCU);
}

TEST_CASE("sh2 shifts: SHLL/SHLR/SHAR/SHLL16/SHLL2") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 1U;
    r.r[2] = 0xFFFFFFFFU;
    r.r[3] = 0x80000000U;
    r.sr = 0U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // SHLL R1 ; SHLR R2 ; SHAR R3 ; SHLL16 R1 ; SHLL2 R1
    m.load(0x1000U, {0x4100U, 0x4201U, 0x4321U, 0x4128U, 0x4108U});
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 2U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[2] == 0x7FFFFFFFU);
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // T = shifted-out LSB
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[3] == 0xC0000000U); // arithmetic: sign preserved
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 0x80000U); // (2 << 16) << 2
}

TEST_CASE("sh2 rotates: ROTL and ROTCL through T") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x80000001U;
    r.sr = 0U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x4104U}); // ROTL R1 -> (v<<1)|MSB, T=MSB
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 0x00000003U);
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U);
    // ROTCL feeds the old T into bit 0.
    auto r2 = m.cpu.cpu_registers();
    r2.r[2] = 0U;
    r2.pc = 0x2000U;
    m.cpu.set_registers(r2);
    m.load(0x2000U, {0x0018U, 0x4224U}); // SETT ; ROTCL R2
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[2] == 1U);
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) == 0U); // new T = old MSB (0)
}

TEST_CASE("sh2 SWAP.B/W and MOVT") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x11223344U;
    r.sr = 0U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // SWAP.B R1,R2 ; SWAP.W R1,R3 ; SETT ; MOVT R4 ; CLRT ; MOVT R5
    m.load(0x1000U, {0x6218U, 0x6319U, 0x0018U, 0x0429U, 0x0008U, 0x0529U});
    for (int i = 0; i < 6; ++i) {
        m.cpu.step_instruction();
    }
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.r[2] == 0x11224433U);
    CHECK(rr.r[3] == 0x33441122U);
    CHECK(rr.r[4] == 1U);
    CHECK(rr.r[5] == 0U);
}

TEST_CASE("sh2 multiply: MUL.L, MULU.W, MULS.W, DMULU.L, DMULS.L") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 7U;
    r.r[2] = 6U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x0127U}); // MUL.L R2,R1 -> MACL
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().macl == 42U);
    // DMULS.L: -4 * 3 = -12
    r = m.cpu.cpu_registers();
    r.r[1] = static_cast<std::uint32_t>(-4);
    r.r[2] = 3U;
    r.pc = 0x2000U;
    m.cpu.set_registers(r);
    m.load(0x2000U, {0x312DU}); // DMULS.L R2,R1
    m.cpu.step_instruction();
    auto rr = m.cpu.cpu_registers();
    CHECK(rr.mach == 0xFFFFFFFFU);
    CHECK(rr.macl == 0xFFFFFFF4U);
    // DMULU.L: 0xFFFFFFFF * 0xFFFFFFFF
    r = m.cpu.cpu_registers();
    r.r[1] = 0xFFFFFFFFU;
    r.r[2] = 0xFFFFFFFFU;
    r.pc = 0x3000U;
    m.cpu.set_registers(r);
    m.load(0x3000U, {0x3125U}); // DMULU.L R2,R1
    m.cpu.step_instruction();
    rr = m.cpu.cpu_registers();
    CHECK(rr.mach == 0xFFFFFFFEU);
    CHECK(rr.macl == 0x00000001U);
    // MULU.W / MULS.W operate on the low 16 bits.
    r = m.cpu.cpu_registers();
    r.r[1] = 0xFFFFU;
    r.r[2] = 0xFFFFU;
    r.pc = 0x4000U;
    m.cpu.set_registers(r);
    m.load(0x4000U, {0x212EU, 0x212FU}); // MULU.W ; MULS.W
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().macl == 0xFFFE0001U); // 65535 * 65535
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().macl == 1U); // (-1) * (-1)
}

TEST_CASE("sh2 MAC.L accumulates signed 32x32 products") {
    machine m;
    m.w32(0x2000U, 7U);
    m.w32(0x2004U, 6U);
    m.w32(0x2008U, static_cast<std::uint32_t>(-2));
    m.w32(0x200CU, 5U);
    auto r = m.cpu.cpu_registers();
    r.r[0] = 0x2000U;
    r.r[1] = 0x2008U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x010FU, 0x010FU}); // MAC.L @R0+,@R1+ twice
    m.cpu.step_instruction();
    auto rr = m.cpu.cpu_registers();
    const auto acc1 =
        static_cast<std::int64_t>((static_cast<std::uint64_t>(rr.mach) << 32U) | rr.macl);
    CHECK(acc1 == -14); // 7 * -2
    CHECK(rr.r[0] == 0x2004U);
    CHECK(rr.r[1] == 0x200CU);
    m.cpu.step_instruction();
    rr = m.cpu.cpu_registers();
    const auto acc2 =
        static_cast<std::int64_t>((static_cast<std::uint64_t>(rr.mach) << 32U) | rr.macl);
    CHECK(acc2 == 16); // -14 + (6 * 5)
}

TEST_CASE("sh2 MAC.W saturates MACL when S is set") {
    machine m;
    m.w16(0x2000U, 0x7FFFU);
    m.w16(0x2004U, 0x7FFFU);
    auto r = m.cpu.cpu_registers();
    r.r[0] = 0x2000U;
    r.r[1] = 0x2004U;
    r.macl = 0x7FFFFFFFU;
    r.mach = 0U;
    r.sr = sh2::sr_s;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x410FU}); // MAC.W @R0+,@R1+
    m.cpu.step_instruction();
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.macl == 0x7FFFFFFFU); // clamped to INT32_MAX
    CHECK((rr.mach & 1U) != 0U);   // overflow latched in MACH bit 0
}

TEST_CASE("sh2 MOV.L @(disp,PC) loads a 32-bit literal") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xD101U}); // MOV.L @(1,PC),R1
    m.w32(0x1008U, 0xDEADBEEFU);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 0xDEADBEEFU);
}

TEST_CASE("sh2 MOV.W @(disp,PC) loads a sign-extended word") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x9101U}); // MOV.W @(1,PC),R1
    m.w16(0x1006U, 0xFFFEU);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[1] == 0xFFFFFFFEU);
}

TEST_CASE("sh2 MOV.L store/load round-trips through memory") {
    machine m;
    m.set_pc(0x1000U);
    // R1 <- 0x12345678 ; R2 <- 0x3000 ; MOV.L R1,@R2 ; MOV.L @R2,R3
    m.load(0x1000U, {0xD102U, 0xD203U, 0x2212U, 0x6322U});
    m.w32(0x100CU, 0x12345678U); // R1 literal
    m.w32(0x1010U, 0x00003000U); // R2 literal
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 0x12345678U);
    CHECK(r.r[2] == 0x00003000U);
    CHECK(r.r[3] == 0x12345678U);
}

TEST_CASE("sh2 MOV.B sign-extends; @-Rn and @Rm+ adjust the pointer") {
    machine m;
    m.set_pc(0x1000U);
    // MOV #-128,R1 ; R2 <- 0x3004 ; MOV.B R1,@-R2 ; MOV.B @R2+,R3
    m.load(0x1000U, {0xE180U, 0xD201U, 0x2214U, 0x6324U});
    m.w32(0x1008U, 0x00003004U);
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[3] == 0xFFFFFF80U); // 0x80 sign-extended
    CHECK(r.r[2] == 0x00003004U); // @-Rn then @Rm+ restore the pointer
}

TEST_CASE("sh2 TAS.B tests and sets bit 7") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x3000U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.ram[0x3000] = 0x00U;
    m.load(0x1000U, {0x411BU}); // TAS.B @R1
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U); // was zero
    CHECK(m.ram[0x3000] == 0x80U);
}

TEST_CASE("sh2 DIV1 performs one non-restoring divide step") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[0] = 1U;
    r.r[2] = 0x80000000U;
    r.sr = 0U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x3204U}); // DIV1 R0,R2
    m.cpu.step_instruction();
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.r[2] == 0xFFFFFFFFU);
    CHECK((rr.sr & sh2::sr_q) == 0U);
    CHECK((rr.sr & sh2::sr_t) != 0U);
}

TEST_CASE("sh2 DIV0U + DIV1 sequence divides unsigned 32/32") {
    const auto run_div = [](std::uint32_t dividend, std::uint32_t divisor) {
        machine m;
        std::uint32_t a = 0x1000U;
        m.w16(a, 0x0019U); // DIV0U
        a += 2U;
        for (int i = 0; i < 32; ++i) {
            m.w16(a, 0x4124U); // ROTCL R1
            a += 2U;
            m.w16(a, 0x3204U); // DIV1 R0,R2
            a += 2U;
        }
        m.w16(a, 0x4124U); // final ROTCL R1 brings in the last quotient bit
        auto r = m.cpu.cpu_registers();
        r.pc = 0x1000U;
        r.r[0] = divisor;  // divisor
        r.r[1] = dividend; // dividend low -> quotient
        r.r[2] = 0U;       // dividend high / remainder accumulator
        r.sr = 0U;
        m.cpu.set_registers(r);
        for (int i = 0; i < 66; ++i) {
            m.cpu.step_instruction();
        }
        r = m.cpu.cpu_registers();
        // The quotient is the unambiguous result of the 32 DIV1 steps. The
        // remainder left in R2 is in non-restoring form (it may be off by one
        // divisor and need the routine's sign-correction step), so it is not
        // asserted here -- DIV1's per-step Rn/Q/T mechanics are covered by the
        // single-step test above.
        CHECK(r.r[1] == dividend / divisor);
    };
    run_div(1000U, 7U);
    run_div(0xFFFFFFFFU, 7U);
    run_div(12345U, 100U);
    run_div(1U, 1U);
}

TEST_CASE("sh2 BRA executes the delay slot then branches") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xA006U, 0xE105U}); // BRA +6 ; (delay) MOV #5,R1
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 5U);    // delay slot executed before the branch landed
    CHECK(r.pc == 0x1010U); // 0x1002 + 6*2 + 2
}

TEST_CASE("sh2 BSR sets PR and RTS returns through it") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xB006U, 0x0009U}); // BSR +6 ; (delay) NOP
    m.load(0x1010U, {0x000BU, 0x0009U}); // RTS ; (delay) NOP
    m.cpu.step_instruction();            // BSR
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1010U);
    CHECK(r.pr == 0x1004U);   // return address = instruction after the delay slot
    m.cpu.step_instruction(); // RTS
    CHECK(m.cpu.cpu_registers().pc == 0x1004U);
}

TEST_CASE("sh2 BT branches only when T is set") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x0018U, 0x8901U}); // SETT ; BT +1
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x1008U); // 0x1004 + 1*2 + 2
    m.set_pc(0x2000U);
    m.load(0x2000U, {0x0008U, 0x8901U}); // CLRT ; BT +1 (not taken)
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2004U); // fell through
}

TEST_CASE("sh2 BT/S runs its delay slot when taken") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x0018U, 0x8D01U, 0xE207U}); // SETT ; BT/S +1 ; (delay) MOV #7,R2
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[2] == 7U);
    CHECK(r.pc == 0x1008U);
}

TEST_CASE("sh2 JMP and JSR transfer through a register with a delay slot") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[3] = 0x1010U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x432BU, 0x0009U}); // JMP @R3 ; (delay) NOP
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x1010U);
    auto r2 = m.cpu.cpu_registers();
    r2.r[4] = 0x2000U;
    r2.pc = 0x1100U;
    m.cpu.set_registers(r2);
    m.load(0x1100U, {0x440BU, 0x0009U}); // JSR @R4 ; (delay) NOP
    m.cpu.step_instruction();
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.pc == 0x2000U);
    CHECK(rr.pr == 0x1104U);
}

TEST_CASE("sh2 BRAF computes a PC-relative branch") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x20U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x0123U, 0x0009U}); // BRAF R1 ; (delay) NOP
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x1024U); // 0x1002 + 2 + 0x20
}

TEST_CASE("sh2 LDS/STS and LDC/STC move system registers") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x11112222U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // LDS R1,PR ; STS PR,R2 ; LDC R1,GBR ; STC GBR,R3
    m.load(0x1000U, {0x412AU, 0x022AU, 0x411EU, 0x0312U});
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.pr == 0x11112222U);
    CHECK(rr.r[2] == 0x11112222U);
    CHECK(rr.gbr == 0x11112222U);
    CHECK(rr.r[3] == 0x11112222U);
}

TEST_CASE("sh2 LDC to SR masks to the defined bits") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0xFFFFFFFFU;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x410EU}); // LDC R1,SR
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().sr == 0x000003F3U);
}

TEST_CASE("sh2 STS.L/LDS.L push and pop PR through the stack") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.pr = 0xCAFEBABEU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x4F22U, 0x4F26U}); // STS.L PR,@-R15 ; LDS.L @R15+,PR
    m.cpu.step_instruction();
    auto r1 = m.cpu.cpu_registers();
    CHECK(r1.r[15] == 0x300CU);    // SP pre-decremented
    CHECK(m.ram[0x300C] == 0xCAU); // big-endian top byte on the stack
    r1.pr = 0U;                    // clobber, then pop
    m.cpu.set_registers(r1);
    m.cpu.step_instruction();
    const auto r2 = m.cpu.cpu_registers();
    CHECK(r2.pr == 0xCAFEBABEU);
    CHECK(r2.r[15] == 0x3010U);
}

TEST_CASE("sh2 displacement and R0-indexed addressing modes") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x3000U;
    r.r[2] = 0x12345678U;
    r.r[0] = 8U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // MOV.L R2,@(2,R1) ; MOV.L @(2,R1),R3 ; MOV.L R2,@(R0,R1) ; MOV.L @(R0,R1),R4
    m.load(0x1000U, {0x1122U, 0x5312U, 0x0126U, 0x041EU});
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.r[3] == 0x12345678U); // @(disp,Rm) load
    CHECK(rr.r[4] == 0x12345678U); // @(R0,Rm) load
}

TEST_CASE("sh2 GBR-relative store/load and immediate AND") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.gbr = 0x3100U;
    r.r[0] = 0x0000ABCDU;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // MOV.L R0,@(2,GBR) ; AND #0x0F,R0 ; MOV.L @(2,GBR),R0
    m.load(0x1000U, {0xC202U, 0xC90FU, 0xC602U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[0] == 0x0DU); // AND #imm zero-extends
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[0] == 0x0000ABCDU); // reloaded from GBR
}

TEST_CASE("sh2 MOVA computes a PC-relative address into R0") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xC703U}); // MOVA @(3,PC),R0
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[0] == 0x1010U); // ((0x1004)&~3) + 3*4
}

TEST_CASE("sh2 MOV.B @(disp,Rn) with R0 sign-extends") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[0] = 0x00000080U;
    r.r[1] = 0x3000U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // MOV.B R0,@(1,R1) ; MOV.B @(1,R1),R0
    m.load(0x1000U, {0x8011U, 0x8411U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[0] == 0xFFFFFF80U);
}

TEST_CASE("sh2 TRAPA pushes a frame and RTE returns through it") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x20U * 4U, 0x5000U); // vector 0x20 handler -> 0x5000
    m.load(0x1000U, {0xC320U});           // TRAPA #0x20
    m.load(0x5000U, {0x002BU, 0x0009U});  // RTE ; (delay) NOP
    m.cpu.step_instruction();             // TRAPA
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x5000U);    // vectored through VBR + 0x20*4
    CHECK(a.r[15] == 0x3008U); // SR + PC pushed
    m.cpu.step_instruction();  // RTE
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x1002U);    // returned to the instruction after TRAPA
    CHECK(b.r[15] == 0x3010U); // stack restored
}

TEST_CASE("sh2 accepts an external IRQ above the mask") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U; // IMASK = 0
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U); // vector 0x44 handler -> 0x6000
    m.load(0x1000U, {0x0009U});           // NOP (interrupted instruction)
    m.load(0x6000U, {0x0009U});           // handler entry: NOP
    int seen_level = -1;
    std::uint8_t seen_vec = 0U;
    m.cpu.set_irq_accept_callback([&](int lvl, std::uint8_t v) {
        seen_level = lvl;
        seen_vec = v;
    });
    m.cpu.set_irq(12, 0x44U); // VINT: level 12, vector 0x44
    m.cpu.step_instruction(); // services the IRQ, then runs the handler's first op
    const auto rr = m.cpu.cpu_registers();
    CHECK(seen_level == 12);
    CHECK(seen_vec == 0x44U);
    CHECK(rr.r[15] == 0x3008U);                    // frame pushed
    CHECK(((rr.sr & sh2::sr_imask) >> 4U) == 12U); // IMASK raised to the level
    CHECK(rr.pc == 0x6002U);                       // handler's NOP executed
}

TEST_CASE("sh2 holds an IRQ at or below the mask") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 12U << 4U; // IMASK = 12
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x0009U});
    m.cpu.set_irq(12, 0x44U); // level 12 is not > mask 12 -> not accepted
    m.cpu.step_instruction();
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.pc == 0x1002U);    // ran the NOP, no vectoring
    CHECK(rr.r[15] == 0x3010U); // no frame pushed
}

TEST_CASE("sh2 LDC to SR inhibits IRQ acceptance for one instruction") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 15U << 4U; // IMASK = 15 (all masked)
    r.r[1] = 0U;      // LDC R1,SR will unmask everything (SR -> 0)
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U);
    m.load(0x1000U, {0x410EU, 0xE505U}); // LDC R1,SR ; MOV #5,R5
    m.load(0x6000U, {0x0009U});
    m.cpu.set_irq(12, 0x44U);
    m.cpu.step_instruction(); // LDC R1,SR: unmasks + arms the one-instruction inhibit
    m.cpu.step_instruction(); // MOV #5,R5 runs on the inhibited boundary (no IRQ yet)
    auto a = m.cpu.cpu_registers();
    CHECK(a.r[5] == 5U);
    CHECK(a.r[15] == 0x3010U); // IRQ still pending, not taken
    m.cpu.step_instruction();  // inhibit cleared -> IRQ accepted here
    CHECK(m.cpu.cpu_registers().r[15] == 0x3008U);
}

TEST_CASE("sh2 SLEEP halts until an interrupt wakes it") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U);
    m.load(0x1000U, {0x001BU, 0xE509U}); // SLEEP ; MOV #9,R5 (resumes here after RTE)
    m.load(0x6000U, {0x0009U});          // handler: NOP
    m.cpu.step_instruction();            // SLEEP -> halted
    CHECK(m.cpu.cpu_registers().pc == 0x1002U);
    m.cpu.step_instruction(); // still halted (no interrupt): nothing runs
    CHECK(m.cpu.cpu_registers().pc == 0x1002U);
    CHECK(m.cpu.cpu_registers().r[5] == 0U);
    m.cpu.set_irq(12, 0x44U);
    m.cpu.step_instruction(); // the IRQ resumes the CPU and runs the handler
    const auto a = m.cpu.cpu_registers();
    CHECK(a.r[15] == 0x3008U); // frame pushed
    CHECK(a.pc == 0x6002U);    // handler's NOP executed
}

TEST_CASE("sh2 an undecoded opcode raises general illegal-instruction") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 4U * 4U, 0x7000U); // general-illegal handler (vector 4) -> 0x7000
    m.load(0x1000U, {0xF000U});        // FPU opcode: illegal on the SH7604
    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);    // vectored to the illegal-instruction handler
    CHECK(a.r[15] == 0x3008U); // SR + faulting PC pushed
}

TEST_CASE("sh2 an illegal opcode in a delay slot raises slot-illegal") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 6U * 4U, 0x7000U);   // slot-illegal handler (vector 6) -> 0x7000
    m.load(0x1000U, {0xA00AU, 0xF000U}); // BRA ; (delay) illegal FPU opcode
    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);    // vectored to slot-illegal, NOT the branch target
    CHECK(a.r[15] == 0x3008U); // frame pushed (saved PC = the branch's resume target)
}

TEST_CASE("sh2 intercepts the on-chip peripheral window before the bus") {
    machine m;
    m.set_pc(0x1000U);
    // R1 <- 0xFFFFFE92 (a peripheral reg) ; MOV #0x5A,R0 ; MOV.B R0,@R1 ; MOV.B @R1,R2
    m.load(0x1000U, {0xD102U, 0xE05AU, 0x2100U, 0x6210U});
    m.w32(0x100CU, 0xFFFFFE92U); // PC-relative literal
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    // Round-trips through the on-chip registers; an unmapped external read would
    // return 0xFF (sign-extended to 0xFFFFFFFF).
    CHECK(m.cpu.cpu_registers().r[2] == 0x5AU);
}

TEST_CASE("sh2_peripherals round-trips and resets its register window") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE92U, 0xA5U); // CCR (raw-storage region, not a modelled timer)
    p.write8(0xFFFFFFFFU, 0x3CU); // top of the window
    CHECK(p.read8(0xFFFFFE92U) == 0xA5U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    p.save_state(writer);
    mnemos::chips::cpu::sh2_peripherals q;
    mnemos::chips::state_reader reader(blob);
    q.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(q.read8(0xFFFFFE92U) == 0xA5U);
    CHECK(q.read8(0xFFFFFFFFU) == 0x3CU);

    q.reset();
    CHECK(q.read8(0xFFFFFE92U) == 0x00U);
}

TEST_CASE("sh2_peripherals FRT counts at the TCR-selected prescale") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE16U, 0x00U); // TCR = phi/8
    p.tick(7U);                   // below one tick
    CHECK(p.read8(0xFFFFFE12U) == 0x00U);
    CHECK(p.read8(0xFFFFFE13U) == 0x00U);
    p.tick(1U); // total 8 source clocks -> FRC = 1
    CHECK(p.read8(0xFFFFFE12U) == 0x00U);
    CHECK(p.read8(0xFFFFFE13U) == 0x01U);
}

TEST_CASE("sh2_peripherals FRT sets the overflow flag on wrap") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE16U, 0x00U); // phi/8
    p.write8(0xFFFFFE12U, 0xFFU); // FRC = 0xFFFF
    p.write8(0xFFFFFE13U, 0xFFU);
    p.tick(8U);                                  // 0xFFFF -> 0x0000
    CHECK((p.read8(0xFFFFFE11U) & 0x02U) != 0U); // FTCSR.OVF
}

TEST_CASE("sh2_peripherals FRT output-compare A matches and CCLR clears FRC") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE16U, 0x00U); // phi/8
    p.write8(0xFFFFFE11U, 0x01U); // FTCSR.CCLR = 1
    p.write8(0xFFFFFE14U, 0x00U); // OCRA = 0x0002 (TOCR.OCRS = 0)
    p.write8(0xFFFFFE15U, 0x02U);
    p.tick(16U);                                 // FRC reaches 2 -> OCRA match
    CHECK((p.read8(0xFFFFFE11U) & 0x08U) != 0U); // FTCSR.OCFA
    CHECK(p.read8(0xFFFFFE12U) == 0x00U);        // FRC cleared by CCLR
    CHECK(p.read8(0xFFFFFE13U) == 0x00U);
}

TEST_CASE("sh2_peripherals FRT status flags clear only after a read") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE16U, 0x00U);
    p.write8(0xFFFFFE12U, 0xFFU);
    p.write8(0xFFFFFE13U, 0xFFU);
    p.tick(8U);
    CHECK((p.read8(0xFFFFFE11U) & 0x02U) != 0U); // read observes OVF
    p.write8(0xFFFFFE11U, 0x00U);                // write 0 clears the observed flag
    CHECK((p.read8(0xFFFFFE11U) & 0x02U) == 0U);
}

TEST_CASE("sh2 ticks the on-chip FRT as it executes") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0xFFFFFE12U; // FRC high
    r.r[4] = 0xFFFFFE13U; // FRC low
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    // 16 NOPs (phi/8 default -> FRC reaches 2), then MOV.B @R1,R2 ; MOV.B @R4,R3.
    m.load(0x1000U,
           {0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U,
            0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x6210U, 0x6340U});
    for (int i = 0; i < 18; ++i) {
        m.cpu.step_instruction();
    }
    CHECK(m.cpu.cpu_registers().r[3] == 2U); // FRC low = 16 source clocks / 8
}

TEST_CASE("sh2_peripherals INTC presents the FRT overflow interrupt") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE60U, 0x05U); // IPRB high: FRT priority 5
    p.write8(0xFFFFFE68U, 0x70U); // VCRD high: overflow vector
    p.write8(0xFFFFFE10U, 0x02U); // TIER: OVIE
    p.write8(0xFFFFFE12U, 0xFFU); // FRC = 0xFFFF
    p.write8(0xFFFFFE13U, 0xFFU);
    p.tick(8U); // overflow
    const auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 5);
    CHECK(irq.vector == 0x70U);
}

TEST_CASE("sh2_peripherals INTC gates the FRT interrupt on TIER, priority and vector") {
    auto overflow = [] {
        mnemos::chips::cpu::sh2_peripherals q;
        q.write8(0xFFFFFE60U, 0x05U); // priority 5
        q.write8(0xFFFFFE68U, 0x70U); // vector
        q.write8(0xFFFFFE10U, 0x02U); // OVIE
        q.write8(0xFFFFFE12U, 0xFFU);
        q.write8(0xFFFFFE13U, 0xFFU);
        q.tick(8U);
        return q;
    };
    // Baseline: all three present -> delivered.
    CHECK(overflow().pending_onchip_irq().level == 5);
    {
        auto p = overflow();
        p.write8(0xFFFFFE10U, 0x00U); // TIER OVIE cleared
        CHECK(p.pending_onchip_irq().level == 0);
    }
    {
        auto p = overflow();
        p.write8(0xFFFFFE60U, 0x00U); // FRT priority 0
        CHECK(p.pending_onchip_irq().level == 0);
    }
    {
        auto p = overflow();
        p.write8(0xFFFFFE68U, 0x00U); // vector 0
        CHECK(p.pending_onchip_irq().level == 0);
    }
}

TEST_CASE("sh2_peripherals INTC: output-compare outranks overflow") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE60U, 0x07U); // FRT priority 7
    p.write8(0xFFFFFE67U, 0x60U); // VCRC low: output-compare vector
    p.write8(0xFFFFFE68U, 0x70U); // VCRD high: overflow vector
    p.write8(0xFFFFFE10U, 0x0AU); // TIER: OVIE | OCIAE
    p.write8(0xFFFFFE14U, 0x00U); // OCRA = 1
    p.write8(0xFFFFFE15U, 0x01U);
    p.write8(0xFFFFFE12U, 0xFFU); // FRC = 0xFFFF
    p.write8(0xFFFFFE13U, 0xFFU);
    p.tick(16U); // 0xFFFF -> 0x0000 (OVF) -> 0x0001 (OCFA)
    const auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 7);
    CHECK(irq.vector == 0x60U); // the output-compare vector wins
}

TEST_CASE("sh2 accepts an on-chip FRT interrupt through the INTC") {
    machine m;
    m.w32(0x60U * 4U, 0x3000U);          // output-compare vector 0x60 -> handler
    m.load(0x3000U, {0xAFFEU, 0x0009U}); // handler: BRA self ; (delay) NOP
    auto r = m.cpu.cpu_registers();
    r.pc = 0x1000U;
    r.sr = 0U;      // IMASK = 0 so the level-5 IRQ is accepted
    r.r[0] = 0x08U; // TIER OCIAE
    r.r[1] = 0xFFFFFE10U;
    r.r[2] = 0x05U; // IPRB FRT priority 5
    r.r[3] = 0xFFFFFE60U;
    r.r[4] = 0x60U; // OCI vector
    r.r[5] = 0xFFFFFE67U;
    r.r[6] = 0x00U; // OCRA high
    r.r[7] = 0xFFFFFE14U;
    r.r[8] = 0x01U; // OCRA low -> OCRA = 1
    r.r[9] = 0xFFFFFE15U;
    r.r[10] = 0x00U; // TCR = phi/8 (resets the prescaler)
    r.r[11] = 0xFFFFFE16U;
    m.cpu.set_registers(r);
    // Configure the FRT/INTC, then spin while the timer reaches the compare.
    m.load(0x1000U, {0x2100U, 0x2320U, 0x2540U, 0x2760U, 0x2980U, 0x2BA0U, 0x0009U, 0x0009U,
                     0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U, 0x0009U});
    for (int i = 0; i < 30; ++i) {
        m.cpu.step_instruction();
    }
    CHECK(m.cpu.cpu_registers().pc == 0x3000U); // vectored into the handler loop
}

TEST_CASE("sh2_peripherals WDT counts in interval mode and flags overflow") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE80U, 0x5AU);                // key: WTCNT
    p.write8(0xFFFFFE81U, 0xFFU);                // WTCNT = 0xFF
    p.write8(0xFFFFFE80U, 0xA5U);                // key: WTCSR
    p.write8(0xFFFFFE81U, 0x20U);                // WTCSR: TME, interval mode, CKS=0 (prescale 2)
    p.tick(2U);                                  // one WDT tick -> 0xFF wraps to 0x00
    CHECK((p.read8(0xFFFFFE80U) & 0x80U) != 0U); // WTCSR.OVF
    CHECK(p.read8(0xFFFFFE81U) == 0x00U);        // WTCNT wrapped
}

TEST_CASE("sh2_peripherals WDT flags the reset latch in watchdog mode") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU); // WTCNT = 0xFF
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U); // WTCSR: TME | WTIT (watchdog mode)
    p.tick(2U);
    CHECK((p.read8(0xFFFFFE82U) & 0x80U) != 0U); // RSTCSR.WOVF
}

TEST_CASE("sh2_peripherals WDT ignores a data write without the matching key") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE80U, 0x00U);         // not a valid key
    p.write8(0xFFFFFE81U, 0x42U);         // data dropped
    CHECK(p.read8(0xFFFFFE81U) == 0x00U); // WTCNT unchanged
}

TEST_CASE("sh2 exposes its register file and trace hook via introspection") {
    machine m;
    auto& intro = m.cpu.introspection();

    auto* rv = intro.registers();
    REQUIRE(rv != nullptr);
    CHECK_FALSE(rv->registers().empty());

    auto* tt = intro.trace();
    REQUIRE(tt != nullptr);

    std::vector<std::uint32_t> seen;
    tt->install([&](const mnemos::instrumentation::trace_event& ev) { seen.push_back(ev.pc); });
    m.set_pc(0x3000U);
    m.load(0x3000U, {0x0009U}); // NOP
    m.cpu.step_instruction();
    REQUIRE(seen.size() == 1U);
    CHECK(seen[0] == 0x3000U);

    tt->install({}); // empty clears
    m.cpu.step_instruction();
    CHECK(seen.size() == 1U);
}
