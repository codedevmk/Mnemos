# ADR 0006: Emu Reuse and Conformance Tooling

**Status:** Accepted
**Date:** 2026-05-22

## Context

A mature, privately-owned multi-system emulator exists at
`C:\Users\mkrol\source\repos\Emu` ("Emu") — portable C11, callback-based chip
libraries composed into systems. It already covers most of the Mnemos roadmap
(6510, VIC-II, SID, CIA, Z80, SMS VDP/PSG, 68000, YM2612, Genesis VDP, SH-2,
Saturn VDP1/2, NES, SNES, …) and ships proven conformance harnesses (Tom Harte /
SingleStepTests, Lorenz 6510). AGENTS.md designates Emu as reference material:
reusable only after re-review against Mnemos architecture, licensing,
determinism, and test requirements.

Mnemos's `m6510` was implemented fresh (cycle-stepped, table-driven, C++23,
`i_bus`-integrated), not ported. It needed a conformance gate stronger than the
originally-planned Klaus 2M65 functional ROM.

## Decision

1. **Reuse Emu, with re-review.** Emu is the default starting point for Mnemos
   chips from M2 onward: port its proven C cores into the Mnemos tiered C++ /
   `i_bus` architecture rather than reimplementing from scratch. Each port is a
   re-review, not a copy — it must fit the chip contract (ADR 0004), be
   deterministic, and carry Mnemos tests. Emu is the owner's own code; porting it
   into Mnemos relicenses it under the Mnemos license split (Apache-2.0 core, MIT
   chips). Record the Emu provenance in each ported chip's `NOTES.md`.

2. **The 6510 keeps its fresh implementation** and adopts Emu's *tests* rather
   than its code: the Tom Harte (SingleStepTests/ProcessorTests) 6502 corpus,
   which validates final CPU state and the exact per-cycle bus trace. This is a
   stronger gate than Klaus 2M65 and is the same bar Emu's `m6510_v2` clears.
   (Adopting it already caught two real bugs in the fresh core: the indexed
   page-cross dummy-read address, and decimal-mode ARR.)

3. **Add `nlohmann/json` as a test-only dependency** (pinned `FetchContent`,
   `v3.11.3`) to parse the corpus. It is linked only by conformance test targets,
   never by product tiers, satisfying the isolation requirement of the dependency
   policy. License: MIT (compatible).

4. **Conformance corpora are never committed.** They are large and externally
   owned. CI fetches them; the parity tests are data-gated and report *skipped*
   (CTest `SKIP_RETURN_CODE`) when the corpus path is absent, so local builds
   without the data stay green. Provenance is documented where the corpus is
   wired in.

## Consequences

- M2+ milestones move faster by porting proven behavior; the cost is disciplined
  re-review and provenance tracking per chip.
- The 6510 has a per-cycle conformance gate (~2.4M vectors across the documented
  + stable-illegal opcodes) that runs in CI when the corpus is present and skips
  cleanly otherwise.
- One new third-party library (`nlohmann/json`) enters the tree, test-only and
  pinned. No product tier gains a JSON dependency.
- The unstable illegal opcodes (SHA/SHX/SHY/TAS/LAS/ANE/LXA) and JAM/KIL remain
  out of scope for v0.1 and are excluded from the gate; see the 6510 `NOTES.md`.
