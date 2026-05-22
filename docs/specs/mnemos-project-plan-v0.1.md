# Mnemos — Project Plan

**Version:** 0.1 (initial draft)
**Status:** Draft, awaiting review
**Owner:** Marius
**Companion document:** `mnemos-architecture-tds-v0.1.md`

---

## 1. Purpose

Define the milestone phasing, success criteria, dependencies, and risk register for building Mnemos from a green field to system parity with the current Emu codebase.

This plan does not assign calendar dates. Effort is expressed in T-shirt sizes (S/M/L/XL) until enough velocity data exists to estimate calendar time meaningfully. T-shirt-to-week mapping is deliberately deferred — premature dates are noise.

| Size | Rough relative effort |
|------|----------------------|
| S | ~1 week solo + agents |
| M | ~2–4 weeks |
| L | ~1–2 months |
| XL | 2+ months |

---

## 2. Strategic Posture

- **Pure rewrite.** Emu is frozen at its current state. No reactive cleanup investments in Emu beyond what's already shipped.
- **Single canonical workspace.** Mnemos lives in its own monorepo. Emu remains in its existing repository for reference and pre-Mnemos play.
- **No code lift from Emu without re-review.** Any code carried over MUST be re-evaluated against the Mnemos architecture and conventions before landing.
- **AI agent participation is expected.** Multiple agents will contribute. Governance, naming, and layering rules are enforced via CI gates, not goodwill.

---

## 3. Milestone Phasing

Eleven milestones, M0 through M10. Each milestone has a hard exit criterion. No milestone is considered done until its exit criterion is independently verified.

### M0 — Workspace and Toolchain Bring-Up

**Effort:** S
**Goal:** A clean, hygienic monorepo with build, test, and CI infrastructure operating on Windows 11 + Linux.

**Scope:**
- Monorepo skeleton with the directory layout from TDS §5.
- Root `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `.gitignore`.
- A no-op foundation library with one trivial unit test.
- CI pipeline (GitHub Actions or chosen equivalent) running configure/build/test on both Windows and Linux for Debug and Release.
- Pre-commit hooks (clang-format dry-run).
- `mnemos_declare_tier` CMake function enforcing dependency direction.
- `LICENSE`, `LICENSE-chips`, `THIRD_PARTY_NOTICES.md`, `README.md` with vision blurb.

**Exit criterion:** A pull request that adds a one-line change passes the full CI matrix on both platforms with zero warnings.

**Dependencies:** None.

---

### M1 — Foundation Library and First Chip

**Effort:** M
**Goal:** A real foundation library and a fully-passing 6502-family CPU (the MOS 6510) demonstrating the chip contract end-to-end.

**Scope:**
- **Foundation library:** allocators, containers (or thin wrappers around stdlib), time, bit manipulation, logging, filesystem facade, threading facade, byte order helpers, span/expected utilities.
- **`chips::common`** with the class taxonomy (`chip_class`), base interfaces (`i_chip`, `i_cpu`, `i_audio_synth`, `i_video`, `i_bus_controller`, `i_storage`, `i_mapper`), and the chip factory registry.
- **`chips::cpu::m6510`** — full instruction set including all documented and illegal opcodes, full addressing modes, cycle-accurate timing, interrupt handling.
- **6502 test ROM suite integrated:** Klaus 2M65 functional test, decimal mode tests, undocumented opcode tests.
- **Golden-cycle traces** for a small set of programs verified against a known-good reference.

**Exit criterion:** The 6510 passes its full test suite in CI, including all undocumented opcodes and decimal mode, on both platforms. Cycle counts match reference within zero tolerance.

**Dependencies:** M0.

---

### M2 — Chip Library Expansion (C64 Set)

**Effort:** M
**Goal:** All chips needed to compose a C64 implemented, individually tested.

**Scope:**
- **`chips::video::vic_ii_6569`** — PAL VIC-II with sprite handling, all graphics modes, raster IRQ, badlines.
- **`chips::audio::sid_6581`** — SID with three voices, ADSR, filter (filter quality may iterate; correctness of register interface is mandatory).
- **`chips::bus_controller::cia_6526`** — CIA with TOD clock, timers A/B, serial port, IRQ generation.
- **`chips::mapper::c64_pla`** — PLA banking logic for the C64 memory map.
- Per-chip unit tests with reference register traces.

**Exit criterion:** Each chip passes its public test suite in CI. SID register interface validates against known SID-tune traces; audio waveform quality is iterated, not gated on this milestone.

**Dependencies:** M1.

---

### M3 — Topology, Manifest Loader, Runtime, and First Boot

**Effort:** L
**Goal:** The C64 boots to the BASIC prompt in a headless runtime, deterministically.

**Scope:**
- **`topology`** library: bus implementation, region table, mapper hooks, MMIO mediation.
- **`manifests::common`** library: TOML loader using `tomlplusplus`, schema validator, manifest-to-component-graph builder.
- **`manifests::c64`** with the PAL manifest and ROM hash declarations.
- **`runtime`** library: master clock scheduler, fixed-divider tick dispatch, save state (header + per-chip chunks + zstd), input routing (frame-tagged buffer), frame boundary signaling.
- **Headless `mnemos_runtime_cli`** test driver: load manifest + ROMs, run N frames, dump framebuffer hash.
- **First golden frame test:** C64 boots to BASIC prompt; framebuffer hash at frame N committed.

**Exit criterion:** `mnemos_runtime_cli load c64.pal --frames 600 --dump-hash` produces a stable, reproducible hash across Win+Linux, Debug+Release. Save → load → continue produces an identical hash trajectory.

**Dependencies:** M2.

---

### M4 — Instrumentation API and Developer Frontend MVP

**Effort:** L
**Goal:** The dev frontend can attach to a running C64, inspect state, set breakpoints, and step.

**Scope:**
- **`instrumentation`** library: `i_runtime_introspection`, event subscription, breakpoint engine, watch engine.
- **Cap'n Proto wire schemas** (`instrumentation/wire/*.capnp`) for state queries, event streams, control commands.
- **Wire transport:** local domain socket / named pipe server in-process; client library generated.
- **`apps::dev` MVP** with frontend SDK primitives:
  - Disassembly view (6502)
  - Memory view (raw bytes, ASCII column)
  - Register panel
  - Breakpoint list (PC, memory R/W)
  - Step / step-over / run / pause controls
  - Framebuffer mirror

**Exit criterion:** Set a PC breakpoint at the BASIC ready prompt entry, hit it, inspect zero page, step ten instructions, observe expected state changes. All over the wire protocol (dev frontend connects to runtime as a wire client, not in-process, even when co-located).

**Dependencies:** M3.

---

### M5 — Player Frontend MVP (Single System)

**Effort:** L
**Goal:** A minimal but pleasant player frontend that launches and plays C64 software.

**Scope:**
- **`frontend_sdk`** initial: Vulkan renderer, custom UI toolkit core (window, panels, lists, buttons, text rendering, theming), input device abstraction.
- **`apps::player` MVP:**
  - Library view (list of `.crt`/`.d64`/`.prg` files in a configured directory)
  - Game launch
  - Framebuffer presentation with integer scaling
  - Audio playback
  - Gamepad + keyboard input mapping
  - Save / load state
  - Pause, reset, exit
- No couch mode, no scraping, no shaders yet — those land in v0.2 work.

**Exit criterion:** Launch a `.prg`, play it with a gamepad, save state, exit, relaunch, load state, continue. Works on both Win + Linux.

**Dependencies:** M3 (M4 not strictly required, but useful for debugging M5).

---

### M6 — Second System (SMS) — Composability Validation

**Effort:** M
**Goal:** Prove the chip library and manifest system compose correctly for a different machine.

**Scope:**
- **`chips::cpu::z80`** — full Z80 with documented + undocumented flag behavior. Passes ZEXALL.
- **`chips::audio::sn76489`** — PSG with three tone channels + noise.
- **`chips::video::sms_vdp`** — SMS VDP modes.
- **`manifests::sms`** with PAL + NTSC variants.
- SMS golden frame tests.
- Confirm: the M6 work consumed zero changes to the chip contract or runtime contract from M1–M3. If contract changes were required, an ADR documents why and v0.1 is amended.

**Exit criterion:** SMS boots, runs a known commercial ROM headlessly with golden hash verification, plays in the player frontend. Z80 passes ZEXALL.

**Dependencies:** M3 (M5 if intended to play in the frontend at exit).

---

### M7 — Scripting (Lua) and IPC (Python)

**Effort:** M
**Goal:** Both scripting surfaces operational.

**Scope:**
- **`instrumentation::scripting_lua`** — Lua 5.4 + sol2, sandbox configuration, exposed API surface, breakpoint and watch sugar.
- **Dev frontend Lua console** — REPL panel, script loading, error reporting.
- **`mnemos-py` package** — generated Python bindings over the wire protocol, idiomatic wrappers, distributed as a separate artifact.
- **Reference Python script:** asset extraction (dump VIC-II character matrix) running against a live C64 session.

**Exit criterion:** A Lua script in the dev frontend can set a conditional breakpoint, dump VIC-II sprite data when triggered, and write it to disk. A Python script outside the process can do the same via wire protocol.

**Dependencies:** M4.

---

### M8 — Genesis (Dual-CPU System)

**Effort:** L
**Goal:** Prove dual-CPU + complex video + complex audio compositions.

**Scope:**
- **`chips::cpu::m68000`** — full 68000 (the 68k that drives the Genesis main CPU). Passes available 68000 test suites.
- **`chips::audio::ym2612`** — Yamaha FM synthesis.
- **`chips::audio::sn76489`** already exists; verify reuse from SMS.
- **`chips::cpu::z80`** already exists; verify reuse from SMS (the Genesis sound Z80).
- **`chips::video::vdp_315_5313`** — Genesis VDP.
- **`manifests::genesis`** with PAL + NTSC.
- Golden frame tests for known commercial Genesis ROMs.

**Exit criterion:** Genesis boots and runs known commercial ROMs headlessly with golden hash verification, plays in the player frontend. Verifies dual-CPU scheduling correctness.

**Dependencies:** M3, M6 (for Z80 reuse confirmation).

---

### M9 — Multiplatform Parity Audit

**Effort:** S
**Goal:** Verify nothing has silently drifted toward Windows-only assumptions.

**Scope:**
- Full clean Linux build from scratch on a fresh container.
- Linux performance baseline established.
- Linux-specific input (evdev / SDL gamepad) verified.
- Linux audio (PipeWire / PulseAudio fallback) verified.
- Sanitizer builds (ASan, UBSan) run on the full system suite.
- Documentation pass: `BUILDING.md` for both platforms, `CONTRIBUTING.md` with platform expectations.

**Exit criterion:** All milestone exit criteria from M0 through M8 are re-verified on Linux without modification.

**Dependencies:** M8.

---

### M10 — Netplay Foundations

**Effort:** XL
**Goal:** Two clients can play a deterministic system together over a local network with rollback.

**Scope:**
- Frame-tagged input ratification: confirm M3's input subsystem meets rollback requirements.
- State hash introspection capability.
- Rollback engine: input prediction, mispredict detection, resimulation.
- Lockstep fallback transport.
- Local lobby + direct-connect (matchmaking and NAT traversal deferred).
- Initial supported system: SMS (simple, well-tested by this point).

**Exit criterion:** Two SMS instances on the same LAN play a 2-player ROM with input from both clients, rollback active, with no desync over a 10-minute session.

**Dependencies:** M9.

---

### Beyond M10 — System Coverage Catch-Up (Not Numbered as Milestones Yet)

These are tracked as planned but not gated by exit criteria until their scoping pass:

- **C64 peripheral coverage:** datasette, 1541 drive (with `chips::storage::c1541`), IEC bus, REU.
- **Sega 32X:** 68000 already exists; add SH-2 CPU (`chips::cpu::sh2`) and 32X VDP.
- **Sega Saturn:** scope and HLE strategy require their own design doc; expected to be the hardest single system to land.
- **Capcom CPS1:** arcade hardware, Z80 + 68000 reuse, custom chips.
- **Commodore Amiga 500:** 68000 reuse, Paula + Denise + Agnus + Gary, custom chipset.
- **Cleanup of pre-existing Emu work:** decide per-system whether to port lessons or re-derive from scratch.

A separate "Mnemos System Roadmap" document will govern these once M10 is in flight.

---

## 4. Cross-Cutting Workstreams

These run in parallel with the milestone work, not after it.

### 4.1 Continuous Integration

- Workflow added in M0; expanded with each milestone.
- Matrix: {Windows, Linux} × {MSVC/GCC, Clang} × {Debug, Release, RelWithDebInfo}.
- Sanitizer jobs (ASan, UBSan, TSan) on Linux Clang.
- Golden frame test suite runs on every PR.
- Per-chip test suites are not optional — adding a chip requires adding its tests in the same PR.

### 4.2 Documentation

- ADRs (architecture decision records) under `docs/adr/` for every non-obvious choice.
- TDS evolves: v0.1 → v0.2 milestones include doc revisions.
- Per-chip implementation notes (`chips/<category>/<part>/NOTES.md`) cover quirks, references, test sources.
- Per-system implementation notes covering manifest authoring, quirks, and HLE rationale where applicable.

### 4.3 Performance

- No performance work before M3. "Make it correct, then make it fast."
- After M3, a microbenchmark suite measures per-chip cycle cost.
- After M6 (multi-system), a system-level benchmark measures frames/sec on reference hardware.
- Optimization changes that risk determinism require an ADR.

### 4.4 Governance and Naming

- Naming conventions (snake_case, namespacing) inherited from the Eliot Engine governance posture, adapted for the Mnemos namespace.
- A `naming_validator` tool runs in CI from M1 onward.
- Chip ID format (`vendor.part.variant`) enforced by the chip factory registry.

---

## 5. Dependencies and Critical Path

```
M0 → M1 → M2 → M3 ─┬→ M4 → M7
                   ├→ M5
                   ├→ M6 → M8 → M9 → M10
                   │
                   └→ (M5 ⟶ M6 useful but not strictly required)
```

**Critical path to a fully playable, networked, multi-system Mnemos:** M0 → M1 → M2 → M3 → M6 → M8 → M9 → M10. Roughly seven dependency hops; everything else parallels off this spine.

**Earliest first deliverable a user could play:** end of M5 (C64 in player frontend).

**Earliest first deliverable a developer could attach to:** end of M4 (C64 in dev frontend).

---

## 6. Risk Register

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Chip contract requires breaking change after M2 | M | H | Build M1's chip implementation deliberately to stress the contract; review contract before M2 lock-in. |
| Wire protocol churn breaks dev frontend | M | M | Versioned schemas from day one; semver discipline. |
| Cycle-accurate Saturn is too slow on target HW | H | M | Saturn is post-v0.1; HLE pre-declared in manifest; performance budget set per chip. |
| Custom UI toolkit underestimated | H | H | Treat M5 frontend SDK as L effort, not S. Defer non-essential widgets to v0.2. |
| Determinism breaks under sanitizers but works otherwise | M | H | Sanitizers in CI from M3; treat any sanitizer-only divergence as a P0 bug. |
| AI-agent contributions drift from architecture | M | M | Governance tooling, layered CMake validation, naming validator, mandatory ADRs. |
| Vulkan-only rendering bites on driver edge cases | M | M | Reference hardware list; willingness to add backend in v0.2 only with measurable cause. |
| Open-source license audit surfaces incompatible code | L | XL | Re-review every M; ADRs for any borrowed code with provenance; CI license-scan. |
| Scope creep into Eliot integration before standalone is solid | M | M | Eliot plugin path is a v1.0 milestone, not a v0.1 one. |

---

## 7. Success Criteria (Aggregate, End of v0.1)

The v0.1 scope is **everything through M10**, plus the C64-peripheral cleanup work that lands opportunistically along the way. v0.1 is considered shipped when:

1. Mnemos plays C64, SMS, and Genesis software in the player frontend on Windows + Linux.
2. The dev frontend attaches to any running system and provides disassembly, memory view, breakpoints, watches, and Lua scripting.
3. A Python client over the wire protocol can drive a headless session and extract assets.
4. Two clients can play SMS together over LAN with rollback netplay.
5. The full CI matrix is green, including sanitizers and golden frame tests.
6. The TDS, project plan, and per-system notes are current.

System coverage beyond C64 / SMS / Genesis (32X, Saturn, CPS1, Amiga) is v1.0 territory and gets its own plan.

---

**End of Mnemos Project Plan v0.1.**
