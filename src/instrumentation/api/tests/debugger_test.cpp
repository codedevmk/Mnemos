#include <mnemos/instrumentation/debugger.hpp>

#include <mnemos/chips/cpu/m6510.hpp>
#include <mnemos/runtime/scheduler.hpp>
#include <mnemos/topology/bus.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {
    using mnemos::chips::cpu::m6510;
    using mnemos::instrumentation::breakpoint_spec;
    using mnemos::instrumentation::cpu_probe;
    using mnemos::instrumentation::debugger;
    using mnemos::instrumentation::halt_reason;
    using reset_kind = mnemos::chips::reset_kind;

    // A minimal 6510 machine: 64K RAM with a tiny program and a reset vector.
    //   $1000: LDA #$01   (A9 01)
    //   $1002: LDX #$02   (A2 02)
    //   $1004: NOP        (EA)
    //   $1005: JMP $1005  (4C 05 10)  -- self loop
    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::little};
        m6510 cpu;

        machine() {
            ram[0x1000] = 0xA9;
            ram[0x1001] = 0x01;
            ram[0x1002] = 0xA2;
            ram[0x1003] = 0x02;
            ram[0x1004] = 0xEA;
            ram[0x1005] = 0x4C;
            ram[0x1006] = 0x05;
            ram[0x1007] = 0x10;
            ram[0xFFFC] = 0x00; // reset vector low
            ram[0xFFFD] = 0x10; // reset vector high -> $1000
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }

        [[nodiscard]] cpu_probe probe() {
            return {.program_counter = [this]() { return cpu.cpu_registers().pc; },
                    .at_instruction_boundary = [this]() { return cpu.at_instruction_boundary(); }};
        }
    };
} // namespace

TEST_CASE("debugger runs to a PC breakpoint") {
    machine m;
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

    const auto id = dbg.add_breakpoint({.address = 0x1004U});
    const auto ev = dbg.run(100U);

    CHECK(ev.reason == halt_reason::breakpoint);
    CHECK(ev.breakpoint == id);
    CHECK(ev.pc == 0x1004U); // stopped before executing the NOP at $1004
    CHECK(m.cpu.cpu_registers().pc == 0x1004U);
    CHECK(ev.master_cycle > 0U);
}

TEST_CASE("debugger steps one instruction at a time") {
    machine m;
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

    // Anchor at a known boundary via a breakpoint, then single-step deterministically.
    dbg.add_breakpoint({.address = 0x1004U});
    REQUIRE(dbg.run(100U).pc == 0x1004U);
    dbg.clear_breakpoints();
    const auto a = dbg.step_instruction(); // execute NOP at $1004
    CHECK(a.reason == halt_reason::step_complete);
    CHECK(a.pc == 0x1005U);
    const auto b = dbg.step_instruction(); // JMP $1005 -> stays at $1005
    CHECK(b.pc == 0x1005U);
}

TEST_CASE("debugger honours a breakpoint condition") {
    SECTION("condition true -> fires") {
        machine m;
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        dbg.add_breakpoint(
            {.address = 0x1005U, .condition = [&m]() { return m.cpu.cpu_registers().x == 0x02U; }});
        const auto ev = dbg.run(100U);
        CHECK(ev.reason == halt_reason::breakpoint);
        CHECK(ev.pc == 0x1005U);
    }
    SECTION("condition false -> never fires") {
        machine m;
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        dbg.add_breakpoint({.address = 0x1005U, .condition = []() { return false; }});
        const auto ev = dbg.run(50U);
        CHECK(ev.reason == halt_reason::budget_exhausted);
    }
}

TEST_CASE("debugger disables and removes breakpoints") {
    machine m;
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

    // $1005 (the JMP self-loop) is revisited every iteration, unlike $1004.
    const auto id = dbg.add_breakpoint({.address = 0x1005U});
    CHECK(dbg.breakpoint_count() == 1U);

    REQUIRE(dbg.set_breakpoint_enabled(id, false));
    CHECK(dbg.run(40U).reason == halt_reason::budget_exhausted); // disabled -> no hit

    REQUIRE(dbg.set_breakpoint_enabled(id, true));
    CHECK(dbg.run(40U).reason == halt_reason::breakpoint); // re-enabled -> hit

    REQUIRE(dbg.remove_breakpoint(id));
    CHECK(dbg.breakpoint_count() == 0U);
    CHECK_FALSE(dbg.remove_breakpoint(id)); // already gone
}

TEST_CASE("debugger reports scheduler progress") {
    machine m;
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

    CHECK(dbg.master_cycle() == 0U);
    dbg.step_instruction();
    CHECK(dbg.master_cycle() > 0U);
    CHECK(dbg.frame_index() == 0U); // no frame source attached
    CHECK(dbg.step_frame() == 0U);  // no frame source -> no-op
}
