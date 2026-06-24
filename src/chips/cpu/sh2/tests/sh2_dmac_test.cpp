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
            ram[a] = static_cast<std::uint8_t>(v >> 8U);
            ram[a + 1U] = static_cast<std::uint8_t>(v);
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
    f.p.set_bus_wait_callback(
        [&](std::uint32_t address, std::uint8_t bytes, mnemos::chips::cpu::data_access_kind kind) {
            CHECK_FALSE(kind == mnemos::chips::cpu::data_access_kind::tas);
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
    m.cpu.set_bus_wait_callback(
        [](std::uint32_t address, std::uint8_t bytes, mnemos::chips::cpu::data_access_kind kind) {
            CHECK_FALSE(kind == mnemos::chips::cpu::data_access_kind::tas);
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

TEST_CASE("sh2_peripherals DRCR keeps only the resource-select bits") {
    mnemos::chips::cpu::sh2_peripherals p;
    CHECK(p.read8(0xFFFFFE71U) == 0x00U); // reset value
    CHECK(p.read8(0xFFFFFE72U) == 0x00U);
    p.write8(0xFFFFFE71U, 0xFDU); // reserved bits 7..2 are not storage
    p.write8(0xFFFFFE72U, 0xFEU);
    CHECK(p.read8(0xFFFFFE71U) == 0x01U);
    CHECK(p.read8(0xFFFFFE72U) == 0x02U);
    p.reset();
    CHECK(p.read8(0xFFFFFE71U) == 0x00U);
}

TEST_CASE("sh2_peripherals DMAC RXI request drains RDR and auto-clears RDRF") {
    dmac_fixture f;
    f.p.write8(0xFFFFFE02U, 0x50U); // SCR: RE | RIE (RIE gates the request)
    f.p.write8(0xFFFFFE71U, 0x01U); // DRCR0: RS = RXI
    f.w32reg(f.sar0, 0xFFFFFE05U);  // RDR, SM=fixed
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x4001U);     // DE | TS=byte | SM=fixed | DM=inc, module request

    f.p.tick(1U); // no received byte yet -> no request
    CHECK(f.ram[0x2000U] == 0x00U);

    f.p.sci_receive_byte(0x11U);
    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0x11U);
    CHECK((f.p.read8(0xFFFFFE04U) & 0x40U) == 0U); // the DMAC's RDR read cleared RDRF
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);

    f.p.tick(1U); // the request self-regulates: no RDRF, no transfer
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);

    f.p.sci_receive_byte(0x22U);                   // no overrun: the previous byte was consumed
    CHECK((f.p.read8(0xFFFFFE04U) & 0x20U) == 0U); // ORER clear
    f.p.tick(1U);
    CHECK(f.ram[0x2001U] == 0x22U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U); // TE on completion
}

TEST_CASE("sh2_peripherals DMAC TXI request feeds TDR a frame at a time") {
    dmac_fixture f;
    std::vector<std::uint8_t> sent;
    f.p.set_sci_transmit_callback([&](std::uint8_t value) { sent.push_back(value); });
    f.p.write8(0xFFFFFE01U, 0x00U); // BRR = 0 keeps the frame short
    f.p.write8(0xFFFFFE02U, 0xA0U); // SCR: TE | TIE (TIE gates the request)
    f.p.write8(0xFFFFFE71U, 0x02U); // DRCR0: RS = TXI
    f.ram[0x1000U] = 0x33U;
    f.ram[0x1001U] = 0x44U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0xFFFFFE03U); // TDR, DM=fixed
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x1001U);     // DE | TS=byte | SM=inc | DM=fixed, module request

    f.p.tick(1U); // reset-state TDRE is set, so the first unit moves at once
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);
    CHECK((f.p.read8(0xFFFFFE04U) & 0x80U) == 0U); // the TDR write cleared TDRE

    f.p.tick(1U); // TDRE still clear mid-frame: the request self-regulates
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);

    for (int i = 0; i < 4096 && sent.size() < 2U; ++i) {
        f.p.tick(64U); // frames complete, TDRE re-arms, the DMAC feeds the next byte
    }
    REQUIRE(sent.size() == 2U);
    CHECK(sent[0] == 0x33U);
    CHECK(sent[1] == 0x44U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
}

TEST_CASE("sh2_peripherals DMAC DACK strobe rides the AM-selected cycle with AL polarity") {
    dmac_fixture f;
    struct strobe final {
        int channel;
        std::uint32_t address;
        std::uint8_t bytes;
        bool active_high;
    };
    std::vector<strobe> strobes;
    f.p.set_dack_strobe(
        [&](int channel, std::uint32_t address, std::uint8_t bytes, bool active_high) {
            strobes.push_back({channel, address, bytes, active_high});
        });
    f.p.set_dreq_query([](int) { return true; }); // held-active level request
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1001U] = 0xA1U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U); // DME
    f.w32reg(f.chcr0, 0x5001U);     // DE | TS=byte | SM=inc | DM=inc, AM=0 (read cycle), AL=0

    f.p.tick(1U);
    f.p.tick(1U);
    REQUIRE(strobes.size() == 2U);
    CHECK(strobes[0].channel == 0);
    CHECK(strobes[0].address == 0x1000U); // AM=0: the source read cycle
    CHECK(strobes[0].bytes == 1U);
    CHECK_FALSE(strobes[0].active_high); // AL=0: active-low
    CHECK(strobes[1].address == 0x1001U);

    strobes.clear();
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U); // observe TE so the rewrite clears it
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.chcr0, 0x5181U); // AM=1 (write cycle) | AL=1 (active-high) | DE
    f.p.tick(1U);
    REQUIRE(strobes.size() == 1U);
    CHECK(strobes[0].address == 0x2002U); // AM=1: the destination write cycle
    CHECK(strobes[0].active_high);
}

TEST_CASE("sh2_peripherals DMAC auto-request and SCI-sourced transfers emit no DACK") {
    dmac_fixture f;
    int strobe_calls = 0;
    f.p.set_dack_strobe([&](int, std::uint32_t, std::uint8_t, bool) { ++strobe_calls; });
    f.ram[0x1000U] = 0xA0U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x5201U); // DE | AR | TS=byte | SM=inc | DM=inc
    f.p.tick(1U);
    CHECK(f.ram[0x2000U] == 0xA0U);
    CHECK(strobe_calls == 0); // auto-request: no external device, no DACK

    f.p.write8(0xFFFFFE02U, 0x50U); // SCR: RE | RIE
    f.p.write8(0xFFFFFE72U, 0x01U); // DRCR1: RS = RXI
    f.w32reg(f.sar1, 0xFFFFFE05U);
    f.w32reg(f.dar1, 0x00002100U);
    f.w32reg(f.tcr1, 1U);
    f.w32reg(f.chcr1, 0x4001U); // DE | TS=byte | DM=inc, module request
    f.p.sci_receive_byte(0x5AU);
    f.p.tick(1U);
    CHECK(f.ram[0x2100U] == 0x5AU);
    CHECK(strobe_calls == 0); // SCI-sourced: on-chip, no DACK
}

TEST_CASE("sh2_peripherals DMAC single-address memory-to-device reads one cycle per unit") {
    dmac_fixture f;
    std::vector<std::uint8_t> device;
    f.p.set_dack_device_port([&](int channel, bool device_to_memory, std::uint8_t data) {
        CHECK(channel == 0);
        CHECK_FALSE(device_to_memory);
        device.push_back(data);
        return std::uint8_t{0U};
    });
    f.p.set_dreq_query([](int) { return true; });
    f.ram[0x1000U] = 0xA0U;
    f.ram[0x1001U] = 0xA1U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U); // device side: never addressed
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x1009U); // DE | TA | TS=byte | SM=inc, AM=0: memory -> device

    f.p.tick(1U);
    f.p.tick(1U);
    REQUIRE(device.size() == 2U);
    CHECK(device[0] == 0xA0U);
    CHECK(device[1] == 0xA1U);
    CHECK(f.ram[0x2000U] == 0x00U); // no memory write cycle exists in this mode
    CHECK((f.r32reg(f.sar0)) == 0x1002U);
    CHECK((f.r32reg(f.dar0)) == 0x2000U); // DM is ignored: the device has no address
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
}

TEST_CASE("sh2_peripherals DMAC single-address device-to-memory writes the port byte") {
    dmac_fixture f;
    f.p.set_dack_device_port([](int, bool device_to_memory, std::uint8_t) {
        CHECK(device_to_memory);
        return std::uint8_t{0x77U};
    });
    f.p.set_dreq_query([](int) { return true; });
    f.w32reg(f.sar0, 0x00001000U); // memory side is DAR; SAR/SM are ignored
    f.w32reg(f.dar0, 0x00003000U);
    f.w32reg(f.tcr0, 2U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x4109U); // DE | TA | AM=1 | TS=byte | DM=inc: device -> memory

    f.p.tick(1U);
    CHECK(f.ram[0x3000U] == 0x77U);
    CHECK((f.r32reg(f.sar0)) == 0x1000U); // SM is ignored in this direction

    f.p.set_dack_device_port({}); // unwired device: the bus is undriven -- no
                                  // value contract; $FF is the deterministic stand-in
    f.p.tick(1U);
    CHECK(f.ram[0x3001U] == 0xFFU);
    CHECK((f.r32reg(f.dar0)) == 0x3002U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
}

TEST_CASE("sh2_peripherals DMAC single-address mode requires an external request") {
    dmac_fixture f;
    f.ram[0x1000U] = 0xA0U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x5209U); // DE | TA | AR -- prohibited: single address + auto-request
    f.p.tick(1U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U); // nothing moved
    CHECK(f.ram[0x2000U] == 0x00U);

    f.p.write8(0xFFFFFE71U, 0x01U); // prohibited: single address + RXI source
    f.p.write8(0xFFFFFE02U, 0x50U);
    f.p.sci_receive_byte(0x5AU);
    f.w32reg(f.chcr0, 0x5009U); // DE | TA, module request
    f.p.tick(1U);
    CHECK((f.r32reg(f.tcr0) & 0x00FFFFFFU) == 1U);
}

TEST_CASE("sh2_peripherals CHCR.TE clears only by the read-then-write-0 protocol") {
    dmac_fixture f;
    f.w32reg(f.chcr0, 0x4203U); // a plain write cannot set TE
    CHECK((f.r32reg(f.chcr0) & 0x02U) == 0U);

    f.ram[0x1000U] = 0xA0U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x4201U); // DE | AR | TS=byte
    f.p.tick(1U);               // completes -> TE sets

    f.w32reg(f.chcr0, 0x4201U); // write 0 to TE *without* a fresh read: TE stays
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U);
    // That CHECK read observed TE, so a write-0 now clears it.
    f.w32reg(f.chcr0, 0x4201U);
    CHECK((f.r32reg(f.chcr0) & 0x02U) == 0U);
}

TEST_CASE("sh2_peripherals DMAOR NMIF/AE cannot be set by a register write") {
    dmac_fixture f;
    f.w32reg(f.dmaor, 0x0000000FU);    // attempt DME | NMIF | AE | PR
    CHECK(f.r32reg(f.dmaor) == 0x09U); // the status flags refuse the write
}

TEST_CASE("sh2_peripherals DRCR and the DMAC flag latches survive a save/load round-trip") {
    dmac_fixture f;
    f.p.write8(0xFFFFFE71U, 0x01U);
    f.p.write8(0xFFFFFE72U, 0x02U);
    f.ram[0x1000U] = 0xA0U;
    f.w32reg(f.sar0, 0x00001000U);
    f.w32reg(f.dar0, 0x00002000U);
    f.w32reg(f.tcr0, 1U);
    f.w32reg(f.dmaor, 0x00000001U);
    f.w32reg(f.chcr0, 0x4201U);
    f.p.tick(1U);                             // TE sets
    CHECK((f.r32reg(f.chcr0) & 0x02U) != 0U); // observe TE before saving

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    f.p.save_state(writer);
    mnemos::chips::cpu::sh2_peripherals q;
    mnemos::chips::state_reader reader(blob);
    q.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(q.read8(0xFFFFFE71U) == 0x01U);
    CHECK(q.read8(0xFFFFFE72U) == 0x02U);
    // The pre-save read observation was preserved: write-0 clears TE.
    q.write8(0xFFFFFF8FU, 0x01U);
    CHECK((q.read8(0xFFFFFF8FU) & 0x02U) == 0U);
}
