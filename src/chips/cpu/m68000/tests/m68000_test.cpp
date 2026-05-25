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
