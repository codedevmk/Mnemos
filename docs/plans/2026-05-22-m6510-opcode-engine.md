# MOS 6510 Opcode Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the MOS 6510 from its current skeleton into a cycle-accurate CPU that passes the Klaus 2M65 functional suite, decimal-mode tests, and undocumented-opcode tests in CI on all four matrix combinations.

**Architecture:** A cycle-stepped core. `tick(n)` calls `step_one_cycle()` exactly `n` times; each cycle performs at most one bus access, so the scheduler (and later the VIC-II) can interleave/steal cycles. Decoding is table-driven: a 256-entry `constexpr` decode table maps each opcode byte to an `operation` + `addressing_mode` + `access_kind`. A per-cycle micro-engine walks the standard cycle pattern for each `(addressing_mode, access_kind)` pair and invokes a small operation handler at the correct cycle. Memory access goes through a CPU-internal `read`/`write` that intercepts the 6510's $00/$01 I/O port before delegating to the attached `i_bus`.

**Tech Stack:** C++23, CMake 3.28+/Ninja, Catch2 v3 (`mnemos_add_test`), MSVC `/W4 /WX` + GCC/Clang `-Wall -Wextra -Wpedantic -Werror`. Existing target: `mnemos::chips::cpu::m6510` (tier 2, links `mnemos::chips::common`).

**Conventions to follow (from the existing codebase):**
- 4-space indentation inside namespaces; `#pragma once`; `[[nodiscard]]` on pure accessors; non-copyable interface pattern.
- All chip types in `mnemos::chips::cpu`; m6510-specific value types nested in `class m6510`.
- Every task ends green: build the test target, run `ctest`, run `clang-format --dry-run --Werror`, commit, push, confirm the CI run is green before checking the matching todo in `docs/architecture/mnemos-todos-v0.1.md`.
- Windows build prelude for every `cmake`/`ctest`/`clang-format` invocation (MSVC dev env):
  ```powershell
  $vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
  Import-Module (Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
  Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
  ```
- Linux parity is mandatory: no platform-specific code in the core; CI covers gcc/clang debug+release.

---

## Reference Material

- **Cycle timing source of truth:** the Klaus Dormann 6502 functional test (`6502_functional_test`, "2M65") plus the per-opcode cycle counts from a documented reference (e.g. the "MCS6500 Family Programming Manual" and Waldemar's *NMOS 6510 Unintended Opcodes* for illegals). Cite the exact source in `src/chips/cpu/m6510/NOTES.md` as it is used (per the cross-cutting "per-chip NOTES.md" backlog item).
- **Behavioral references only** — do not copy emulator source. Reproduce behavior from documentation and validate against the public test ROMs (AGENTS.md §Dependency Policy).
- ROM/test binaries are **never committed**; CI obtains them from a configured source, mirroring the C64 ROM convention (todos M3 / `ROMS.md`).

---

## File Structure

All paths under `src/chips/cpu/m6510/`.

- `include/mnemos/chips/cpu/m6510.hpp` — **modify**: extend the `m6510` class with the cycle-stepped engine state, nested `operation` / `access_kind` enums, and the public surface used by tests (already holds `registers`, `status_flag`, `addressing_mode`).
- `include/mnemos/chips/cpu/m6510/decode_table.hpp` — **create**: `struct decoded { operation op; addressing_mode mode; access_kind kind; bool illegal; };` and `constexpr std::array<decoded, 256> decode_table`.
- `src/decode_table.cpp` — **create** (or keep the table header-only `constexpr` and drop the .cpp): the 256-entry table population, split documented vs illegal.
- `src/m6510.cpp` — **modify**: implement `step_one_cycle()`, the per-cycle micro-engine, memory access with $00/$01 intercept, operation handlers, interrupt sequencing, save/load, introspection.
- `src/m6510_operations.cpp` — **create**: the operation handlers (`op_lda`, `op_adc`, …) if `m6510.cpp` grows past ~600 lines; otherwise keep them in `m6510.cpp`. Decision deferred to the task that first crosses the size threshold.
- `tests/m6510_test.cpp` — **modify/extend**: keep existing skeleton tests; add per-group microtests.
- `tests/m6510_conformance_test.cpp` — **create**: loads the Klaus functional ROM (path via env/CMake cache var), runs to completion, asserts the success trap PC.
- `NOTES.md` — **create**: cite every behavioral reference used.
- `CMakeLists.txt` — **modify**: add new sources; add the conformance test gated on ROM availability.

Tier rule reminder: this target stays tier 2 and may only link `mnemos::chips::common` (same tier) and lower. No topology dependency — tests use a `fake_bus` implementing `i_bus`.

---

## Shared Test Helper

Most microtests need a CPU wired to RAM with a program loaded and the reset vector pointed at it. Add this helper to `tests/m6510_test.cpp` (Task 1) and reuse it. **Replace the existing skeleton `fake_bus`** (in the test's anonymous namespace) with `flat_ram` — it has an identical body — so the TU has exactly one RAM-bus type. Update the existing skeleton tests to use `flat_ram`/`test_system`.

```cpp
namespace {

    class flat_ram final : public mnemos::chips::i_bus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFU];
        }
        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFU] = value;
        }
    };

    struct test_system final {
        flat_ram bus;
        mnemos::chips::cpu::m6510 cpu;

        // Load `program` at `origin`, set the reset vector, and reset.
        void boot(std::uint16_t origin, std::initializer_list<std::uint8_t> program) {
            std::uint16_t addr = origin;
            for (std::uint8_t byte : program) {
                bus.memory[addr++] = byte;
            }
            bus.memory[0xFFFCU] = static_cast<std::uint8_t>(origin & 0xFFU);
            bus.memory[0xFFFDU] = static_cast<std::uint8_t>(origin >> 8U);
            cpu.attach_bus(bus);
            cpu.reset(mnemos::chips::reset_kind::power_on);
        }

        // Execute one full instruction; return the cycles it consumed.
        std::uint64_t step_instruction() {
            const std::uint64_t before = cpu.elapsed_cycles();
            cpu.tick(1U);                       // first cycle of next instruction
            while (!cpu.at_instruction_boundary()) {
                cpu.tick(1U);
            }
            return cpu.elapsed_cycles() - before;
        }
    };

} // namespace
```

`at_instruction_boundary()` and a free-running `elapsed_cycles()` are added in Task 2. The helper validates the cycle-stepped contract directly: stepping one cycle at a time must reach the same state as the instruction implies, and the cycle count must match the datasheet.

---

## Task 1: CPU memory access with $00/$01 I/O port intercept

**Files:**
- Modify: `include/mnemos/chips/cpu/m6510.hpp`
- Modify: `src/m6510.cpp`
- Modify: `tests/m6510_test.cpp`

The 6510 intercepts CPU accesses to $0000 (data direction register) and $0001 (I/O port latch); only bits configured as inputs read from the external pin, output bits read back the latch. This is what the C64 PLA later reads to bank memory. Implement the latch now; the PLA wiring is M2.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("m6510 $00/$01 I/O port latches output bits") {
    test_system sys;
    sys.boot(0xC000U, {});                     // no program needed
    // DDR = $2F (bits 0-5 output as on a C64), port write of $37.
    sys.cpu.write(0x0000U, 0x2FU);
    sys.cpu.write(0x0001U, 0x37U);
    CHECK(sys.cpu.read(0x0000U) == 0x2FU);
    // DDR $2F = 0010_1111: outputs are bits 0,1,2,3,5; inputs are bits 4,6,7.
    // Output bits read back from the latch: data & ddr = $37 & $2F = $27.
    // Input bits read the default pull (high): pull & ~ddr = $FF & $D0 = $D0.
    // Result = $27 | $D0 = $F7.
    CHECK(sys.cpu.read(0x0001U) == 0xF7U);
}

TEST_CASE("m6510 passes non-port addresses through to the bus") {
    test_system sys;
    sys.boot(0xC000U, {});
    sys.cpu.write(0x0200U, 0xABU);
    CHECK(sys.cpu.read(0x0200U) == 0xABU);
    CHECK(sys.bus.memory[0x0200U] == 0xABU);
}
```

- [ ] **Step 2: Run, verify they fail to compile** (`read`/`write` not yet public)

  Run (after dev-shell prelude):
  `cmake --build build/windows-msvc-debug --target mnemos_chips_cpu_m6510_test`
  Expected: compile error — no member `read`/`write`.

- [ ] **Step 3: Implement `read`/`write` with the port intercept**

  In `m6510.hpp`, add members and public methods:
  ```cpp
  [[nodiscard]] std::uint8_t read(std::uint16_t address) noexcept;
  void write(std::uint16_t address, std::uint8_t value) noexcept;
  ```
  and private state `std::uint8_t port_ddr_{};`, `std::uint8_t port_data_{};`, and a constant for default input-pin pull (`0xFFU`).
  In `m6510.cpp`:
  ```cpp
  std::uint8_t m6510::read(std::uint16_t address) noexcept {
      if (address == 0x0000U) {
          return port_ddr_;
      }
      if (address == 0x0001U) {
          const auto outputs = static_cast<std::uint8_t>(port_data_ & port_ddr_);
          const auto inputs = static_cast<std::uint8_t>(port_input_pull & ~port_ddr_);
          return static_cast<std::uint8_t>(outputs | inputs);
      }
      return bus_ != nullptr ? bus_->read8(address) : 0U;
  }
  void m6510::write(std::uint16_t address, std::uint8_t value) noexcept {
      if (address == 0x0000U) { port_ddr_ = value; return; }
      if (address == 0x0001U) { port_data_ = value; return; }
      if (bus_ != nullptr) { bus_->write8(address, value); }
  }
  ```

- [ ] **Step 4: Run tests, verify pass.** `ctest --test-dir build/windows-msvc-debug -R m6510 --output-on-failure` → PASS.
- [ ] **Step 5: clang-format + commit + push + wait for green CI**, then mark the todo `Implement the 6510 I/O port at addresses $00/$01.` `[x]` with the run evidence.

---

## Task 2: Cycle-stepped engine scaffold (NOP vertical slice)

**Files:**
- Modify: `include/mnemos/chips/cpu/m6510.hpp`
- Modify: `src/m6510.cpp`
- Modify: `tests/m6510_test.cpp`

Establish the per-cycle state machine end-to-end with the simplest instruction (`NOP`, $EA, 2 cycles) before any table or addressing modes.

Add engine state to `m6510`: `std::uint8_t ir_{};` (opcode), `std::uint8_t tcu_{};` (cycle within instruction; 0 = at boundary), working temporaries `std::uint16_t ea_{}; std::uint8_t operand_{};`. Add public observers used by tests:
```cpp
[[nodiscard]] bool at_instruction_boundary() const noexcept { return tcu_ == 0U; }
```
Change `tick`:
```cpp
void m6510::tick(std::uint64_t cycles) {
    for (std::uint64_t i = 0; i < cycles; ++i) {
        step_one_cycle();
        ++cycles_;
    }
}
```
`step_one_cycle()` first slice:
```cpp
void m6510::step_one_cycle() {
    if (tcu_ == 0U) {
        ir_ = read(registers_.pc++);   // opcode fetch is cycle 1
        tcu_ = 1U;
        return;
    }
    // Temporary: only NOP ($EA) is known; everything else asserts in tests.
    switch (ir_) {
    case 0xEAU:                        // NOP: one internal cycle, then done
        tcu_ = 0U;
        return;
    default:
        tcu_ = 0U;                     // replaced by the decode engine in Task 3
        return;
    }
}
```

- [ ] **Step 1: Write failing test**
```cpp
TEST_CASE("m6510 NOP takes two cycles and advances PC by one") {
    test_system sys;
    sys.boot(0xC000U, {0xEAU});         // NOP
    const std::uint64_t cycles = sys.step_instruction();
    CHECK(cycles == 2U);
    CHECK(sys.cpu.cpu_registers().pc == 0xC001U);
}
```
- [ ] **Step 2: Run, verify it fails** (no `step_one_cycle`/`at_instruction_boundary`/`tick` loop yet).
- [ ] **Step 3: Implement the scaffold above.**
- [ ] **Step 4: Run, verify pass.**
- [ ] **Step 5: clang-format + commit + push + green CI.** (No todo flips yet; engine groundwork.)

---

## Task 3: Decode table + read-addressing micro-engine (LDA immediate/zp/abs)

**Files:**
- Create: `include/mnemos/chips/cpu/m6510/decode_table.hpp`
- Modify: `include/mnemos/chips/cpu/m6510.hpp` (add `operation`, `access_kind` enums)
- Modify: `src/m6510.cpp`
- Modify: `tests/m6510_test.cpp`

Introduce the table-driven core. Add nested enums:
```cpp
enum class access_kind : std::uint8_t { read, write, read_modify_write, implied, relative, stack, jump, other };
enum class operation : std::uint8_t { nop, lda, /* … grows per task … */ kil };
```
`decode_table.hpp`:
```cpp
#pragma once
#include <mnemos/chips/cpu/m6510.hpp>
#include <array>
namespace mnemos::chips::cpu {
    struct decoded final {
        m6510::operation op{m6510::operation::kil};
        m6510::addressing_mode mode{m6510::addressing_mode::implied};
        m6510::access_kind kind{m6510::access_kind::other};
        bool illegal{true};
    };
    [[nodiscard]] const std::array<decoded, 256>& decode_table() noexcept;
}
```
Implement the per-cycle **read** micro-engine for `immediate`, `zero_page`, `absolute`: walk the standard fetch cycles, set `operand_`, then invoke `execute_read_op(op, operand_)` which dispatches to `op_lda` (sets A, updates N/Z). Set `tcu_ = 0` on the final cycle.

- [ ] **Step 1: Failing tests** for cycle counts and flags:
```cpp
TEST_CASE("LDA immediate: 2 cycles, sets N/Z") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x00U});            // LDA #$00
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().a == 0x00U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
}
TEST_CASE("LDA zero page: 3 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xA5U, 0x10U});            // LDA $10
    sys.bus.memory[0x0010U] = 0x80U;
    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.cpu.cpu_registers().a == 0x80U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
}
TEST_CASE("LDA absolute: 4 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xADU, 0x34U, 0x12U});     // LDA $1234
    sys.bus.memory[0x1234U] = 0x42U;
    CHECK(sys.step_instruction() == 4U);
    CHECK(sys.cpu.cpu_registers().a == 0x42U);
}
```
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** the decode table entries for $A9/$A5/$AD, the read micro-engine for those three modes, and `op_lda`. (Use `m6510` friendship or member access for `operation`/`access_kind` since they are nested.)
- [ ] **Step 4: Run, verify pass.**
- [ ] **Step 5: clang-format + commit + push + green CI.**

---

## Task 4: Complete the read-addressing modes (page-cross penalty)

Add the remaining read-path modes to the micro-engine, validated with `LDA`/`LDX`/`LDY`:
`zero_page_x`, `zero_page_y`, `absolute_x`, `absolute_y`, `indexed_indirect` ((zp,X)), `indirect_indexed` ((zp),Y). Include the **+1 cycle on page cross** for `absolute_x`, `absolute_y`, `indirect_indexed`, and the zero-page wrap for indexed zero-page modes.

- [ ] **Step 1: Failing tests** — one per mode, each asserting the exact cycle count for the no-cross and cross cases, e.g.:
```cpp
TEST_CASE("LDA absolute,X adds a cycle only on page cross") {
    test_system a; a.boot(0xC000U, {0xBDU, 0xFFU, 0x12U});  // LDA $12FF,X
    a.cpu /* set X via a preceding LDX or a test seam */;
    // X = 0x01 -> $1300 crosses page: 5 cycles. X=0x00 -> $12FF: 4 cycles.
}
```
  (Set index registers via a short preceding program — `LDX #imm` lands in Task 6; until then add a test-only seam `set_registers_for_test(...)` guarded behind a clearly-named method, or sequence `LDX` after Task 6 and reorder. Prefer reordering so tests use real instructions.)
- [ ] **Step 2-5:** fail → implement modes + `op_ldx`/`op_ldy` → pass → clang-format/commit/push/green CI.

> Sequencing note: if index-register loads are needed to test indexed modes, do **Task 6 (loads/transfers)** before this task. Reorder freely; the plan lists them by topic, not strict order.

---

## Task 5: Store instructions (write micro-engine)

`STA`/`STX`/`STY` across their legal modes. The **write** path never adds the page-cross cycle (indexed stores always pay the extra cycle for the fixed-up address). Implement the write micro-engine and table entries; tests assert cycle counts and the written byte.

- [ ] TDD steps as above (failing test → implement → pass → commit/push/CI).

---

## Task 6: Loads, transfers, and stack operations

- Loads with immediate/implied already partly covered; finish `LDX`/`LDY` modes.
- Transfers: `TAX TAY TXA TYA TSX TXS` (2 cycles, implied; `TXS` does not affect flags).
- Stack: `PHA PLA PHP PLP` (3/4 cycles), with correct SP wrap in page $0100 and the P-register bit-5/bit-4 behavior on push/pull (`PHP` pushes B and unused set; `PLP` ignores B and unused).

- [ ] TDD per sub-group; assert SP movement, flag side effects, cycle counts.

---

## Task 7: ALU operations (binary mode)

`ADC SBC AND ORA EOR CMP CPX CPY BIT` in binary mode across read modes. Implement `op_adc`/`op_sbc` with V and C flags (binary), `op_cmp` family (no register write, sets N/Z/C), `op_bit` (N/V from operand, Z from A&operand).

- [ ] TDD: table-driven flag tests (carry in/out, overflow cases V), reusing the read micro-engine. Cycle counts inherit from the addressing modes already proven.

---

## Task 8: Decimal mode arithmetic

Add NMOS-accurate BCD behavior to `ADC`/`SBC` when the D flag is set, including the documented NMOS quirks for N/V/Z in decimal mode.

- [ ] **Step 1: Failing tests** from a published decimal-mode test vector set (cite source in `NOTES.md`); cover representative valid and invalid BCD inputs.
- [ ] **Steps 2-5:** implement the decimal path in `op_adc`/`op_sbc` → pass → commit/push/CI, then mark todo `Implement decimal mode arithmetic.` `[x]`.

---

## Task 9: Read-modify-write instructions

`ASL LSR ROL ROR INC DEC` (memory + accumulator forms) and `INX INY DEX DEY`. The memory RMW path performs the **dummy write of the original value** before the modified write (5/6/7 cycles by mode). Tests must assert both the final value and the cycle count (the dummy write is observable via cycle count and, for I/O, side effects).

- [ ] TDD per op; assert flags (C from shifts/rotates), cycle counts, and the dummy-write cycle.

---

## Task 10: Branches, jumps, and subroutine/interrupt-return ops

- Branches `BPL BMI BVC BVS BCC BCS BNE BEQ`: 2 cycles not taken, +1 taken, +1 more if the branch crosses a page.
- `JMP` absolute (3) and `JMP` indirect (5) **including the page-boundary wrap bug** (`JMP ($xxFF)` reads the high byte from `$xx00`).
- `JSR` (6), `RTS` (6), `RTI` (6), `BRK` (7, sets B on the pushed status, vector $FFFE/$FFFF).

- [ ] TDD: assert taken/not-taken/page-cross cycle counts, the indirect-JMP bug, and stack contents for JSR/RTS/RTI/BRK.

---

## Task 11: Flag and remaining implied ops

`CLC SEC CLI SEI CLD SED CLV` and any remaining official `NOP` encodings’ canonical form. 2 cycles each.

- [ ] TDD: each flag toggles exactly its bit; cycle count 2.

**At the end of Tasks 3–11 the documented set is complete.** Mark todo `Implement all 151 documented opcodes with cycle counts.` `[x]` once the per-group microtests and (Task 16) Klaus ROM are green in CI.

---

## Task 12: Interrupts — IRQ / NMI / RES cycle semantics

Refine the functional reset into the cycle-accurate 7-cycle sequences and add the interrupt entry path in `step_one_cycle()` at the instruction boundary:
- **NMI** edge-triggered (latch on falling edge), vector $FFFA/$FFFB.
- **IRQ** level-triggered, suppressed when I=1, vector $FFFE/$FFFF.
- **RES** suppresses writes (reads stack but does not push), vector $FFFC/$FFFD, leaves SP decremented by 3.
- B flag clear on the pushed status for IRQ/NMI, set for BRK; I set on entry.
- Model the `i_bus`/line inputs via test seams: `set_irq_line(bool)`, `pulse_nmi()`.

- [ ] **Step 1: Failing tests** for: IRQ taken only when I=0; IRQ ignored when masked; NMI edge behavior; vector selection; 7-cycle timing; SP/stack contents; RES write suppression.
- [ ] **Steps 2-5:** implement → pass → commit/push/CI, then mark todo `Implement IRQ, NMI, RES handling with correct cycle semantics.` `[x]`.

---

## Task 13: Undocumented (illegal) opcodes used by C64 software

Implement the stable illegal opcodes the C64 relies on (`LAX SAX DCP ISC SLO RLA SRE RRA`, the `NOP` variants with operands, `ANC ALR ARR SBX`, and the documented behavior of the unstable ones used in practice). Mark the genuinely unstable/`KIL` opcodes as `KIL` (jam).

- [ ] **Step 1: Failing tests** from the undocumented-opcode test suite (cite source in `NOTES.md`).
- [ ] **Steps 2-5:** implement → pass → commit/push/CI, then mark todo `Implement all undocumented (illegal) opcodes the C64 software relies on.` `[x]`.

---

## Task 14: Save / load state — DEFERRED TO M3 (decided 2026-05-22)

**Decision:** m6510 save/load is deferred to M3, when the runtime save-state format and the `state_writer`/`state_reader` types are introduced (todos §"Runtime library"). Defining a serialization contract now would be tier scope creep ahead of the runtime design. The `save_state`/`load_state` overrides remain documented stubs until then.

**State-chunk layout to implement in M3** (recorded here so it isn't lost): all registers (A/X/Y/SP/P/PC), the I/O port latches (`port_ddr_`, `port_data_`), the engine state (`ir_`, `tcu_`, `ea_`, `operand_`, pending-interrupt latches), and `cycles_`.

- [ ] (M3) TDD: save → mutate → load round-trip restores identical observable state and cycle trajectory.

---

## Task 15: Introspection

Expose the introspection data **directly on `m6510`** (e.g. `register_snapshot()` returning `std::span<const register_descriptor>`, plus `elapsed_cycles()` already present and PC via `cpu_registers()`). Do **not** add methods to `i_chip_introspection` here — that interface is deliberately minimal in M1 and gets its full definition in M4 (todos note). The M1 minimal `introspection()` accessor stays as-is; the snapshot lives on the concrete type until M4 promotes the interface. Document this in `NOTES.md`.

- [ ] TDD: register snapshot reports A/X/Y/SP/P/PC with correct widths/formats; cycle counter matches `elapsed_cycles()`.

---

## Task 16: Conformance test infrastructure + CI wiring

**Files:**
- Create: `tests/m6510_conformance_test.cpp`
- Modify: `CMakeLists.txt`
- Create: `NOTES.md` (if not already), `tests/README.md` documenting ROM acquisition
- Modify: `.github/workflows/ci.yml` (provide the test ROM to CI)

- [ ] **Step 1:** Author `m6510_conformance_test`: read the Klaus functional ROM path from a CMake cache var / env (`MNEMOS_M6510_FUNCTIONAL_ROM`); if unset, **skip** (don't fail) locally but **require** it in CI. The standard `6502_functional_test` build produces a full 64K image written to memory starting at offset `$000A` (zero page `$00`–`$09` is reserved); set PC to the test's entry point and run with a generous cycle budget, asserting the PC settles at the **success-trap address taken from the test's listing** (do not hardcode a guessed value — read it from the `.lst` for the exact ROM build used). A failing test traps by branching to itself at a different PC; detect "PC stopped advancing" and compare against the success address.
- [ ] **Step 2:** Vendor/fetch the Klaus 2M65 ROM in CI with provenance documented in `tests/README.md` (ROM not committed). Add the decimal and undocumented-opcode suites similarly.
- [ ] **Step 3:** Wire the conformance test into `ci.yml` for all four matrix combinations; ensure it runs under the existing presets.
- [ ] **Step 4:** Author per-opcode microtests for at least the trickiest 20 opcodes (page-cross, RMW dummy write, indirect-JMP bug, decimal edge cases, branch timing) — most already exist from Tasks 3–13; consolidate and ensure ≥20 are explicitly covered.
- [ ] **Step 5:** Commit/push/green CI, then mark the test-infra todos and the M1 acceptance items:
  - `Vendor or fetch Klaus 2M65 functional test ROM …`
  - `Vendor or fetch decimal mode test.`
  - `Vendor or fetch undocumented opcode test suite.`
  - `Author m6510_conformance_test integrating all of the above; runs in CI.`
  - `Author per-opcode microtests … for at least the trickiest 20 opcodes.`
  - Acceptance: `m6510_conformance_test passes in CI on all four matrix combinations.`
  - Acceptance: `Zero warnings, zero sanitizer hits when run under ASan+UBSan on Linux Clang.` (verify with `linux-clang-asan` preset)

---

## Task 17: ASan/UBSan + final acceptance sweep

- [ ] Run the suite under the `linux-clang-asan` preset (and a UBSan config) in CI; fix any hits.
- [ ] Confirm zero warnings across the full matrix.
- [ ] ADR `docs/adr/0004-chip-contract.md` already records the contract; add any opcode-engine decisions (cycle-stepped model, decode-table design) as an amendment or a short follow-up ADR if they rose to architecture significance.
- [ ] Final pass over `docs/architecture/mnemos-todos-v0.1.md`: every M1 6510 + test-infra + acceptance item checked with CI evidence.

---

## Risks & Open Questions (resolve before/at the relevant task)

1. **`state_writer`/`state_reader` do not exist yet** (Task 14). **Resolved 2026-05-22: deferred to M3** — implement m6510 save/load when the runtime save-state format lands; chunk layout is documented in Task 14.
2. **Static-init factory registration stripping** (already flagged): the manifest layer (M3) instantiating purely by ID may need whole-archive linking or OBJECT libraries. Not blocking M1 (tests reference the symbol directly).
3. **ROM provenance/licensing** for Klaus 2M65 and the illegal-opcode suite: confirm the source and document in `tests/README.md`; ROMs never committed (AGENTS.md).
4. **Nested-type access from the decode table**: `operation`/`access_kind` are nested in `m6510`; the table lives in a sibling TU. Either expose them as public nested types (current plan) or hoist to namespace scope. *Recommendation:* keep nested + public; revisit only if it forces awkward friending.

---

## Definition of Done (milestone M1 CPU)

- `m6510_conformance_test` green in CI on Win/Linux × Debug/Release.
- Zero warnings; zero ASan/UBSan hits on Linux Clang.
- All M1 "MOS 6510 implementation" and "Test infrastructure for 6502 family" todos checked with CI evidence.
- `NOTES.md` cites every behavioral reference used.
```
