#include "scu_dsp.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "introspection_views.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::cpu::scu_dsp;
    using reset_kind = mnemos::chips::reset_kind;

    // Microinstruction opcode classes (top 2 bits = format selector).
    constexpr std::uint32_t mvi_op = 0x80000000U; // bits 31-30 = 10

    // MVI <imm25>, [dst] (unconditional). dst occupies bits 29-26.
    constexpr std::uint32_t mvi(std::uint32_t dst, std::uint32_t imm25) {
        return mvi_op | (dst << 26U) | (imm25 & 0x01FFFFFFU);
    }

    // Operation command: ALU + X + Y + D1 (bits 31-30 = 00).
    constexpr std::uint32_t op_cmd(std::uint32_t alu, std::uint32_t x, std::uint32_t y,
                                   std::uint32_t d1) {
        return (alu << 26U) | (x << 20U) | (y << 14U) | (d1 & 0x3FFFU);
    }

    // Control-class opcodes (bits 31-28).
    constexpr std::uint32_t end_op = 0xF0000000U;  // END
    constexpr std::uint32_t endi_op = 0xF8000000U; // ENDI (raises the E IRQ flag)

    // JMP <addr8> (unconditional): bits 31-28 = 1101.
    constexpr std::uint32_t jmp(std::uint8_t addr) {
        return 0xD0000000U | static_cast<std::uint32_t>(addr);
    }

    // A DMA between the D0-bus and DSP data RAM (bits 31-28 = 1100).
    //   dir: 0 = external->DSP, 1 = DSP->external
    //   bank: target/source data-RAM bank
    //   count: immediate longword count (bits 7-0)
    //   stride: address-add mode (bits 17-15). On the DSP->external path the add
    //     is (1<<stride)&~1, so stride 2 -> +4 bytes; the external->DSP path
    //     honours only stride bit 1.
    constexpr std::uint32_t dma(bool dsp_to_ext, std::uint32_t bank, std::uint32_t count,
                                std::uint32_t stride) {
        std::uint32_t op = 0xC0000000U;
        if (dsp_to_ext) {
            op |= 0x1000U;
        }
        op |= (stride & 0x7U) << 15U;
        op |= (bank & 0x3U) << 8U;
        op |= (count & 0xFFU);
        return op;
    }

    // A small machine: a topology bus for the D0-bus DMA plus the DSP. The
    // microprogram is loaded into internal program RAM; PPAF execute is driven
    // through the port window so the pipeline primes naturally.
    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{32U, mnemos::topology::endianness::big};
        scu_dsp dsp;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            dsp.attach_bus(bus);
            dsp.reset(reset_kind::power_on);
        }

        void load(std::uint8_t addr, std::initializer_list<std::uint32_t> words) {
            std::uint8_t a = addr;
            for (const std::uint32_t word : words) {
                dsp.write_program(a++, word);
            }
        }

        // Arm execution from program address 0 (PPAF: bit15 load-PC, bit16 EX).
        void start() { dsp.write_reg(0x0U, 0x00008000U | 0x00010000U); }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, scu_dsp>);

TEST_CASE("scu_dsp reports identity and registers under sega.scu_dsp") {
    const scu_dsp dsp;
    const auto md = dsp.metadata();
    CHECK(md.manufacturer == "Sega");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("sega.scu_dsp");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
    CHECK(chip->metadata().part_number == "SCU-DSP");
}

TEST_CASE("scu_dsp resets to a defined state") {
    machine m;
    const auto r = m.dsp.cpu_registers();
    CHECK(r.pc == 0U);
    CHECK(r.rx == 0U);
    CHECK(r.ry == 0U);
    CHECK(r.lop == 0U);
    CHECK_FALSE(r.ex_flag);
    CHECK_FALSE(r.z_flag);
    CHECK_FALSE(r.end_irq);
    CHECK(m.dsp.elapsed_cycles() == 0U);
}

TEST_CASE("scu_dsp MVI loads an immediate into RX") {
    machine m;
    m.load(0x00U, {mvi(4U, 0x42U), end_op}); // MVI #$42,RX ; END
    m.start();
    m.dsp.step_instruction();
    CHECK(m.dsp.cpu_registers().rx == 0x42U);
}

TEST_CASE("scu_dsp MVI sign-extends a negative 25-bit immediate") {
    machine m;
    // imm25 = 0x1FFFFFF == -1 sign-extended across the 25-bit field.
    m.load(0x00U, {mvi(4U, 0x01FFFFFFU), end_op});
    m.start();
    m.dsp.step_instruction();
    CHECK(m.dsp.cpu_registers().rx == 0xFFFFFFFFU);
}

TEST_CASE("scu_dsp ALU ADD writes the ALU register and MOV ALU,A updates AC") {
    machine m;
    // Seed AC.L = 5 and P.L = 3 via MVI, then ADD with Y-bus MOV ALU,A.
    //   MVI #5,[dst=Y? ] -- instead seed via data RAM is complex; drive AC/P
    //   directly through set_registers for a deterministic ALU check.
    auto r = m.dsp.cpu_registers();
    r.acl = 5U;
    r.pl = 3U;
    m.dsp.set_registers(r);

    // y_process = 0b010 (MOV ALU,A) -> y_ctrl = 0x10; ALU = ADD (4).
    m.load(0x00U, {op_cmd(4U, 0U, 0x10U, 0U), end_op});
    m.start();
    m.dsp.step_instruction();

    const auto after = m.dsp.cpu_registers();
    CHECK(after.alu_l == 8U); // 5 + 3
    CHECK(after.acl == 8U);   // copied back by MOV ALU,A
    CHECK_FALSE(after.z_flag);
    CHECK_FALSE(after.c_flag);
}

TEST_CASE("scu_dsp ALU ADD sets carry and zero on wraparound") {
    machine m;
    auto r = m.dsp.cpu_registers();
    r.acl = 0xFFFFFFFFU;
    r.pl = 1U;
    m.dsp.set_registers(r);
    m.load(0x00U, {op_cmd(4U, 0U, 0U, 0U), end_op}); // ADD only (no MOV ALU,A)
    m.start();
    m.dsp.step_instruction();
    const auto after = m.dsp.cpu_registers();
    CHECK(after.alu_l == 0U);
    CHECK(after.c_flag);
    CHECK(after.z_flag);
}

TEST_CASE("scu_dsp END stops execution") {
    machine m;
    m.load(0x00U, {end_op});
    m.start();
    CHECK(m.dsp.executing());
    m.dsp.step_instruction();
    CHECK_FALSE(m.dsp.executing());
}

TEST_CASE("scu_dsp ENDI raises the end-of-program IRQ flag") {
    machine m;
    m.load(0x00U, {endi_op});
    m.start();
    m.dsp.step_instruction();
    CHECK(m.dsp.end_irq());
    CHECK_FALSE(m.dsp.executing());
    m.dsp.clear_end_irq();
    CHECK_FALSE(m.dsp.end_irq());
}

TEST_CASE("scu_dsp JMP redirects the program counter through the delay slot") {
    machine m;
    // MVI destinations: 4 = RX, 5 = PL. The JMP target writes PL so the delay
    // slot (RX) and the post-jump instruction land in distinct registers.
    // 0: JMP 4 ; 1: MVI #$11,RX (delay slot) ; 4: MVI #$22,PL ; 5: END
    m.load(0x00U, {jmp(0x04U), mvi(4U, 0x11U)});
    m.load(0x04U, {mvi(5U, 0x22U), end_op});
    m.start();
    m.dsp.step_instruction(); // executes JMP (pc retargets to 4)
    m.dsp.step_instruction(); // executes the prefetched delay slot (MVI RX)
    CHECK(m.dsp.cpu_registers().rx == 0x11U);
    m.dsp.step_instruction(); // executes the jump target (MVI PL)
    CHECK(m.dsp.cpu_registers().pl == 0x22U);
}

TEST_CASE("scu_dsp DMA copies external memory into data RAM") {
    machine m;
    // Four big-endian longwords at $0100.
    const std::array<std::uint32_t, 4> source{0x11223344U, 0x55667788U, 0x99AABBCCU, 0xDDEEFF00U};
    for (std::uint32_t i = 0U; i < source.size(); ++i) {
        m.bus.write32_be(0x0100U + i * 4U, source[i]);
    }

    auto r = m.dsp.cpu_registers();
    r.ra0 = 0x0100U; // D0-bus read address (byte address)
    m.dsp.set_registers(r);

    // DMA external->DSP, bank 0, count 4. The external->DSP path honours only
    // stride bit 1, so stride 2 advances the source by four bytes per longword.
    m.load(0x00U, {dma(false, 0U, 4U, 2U), end_op});
    m.start();
    m.dsp.step_instruction(); // DMA
    for (std::uint32_t i = 0U; i < source.size(); ++i) {
        CHECK(m.dsp.read_data(0U, static_cast<std::uint8_t>(i)) == source[i]);
    }
    // The read address advanced four bytes per transferred longword.
    CHECK(m.dsp.cpu_registers().ra0 == 0x0100U + 16U);
    // CT0 advanced past the four written words.
    CHECK(m.dsp.cpu_registers().ct[0] == 4U);
}

TEST_CASE("scu_dsp DMA writes data RAM back to external memory") {
    machine m;
    m.dsp.write_data(1U, 0U, 0xCAFEBABEU);
    m.dsp.write_data(1U, 1U, 0x0BADF00DU);

    auto r = m.dsp.cpu_registers();
    r.wa0 = 0x0200U;
    m.dsp.set_registers(r);

    m.load(0x00U, {dma(true, 1U, 2U, 2U), end_op}); // DSP->external, bank 1, count 2, +4/word
    m.start();
    m.dsp.step_instruction();
    CHECK(m.bus.read32_be(0x0200U) == 0xCAFEBABEU);
    CHECK(m.bus.read32_be(0x0204U) == 0x0BADF00DU);
    CHECK(m.dsp.cpu_registers().wa0 == 0x0208U);
}

TEST_CASE("scu_dsp tick catches up by whole instructions") {
    machine m;
    // Three operation NOPs then END; the DSP issues one command per two cycles.
    m.load(0x00U, {op_cmd(0U, 0U, 0U, 0U), op_cmd(0U, 0U, 0U, 0U), op_cmd(0U, 0U, 0U, 0U), end_op});
    m.start();
    m.dsp.tick(6U); // 6 cycles -> 3 instructions (2 cycles each)
    CHECK(m.dsp.elapsed_cycles() >= 6U);
    CHECK(m.dsp.cpu_registers().pc >= 0x03U);
}

TEST_CASE("scu_dsp register_view exposes the live register snapshot") {
    machine m;
    auto r = m.dsp.cpu_registers();
    r.rx = 0xDEADBEEFU;
    r.pc = 0x12U;
    m.dsp.set_registers(r);

    auto* regs = m.dsp.introspection().registers();
    REQUIRE(regs != nullptr);
    const auto descriptors = regs->registers();
    bool saw_rx = false;
    bool saw_pc = false;
    for (const auto& d : descriptors) {
        if (d.name == "RX") {
            saw_rx = true;
            CHECK(d.value == 0xDEADBEEFU);
        }
        if (d.name == "PC") {
            saw_pc = true;
            CHECK(d.value == 0x12U);
        }
    }
    CHECK(saw_rx);
    CHECK(saw_pc);
}

TEST_CASE("scu_dsp trace_target fires once per executed instruction") {
    machine m;
    m.load(0x00U, {op_cmd(0U, 0U, 0U, 0U), op_cmd(0U, 0U, 0U, 0U), end_op});
    m.start();

    auto* trace = m.dsp.introspection().trace();
    REQUIRE(trace != nullptr);
    std::vector<mnemos::instrumentation::trace_event> events;
    trace->install(
        [&events](const mnemos::instrumentation::trace_event& ev) { events.push_back(ev); });

    m.dsp.step_instruction();
    m.dsp.step_instruction();
    REQUIRE(events.size() == 2U);
    CHECK(events[0].pc == 0x00U);
    CHECK(events[1].pc == 0x01U);
}

TEST_CASE("scu_dsp save_state / load_state round-trips bit-identically") {
    machine m;
    // Drive some non-trivial state: program, data RAM, registers, flags.
    m.load(0x00U, {mvi(4U, 0x1234U), op_cmd(4U, 0U, 0x10U, 0U), dma(false, 2U, 3U, 1U), endi_op});
    m.dsp.write_data(3U, 7U, 0xABCDEF01U);
    auto r = m.dsp.cpu_registers();
    r.rx = 0x55AA55AAU;
    r.ry = 0x0F0F0F0FU;
    r.acl = 0x80000000U;
    r.ach = 0x1234U;
    r.lop = 0x0ABU;
    r.top = 0x33U;
    r.pc = 0x21U;
    r.ra0 = 0x06010000U & 0x07FFFFFCU;
    r.s_flag = true;
    r.c_flag = true;
    r.ex_flag = true;
    m.dsp.set_registers(r);
    m.dsp.step_instruction(); // advance the pipeline so next_instr_/pc differ

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer{blob};
    m.dsp.save_state(writer);

    const auto before = m.dsp.cpu_registers();
    const std::uint32_t prog0 = m.dsp.read_program(0U);
    const std::uint32_t data37 = m.dsp.read_data(3U, 7U);

    // Restore into a fresh, reset DSP and compare.
    scu_dsp fresh;
    fresh.reset(reset_kind::power_on);
    mnemos::chips::state_reader reader{blob};
    fresh.load_state(reader);
    CHECK(reader.ok());

    const auto restored = fresh.cpu_registers();
    CHECK(restored.rx == before.rx);
    CHECK(restored.ry == before.ry);
    CHECK(restored.acl == before.acl);
    CHECK(restored.ach == before.ach);
    CHECK(restored.lop == before.lop);
    CHECK(restored.top == before.top);
    CHECK(restored.pc == before.pc);
    CHECK(restored.ra0 == before.ra0);
    CHECK(restored.s_flag == before.s_flag);
    CHECK(restored.c_flag == before.c_flag);
    CHECK(restored.ex_flag == before.ex_flag);
    CHECK(restored.end_irq == before.end_irq);
    CHECK(restored.ct == before.ct);
    CHECK(fresh.read_program(0U) == prog0);
    CHECK(fresh.read_data(3U, 7U) == data37);
    CHECK(fresh.elapsed_cycles() == m.dsp.elapsed_cycles());

    // Re-saving the restored chip yields a byte-identical blob.
    std::vector<std::uint8_t> blob2;
    mnemos::chips::state_writer writer2{blob2};
    fresh.save_state(writer2);
    CHECK(blob2 == blob);
}
