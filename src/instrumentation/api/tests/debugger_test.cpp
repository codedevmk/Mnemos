#include "debugger.hpp"

#include "bus.hpp"
#include "m6510.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {
    using mnemos::chips::cpu::m6510;
    using mnemos::instrumentation::cpu_probe;
    using mnemos::instrumentation::debugger;
    using mnemos::instrumentation::halt_reason;
    using mnemos::instrumentation::watch_kind;
    using mnemos::topology::access_event;
    using reset_kind = mnemos::chips::reset_kind;

    // A 6510 machine: 64K RAM holding a program at `origin`, with the reset vector
    // pointed at it. The bus is exposed so the debugger can observe accesses.
    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::little};
        m6510 cpu;

        machine(std::span<const std::uint8_t> program, std::uint16_t origin) {
            for (std::size_t i = 0; i < program.size(); ++i) {
                ram[origin + i] = program[i];
            }
            ram[0xFFFC] = static_cast<std::uint8_t>(origin & 0xFFU);
            ram[0xFFFD] = static_cast<std::uint8_t>(origin >> 8U);
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }

        [[nodiscard]] cpu_probe probe() {
            return {.program_counter = [this]() { return cpu.cpu_registers().pc; },
                    .at_instruction_boundary = [this]() { return cpu.at_instruction_boundary(); }};
        }
    };

    // LDA #$01; LDX #$02; NOP ($1004); JMP $1005 (self loop at $1005).
    constexpr std::array<std::uint8_t, 8> bp_program = {0xA9U, 0x01U, 0xA2U, 0x02U,
                                                        0xEAU, 0x4CU, 0x05U, 0x10U};

    // LDA #$42; STA $2000 ($1002); LDA $2000 ($1005); JMP $1008 (self loop).
    constexpr std::array<std::uint8_t, 11> wp_program = {0xA9U, 0x42U, 0x8DU, 0x00U, 0x20U, 0xADU,
                                                         0x00U, 0x20U, 0x4CU, 0x08U, 0x10U};
} // namespace

TEST_CASE("debugger runs to a PC breakpoint") {
    machine m(bp_program, 0x1000U);
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
    machine m(bp_program, 0x1000U);
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

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
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        dbg.add_breakpoint(
            {.address = 0x1005U, .condition = [&m]() { return m.cpu.cpu_registers().x == 0x02U; }});
        const auto ev = dbg.run(100U);
        CHECK(ev.reason == halt_reason::breakpoint);
        CHECK(ev.pc == 0x1005U);
    }
    SECTION("condition false -> never fires") {
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        dbg.add_breakpoint({.address = 0x1005U, .condition = []() { return false; }});
        const auto ev = dbg.run(50U);
        CHECK(ev.reason == halt_reason::budget_exhausted);
    }
}

TEST_CASE("debugger disables and removes breakpoints") {
    machine m(bp_program, 0x1000U);
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
    machine m(bp_program, 0x1000U);
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe());

    CHECK(dbg.master_cycle() == 0U);
    dbg.step_instruction();
    CHECK(dbg.master_cycle() > 0U);
    CHECK(dbg.frame_index() == 0U); // no frame source attached
    CHECK(dbg.step_frame() == 0U);  // no frame source -> no-op
}

TEST_CASE("debugger stops on a memory write watchpoint") {
    machine m(wp_program, 0x1000U);
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe(), &m.bus);

    const auto id = dbg.add_watchpoint({.address = 0x2000U, .kind = watch_kind::write});
    const auto ev = dbg.run(100U);

    CHECK(ev.reason == halt_reason::watchpoint);
    CHECK(ev.watchpoint == id);
    CHECK(ev.pc == 0x1005U);       // stopped after the STA at $1002
    CHECK(m.ram[0x2000] == 0x42U); // the write itself completed
}

TEST_CASE("debugger distinguishes read from write watchpoints") {
    machine m(wp_program, 0x1000U);
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe(), &m.bus);

    // A read watchpoint ignores the STA write and fires on the LDA $2000 at $1005.
    dbg.add_watchpoint({.address = 0x2000U, .kind = watch_kind::read});
    const auto ev = dbg.run(100U);
    CHECK(ev.reason == halt_reason::watchpoint);
    CHECK(ev.pc == 0x1008U); // stopped after the LDA at $1005
}

TEST_CASE("debugger watchpoints honour value conditions and ranges") {
    SECTION("value condition gates the hit") {
        machine m(wp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe(), &m.bus);
        dbg.add_watchpoint({.address = 0x2000U,
                            .kind = watch_kind::write,
                            .condition = [](const access_event& a) { return a.value == 0x99U; }});
        CHECK(dbg.run(100U).reason == halt_reason::budget_exhausted); // only $42 is ever written
    }
    SECTION("a range watchpoint covers the address") {
        machine m(wp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe(), &m.bus);
        // [$1FF0, $2010) covers the $2000 write.
        dbg.add_watchpoint({.address = 0x1FF0U, .length = 0x20U, .kind = watch_kind::access});
        CHECK(dbg.run(100U).reason == halt_reason::watchpoint);
    }
}

TEST_CASE("debugger watchpoints never fire without a bus") {
    machine m(wp_program, 0x1000U);
    mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
    debugger dbg(sched, m.probe()); // no watch bus supplied

    dbg.add_watchpoint({.address = 0x2000U, .kind = watch_kind::access});
    CHECK(dbg.watchpoint_count() == 1U);
    CHECK(dbg.run(100U).reason == halt_reason::budget_exhausted);
}

namespace {
    using mnemos::instrumentation::event;
    using mnemos::instrumentation::event_kind;

    struct recording_sink final : mnemos::instrumentation::event_sink {
        std::vector<event> events;
        void on_event(const event& e) override { events.push_back(e); }
    };
} // namespace

TEST_CASE("debugger delivers filtered events to subscribers") {
    SECTION("breakpoint + step events reach a matching subscriber") {
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        recording_sink sink;
        dbg.subscribe(event_kind::breakpoint | event_kind::step, sink);

        const auto id = dbg.add_breakpoint({.address = 0x1004U});
        (void)dbg.run(100U);
        REQUIRE(sink.events.size() == 1U);
        CHECK(sink.events[0].kind == event_kind::breakpoint);
        CHECK(sink.events[0].id == id);
        CHECK(sink.events[0].pc == 0x1004U);

        dbg.clear_breakpoints();
        dbg.step_instruction(); // execute NOP -> a step event
        REQUIRE(sink.events.size() == 2U);
        CHECK(sink.events[1].kind == event_kind::step);
    }

    SECTION("a watchpoint event is delivered") {
        machine m(wp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe(), &m.bus);
        recording_sink sink;
        dbg.subscribe(event_kind::watchpoint, sink);
        const auto id = dbg.add_watchpoint({.address = 0x2000U, .kind = watch_kind::write});
        (void)dbg.run(100U);
        REQUIRE(sink.events.size() == 1U);
        CHECK(sink.events[0].kind == event_kind::watchpoint);
        CHECK(sink.events[0].id == id);
    }

    SECTION("the filter excludes unselected kinds") {
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        recording_sink sink;
        dbg.subscribe(event_kind::watchpoint, sink); // not breakpoint
        dbg.add_breakpoint({.address = 0x1004U});
        (void)dbg.run(100U);
        CHECK(sink.events.empty()); // breakpoint event filtered out
    }

    SECTION("unsubscribe stops delivery") {
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        recording_sink sink;
        const auto handle = dbg.subscribe(mnemos::instrumentation::all_events, sink);
        REQUIRE(dbg.unsubscribe(handle));
        CHECK_FALSE(dbg.unsubscribe(handle)); // already gone
        dbg.add_breakpoint({.address = 0x1004U});
        (void)dbg.run(100U);
        CHECK(sink.events.empty());
    }

    SECTION("step_frame emits a frame event") {
        machine m(bp_program, 0x1000U);
        mnemos::runtime::scheduler sched({{&m.cpu, 1U}}, nullptr);
        debugger dbg(sched, m.probe());
        recording_sink sink;
        dbg.subscribe(event_kind::frame, sink);
        dbg.step_frame();
        REQUIRE(sink.events.size() == 1U);
        CHECK(sink.events[0].kind == event_kind::frame);
    }
}
