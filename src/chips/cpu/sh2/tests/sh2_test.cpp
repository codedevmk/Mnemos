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
    using mnemos::chips::cpu::sh2_peripherals;
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
        [[nodiscard]] std::uint32_t r32(std::uint32_t a) const {
            return (static_cast<std::uint32_t>(ram[a]) << 24U) |
                   (static_cast<std::uint32_t>(ram[a + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(ram[a + 2U]) << 8U) | ram[a + 3U];
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

    void write_peripheral32(sh2_peripherals& p, std::uint32_t addr, std::uint32_t v) {
        p.write8(addr, static_cast<std::uint8_t>(v >> 24U));
        p.write8(addr + 1U, static_cast<std::uint8_t>(v >> 16U));
        p.write8(addr + 2U, static_cast<std::uint8_t>(v >> 8U));
        p.write8(addr + 3U, static_cast<std::uint8_t>(v));
    }

    std::uint32_t read_peripheral32(const sh2_peripherals& p, std::uint32_t addr) {
        return (static_cast<std::uint32_t>(p.read8(addr)) << 24U) |
               (static_cast<std::uint32_t>(p.read8(addr + 1U)) << 16U) |
               (static_cast<std::uint32_t>(p.read8(addr + 2U)) << 8U) | p.read8(addr + 3U);
    }
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

TEST_CASE("sh2 fixed-state timing covers multiply and read-modify-write instructions") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[0] = 0x2000U;
    r.r[1] = 0x2008U;
    r.r[2] = 6U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.w32(0x2000U, 7U);
    m.w32(0x2008U, static_cast<std::uint32_t>(-2));
    m.load(0x1000U, {0x0127U, 0x3125U, 0x010FU}); // MUL.L ; DMULU.L ; MAC.L
    CHECK(m.cpu.step_instruction() == 2);
    CHECK(m.cpu.step_instruction() == 2);
    CHECK(m.cpu.step_instruction() == 3);

    r = m.cpu.cpu_registers();
    r.r[0] = 0x2010U;
    r.r[1] = 0x2014U;
    r.pc = 0x1100U;
    m.cpu.set_registers(r);
    m.w16(0x2010U, 3U);
    m.w16(0x2014U, 4U);
    m.load(0x1100U, {0x410FU}); // MAC.W
    CHECK(m.cpu.step_instruction() == 3);

    r = m.cpu.cpu_registers();
    r.r[3] = 0x3000U;
    r.pc = 0x1200U;
    m.cpu.set_registers(r);
    m.ram[0x3000] = 0x00U;
    m.load(0x1200U, {0x431BU}); // TAS.B @R3
    CHECK(m.cpu.step_instruction() == 4);
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

TEST_CASE("sh2 TAS.B adds board-provided locked bus wait states") {
    machine m;
    int calls = 0;
    std::uint32_t seen_addr = 0U;
    std::uint8_t seen_bytes = 0U;
    bool seen_locked = false;
    m.cpu.set_bus_wait_callback([&](std::uint32_t address, std::uint8_t bytes, bool locked) {
        ++calls;
        seen_addr = address;
        seen_bytes = bytes;
        seen_locked = locked;
        return 3;
    });

    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x3000U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.ram[0x3000] = 0x00U;
    m.load(0x1000U, {0x411BU}); // TAS.B @R1

    CHECK(m.cpu.step_instruction() == 7);
    CHECK(calls == 1);
    CHECK(seen_addr == 0x3000U);
    CHECK(seen_bytes == 1U);
    CHECK(seen_locked);
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
    CHECK(m.cpu.step_instruction() == 2);
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[1] == 5U);    // delay slot executed before the branch landed
    CHECK(r.pc == 0x1010U); // 0x1002 + 6*2 + 2
}

TEST_CASE("sh2 BSR sets PR and RTS returns through it") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xB006U, 0x0009U});  // BSR +6 ; (delay) NOP
    m.load(0x1010U, {0x000BU, 0x0009U});  // RTS ; (delay) NOP
    CHECK(m.cpu.step_instruction() == 2); // BSR
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1010U);
    CHECK(r.pr == 0x1004U);               // return address = instruction after the delay slot
    CHECK(m.cpu.step_instruction() == 2); // RTS
    CHECK(m.cpu.cpu_registers().pc == 0x1004U);
}

TEST_CASE("sh2 BT branches only when T is set") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x0018U, 0x8901U}); // SETT ; BT +1
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.step_instruction() == 3);
    CHECK(m.cpu.cpu_registers().pc == 0x1008U); // 0x1004 + 1*2 + 2
    m.set_pc(0x2000U);
    m.load(0x2000U, {0x0008U, 0x8901U}); // CLRT ; BT +1 (not taken)
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().pc == 0x2004U); // fell through
}

TEST_CASE("sh2 BT/S runs its delay slot when taken") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x0018U, 0x8D01U, 0xE207U}); // SETT ; BT/S +1 ; (delay) MOV #7,R2
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.step_instruction() == 2);
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
    CHECK(m.cpu.step_instruction() == 2);
    CHECK(m.cpu.cpu_registers().pc == 0x1010U);
    auto r2 = m.cpu.cpu_registers();
    r2.r[4] = 0x2000U;
    r2.pc = 0x1100U;
    m.cpu.set_registers(r2);
    m.load(0x1100U, {0x440BU, 0x0009U}); // JSR @R4 ; (delay) NOP
    CHECK(m.cpu.step_instruction() == 2);
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
    CHECK(m.cpu.step_instruction() == 2);
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

TEST_CASE("sh2 fixed-state timing covers system-register memory transfers") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[15] = 0x3010U;
    r.sr = 0x000000F0U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x4F03U, 0x4F07U}); // STC.L SR,@-R15 ; LDC.L @R15+,SR
    CHECK(m.cpu.step_instruction() == 2);
    auto a = m.cpu.cpu_registers();
    CHECK(a.r[15] == 0x300CU);
    CHECK(m.ram[0x300C] == 0x00U);
    CHECK(m.ram[0x300F] == 0xF0U);
    CHECK(m.cpu.step_instruction() == 3);
    const auto b = m.cpu.cpu_registers();
    CHECK(b.r[15] == 0x3010U);
    CHECK(b.sr == 0x000000F0U);
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

TEST_CASE("sh2 fixed-state timing covers GBR byte-immediate logical ops") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.gbr = 0x3200U;
    r.r[0] = 0x10U;
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    m.ram[0x3210] = 0xF0U;
    m.load(0x1000U, {0xCC0FU, 0xCD0FU, 0xCE0FU, 0xCF0FU});

    CHECK(m.cpu.step_instruction() == 3); // TST.B #0x0F,@(R0,GBR)
    CHECK((m.cpu.cpu_registers().sr & sh2::sr_t) != 0U);
    CHECK(m.cpu.step_instruction() == 3); // AND.B #0x0F,@(R0,GBR)
    CHECK(m.ram[0x3210] == 0x00U);
    CHECK(m.cpu.step_instruction() == 3); // XOR.B #0x0F,@(R0,GBR)
    CHECK(m.ram[0x3210] == 0x0FU);
    CHECK(m.cpu.step_instruction() == 3); // OR.B #0x0F,@(R0,GBR)
    CHECK(m.ram[0x3210] == 0x0FU);
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
    CHECK(m.cpu.step_instruction() == 8); // TRAPA
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x5000U);               // vectored through VBR + 0x20*4
    CHECK(a.r[15] == 0x3008U);            // SR + PC pushed
    CHECK(m.cpu.step_instruction() == 4); // RTE
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x1002U);    // returned to the instruction after TRAPA
    CHECK(b.r[15] == 0x3010U); // stack restored
}

TEST_CASE("sh2 address-error exception vectors odd instruction fetches") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1001U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U); // CPU address-error handler -> 0x7000

    CHECK(m.cpu.step_instruction() == 1);
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.ram[0x3008] == 0x00U);
    CHECK(m.ram[0x300B] == 0x01U); // saved PC = odd fetch address 0x1001
}

TEST_CASE("sh2 address-error exception vectors misaligned word and long data accesses") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x2001U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1000U, {0x6211U}); // MOV.W @R1,R2

    m.cpu.step_instruction();
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.ram[0x300B] == 0x02U); // saved PC = instruction after the faulting op

    r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[3] = 0x3002U;
    r.r[4] = 0x12345678U;
    r.r[15] = 0x3010U;
    r.pc = 0x1100U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1100U, {0x2346U}); // MOV.L R4,@-R3, target 0x2FFE is not long-aligned
    m.cpu.step_instruction();
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x7000U);
    CHECK(b.r[3] == 0x3002U); // predecrement did not retire
    CHECK(m.ram[0x2FFE] == 0x00U);
    CHECK(m.ram[0x300B] == 0x02U);
}

TEST_CASE("sh2 address-error exception is deferred from delay-slot data access") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x2001U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1000U, {0xA006U, 0x6211U}); // BRA 0x1010 ; (slot) MOV.W @R1,R2

    CHECK(m.cpu.step_instruction() == 2);
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.r32(0x3008U) == 0x00001010U); // saved PC = committed branch target
}

TEST_CASE("sh2 address-error exception enforces on-chip access classes") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0xFFFFFF00U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1200U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1200U, {0x6210U}); // MOV.B @R1,R2: byte access in high on-chip space
    m.cpu.step_instruction();
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(m.r32(0x3008U) == 0x00001202U);

    r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0xFFFFFE00U;
    r.r[2] = 0xBBBBBBBBU;
    r.r[15] = 0x3010U;
    r.pc = 0x1300U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1300U, {0x6212U}); // MOV.L @R1,R2: long access in low on-chip space
    m.cpu.step_instruction();
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x7000U);
    CHECK(b.r[2] == 0xBBBBBBBBU);
    CHECK(m.r32(0x3008U) == 0x00001302U);
}

TEST_CASE("sh2 address-error exception rejects PC-relative loads into on-chip space") {
    machine m;
    std::array<std::uint8_t, 0x1000> high_ram{};
    m.bus.map_ram(0xFFFFF000U, std::span<std::uint8_t>(high_ram), 1);
    high_ram[0xDFCU] = 0x92U; // MOV.W @(0,PC),R2 at 0xFFFFFDFC
    high_ram[0xDFDU] = 0x00U;

    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0xFFFFFDFCU;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);

    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(m.r32(0x3008U) == 0xFFFFFDFEU);
}

TEST_CASE("sh2 stacking fault on a misaligned SP vectors through 9 without recursing") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3011U; // misaligned SP: the exception-frame push address-errors
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 4U * 4U, 0x5000U); // general-illegal handler (vector 4)
    m.w32(0x4000U + 9U * 4U, 0x7000U); // address-error handler (vector 9)
    m.load(0x1000U, {0x0000U});        // illegal opcode -> vector 4, but the push faults

    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    // The stacking fault diverts to vector 9 (not the illegal handler) and does
    // not recurse or reset; SP is still decremented by the stacked frames.
    CHECK(a.pc == 0x7000U);
    CHECK(a.pc != 0x5000U);
    CHECK(a.r[15] < 0x3011U);
}

TEST_CASE("sh2 address-error faults byte/word access to cache-control spaces") {
    machine m;
    m.w32(0x4000U + 9U * 4U, 0x7000U); // address-error handler

    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x60000000U; // address-array space (longword-only)
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x6210U}); // MOV.B @R1,R2 -- byte access to the address array
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x7000U);
    CHECK(m.cpu.cpu_registers().r[2] == 0xAAAAAAAAU); // the load did not retire

    auto r2 = m.cpu.cpu_registers();
    r2.vbr = 0x4000U;
    r2.r[1] = 0x40000000U; // purge space
    r2.r[15] = 0x3010U;
    r2.pc = 0x1100U;
    r2.sr = 0U;
    m.cpu.set_registers(r2);
    m.load(0x1100U, {0x6211U}); // MOV.W @R1,R2 -- word access to the purge space
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x7000U);

    // The 32X data array ($C0000000) is the cache-as-RAM scratch, NOT a fault: a
    // long access there proceeds normally to the (unmapped here) external bus.
    auto r3 = m.cpu.cpu_registers();
    r3.vbr = 0x4000U;
    r3.r[1] = 0xC0000000U;
    r3.pc = 0x1200U;
    r3.sr = 0U;
    m.cpu.set_registers(r3);
    m.load(0x1200U, {0x6212U}); // MOV.L @R1,R2
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x1202U); // advanced normally, no vector-9
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
    m.load(0x1000U, {0x001BU, 0xE509U});  // SLEEP ; MOV #9,R5 (resumes here after RTE)
    m.load(0x6000U, {0x0009U});           // handler: NOP
    CHECK(m.cpu.step_instruction() == 3); // SLEEP -> halted
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
    p.write8(0xFFFFFE00U, 0x1BU); // modelled SCI fields are explicit state
    p.write8(0xFFFFFE01U, 0x22U);
    p.write8(0xFFFFFE02U, 0x50U);
    p.write8(0xFFFFFE03U, 0x5AU);
    p.sci_receive_byte(0x77U); // SSR is not plain storage: set RDRF (0x40) via receive
    p.write8(0xFFFFFE05U, 0x42U);
    p.write8(0xFFFFFEE2U, 0x80U); // IPRA extension-era fields are first-class state
    write_peripheral32(p, 0xFFFFFFA0U, 0x55U);
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
    CHECK(q.read8(0xFFFFFE00U) == 0x1BU);
    CHECK(q.read8(0xFFFFFE01U) == 0x22U);
    CHECK(q.read8(0xFFFFFE02U) == 0x50U);
    CHECK(q.read8(0xFFFFFE03U) == 0x5AU);
    CHECK(q.read8(0xFFFFFE04U) == 0x40U);
    CHECK(q.read8(0xFFFFFE05U) == 0x42U);
    CHECK(q.read8(0xFFFFFEE2U) == 0x80U);
    CHECK(read_peripheral32(q, 0xFFFFFFA0U) == 0x00000055U);

    q.reset();
    CHECK(q.read8(0xFFFFFE92U) == 0x00U);
}

TEST_CASE("sh2_peripherals rejects unversioned state chunks") {
    std::vector<std::uint8_t> blob(32U, 0U);
    mnemos::chips::cpu::sh2_peripherals q;
    mnemos::chips::state_reader reader(blob);
    q.load_state(reader);
    CHECK_FALSE(reader.ok());
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

TEST_CASE("sh2_peripherals INTC gates the FRT interrupt on TIER and priority") {
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
        // A zero vector (VBR + 0) is still a legal vector, not a mask: TIER and
        // priority gate delivery, VCR does not -- the source is still presented.
        auto p = overflow();
        p.write8(0xFFFFFE68U, 0x00U); // VCRD overflow vector = 0
        const auto irq = p.pending_onchip_irq();
        CHECK(irq.level == 5);
        CHECK(irq.vector == 0x00U);
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

TEST_CASE("sh2_peripherals INTC masks priority and vector registers") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFEE2U, 0xFFU); // IPRA high: DIVU + DMAC priorities
    p.write8(0xFFFFFEE3U, 0xFFU); // IPRA low: WDT priority, reserved low nibble
    CHECK(p.read8(0xFFFFFEE2U) == 0xFFU);
    CHECK(p.read8(0xFFFFFEE3U) == 0xF0U);

    p.write8(0xFFFFFE62U, 0xFFU); // VCRA high: SCI ERI vector
    p.write8(0xFFFFFE63U, 0xFFU); // VCRA low: SCI RXI vector
    CHECK(p.read8(0xFFFFFE62U) == 0x7FU);
    CHECK(p.read8(0xFFFFFE63U) == 0x7FU);

    p.write8(0xFFFFFEE4U, 0xFFU); // VCRWDT high: ITI vector
    p.write8(0xFFFFFEE5U, 0xFFU); // VCRWDT low: BSC CMI vector storage
    CHECK(p.read8(0xFFFFFEE4U) == 0x7FU);
    CHECK(p.read8(0xFFFFFEE5U) == 0x7FU);

    write_peripheral32(p, 0xFFFFFFA0U, 0xFFFFFFFFU); // VCRDMA0
    CHECK(read_peripheral32(p, 0xFFFFFFA0U) == 0x0000007FU);
}

TEST_CASE("sh2_peripherals SCI resets and round-trips registers") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE00U, 0x1BU);
    p.write8(0xFFFFFE01U, 0x22U);
    p.write8(0xFFFFFE02U, 0x50U);
    p.write8(0xFFFFFE03U, 0x5AU);
    p.sci_receive_byte(0x77U); // SSR is not plain storage: set RDRF (0x40) via receive
    p.write8(0xFFFFFE05U, 0x42U);

    CHECK(p.read8(0xFFFFFE00U) == 0x1BU);
    CHECK(p.read8(0xFFFFFE01U) == 0x22U);
    CHECK(p.read8(0xFFFFFE02U) == 0x50U);
    CHECK(p.read8(0xFFFFFE03U) == 0x5AU);
    CHECK(p.read8(0xFFFFFE04U) == 0x40U);
    CHECK(p.read8(0xFFFFFE05U) == 0x42U);

    p.reset();
    CHECK(p.read8(0xFFFFFE00U) == 0x00U);
    CHECK(p.read8(0xFFFFFE01U) == 0xFFU);
    CHECK(p.read8(0xFFFFFE02U) == 0x00U);
    CHECK(p.read8(0xFFFFFE03U) == 0xFFU);
    CHECK(p.read8(0xFFFFFE04U) == 0x84U);
    CHECK(p.read8(0xFFFFFE05U) == 0x00U);
}

TEST_CASE("sh2_peripherals SCI transmit completion presents TXI and TEI") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE60U, 0xA0U); // SCI priority 10
    p.write8(0xFFFFFE64U, 0x33U); // TXI vector
    p.write8(0xFFFFFE65U, 0x34U); // TEI vector
    p.write8(0xFFFFFE02U, 0xA4U); // SCR: TIE | TE | TEIE

    p.write8(0xFFFFFE03U, 0x5AU); // TDR write starts a coarse transmit
    CHECK(p.read8(0xFFFFFE03U) == 0x5AU);
    CHECK((p.read8(0xFFFFFE04U) & 0x84U) == 0x00U);
    CHECK(p.pending_onchip_irq().level == 0);

    p.tick(1U);
    CHECK((p.read8(0xFFFFFE04U) & 0x84U) == 0x84U);
    auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x33U); // TXI outranks TEI in the SCI group

    p.write8(0xFFFFFE04U, 0x04U); // clear TDRE, leave TEND
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x34U);
}

TEST_CASE("sh2_peripherals SCI receive latches RXI, ERI, and overrun") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE60U, 0xA0U); // SCI priority 10
    p.write8(0xFFFFFE62U, 0x31U); // ERI vector
    p.write8(0xFFFFFE63U, 0x32U); // RXI vector
    p.sci_receive_byte(0x11U);
    CHECK((p.read8(0xFFFFFE04U) & 0x40U) == 0x00U); // receiver disabled

    p.write8(0xFFFFFE02U, 0x50U); // SCR: RIE | RE
    p.sci_receive_byte(0x42U);
    CHECK(p.read8(0xFFFFFE05U) == 0x42U);
    CHECK((p.read8(0xFFFFFE04U) & 0x40U) == 0x40U);
    auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x32U);

    p.write8(0xFFFFFE04U, 0x00U); // clear RDRF
    CHECK(p.pending_onchip_irq().level == 0);

    p.sci_receive_byte(0x43U, 0x10U); // framing error
    CHECK(p.read8(0xFFFFFE05U) == 0x43U);
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x31U);

    // FER was not read since it was set, so write-0 cannot clear it (the SH7604
    // read-then-write-0 protocol); the next receive overruns and raises ORER, so
    // ERI stays asserted on the error group either way.
    p.write8(0xFFFFFE04U, 0x40U);
    p.sci_receive_byte(0x44U);
    CHECK(p.read8(0xFFFFFE05U) == 0x43U);
    CHECK((p.read8(0xFFFFFE04U) & 0x20U) == 0x20U);
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x31U);
}

TEST_CASE("sh2_peripherals SCI SSR clears a status flag only after a read observes it") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE02U, 0x10U); // SCR: RE (receiver enable)
    p.sci_receive_byte(0x42U);    // sets RDRF
    // Write 0 to RDRF before any SSR read: the flag is NOT cleared.
    p.write8(0xFFFFFE04U, 0x00U);
    CHECK((p.read8(0xFFFFFE04U) & 0x40U) == 0x40U); // still set (this read observes it)
    // Now that a read has observed RDRF, a write-0 clears it.
    p.write8(0xFFFFFE04U, 0x00U);
    CHECK((p.read8(0xFFFFFE04U) & 0x40U) == 0x00U); // cleared
    // TEND is read-only: a write-1 to it does nothing, a write-0 cannot clear it.
    CHECK((p.read8(0xFFFFFE04U) & 0x04U) == 0x04U);
}

TEST_CASE("sh2_peripherals INTC presents raw SCI status interrupts") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE60U, 0xA0U); // SCI priority 10, FRT priority 0
    p.write8(0xFFFFFE62U, 0x31U); // ERI vector
    p.write8(0xFFFFFE63U, 0x32U); // RXI vector
    p.write8(0xFFFFFE64U, 0x33U); // TXI vector
    p.write8(0xFFFFFE65U, 0x34U); // TEI vector
    p.write8(0xFFFFFE02U, 0xD4U); // SCR: TIE | RIE | RE | TEIE

    // SSR status flags cannot be set by a register write; reach TDRE|RDRF|ORER|
    // TEND through real paths (TDRE|TEND after reset, a receive sets RDRF, a
    // second receive overruns to set ORER).
    p.sci_receive_byte(0x11U);
    p.sci_receive_byte(0x22U);
    auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x31U); // ERI outranks the other SCI sources

    // Step down the priority by clearing one flag at a time (read-then-write-0).
    static_cast<void>(p.read8(0xFFFFFE04U)); // observe the flags before clearing
    p.write8(0xFFFFFE04U, 0xC4U);            // clear ORER -> RXI
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x32U);

    static_cast<void>(p.read8(0xFFFFFE04U)); // observe the flags before clearing
    p.write8(0xFFFFFE04U, 0x84U);            // clear RDRF -> TXI
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x33U);

    static_cast<void>(p.read8(0xFFFFFE04U)); // observe the flags before clearing
    p.write8(0xFFFFFE04U, 0x04U);            // clear TDRE -> TEI
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 10);
    CHECK(irq.vector == 0x34U);
}

TEST_CASE("sh2_peripherals INTC presents the WDT interval interrupt") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFEE3U, 0x60U); // IPRA[7:4]: WDT priority 6
    p.write8(0xFFFFFEE4U, 0x55U); // VCRWDT high: ITI vector
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU); // WTCNT = 0xFF
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x20U); // interval mode, TME, prescale 2
    p.tick(2U);
    auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 6);
    CHECK(irq.vector == 0x55U);

    CHECK((p.read8(0xFFFFFE80U) & 0x80U) != 0U); // observe WTCSR.OVF
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x20U); // write 0 to the observed OVF bit
    CHECK(p.pending_onchip_irq().level == 0);
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

TEST_CASE("sh2_peripherals WDT flags overflow and resets WDT when RSTE is clear") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU); // WTCNT = 0xFF
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U); // WTCSR: TME | WTIT (watchdog mode)
    p.tick(2U);
    CHECK((p.read8(0xFFFFFE82U) & 0x80U) != 0U); // RSTCSR.WOVF
    CHECK(p.read8(0xFFFFFE80U) == 0x18U);        // WTCSR reset within WDT
    CHECK(p.read8(0xFFFFFE81U) == 0x00U);        // WTCNT reset within WDT
    CHECK_FALSE(p.consume_watchdog_reset().asserted);
    p.write8(0xFFFFFE82U, 0xA5U);
    p.write8(0xFFFFFE83U, 0x00U); // clear WOVF; RSTE/RSTS unaffected
    CHECK((p.read8(0xFFFFFE82U) & 0x80U) == 0U);
}

TEST_CASE("sh2_peripherals WDT requests the selected internal reset when RSTE is set") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE82U, 0x5AU);
    p.write8(0xFFFFFE83U, 0x40U); // RSTCSR.RSTE=1, RSTS=0 -> power-on reset
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU);
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U);
    p.tick(2U);
    auto request = p.consume_watchdog_reset();
    REQUIRE(request.asserted);
    CHECK(request.kind == reset_kind::power_on);
    CHECK_FALSE(p.consume_watchdog_reset().asserted);

    p.write8(0xFFFFFE82U, 0x5AU);
    p.write8(0xFFFFFE83U, 0x60U); // RSTE=1, RSTS=1 -> hard/manual reset
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU);
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U);
    p.tick(2U);
    request = p.consume_watchdog_reset();
    REQUIRE(request.asserted);
    CHECK(request.kind == reset_kind::hard);
}

TEST_CASE("sh2_peripherals WDT serializes a pending internal reset request") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE82U, 0x5AU);
    p.write8(0xFFFFFE83U, 0x60U); // RSTE=1, RSTS=1 -> hard/manual reset
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU);
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U);
    p.tick(2U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    p.save_state(writer);

    mnemos::chips::cpu::sh2_peripherals q;
    mnemos::chips::state_reader reader(blob);
    q.load_state(reader);
    REQUIRE(reader.ok());
    const auto request = q.consume_watchdog_reset();
    REQUIRE(request.asserted);
    CHECK(request.kind == reset_kind::hard);
    CHECK((q.read8(0xFFFFFE82U) & 0xE0U) == 0xE0U);
}

TEST_CASE("sh2 WDT internal reset reloads vectors and preserves RSTCSR") {
    machine m;
    m.w32(0x0000U, 0x2000U);
    m.w32(0x0004U, 0x0000FFF0U);
    m.set_pc(0x1000U);
    m.load(0x1000U, {0x001BU}); // SLEEP gives the WDT enough cycles to overflow

    auto r = m.cpu.cpu_registers();
    r.sr = 0U;
    r.r[3] = 0x12345678U;
    m.cpu.set_registers(r);

    sh2_peripherals& p = m.cpu.peripherals();
    p.write8(0xFFFFFE82U, 0x5AU);
    p.write8(0xFFFFFE83U, 0x40U); // enable internal reset, power-on type
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU);
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U); // watchdog mode, TME, prescale 2

    CHECK(m.cpu.step_instruction() == 3);
    const auto after = m.cpu.cpu_registers();
    CHECK(after.pc == 0x2000U);
    CHECK(after.r[15] == 0x0000FFF0U);
    CHECK(after.r[3] == 0U);
    CHECK(after.sr == sh2::sr_imask);
    CHECK((m.cpu.peripherals().read8(0xFFFFFE82U) & 0xC0U) == 0xC0U);
}

TEST_CASE("sh2_peripherals WDT ignores a data write without the matching key") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFE80U, 0x00U);         // not a valid key
    p.write8(0xFFFFFE81U, 0x42U);         // data dropped
    CHECK(p.read8(0xFFFFFE81U) == 0x00U); // WTCNT unchanged
}

namespace {
    // A peripherals instance backed by a flat RAM bus the DMAC can transfer over.
    struct dmac_fixture final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{32U, mnemos::topology::endianness::big};
        mnemos::chips::cpu::sh2_peripherals p;

        dmac_fixture() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            p.set_bus(&bus);
        }
        // DMAC channel registers are 32-bit, big-endian byte lanes.
        static constexpr std::uint32_t sar0 = 0xFFFFFF80U;
        static constexpr std::uint32_t dar0 = 0xFFFFFF84U;
        static constexpr std::uint32_t tcr0 = 0xFFFFFF88U;
        static constexpr std::uint32_t chcr0 = 0xFFFFFF8CU;
        static constexpr std::uint32_t sar1 = 0xFFFFFF90U;
        static constexpr std::uint32_t dar1 = 0xFFFFFF94U;
        static constexpr std::uint32_t tcr1 = 0xFFFFFF98U;
        static constexpr std::uint32_t chcr1 = 0xFFFFFF9CU;
        static constexpr std::uint32_t vcrdma0 = 0xFFFFFFA0U;
        static constexpr std::uint32_t dmaor = 0xFFFFFFB0U;
        void w32reg(std::uint32_t addr, std::uint32_t v) {
            p.write8(addr, static_cast<std::uint8_t>(v >> 24U));
            p.write8(addr + 1U, static_cast<std::uint8_t>(v >> 16U));
            p.write8(addr + 2U, static_cast<std::uint8_t>(v >> 8U));
            p.write8(addr + 3U, static_cast<std::uint8_t>(v));
        }
        std::uint32_t r32reg(std::uint32_t addr) {
            return (static_cast<std::uint32_t>(p.read8(addr)) << 24U) |
                   (static_cast<std::uint32_t>(p.read8(addr + 1U)) << 16U) |
                   (static_cast<std::uint32_t>(p.read8(addr + 2U)) << 8U) |
                   static_cast<std::uint32_t>(p.read8(addr + 3U));
        }
    };
} // namespace

TEST_CASE("sh2_peripherals DMAC auto-request copies a long-word block") {
    dmac_fixture f;
    for (std::uint32_t i = 0; i < 16U; ++i) {
        f.ram[0x1000U + i] = static_cast<std::uint8_t>(0xA0U + i);
    }
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 4U);           // 4 long-word units = 16 bytes
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x5A11U);     // DE | TB | AR | TS=long | SM=inc | DM=inc
    f.p.tick(1U);
    for (std::uint32_t i = 0; i < 16U; ++i) {
        CHECK(f.ram[0x2000U + i] == static_cast<std::uint8_t>(0xA0U + i));
    }
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U); // CHCR.TE set on completion
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 0U);
}

TEST_CASE("sh2_peripherals DMAC burst is bounded per tick and resumes across ticks") {
    dmac_fixture f;
    f.ram[0x1000U] = 0x5AU;
    constexpr std::uint32_t total = 0x4000U; // 16384 byte units -- far above any per-tick cap
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, total);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x4211U);     // DE | TB(burst) | AR | TS=byte | SM=fixed | DM=inc

    // A single tick must NOT drain the whole block (the pre-fix wedge): the
    // burst is capped per tick, leaves CHCR.TE clear, and makes partial progress.
    f.p.tick(1U);
    const std::uint32_t after_first = f.r32reg(f.tcr0) & 0x00FFFFFFU;
    CHECK(after_first > 0U);
    CHECK(after_first < total);
    CHECK((f.r32reg(f.chcr0) & 0x02U) == 0U);

    // It resumes on later ticks and eventually completes the full transfer.
    for (int i = 0; i < 4096 && (f.r32reg(f.chcr0) & 0x02U) == 0U; ++i) {
        f.p.tick(1U);
    }
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 0U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);    // TE set after completing
    CHECK(f.ram[0x2000U + total - 1U] == 0x5AU); // last unit landed
}

TEST_CASE("sh2_peripherals DMAC reports source and destination bus waits") {
    dmac_fixture f;
    for (std::uint32_t i = 0; i < 4U; ++i) {
        f.ram[0x1000U + i] = static_cast<std::uint8_t>(0xA0U + i);
    }
    int calls = 0;
    f.p.set_bus_wait_callback([&](std::uint32_t address, std::uint8_t bytes, bool locked) {
        CHECK_FALSE(locked);
        CHECK(bytes == 4U);
        ++calls;
        if (address == 0x1000U) {
            return 2;
        }
        if (address == 0x2000U) {
            return 3;
        }
        return 0;
    });
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x5A11U);     // DE | TB | AR | TS=long | SM=inc | DM=inc

    CHECK(f.p.tick(1U) == 5U);
    CHECK(calls == 2);
    CHECK(f.ram[0x2000U] == 0xA0U);
    CHECK(f.ram[0x2003U] == 0xA3U);
}

TEST_CASE("sh2 DMAC bus waits extend the owning CPU instruction") {
    machine m;
    m.ram[0x2000U] = 0x5AU;
    m.load(0x1000U, {0x0009U}); // NOP; DMAC work runs during the peripheral tick
    auto r = m.cpu.cpu_registers();
    r.pc = 0x1000U;
    m.cpu.set_registers(r);
    write_peripheral32(m.cpu.peripherals(), dmac_fixture::sar0, 0x00002000U);
    write_peripheral32(m.cpu.peripherals(), dmac_fixture::dar0, 0x00003000U);
    write_peripheral32(m.cpu.peripherals(), dmac_fixture::tcr0, 1U);
    write_peripheral32(m.cpu.peripherals(), dmac_fixture::dmaor, 0x00000001U);
    write_peripheral32(m.cpu.peripherals(), dmac_fixture::chcr0, 0x4201U); // byte unit
    m.cpu.set_bus_wait_callback([](std::uint32_t address, std::uint8_t bytes, bool locked) {
        CHECK_FALSE(locked);
        CHECK(bytes == 1U);
        if (address == 0x2000U) {
            return 2;
        }
        if (address == 0x3000U) {
            return 3;
        }
        return 0;
    });

    CHECK(m.cpu.step_instruction() == 6);
    CHECK(m.cpu.elapsed_cycles() == 6U);
    CHECK(m.ram[0x3000U] == 0x5AU);
}

TEST_CASE("sh2_peripherals DMAC cycle-steal transfers one unit per tick") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1001U] = 0xA1U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x5201U);     // DE | AR | TS=byte | SM=inc | DM=inc, cycle-steal

    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0xA0U);
    CHECK(f.ram[0x2001U] == 0x00U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) == 0U);

    f.p.tick(1U);
    CHECK(f.ram[0x2001U] == 0xA1U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 0U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
}

TEST_CASE("sh2_peripherals DMAC fixed priority selects channel zero first") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1100U] = 0xB0U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.chcr0, 0x5201U);
    f.w32reg(f.sar1, 0x00001100U);
    f.w32reg(f.dar1, 0x00002100U);
    f.w32reg(f.tcr1, 1U);
    f.w32reg(f.chcr1, 0x5201U);
    f.w32reg(f.dmaor, 0x00000001U); // DME, fixed priority

    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0xA0U);
    CHECK(f.ram[0x2100U] == 0x00U);

    f.p.tick(1U);
    CHECK(f.ram[0x2100U] == 0xB0U);
}

TEST_CASE("sh2_peripherals DMAC round-robin starts at channel one and alternates") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1001U] = 0xA1U;
    f.ram[0x1100U] = 0xB0U;
    f.ram[0x1101U] = 0xB1U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.chcr0, 0x5201U);
    f.w32reg(f.sar1, 0x00001100U);
    f.w32reg(f.dar1, 0x00002100U);
    f.w32reg(f.tcr1, 2U);
    f.w32reg(f.chcr1, 0x5201U);
    f.w32reg(f.dmaor, 0x00000009U); // DME | PR

    f.p.tick(1U);
    CHECK(f.ram[0x2100U] == 0xB0U);
    CHECK(f.ram[0x2000U] == 0x00U);

    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0xA0U);

    f.p.tick(1U);
    CHECK(f.ram[0x2101U] == 0xB1U);
}

TEST_CASE("sh2_peripherals DMAC edge request consumes one latched edge") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1001U] = 0xA1U;
    bool dreq = false;
    f.p.set_dreq_query([&](int channel) {
        CHECK(channel == 0);
        return dreq;
    });
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x4441U);     // DE | DS=edge | TS=word | DM=inc, AR=0

    dreq = true;
    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0xA0U);
    CHECK(f.ram[0x2001U] == 0xA1U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);

    f.ram[0x1000U] = 0xB0U;
    f.ram[0x1001U] = 0xB1U;
    f.p.tick(1U); // held-active edge line does not retrigger
    CHECK(f.ram[0x2002U] == 0x00U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);

    dreq = false;
    f.p.tick(1U);
    dreq = true;
    f.p.tick(1U);
    CHECK(f.ram[0x2002U] == 0xB0U);
    CHECK(f.ram[0x2003U] == 0xB1U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
}

TEST_CASE("sh2_peripherals INTC presents DMAC transfer-end interrupts") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA5U;
    f.p.write8(0xFFFFFEE2U, 0x09U); // IPRA[11:8]: DMAC priority 9
    f.w32reg(f.vcrdma0, 0x00000051U);
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x4205U);     // DE | IE | AR | TS=byte | DM=inc
    f.p.tick(1U);

    const auto irq = f.p.pending_onchip_irq();
    CHECK(irq.level == 9);
    CHECK(irq.vector == 0x51U);

    f.w32reg(f.chcr0, f.r32reg(f.chcr0) & ~0x04U); // CHCR.IE gates the request
    CHECK(f.p.pending_onchip_irq().level == 0);
}

TEST_CASE("sh2_peripherals DMAC honours fixed-source / incrementing-dest modes") {
    dmac_fixture f;
    f.ram[0x1000U] = 0x5AU; // single source byte, read repeatedly
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00003000U);
    f.w32reg(f.tcr0, 4U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x4211U); // DE | TB | AR | TS=byte | SM=fixed | DM=inc
    f.p.tick(1U);
    for (std::uint32_t i = 0; i < 4U; ++i) {
        CHECK(f.ram[0x3000U + i] == 0x5AU);
    }
}

TEST_CASE("sh2_peripherals DMAC stays idle until the master enable is set") {
    dmac_fixture f;
    f.ram[0x1000U] = 0x11U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00004000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.chcr0, 0x4201U); // channel enabled but DMAOR.DME still clear
    f.p.tick(1U);
    CHECK(f.ram[0x4000U] == 0x00U); // no transfer without DME
    f.w32reg(f.dmaor, 0x00000001U);
    f.p.tick(1U);
    CHECK(f.ram[0x4000U] == 0x11U); // now it runs
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

TEST_CASE("sh2_peripherals DIVU performs signed 32/32 division on the DVDNT write") {
    mnemos::chips::cpu::sh2_peripherals p;
    const auto wr32 = [&p](std::uint32_t addr, std::uint32_t v) {
        p.write8(addr, static_cast<std::uint8_t>(v >> 24U));
        p.write8(addr + 1U, static_cast<std::uint8_t>(v >> 16U));
        p.write8(addr + 2U, static_cast<std::uint8_t>(v >> 8U));
        p.write8(addr + 3U, static_cast<std::uint8_t>(v));
    };
    const auto rd32 = [&p](std::uint32_t addr) {
        return (static_cast<std::uint32_t>(p.read8(addr)) << 24U) |
               (static_cast<std::uint32_t>(p.read8(addr + 1U)) << 16U) |
               (static_cast<std::uint32_t>(p.read8(addr + 2U)) << 8U) | p.read8(addr + 3U);
    };

    wr32(0xFFFFFF00U, 7U);   // DVSR
    wr32(0xFFFFFF04U, 100U); // DVDNT -> divide fires
    p.tick(38U);
    CHECK(rd32(0xFFFFFF04U) == 100U); // not complete before cycle 39
    p.tick(1U);
    CHECK(rd32(0xFFFFFF04U) == 14U);         // quotient
    CHECK(rd32(0xFFFFFF14U) == 14U);         // DVDNTL mirror
    CHECK(rd32(0xFFFFFF10U) == 2U);          // remainder in DVDNTH
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 0U); // no overflow

    // Negative dividend: -100 / 7 = -14 rem -2 (truncating like the hardware).
    wr32(0xFFFFFF04U, static_cast<std::uint32_t>(-100));
    p.tick(39U);
    CHECK(rd32(0xFFFFFF04U) == static_cast<std::uint32_t>(-14));
    CHECK(rd32(0xFFFFFF10U) == static_cast<std::uint32_t>(-2));

    // Divide by zero saturates and sets DVCR.OVF.
    wr32(0xFFFFFF00U, 0U);
    wr32(0xFFFFFF04U, 5U);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 0U);
    p.tick(5U);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 0U);
    p.tick(1U);
    CHECK(rd32(0xFFFFFF04U) == 0x7FFFFFFFU);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 1U);
}

TEST_CASE("sh2 DIVU register read stalls until the pending operation completes") {
    machine m;
    m.load(0x1000U, {0x2102U, 0x6212U}); // MOV.L R0,@R1 ; MOV.L @R1,R2

    auto r = m.cpu.cpu_registers();
    r.pc = 0x1000U;
    r.r[0] = 100U;
    r.r[1] = 0xFFFFFF04U; // DVDNT
    m.cpu.set_registers(r);
    write_peripheral32(m.cpu.peripherals(), 0xFFFFFF00U, 7U);

    CHECK(m.cpu.step_instruction() == 1); // write starts the divider
    CHECK(read_peripheral32(m.cpu.peripherals(), 0xFFFFFF04U) == 100U);
    CHECK(m.cpu.step_instruction() == 39); // 38-cycle DIVU wait + 1-cycle load
    const auto after = m.cpu.cpu_registers();
    CHECK(after.r[2] == 14U);
    CHECK(m.cpu.elapsed_cycles() == 40U);
}

TEST_CASE("sh2 DIVU read right after a write costs one extra cycle when idle") {
    machine m;
    // MOV.L R0,@R1 (write DVSR -- no divide) ; MOV.L @R1,R2 ; MOV.L @R1,R2
    m.load(0x1000U, {0x2102U, 0x6212U, 0x6212U});
    auto r = m.cpu.cpu_registers();
    r.pc = 0x1000U;
    r.r[0] = 7U;
    r.r[1] = 0xFFFFFF00U; // DVSR: a write here arms the penalty without dividing
    m.cpu.set_registers(r);

    CHECK(m.cpu.step_instruction() == 1); // the write arms the write-then-read +1
    CHECK(m.cpu.step_instruction() == 2); // read right after the write: 1 + 1 penalty
    CHECK(m.cpu.cpu_registers().r[2] == 7U);
    CHECK(m.cpu.step_instruction() == 1); // second read: penalty already consumed
}

TEST_CASE("sh2_peripherals INTC presents DIVU overflow interrupts") {
    mnemos::chips::cpu::sh2_peripherals p;
    p.write8(0xFFFFFEE2U, 0x80U);              // IPRA[15:12]: DIVU priority 8
    write_peripheral32(p, 0xFFFFFF0CU, 0x42U); // VCRDIV
    write_peripheral32(p, 0xFFFFFF08U, 0x02U); // DVCR.OVFIE
    write_peripheral32(p, 0xFFFFFF00U, 0U);    // DVSR = 0 -> overflow
    write_peripheral32(p, 0xFFFFFF04U, 5U);    // DVDNT write starts 32/32 divide

    auto irq = p.pending_onchip_irq();
    CHECK(irq.level == 0);
    p.tick(5U);
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 0);
    p.tick(1U);
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 8);
    CHECK(irq.vector == 0x42U);

    write_peripheral32(p, 0xFFFFFF08U, 0x01U); // OVF remains, OVFIE cleared
    irq = p.pending_onchip_irq();
    CHECK(irq.level == 0);
}

TEST_CASE("sh2 accepts a DIVU overflow interrupt through the INTC") {
    machine m;
    m.w32(0x42U * 4U, 0x3000U);          // DIVU vector 0x42 -> handler
    m.load(0x3000U, {0xAFFEU, 0x0009U}); // handler: BRA self ; (delay) NOP
    auto& p = m.cpu.peripherals();
    p.write8(0xFFFFFEE2U, 0x80U);              // level 8 DIVU
    write_peripheral32(p, 0xFFFFFF0CU, 0x42U); // VCRDIV
    write_peripheral32(p, 0xFFFFFF08U, 0x02U); // DVCR.OVFIE
    write_peripheral32(p, 0xFFFFFF00U, 0U);    // DVSR = 0
    write_peripheral32(p, 0xFFFFFF04U, 5U);    // overflow is now pending
    p.tick(6U);

    auto r = m.cpu.cpu_registers();
    r.pc = 0x1000U;
    r.sr = 0U; // IMASK = 0 so the level-8 IRQ is accepted
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x0009U});
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x3000U); // vectored into the handler loop
}

TEST_CASE("sh2_peripherals DIVU performs signed 64/32 division on the DVDNTL write") {
    mnemos::chips::cpu::sh2_peripherals p;
    const auto wr32 = [&p](std::uint32_t addr, std::uint32_t v) {
        p.write8(addr, static_cast<std::uint8_t>(v >> 24U));
        p.write8(addr + 1U, static_cast<std::uint8_t>(v >> 16U));
        p.write8(addr + 2U, static_cast<std::uint8_t>(v >> 8U));
        p.write8(addr + 3U, static_cast<std::uint8_t>(v));
    };
    const auto rd32 = [&p](std::uint32_t addr) {
        return (static_cast<std::uint32_t>(p.read8(addr)) << 24U) |
               (static_cast<std::uint32_t>(p.read8(addr + 1U)) << 16U) |
               (static_cast<std::uint32_t>(p.read8(addr + 2U)) << 8U) | p.read8(addr + 3U);
    };

    // (1 << 35) / 32 = 1 << 30, remainder 0. Dividend = $00000008:00000000.
    wr32(0xFFFFFF00U, 32U);         // DVSR
    wr32(0xFFFFFF10U, 0x00000008U); // DVDNTH
    wr32(0xFFFFFF14U, 0x00000000U); // DVDNTL -> divide fires
    p.tick(39U);
    CHECK(rd32(0xFFFFFF14U) == 0x40000000U);
    CHECK(rd32(0xFFFFFF10U) == 0U);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 0U);
    // The shadow registers latch the result; the +$20 mirror reads the same.
    CHECK(rd32(0xFFFFFF1CU) == 0x40000000U);
    CHECK(rd32(0xFFFFFF34U) == 0x40000000U);

    // Quotient overflow (1 << 35 / 2 needs 34 bits): saturate + OVF.
    wr32(0xFFFFFF00U, 2U);
    wr32(0xFFFFFF10U, 0x00000008U);
    wr32(0xFFFFFF14U, 0x00000000U);
    p.tick(6U);
    CHECK(rd32(0xFFFFFF14U) == 0x7FFFFFFFU);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 1U);
}

TEST_CASE("sh2_peripherals DIVU pending result survives state round-trip") {
    mnemos::chips::cpu::sh2_peripherals p;
    write_peripheral32(p, 0xFFFFFF00U, 7U);
    write_peripheral32(p, 0xFFFFFF04U, 100U);
    p.tick(10U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    p.save_state(writer);

    mnemos::chips::cpu::sh2_peripherals q;
    mnemos::chips::state_reader reader(blob);
    q.load_state(reader);
    REQUIRE(reader.ok());
    q.tick(28U);
    CHECK(read_peripheral32(q, 0xFFFFFF04U) == 100U);
    q.tick(1U);
    CHECK(read_peripheral32(q, 0xFFFFFF04U) == 14U);
    CHECK(read_peripheral32(q, 0xFFFFFF10U) == 2U);
}

TEST_CASE("sh2_peripherals DIVU 64/32 of INT64_MIN by -1 overflows instead of crashing") {
    // $80000000:00000000 / $FFFFFFFF asks for +2^63: the one quotient that does
    // not fit the host's int64 either. The divider must take the overflow path
    // without evaluating the division (UB / SIGFPE on x86).
    mnemos::chips::cpu::sh2_peripherals p;
    const auto wr32 = [&p](std::uint32_t addr, std::uint32_t v) {
        p.write8(addr, static_cast<std::uint8_t>(v >> 24U));
        p.write8(addr + 1U, static_cast<std::uint8_t>(v >> 16U));
        p.write8(addr + 2U, static_cast<std::uint8_t>(v >> 8U));
        p.write8(addr + 3U, static_cast<std::uint8_t>(v));
    };
    const auto rd32 = [&p](std::uint32_t addr) {
        return (static_cast<std::uint32_t>(p.read8(addr)) << 24U) |
               (static_cast<std::uint32_t>(p.read8(addr + 1U)) << 16U) |
               (static_cast<std::uint32_t>(p.read8(addr + 2U)) << 8U) | p.read8(addr + 3U);
    };
    wr32(0xFFFFFF00U, 0xFFFFFFFFU); // DVSR = -1
    wr32(0xFFFFFF10U, 0x80000000U); // DVDNTH
    wr32(0xFFFFFF14U, 0x00000000U); // DVDNTL -> divide fires
    p.tick(6U);
    CHECK((rd32(0xFFFFFF08U) & 0x1U) == 1U); // DVCR.OVF
    // Negative dividend, negative divisor: positive overflow saturates high.
    CHECK(rd32(0xFFFFFF14U) == 0x7FFFFFFFU);
}
