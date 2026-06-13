# SH-2 cycle ledger + cycle-corpus vector schema

Authority: **ADR-0026** (official SH7600/SH7604 + 32X manuals; Emu/Ymir are L5
advisory cross-check only). This doc defines (1) how Mnemos accounts a
per-instruction cycle count and (2) the JSON vector schema the data-gated
`mnemos_chips_cpu_sh2_cycle_conformance_test` (`MNEMOS_SH2_CYCLE_TESTS_DIR`)
consumes. It is the spec the X2/X3 increments (plan
`docs/plans/2026-06-12-sh2-x2-x3-cycle-true.md`) make true.

## The ledger

`sh2::step_instruction()` returns the cycles consumed by one instruction. The
ledger that return value must satisfy:

```
cycles = base
       + internal_states         (X2: per-opcode state count from the SH7600 PM)
       + load_use_penalty         (X2: +1 when this op reads a GPR/T the previous op loaded)
       + bus_access_cycles        (X3: region-aware per data access, SH7604/32X table)
       + cache_penalty            (miss/fill timing, SH7604 cache)
```

Current implementation (pre-X2/X3-default) and the rules every increment keeps:

1. **Base / floor.** `cycles_ = 1` at entry; per-opcode minima are applied as a
   **max-floor** via `account_cycles(N)`. The states table (Z4) replaces the
   scattered magic-number floors but keeps floor semantics (an op never costs
   less than its documented minimum).
2. **Baseline overlap (must be explicit).** The SH7600 manual's "states" count
   already includes the instruction fetch, and the first data-access cycle
   overlaps the 1-cycle EX baseline. A naive `floor + additive bus wait` double-
   counts. Rule: a memory op's `bus_access_cycles` is charged on top of the
   states floor only for the portion **not** already covered by the floor; the
   first access absorbs the baseline. Z4 encodes this per `timing.kind`
   (INTERNAL / MEMORY_READ / MEMORY_WRITE / RMW / BRANCH / DELAYED_BRANCH /
   EXCEPTION / SLEEP).
3. **Load-use ordering.** The load-use +1 (`set_load_use_interlock`) is decided
   from the previous instruction's load destination and **must be charged before
   the consumer's data access** so X3 access timestamps do not drift. (Today the
   +1 is added at end-of-step *after* the X3 charge — `sh2.cpp` ~1585/1594 — which
   Z4 reorders.) Exemptions per the manual: load→load-same-dest, load→MAC.
4. **Bus access cycles (X3).** Charged via the board bus-wait hook
   (`set_bus_wait_callback`); region-aware per ADR-0026's 32X access-timing table
   (NOT Saturn/Ymir numbers). `access_cycle = instr_start + pre_access_stall +
   MA_offset` (not elapsed-at-start). Charged at end-of-step to avoid an
   `account_cycles` floor swallowing it (the PR #133 hazard), but the offset above
   models *when* the access happens.
5. **Contention tie-breaks (X3).** Deterministic: master SH-2 before slave, then
   same-owner DMAC. Fixed, tested, never time-of-day.
6. **Cache (Z7).** Hit vs miss/fill timing per the SH7604 cache; cache state is
   part of save/load. Hit-only is NOT a valid default-on state (the 32X SDRAM
   model is built around 8-word miss fills).
7. **Determinism.** Timing changes cycle *counts*, never observable *values*.
   Guard: sync-vs-threaded equivalence under X2/X3-on; `save_state` must carry the
   load-use state (`pending_load_reg_`/`pending_load_t_`, version bump in Z4) and
   cache state, or `load()` desyncs timing.

## Vector schema (`*.json`, one array of cases per file)

Each case fixes an initial register file, a fetch frame, the per-step expected
cycle counts, and (optionally) the final state. Every case cites the manual.

```json
{
  "manual_ref": "SH7600 Programming Manual, Table 8.x (or 32X HW manual §4.1)",
  "model": { "load_use": true, "bus_contention": false },
  "initial": {
    "R": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    "PC": 0, "PR": 0, "SR": 0, "GBR": 0, "VBR": 0, "MACH": 0, "MACL": 0
  },
  "opcodes": [0, 0, 0, 0, 0],
  "steps": [ { "cycles": 1 } ],
  "final": {
    "R": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    "PC": 0, "PR": 0, "SR": 0, "GBR": 0, "VBR": 0, "MACH": 0, "MACL": 0
  }
}
```

Field notes:
- **`manual_ref`** — required; the SH7600/SH7604/32X section the expected cycles
  come from. The authority is the manual, not any emulator (ADR-0026).
- **`model`** — which timing models the harness enables for this case
  (`set_load_use_interlock` / `set_shared_contention_metering`). Lets the corpus
  pin the load-use delta in isolation and the bus model separately.
- **`opcodes`** — a 5-word frame, same convention as the functional corpus:
  `[0..3]` are the four fetches placed at `PC + 2i`; `[4]` is served for any
  out-of-flow fetch. Branch cases: one `step_instruction()` folds a taken
  delayed branch's delay slot, so `steps` has one entry per `step_instruction`
  call, not per fetch.
- **`steps[i].cycles`** — the expected return of the i-th `step_instruction()`.
- **`final`** — optional; when present, the register file is compared after the
  last step (SR masked to `sh2::sr_mask`), so a timing vector also guards
  semantics.
- **Bus regions** — the internal-timing corpus runs a uniform bus (no bus-wait
  hook). The X3 corpus (Z4+) adds a `bus` region map; documented there when it lands.

Vectors are never machine-committed from an emulator; they are authored from the
manuals and may be cross-checked against Emu/Ymir, whose disagreements are logged
for human adjudication (ADR-0026), never adopted as the expected value.
