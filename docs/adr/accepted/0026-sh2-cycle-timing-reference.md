---
id: ADR-0026
title: "SH-2 Cycle-Timing Reference (X2/X3)"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-12
ratified: 2026-06-12
---

# ADR 0026: SH-2 Cycle-Timing Reference (X2/X3)

## Context

The two remaining Sega 32X parity items are SH-2 (SH7604) cycle timing:

- **X2** — per-instruction internal timing (states, branch/delay-slot costs) and
  the **load-use interlock** (+1 cycle when an instruction reads a GPR the
  previous one loaded).
- **X3** — SH-2↔SH-2 bus access-cycle and contention timing on the shared
  SDRAM / framebuffer / COMM regions.

Both ship **opt-in** (`MNEMOS_32X_LOAD_USE`, `MNEMOS_32X_BUS_CONTENTION`,
default-OFF) because the project had no validated cycle reference to gate them
on. A 2026-06-12 design cross-check (Codex, L5) plus first-party source
verification settled what the reference can and cannot be:

1. **`CONSTITUTION.md` §2.3 forbids an emulator as oracle** — "Other emulators
   are L5 — advisory, never an oracle. Behavioral authority for silicon is
   official documentation plus the oracle registry." An earlier proposal to use
   Emu's `sh2.c` as the cycle oracle was withdrawn on this ground.
2. **Emu is not a cycle reference anyway.** Verified in
   `Emu/chips/sh2/sh2.c`: the load-use hazard helper
   (`sh2_opcode_load_use_hazard_query`) is metadata that is **never called in
   the step path**, so Emu never charges the load-use +1; its cache fill is a
   *Ymir-simplified* model (partition-0 line-fill clamped to 1 cycle,
   `emulateSH2Cache=false`) and `bus_access_cycles` uses **Saturn** regions.
   Emu is Ymir-parity, not SH7604-accurate.
3. **No public cycle-exact SH-2 corpus and no hardware-trace rig exist.** The
   `hitachi.sh7604` registry entry already records this gap (risk R3).

The achievable authoritative reference is therefore official documentation, not
any emulator.

## Decision

**The behavioral authority for SH-2 cycle timing is official documentation:**

- **SH7600 Programming Manual** — per-instruction state counts, the load-use
  hazard rule, and the +1 penalty magnitude.
- **SH7604 Hardware Manual** — bus access classes and on-chip cache behavior.
- **32X Hardware Manual** — the CPU-to-32X-block access-timing table (SDRAM /
  framebuffer / COMM / cart / VDP / boot-ROM wait states).

These are encoded into a **manual-derived cycle corpus** of per-instruction
vectors (each citing its manual section), consumed by a data-gated Mnemos
cycle-conformance suite (`MNEMOS_SH2_CYCLE_TESTS_DIR`) registered under
`hitachi.sh7604` in `tests/oracles/registry.yaml`.

**Emu and Ymir are L5 advisory cross-checks only** (§2.3). They may run the same
vectors and surface divergences for human adjudication; they **never** supply an
expected value. The manual wins; an Emu/Ymir disagreement is a recorded note,
not an override.

**Target = manual-grounded and cross-checked, explicitly NOT silicon-bit-exact.**
There is no silicon reference; claiming bit-exactness would be false. "Done" for
X2/X3 is the gate below, not "the titles still boot."

**Default-on gate (all required):**

1. The manual-derived cycle corpus passes.
2. The functional SH-2 conformance corpus still passes (no semantic regression).
3. Synchronous-vs-threaded equivalence holds with X2/X3 on
   (`MNEMOS_32X_THREAD=0` and threaded).
4. Save-state round-trips the timing state (pending load-use + cache).
5. The 32X title sweep shows no frame-**hash** regression (stronger than "no new
   boot regression").

Implementation is staged in
`docs/plans/2026-06-12-sh2-x2-x3-cycle-true.md`.

### In practice

- DO: cite the manual section per corpus vector; model the baseline-overlap rule
  explicitly (the first data access absorbs the 1-cycle EX baseline); apply the
  load-use bubble **before** the consumer's data access so X3 timestamps do not
  drift; cross-check against Emu/Ymir and log divergences for adjudication.
- DO NOT: treat an Emu/Ymir cycle count as authoritative; flip default-on on
  "it boots"; ship hit-only cache as the default-on state (the 32X SDRAM model
  is built around 8-word cache-miss fills).

## Consequences

- X2/X3 remain opt-in until the gate is met; the manual-derived corpus becomes
  the SH-2 cycle oracle floor and the ratchet picks it up via the registry.
- `sh2::save_state` must gain a version bump to serialize the load-use state
  (`pending_load_reg_` / `pending_load_t_`), which it currently omits — tracked
  in the plan (Z4).
- On ratification: move this file to `docs/adr/accepted/`, set `status:
  accepted` with a `ratified` date, regenerate `docs/adr/INDEX.md`, and add the
  following suite to `tests/oracles/registry.yaml` under `hitachi.sh7604`
  (correcting the stale gap note: the functional SH-4-corpus suite shipped in
  PR #131; this entry is the cycle oracle):

  ```yaml
  - id: ORC-SH2-CYCLES
    ctest: mnemos_chips_cpu_sh2_cycle_conformance_test
    kind: conformance
    provenance: >
      SH7600 Programming Manual instruction states + load-use rule, SH7604
      Hardware Manual bus/cache, 32X Hardware Manual CPU-access-timing table.
      Emu/Ymir are L5 advisory cross-checks only (CONSTITUTION §2.3), never the
      source of an expected value.
    pass_criteria: per-instruction cycle count and final state match every vector
    data_gated: true
    data_env: MNEMOS_SH2_CYCLE_TESTS_DIR
  ```
