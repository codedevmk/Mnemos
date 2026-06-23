#include "sh2.hpp"

#include "bus.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace {
    using mnemos::chips::cpu::sh2;
    using mnemos::chips::cpu::sh2_peripherals;

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

TEST_CASE("sh2_peripherals DIVU module-stop (SBYCR.MSTP2) freezes the divider") {
    mnemos::chips::cpu::sh2_peripherals p;
    write_peripheral32(p, 0xFFFFFF00U, 7U);           // DVSR
    write_peripheral32(p, 0xFFFFFF04U, 100U);         // DVDNT -> starts a 39-cycle divide
    p.write8(0xFFFFFE91U, 0x04U);                     // SBYCR.MSTP2: stop the DIVU clock
    p.tick(100U);                                     // would normally complete; frozen instead
    CHECK(read_peripheral32(p, 0xFFFFFF04U) == 100U); // not yet divided
    p.write8(0xFFFFFE91U, 0x00U);                     // clear MSTP2: the clock resumes
    p.tick(39U);
    CHECK(read_peripheral32(p, 0xFFFFFF04U) == 14U); // now it completes
    CHECK(read_peripheral32(p, 0xFFFFFF10U) == 2U);  // remainder
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
