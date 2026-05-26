#include "m68000.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
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
    using mnemos::chips::cpu::m68000;
    using reset_kind = mnemos::chips::reset_kind;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{24U, mnemos::topology::endianness::big};
        m68000 cpu;

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

static_assert(std::is_base_of_v<mnemos::chips::icpu, m68000>);
static_assert(m68000::static_class == mnemos::chips::chip_class::cpu);

TEST_CASE("m68000 reports identity and registers under motorola.68000") {
    const m68000 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Motorola");
    CHECK(md.part_number == "MC68000");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("motorola.68000");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("MC68000"));
}

TEST_CASE("m68000 resets from the SSP/PC vectors in supervisor mode") {
    machine m;
    m.w32(0x0000U, 0x00002000U); // SSP
    m.w32(0x0004U, 0x00001000U); // PC
    m.cpu.reset(reset_kind::power_on);

    const auto r = m.cpu.cpu_registers();
    CHECK(r.a[7] == 0x2000U);
    CHECK(r.ssp == 0x2000U);
    CHECK(r.pc == 0x1000U);
    CHECK((r.sr & m68000::sr_s) != 0U);
    CHECK((r.sr & m68000::sr_ipm) == m68000::sr_ipm);
}

TEST_CASE("m68000 executes MOVEQ with sign extension and flags") {
    machine m;
    m.load(0x1000U, {0x7042U}); // MOVEQ #$42,D0
    m.set_pc(0x1000U);
    const int cycles = m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.d[0] == 0x00000042U);
    CHECK(cycles == 4);
    CHECK((r.sr & m68000::sr_z) == 0U);
    CHECK((r.sr & m68000::sr_n) == 0U);

    m.load(0x1000U, {0x70FFU}); // MOVEQ #-1,D0
    m.set_pc(0x1000U);
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.d[0] == 0xFFFFFFFFU); // sign-extended
    CHECK((r.sr & m68000::sr_n) != 0U);
}

TEST_CASE("m68000 MOVE.W and MOVE.L with immediates set N/Z and clear V/C") {
    machine m;
    // MOVE.W #$1234,D1
    m.load(0x1000U, {0x323CU, 0x1234U});
    m.set_pc(0x1000U);
    int cycles = m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().d[1] & 0xFFFFU) == 0x1234U);
    CHECK(cycles == 8); // opcode 4 + immediate word 4

    // MOVE.L #$12345678,D2
    m.load(0x1000U, {0x243CU, 0x1234U, 0x5678U});
    m.set_pc(0x1000U);
    cycles = m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().d[2] == 0x12345678U);
    CHECK(cycles == 12); // opcode 4 + immediate long 8
}

TEST_CASE("m68000 MOVE.W to a data register preserves the upper word") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.d[1] = 0xAAAA0000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x323CU, 0x1234U}); // MOVE.W #$1234,D1
    m.set_pc(0x1000U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().d[1] == 0xAAAA1234U);
}

TEST_CASE("m68000 MOVEA.W sign-extends and leaves the flags alone") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_ccr); // all CCR bits set
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x367CU, 0x8000U}); // MOVEA.W #$8000,A3
    m.set_pc(0x1000U);
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.a[3] == 0xFFFF8000U);                     // sign-extended to 32 bits
    CHECK((r.sr & m68000::sr_ccr) == m68000::sr_ccr); // flags untouched by MOVEA
}

TEST_CASE("m68000 MOVE.L postincrement reads memory and bumps the address") {
    machine m;
    m.w32(0x2000U, 0x11223344U);
    auto r = m.cpu.cpu_registers();
    r.a[0] = 0x2000U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x2018U}); // MOVE.L (A0)+,D0
    m.set_pc(0x1000U);
    const int cycles = m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.d[0] == 0x11223344U);
    CHECK(r.a[0] == 0x2004U); // post-incremented by 4
    CHECK(cycles == 12);      // opcode 4 + long read 8
}

TEST_CASE("m68000 MOVE.W predecrement adjusts the address and adds idle") {
    machine m;
    m.w16(0x3000U, 0x5566U);
    auto r = m.cpu.cpu_registers();
    r.a[1] = 0x3002U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x3221U}); // MOVE.W -(A1),D1
    m.set_pc(0x1000U);
    const int cycles = m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK((r.d[1] & 0xFFFFU) == 0x5566U);
    CHECK(r.a[1] == 0x3000U); // pre-decremented by 2
    CHECK(cycles == 10);      // opcode 4 + predec idle 2 + word read 4
}

TEST_CASE("m68000 MOVE.W from absolute-short sets Z and clears V/C") {
    machine m;
    m.w16(0x3000U, 0x0000U);
    auto r = m.cpu.cpu_registers();
    r.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_n | m68000::sr_v | m68000::sr_c |
                                      m68000::sr_x);
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x3038U, 0x3000U}); // MOVE.W ($3000).W,D0
    m.set_pc(0x1000U);
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK((r.d[0] & 0xFFFFU) == 0x0000U);
    CHECK((r.sr & m68000::sr_z) != 0U);
    CHECK((r.sr & m68000::sr_n) == 0U);
    CHECK((r.sr & m68000::sr_v) == 0U);
    CHECK((r.sr & m68000::sr_c) == 0U);
    CHECK((r.sr & m68000::sr_x) != 0U); // X is untouched by MOVE
}

TEST_CASE("m68000 resolves PC-relative source operands") {
    machine m;
    // MOVE.W (2,PC),D0 -> reads the word two bytes past the displacement word.
    m.load(0x1000U, {0x303AU, 0x0002U, 0x4321U});
    m.set_pc(0x1000U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().d[0] & 0xFFFFU) == 0x4321U);
}

TEST_CASE("m68000 round-trips its register state") {
    machine m;
    auto r = m.cpu.cpu_registers();
    for (std::size_t i = 0; i < 8; ++i) {
        r.d[i] = static_cast<std::uint32_t>(0x11111111U * (i + 1U));
        r.a[i] = static_cast<std::uint32_t>(0x22220000U + i);
    }
    r.pc = 0x00ABCDEU;
    r.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_z);
    r.usp = 0x0001F000U;
    r.ssp = 0x0000F000U;
    m.cpu.set_registers(r);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    m68000 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    const auto rr = restored.cpu_registers();
    CHECK(rr.d[3] == r.d[3]);
    CHECK(rr.a[5] == r.a[5]);
    CHECK(rr.pc == r.pc);
    CHECK(rr.sr == r.sr);
    CHECK(rr.usp == r.usp);
    CHECK(rr.ssp == r.ssp);
}

namespace {
    // Run a single instruction made of `words` at $1000 with D/A registers seeded,
    // and return the resulting register file.
    m68000::registers run_one(machine& m, std::initializer_list<std::uint16_t> words,
                              const m68000::registers& seed) {
        m.cpu.set_registers(seed);
        m.load(0x1000U, words);
        m.set_pc(0x1000U);
        m.cpu.step_instruction();
        return m.cpu.cpu_registers();
    }
} // namespace

TEST_CASE("m68000 ADD sets carry, extend and zero on word overflow") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000FFFFU;
    const auto r = run_one(m, {0x0640U, 0x0001U}, s); // ADDI.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0000U);
    CHECK((r.sr & m68000::sr_z) != 0U);
    CHECK((r.sr & m68000::sr_c) != 0U);
    CHECK((r.sr & m68000::sr_x) != 0U);
    CHECK((r.sr & m68000::sr_v) == 0U);
}

TEST_CASE("m68000 ADD.L between data registers") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000010U;
    s.d[1] = 0x00000020U;
    const auto r = run_one(m, {0xD281U}, s); // ADD.L D1,D0  -> D1 dest? check
    // ADD <ea>,Dn with dn=1, ea=D1(mode0 reg1): 0xD281 = ADD.L D1,D1? decode:
    // 1101 dn=001 opm=010(.L) mode=000 reg=001 -> ADD.L D1,D1 = 0x20+0x20.
    CHECK(r.d[1] == 0x00000040U);
}

TEST_CASE("m68000 SUBI sets the borrow/extend flags") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000000U;
    const auto r = run_one(m, {0x0440U, 0x0001U}, s); // SUBI.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0xFFFFU);
    CHECK((r.sr & m68000::sr_n) != 0U);
    CHECK((r.sr & m68000::sr_c) != 0U);
    CHECK((r.sr & m68000::sr_x) != 0U);
}

TEST_CASE("m68000 CMPI sets Z on equality and leaves operands intact") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000042U;
    const auto r = run_one(m, {0x0C40U, 0x0042U}, s); // CMPI.W #$42,D0
    CHECK(r.d[0] == 0x00000042U);                     // unchanged
    CHECK((r.sr & m68000::sr_z) != 0U);
    CHECK((r.sr & m68000::sr_c) == 0U);
}

TEST_CASE("m68000 ADDQ and SUBQ") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000000AU;
    s.d[2] = 0x00000005U;
    auto r = run_one(m, {0x5680U}, s); // ADDQ.L #3,D0
    CHECK(r.d[0] == 0x0000000DU);
    r = run_one(m, {0x5342U}, s); // SUBQ.W #1,D2
    CHECK((r.d[2] & 0xFFFFU) == 0x0004U);
}

TEST_CASE("m68000 ADDQ to an address register skips the flags and is full-width") {
    machine m;
    m68000::registers s{};
    s.a[0] = 0x00001000U;
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_ccr);
    const auto r = run_one(m, {0x5288U}, s); // ADDQ.L #1,A0
    CHECK(r.a[0] == 0x00001001U);
    CHECK((r.sr & m68000::sr_ccr) == m68000::sr_ccr); // flags untouched
}

TEST_CASE("m68000 MULU and MULS") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000004U;
    s.d[1] = 0x00000003U;
    auto r = run_one(m, {0xC0FCU, 0x0003U}, s); // MULU #3,D0
    CHECK(r.d[0] == 0x0000000CU);

    r = run_one(m, {0xC3FCU, 0xFFFEU}, s); // MULS #-2,D1
    CHECK(r.d[1] == 0xFFFFFFFAU);          // 3 * -2 = -6
    CHECK((r.sr & m68000::sr_n) != 0U);
}

TEST_CASE("m68000 NEG and NEGX") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000001U;
    auto r = run_one(m, {0x4440U}, s); // NEG.W D0
    CHECK((r.d[0] & 0xFFFFU) == 0xFFFFU);
    CHECK((r.sr & m68000::sr_c) != 0U);

    s.d[0] = 0x00000000U;
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_x); // X set
    r = run_one(m, {0x4000U}, s);                                   // NEGX.B D0  (0 - 0 - X)
    CHECK((r.d[0] & 0xFFU) == 0xFFU);
}

TEST_CASE("m68000 CLR zeroes the operand and sets Z") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x12345678U;
    const auto r = run_one(m, {0x4280U}, s); // CLR.L D0
    CHECK(r.d[0] == 0x00000000U);
    CHECK((r.sr & m68000::sr_z) != 0U);
}

TEST_CASE("m68000 EXT sign-extends byte->word and word->long") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000080U;
    auto r = run_one(m, {0x4880U}, s); // EXT.W D0
    CHECK((r.d[0] & 0xFFFFU) == 0xFF80U);
    CHECK((r.sr & m68000::sr_n) != 0U);

    s.d[1] = 0x00008000U;
    r = run_one(m, {0x48C1U}, s); // EXT.L D1
    CHECK(r.d[1] == 0xFFFF8000U);
}

TEST_CASE("m68000 TST sets N/Z from the operand and clears V/C") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000000U;
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_v | m68000::sr_c);
    const auto r = run_one(m, {0x4A40U}, s); // TST.W D0
    CHECK((r.sr & m68000::sr_z) != 0U);
    CHECK((r.sr & m68000::sr_v) == 0U);
    CHECK((r.sr & m68000::sr_c) == 0U);
}

TEST_CASE("m68000 ADDX adds with the extend flag") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000001U;
    s.d[1] = 0x00000002U;
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_x);
    const auto r = run_one(m, {0xD300U}, s); // ADDX.B D0,D1  -> 2 + 1 + X(1)
    CHECK((r.d[1] & 0xFFU) == 0x04U);
}

TEST_CASE("m68000 ADD to memory writes back the result") {
    machine m;
    m.w16(0x2000U, 0x1111U);
    m68000::registers s{};
    s.d[0] = 0x00002222U;
    s.a[0] = 0x00002000U;
    const auto r = run_one(m, {0xD150U}, s); // ADD.W D0,(A0)
    CHECK(m.bus.read8(0x2000U) == 0x33U);    // 0x1111 + 0x2222 = 0x3333 (big-endian hi byte)
    CHECK(m.bus.read8(0x2001U) == 0x33U);
    (void)r;
}

TEST_CASE("m68000 ANDI/ORI/EORI immediates") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000FFFFU;
    auto r = run_one(m, {0x0240U, 0x0F0FU}, s); // ANDI.W #$0F0F,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0F0FU);

    s.d[1] = 0x00000000U;
    r = run_one(m, {0x0001U, 0x0080U}, s); // ORI.B #$80,D1
    CHECK((r.d[1] & 0xFFU) == 0x80U);
    CHECK((r.sr & m68000::sr_n) != 0U); // byte bit 7 set

    s.d[2] = 0x12345678U;
    r = run_one(m, {0x0A82U, 0xFFFFU, 0xFFFFU}, s); // EORI.L #$FFFFFFFF,D2
    CHECK(r.d[2] == 0xEDCBA987U);
}

TEST_CASE("m68000 AND/OR/EOR register forms") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000FFFFU;
    auto r = run_one(m, {0xC07CU, 0x0FF0U}, s); // AND.W #$0FF0,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0FF0U);

    s.d[0] = 0x00000000U;
    r = run_one(m, {0x807CU, 0x1234U}, s); // OR.W #$1234,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x1234U);

    s.d[0] = 0x0000AAAAU;
    s.d[1] = 0x00005555U;
    r = run_one(m, {0xB340U}, s); // EOR.W D1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0xFFFFU);
}

TEST_CASE("m68000 NOT inverts the operand") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00001234U;
    const auto r = run_one(m, {0x4640U}, s); // NOT.W D0
    CHECK((r.d[0] & 0xFFFFU) == 0xEDCBU);
}

TEST_CASE("m68000 static bit ops BTST/BSET") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000004U;                       // bit 2 set
    auto r = run_one(m, {0x0800U, 0x0002U}, s); // BTST #2,D0
    CHECK(r.d[0] == 0x00000004U);               // unchanged
    CHECK((r.sr & m68000::sr_z) == 0U);         // bit was 1 -> Z clear

    s.d[0] = 0x00000000U;
    r = run_one(m, {0x08C0U, 0x0003U}, s); // BSET #3,D0
    CHECK(r.d[0] == 0x00000008U);
    CHECK((r.sr & m68000::sr_z) != 0U); // tested bit was 0 -> Z set
}

TEST_CASE("m68000 dynamic BCLR clears the selected bit") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x000000FFU;
    s.d[1] = 0x00000000U;                    // bit number 0
    const auto r = run_one(m, {0x0380U}, s); // BCLR D1,D0
    CHECK(r.d[0] == 0x000000FEU);
    CHECK((r.sr & m68000::sr_z) == 0U); // bit 0 was set -> Z clear
}

TEST_CASE("m68000 ANDI to CCR masks the condition codes") {
    machine m;
    m68000::registers s{};
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_ccr); // all CCR bits set
    const auto r = run_one(m, {0x023CU, 0x0000U}, s);                 // ANDI.B #0,CCR
    CHECK((r.sr & m68000::sr_ccr) == 0U);
    CHECK((r.sr & m68000::sr_s) != 0U); // supervisor bit untouched
}

TEST_CASE("m68000 LSL/LSR set carry+extend and zero") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00008000U;
    auto r = run_one(m, {0xE348U}, s); // LSL.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0000U);
    CHECK((r.sr & m68000::sr_c) != 0U);
    CHECK((r.sr & m68000::sr_x) != 0U);
    CHECK((r.sr & m68000::sr_z) != 0U);

    s.d[0] = 0x00000001U;
    r = run_one(m, {0xE248U}, s); // LSR.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0000U);
    CHECK((r.sr & m68000::sr_c) != 0U);
    CHECK((r.sr & m68000::sr_z) != 0U);
}

TEST_CASE("m68000 ASR preserves the sign bit") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00008000U;
    const auto r = run_one(m, {0xE240U}, s); // ASR.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0xC000U);
    CHECK((r.sr & m68000::sr_n) != 0U);
    CHECK((r.sr & m68000::sr_c) == 0U);
}

TEST_CASE("m68000 ROL/ROR rotate and set carry from the wrapped bit") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00008000U;
    auto r = run_one(m, {0xE358U}, s); // ROL.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0001U);
    CHECK((r.sr & m68000::sr_c) != 0U);

    s.d[0] = 0x00000001U;
    r = run_one(m, {0xE258U}, s); // ROR.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x8000U);
    CHECK((r.sr & m68000::sr_c) != 0U);
}

TEST_CASE("m68000 ROXL rotates through the extend bit") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000000U;
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_x); // X set
    const auto r = run_one(m, {0xE350U}, s);                        // ROXL.W #1,D0
    CHECK((r.d[0] & 0xFFFFU) == 0x0001U);                           // X rotated into bit 0
    CHECK((r.sr & m68000::sr_c) == 0U);                             // nothing shifted out
    CHECK((r.sr & m68000::sr_x) == 0U);
}

TEST_CASE("m68000 ASL.L with the shift count in a register") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000001U;
    s.d[1] = 0x00000004U;                    // count = 4
    const auto r = run_one(m, {0xE3A0U}, s); // ASL.L D1,D0
    CHECK(r.d[0] == 0x00000010U);
}

TEST_CASE("m68000 memory shift operates on a word in memory") {
    machine m;
    m.w16(0x2000U, 0x0002U);
    m68000::registers s{};
    s.a[0] = 0x00002000U;
    const auto r = run_one(m, {0xE2D0U}, s); // LSR (A0)
    CHECK(m.bus.read8(0x2000U) == 0x00U);
    CHECK(m.bus.read8(0x2001U) == 0x01U); // 0x0002 >> 1 = 0x0001
    (void)r;
}

TEST_CASE("m68000 BRA and conditional Bcc") {
    machine m;
    m68000::registers s{};
    auto r = run_one(m, {0x6004U}, s); // BRA.B +4 -> ($1002)+4
    CHECK(r.pc == 0x1006U);

    s.sr = m68000::sr_z;
    r = run_one(m, {0x6704U}, s); // BEQ.B +4, Z set -> taken
    CHECK(r.pc == 0x1006U);

    s.sr = 0U;
    r = run_one(m, {0x6704U}, s); // BEQ.B +4, Z clear -> not taken
    CHECK(r.pc == 0x1002U);
}

TEST_CASE("m68000 BSR pushes the return address") {
    machine m;
    m68000::registers s{};
    s.a[7] = 0x00003000U;
    const auto r = run_one(m, {0x6104U}, s); // BSR.B +4
    CHECK(r.pc == 0x1006U);
    CHECK(r.a[7] == 0x00002FFCU);         // SP -= 4
    CHECK(m.bus.read8(0x2FFEU) == 0x10U); // return address $00001002, big-endian
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
}

TEST_CASE("m68000 JSR then RTS returns to the caller") {
    machine m;
    m68000::registers s{};
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4EB8U, 0x2000U}); // JSR ($2000).W
    m.load(0x2000U, {0x4E75U});          // RTS
    m.cpu.step_instruction();            // JSR
    CHECK(m.cpu.cpu_registers().pc == 0x2000U);
    m.cpu.step_instruction();                   // RTS
    CHECK(m.cpu.cpu_registers().pc == 0x1004U); // instruction after the JSR
    CHECK(m.cpu.cpu_registers().a[7] == 0x00003000U);
}

TEST_CASE("m68000 DBF loops until the counter underflows") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000002U;
    auto r = run_one(m, {0x51C8U, 0xFFFEU}, s); // DBF D0,-2
    CHECK((r.d[0] & 0xFFFFU) == 0x0001U);
    CHECK(r.pc == 0x1000U); // branched back

    s.d[0] = 0x00000000U;
    r = run_one(m, {0x51C8U, 0xFFFEU}, s); // DBF D0,-2 with D0 == 0
    CHECK((r.d[0] & 0xFFFFU) == 0xFFFFU);  // underflowed
    CHECK(r.pc == 0x1004U);                // fell through
}

TEST_CASE("m68000 Scc writes a boolean byte") {
    machine m;
    m68000::registers s{};
    s.sr = m68000::sr_z;
    auto r = run_one(m, {0x57C0U}, s); // SEQ D0, Z set -> 0xFF
    CHECK((r.d[0] & 0xFFU) == 0xFFU);
    s.sr = 0U;
    r = run_one(m, {0x57C0U}, s); // SEQ D0, Z clear -> 0x00
    CHECK((r.d[0] & 0xFFU) == 0x00U);
}

TEST_CASE("m68000 TRAP vectors through the exception table") {
    machine m;
    m.w32(0x0080U, 0x00004000U); // vector 32 (TRAP #0) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E40U}); // TRAP #0
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FFAU);         // pushed PC (4) + SR (2)
    CHECK(m.bus.read8(0x2FFEU) == 0x10U); // stacked return PC $1002
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
}

TEST_CASE("m68000 RTE restores SR and PC from the stack frame") {
    machine m;
    m.w16(0x2FFAU, 0x2000U);     // saved SR (S set)
    m.w32(0x2FFCU, 0x00001234U); // saved PC
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00002FFAU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E73U}); // RTE
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00001234U);
    CHECK((r.sr & m68000::sr_s) != 0U);
    CHECK(r.a[7] == 0x00003000U);
}

TEST_CASE("m68000 services an autovectored interrupt") {
    machine m;
    m.w32(0x0070U, 0x00005000U); // autovector level 4 = vector 28 -> $5000
    m68000::registers s{};
    s.sr = m68000::sr_s; // IPM = 0, so a level-4 request is accepted
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.cpu.set_irq_level(4);
    m.cpu.step_instruction(); // takes the interrupt instead of executing
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00005000U);
    CHECK(((r.sr >> 8U) & 7U) == 4U); // IPM raised to the accepted level
    CHECK(r.a[7] == 0x00002FFAU);
}

TEST_CASE("m68000 traps a privileged instruction executed in user mode") {
    machine m;
    m.w32(0x0020U, 0x00006000U); // privilege-violation vector (8) -> $6000
    m68000::registers s{};
    s.sr = 0U; // user mode
    s.a[7] = 0x00003000U;
    s.ssp = 0x00004000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E72U, 0x2000U}); // STOP #$2000 (privileged)
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00006000U);
    CHECK((r.sr & m68000::sr_s) != 0U); // now supervisor
    CHECK(r.a[7] == 0x00003FFAU);       // pushed onto SSP ($4000 - 6)
}

TEST_CASE("m68000 LINK and UNLK manage a stack frame") {
    machine m;
    m68000::registers s{};
    s.a[6] = 0x12345678U;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E56U, 0xFFFCU}); // LINK A6,#-4
    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.a[6] == 0x00002FFCU); // A6 = SP after the push
    CHECK(r.a[7] == 0x00002FF8U); // SP -= 4 (frame) after pushing old A6

    m.load(r.pc, {0x4E5EU}); // UNLK A6
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.a[6] == 0x12345678U); // restored
    CHECK(r.a[7] == 0x00003000U); // SP restored
}

TEST_CASE("m68000 DIVU and DIVS produce quotient + remainder") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000000AU;                       // 10
    auto r = run_one(m, {0x80FCU, 0x0003U}, s); // DIVU.W #3,D0
    CHECK(r.d[0] == 0x00010003U);               // remainder 1 (hi), quotient 3 (lo)

    s.d[0] = 0x0000000AU;
    r = run_one(m, {0x81FCU, 0xFFFDU}, s); // DIVS.W #-3,D0
    CHECK(r.d[0] == 0x0001FFFDU);          // remainder 1, quotient -3
    CHECK((r.sr & m68000::sr_n) != 0U);
}

TEST_CASE("m68000 DIVU by zero traps through vector 5") {
    machine m;
    m.w32(0x0014U, 0x00007000U); // div-by-zero vector (5)
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.d[0] = 0x12345678U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x80FCU, 0x0000U}); // DIVU.W #0,D0
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x00007000U);
    CHECK(m.cpu.cpu_registers().a[7] == 0x00002FFAU); // exception frame pushed
}

TEST_CASE("m68000 CHK passes in range and traps out of range") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000005U;
    auto r = run_one(m, {0x41BCU, 0x000AU}, s); // CHK #10,D0 (0 <= 5 <= 10)
    CHECK(r.pc == 0x1004U);                     // no trap: fell through

    machine m2;
    m2.w32(0x0018U, 0x00008000U); // CHK vector (6)
    m68000::registers s2{};
    s2.sr = m68000::sr_s;
    s2.a[7] = 0x00003000U;
    s2.d[0] = 0x00000014U; // 20 > 10
    s2.pc = 0x1000U;
    m2.cpu.set_registers(s2);
    m2.load(0x1000U, {0x41BCU, 0x000AU}); // CHK #10,D0
    m2.cpu.step_instruction();
    CHECK(m2.cpu.cpu_registers().pc == 0x00008000U); // trapped
}

TEST_CASE("m68000 MOVE from SR and to CCR") {
    machine m;
    m68000::registers s{};
    s.sr = static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_z);
    auto r = run_one(m, {0x40C0U}, s); // MOVE SR,D0
    CHECK((r.d[0] & 0xFFFFU) == static_cast<std::uint16_t>(m68000::sr_s | m68000::sr_z));

    s = m68000::registers{};
    s.d[0] = 0x0000001FU;
    r = run_one(m, {0x44C0U}, s); // MOVE D0,CCR
    CHECK((r.sr & m68000::sr_ccr) == m68000::sr_ccr);
}

TEST_CASE("m68000 MOVE to SR and ORI to SR are privileged writes") {
    machine m;
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.d[0] = 0x2700U;                  // S + IPM 7
    auto r = run_one(m, {0x46C0U}, s); // MOVE D0,SR
    CHECK(r.sr == 0x2700U);

    s = m68000::registers{};
    s.sr = m68000::sr_s;                       // S, IPM 0
    r = run_one(m, {0x007CU, 0x0700U}, s);     // ORI #$0700,SR
    CHECK((r.sr & m68000::sr_ipm) == 0x0700U); // IPM raised to 7

    // In user mode the SR write traps (privilege).
    machine m2;
    m2.w32(0x0020U, 0x00006000U); // privilege vector (8)
    m68000::registers u{};
    u.sr = 0U; // user
    u.a[7] = 0x00003000U;
    u.ssp = 0x00004000U;
    u.pc = 0x1000U;
    m2.cpu.set_registers(u);
    m2.load(0x1000U, {0x46C0U}); // MOVE D0,SR in user mode
    m2.cpu.step_instruction();
    CHECK(m2.cpu.cpu_registers().pc == 0x00006000U);
}

TEST_CASE("m68000 ABCD / SBCD / NBCD packed-decimal math") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000019U;              // BCD 19
    s.d[1] = 0x00000008U;              // BCD 8
    auto r = run_one(m, {0xC101U}, s); // ABCD D1,D0 -> 27
    CHECK((r.d[0] & 0xFFU) == 0x27U);

    r = run_one(m, {0x8101U}, s); // SBCD D1,D0 -> 11
    CHECK((r.d[0] & 0xFFU) == 0x11U);

    s = m68000::registers{};
    s.d[0] = 0x00000001U;
    r = run_one(m, {0x4800U}, s); // NBCD D0 -> 99 with borrow
    CHECK((r.d[0] & 0xFFU) == 0x99U);
    CHECK((r.sr & m68000::sr_c) != 0U);
}

TEST_CASE("m68000 SWAP exchanges the register halves") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x12345678U;
    const auto r = run_one(m, {0x4840U}, s); // SWAP D0
    CHECK(r.d[0] == 0x56781234U);
}

TEST_CASE("m68000 EXG exchanges two registers") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x0000AAAAU;
    s.d[1] = 0x0000BBBBU;
    auto r = run_one(m, {0xC141U}, s); // EXG D0,D1
    CHECK(r.d[0] == 0x0000BBBBU);
    CHECK(r.d[1] == 0x0000AAAAU);

    s = m68000::registers{};
    s.d[0] = 0x11111111U;
    s.a[1] = 0x22222222U;
    r = run_one(m, {0xC189U}, s); // EXG D0,A1
    CHECK(r.d[0] == 0x22222222U);
    CHECK(r.a[1] == 0x11111111U);
}

TEST_CASE("m68000 LEA loads the effective address") {
    machine m;
    m68000::registers s{};
    s.a[0] = 0x00001000U;
    const auto r = run_one(m, {0x43E8U, 0x0004U}, s); // LEA (4,A0),A1
    CHECK(r.a[1] == 0x00001004U);
}

TEST_CASE("m68000 PEA pushes the effective address") {
    machine m;
    m68000::registers s{};
    s.a[0] = 0x00002000U;
    s.a[7] = 0x00003000U;
    const auto r = run_one(m, {0x4850U}, s); // PEA (A0)
    CHECK(r.a[7] == 0x00002FFCU);
    CHECK(m.bus.read8(0x2FFEU) == 0x20U); // pushed $00002000
    CHECK(m.bus.read8(0x2FFFU) == 0x00U);
}

TEST_CASE("m68000 TAS tests and sets bit 7") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00000000U;
    const auto r = run_one(m, {0x4AC0U}, s); // TAS D0
    CHECK((r.d[0] & 0xFFU) == 0x80U);
    CHECK((r.sr & m68000::sr_z) != 0U); // the original (0) was tested
}

TEST_CASE("m68000 MOVEM round-trips registers through the stack") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0xAAAA0000U;
    s.d[1] = 0xBBBB1111U;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x48E7U, 0xC000U}); // MOVEM.L D0/D1,-(A7)
    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.a[7] == 0x00002FF8U); // two longwords pushed

    m.load(r.pc, {0x4CDFU, 0x000CU}); // MOVEM.L (A7)+,D2/D3
    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.d[2] == 0xAAAA0000U); // restored from D0
    CHECK(r.d[3] == 0xBBBB1111U); // restored from D1
    CHECK(r.a[7] == 0x00003000U); // stack balanced
}

TEST_CASE("m68000 MOVEP scatters and gathers bytes at even addresses") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x00001234U;
    s.a[0] = 0x00002000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x0188U, 0x0000U}); // MOVEP.W D0,(0,A0)
    m.cpu.step_instruction();
    CHECK(m.bus.read8(0x2000U) == 0x12U); // high byte at the base
    CHECK(m.bus.read8(0x2002U) == 0x34U); // low byte two bytes on
    CHECK(m.bus.read8(0x2001U) == 0x00U); // odd byte untouched

    m.load(0x1006U, {0x0308U, 0x0000U}); // MOVEP.W (0,A0),D1
    m.set_pc(0x1006U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().d[1] & 0xFFFFU) == 0x1234U);
}

TEST_CASE("m68000 MOVEP.L writes four bytes with a 2-byte stride") {
    machine m;
    m68000::registers s{};
    s.d[0] = 0x12345678U;
    s.a[0] = 0x00002000U;
    const auto r = run_one(m, {0x01C8U, 0x0000U}, s); // MOVEP.L D0,(0,A0)
    CHECK(m.bus.read8(0x2000U) == 0x12U);
    CHECK(m.bus.read8(0x2002U) == 0x34U);
    CHECK(m.bus.read8(0x2004U) == 0x56U);
    CHECK(m.bus.read8(0x2006U) == 0x78U);
    (void)r;
}

TEST_CASE("JSR pushes return address as big-endian long (BoV $0690 scenario)") {
    machine m;
    // Mirror BoV's first JSR pattern, but scaled to the 64 KiB test bus:
    //   SSP = $0FFE, initial PC = $0190
    //   Instruction at $0190: JSR $00000500 (6 bytes: 4EB9 0000 0500)
    //   Return address pushed should be $0196 (PC + 6).
    m.w32(0x0000U, 0x00000FFEU); // SSP
    m.w32(0x0004U, 0x00000190U); // PC
    m.load(0x0190U, {0x4EB9U, 0x0000U, 0x0500U});
    m.cpu.reset(reset_kind::power_on);

    auto r = m.cpu.cpu_registers();
    REQUIRE(r.a[7] == 0x00000FFEU);
    REQUIRE(r.pc == 0x00000190U);

    m.cpu.step_instruction();

    r = m.cpu.cpu_registers();
    CHECK(r.a[7] == 0x00000FFAU); // SP -= 4
    CHECK(r.pc == 0x00000500U);   // jumped to target

    // 4-byte BE encoding of return address $00000196 at SP..SP+3 = $FFA..$FFD.
    INFO("ram[$FFA]=" << std::hex << +m.ram[0x0FFAU]
         << " [$FFB]=" << +m.ram[0x0FFBU]
         << " [$FFC]=" << +m.ram[0x0FFCU]
         << " [$FFD]=" << +m.ram[0x0FFDU]);
    CHECK(m.ram[0x0FFAU] == 0x00U); // bits 24-31 of $00000196
    CHECK(m.ram[0x0FFBU] == 0x00U); // bits 16-23
    CHECK(m.ram[0x0FFCU] == 0x01U); // bits  8-15
    CHECK(m.ram[0x0FFDU] == 0x96U); // bits  0- 7

    // What the 68K would read back as a word at $FFC must be $0196.
    const std::uint32_t word = (static_cast<std::uint32_t>(m.ram[0x0FFCU]) << 8U) |
                               m.ram[0x0FFDU];
    CHECK(word == 0x0196U);
}
