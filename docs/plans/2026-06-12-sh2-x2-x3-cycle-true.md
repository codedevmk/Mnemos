# SH-2 X2/X3 cycle-true timing — implementation plan (rev. 2)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline,
> human checkpoint after every increment). Subagent-driven execution unavailable
> (quota). Steps use `- [ ]` checkboxes.

**Goal:** Make SH-2 per-instruction timing (X2: internal states + load-use) and
SH-2↔SH-2 bus access/contention timing (X3) cycle-true and **default-on**,
validated against an authoritative reference and the full regression gate.

**Reference posture (decided 2026-06-12, after a Codex cross-check + source
verification):** Per `CONSTITUTION.md` §2.3, *other emulators are L5 — advisory,
never an oracle; behavioral authority for silicon is official documentation plus
the oracle registry.* So:
- **Authority = official docs:** SH7600 Programming Manual (per-instruction
  states, the load-use rule, the +1 magnitude), SH7604 Hardware Manual (bus/cache),
  and the 32X hardware-manual **CPU-to-32X-block access-timing table**.
- **Emu / Ymir = L5 advisory cross-check only** — run the same vectors, flag
  divergence for human adjudication; never the source of an expected value.
- **Honest limit:** there is NO bit-exact silicon SH-2 reference. Target =
  *manual-grounded, cross-checked*, not bit-exact. "Done" = the gate in Stage D.

**Why the earlier "Emu as oracle" framing was dropped (verified in source):**
- `CONSTITUTION.md` §2.3 forbids an emulator as oracle.
- Emu never charges the load-use +1 (`sh2_opcode_load_use_hazard_query` is metadata,
  never called in the step path) — it can't oracle X2's core.
- Emu's cache fill is *Ymir-simplified* (partition-0 line-fill clamped to 1 cy,
  `emulateSH2Cache=false`) and `bus_access_cycles` uses Saturn regions — it is
  Ymir-parity, not SH7604-accurate.

**Tech stack:** C++23 (Mnemos sh2 + Catch2), a manual-derived cycle corpus
(JSON, data-gated like `MNEMOS_SH2_TESTS_DIR`), optional Emu/Ymir cross-check tool.

---

## 32X access timing (authority: official 32X Hardware Manual §4.4)

SOURCED 2026-06-13 from the official **32X Hardware Manual (1994, Sega)** §4.4
"Access Timing of each CPU to 32X Block" (archive.org full text), cross-checked
against the L5 matiaszanolli transcription — they agree. These are SH-2 **wait
states** the 32X board inserts, **added on top of the instruction's base MA cycle**
(Table 7.2 already counts MA=1 for a no-wait access — do NOT double-count the base):

| Region | Read | Write | Kind |
|---|---|---|---|
| System / COMM regs | 1 | 1 | **constant** |
| 32X VDP regs | 5 | 5 | **constant** |
| Boot ROM | 1 | — | **constant** |
| SDRAM `$06000000` | 12 / 8-word burst (cache-line fill) | 2 / word | **cache-coupled (read)** |
| Frame buffer `$04000000` | 5–12 | 1–3 (5 if FIFO full) | range |
| Cart ROM | 6–15 | 6–15 | range |
| Palette | 5 → up to 64µs on VDP conflict | same | range |

SDRAM cache-through (`$20000000`) reads still pay the 8-word burst. **Z5a** models
the constants (COMM/VDP/boot) + SDRAM-write (2/word) + FB-write (min); the ranges
(cart/FB-read/palette) are condition-dependent like the X2 multiplier scoreboard;
SDRAM read is cache-coupled → folds into **Z5b/Z7**.

---

## Cycle-ledger semantics (must be explicit before any table port)

1. **Baseline overlap.** Emu/Ymir subtract the 1-cycle instruction baseline from
   the first data access. Mnemos today: `cycles_ = 1` base, then `account_cycles`
   floors, then `add_external_wait_cycles` *adds* board waits. A naive states-table
   port will over/undercount memory ops unless the baseline-overlap rule is modeled
   explicitly (define: does the manual's "states" already include the fetch, and
   does the first MA cycle overlap the EX baseline?).
2. **Ordering (verified bug to fix):** Mnemos charges X3 bus waits at
   `sh2.cpp:1585-1587` then adds the load-use +1 at `1594-1598` — i.e. **load-use
   is applied AFTER X3**. The load-use bubble must land **before** the consumer's
   data access so X3 timestamps don't drift. Re-order in Z4/Z5.
3. **Tie-breaks must be fixed/deterministic** (master-SH2 before slave, then DMAC)
   — already the X3 design; keep it explicit and tested.

---

## Stage A — governance + ledger (no behavior change)

### Increment Z1: oracle ADR + registry entry
**Files:** Create `docs/adr/proposed/NNNN-sh2-cycle-timing-reference.md`; create/append
`tests/oracles/registry.yaml` (create if absent).
- [ ] Z1.1 Write a proposed ADR: reference = official SH7600/SH7604 + 32X manuals;
  Emu/Ymir explicitly L5 cross-check; target = manual-grounded not bit-exact; the
  Stage-D default-on gate. (Humans ratify — §2.1; don't set `accepted`.)
- [ ] Z1.2 Add the registry entry naming the manuals + the corpus path.
- [ ] Z1.3 Commit. **PR: "docs(adr): SH-2 cycle-timing reference + oracle registry".**
  *Checkpoint: Marius ratifies the ADR before code lands.*

### Increment Z2: cycle-ledger model doc + harness skeleton (RED)
**Files:** `docs/sh2-cycle-ledger.md`; `src/chips/cpu/sh2/tests/sh2_cycle_conformance_test.cpp`;
`src/chips/cpu/sh2/CMakeLists.txt`.
- [ ] Z2.1 Document the ledger: base/states/MA-overlap/load-use/bus/cache and the
  baseline-overlap rule; define the vector schema (pins: manual section cited,
  pre-state, opcode frame, bus-region map, cache state, expected cycles, final state).
- [ ] Z2.2 Data-gated harness (`MNEMOS_SH2_CYCLE_TESTS_DIR`, ctest SKIP=4): load
  vector, set pre-state + cache, `step_instruction()`, assert `cycles_ == expected`
  AND final state. Branch vectors compare ONE Mnemos step to one logical step
  (delay-slot folded). Interrupts/exceptions = a separate corpus.
- [ ] Z2.3 Commit harness (RED acceptable — data-gated, skips in CI).

---

## Stage B — X2 (internal + load-use), manual-derived

### Increment Z3: internal-timing corpus (manual) + cross-check
**Files:** `tests/oracles/sh2-cycles/internal/*.json` (+ a small Emu/Ymir cross-check tool).
- [ ] Z3.1 Author internal-timing vectors from the SH7600 state tables (uniform
  bus to isolate states from bus variance). Cite the manual section per vector.
- [ ] Z3.2 Optional Emu/Ymir cross-check tool runs the same frames; **log
  divergences for Marius to adjudicate** (manual wins; divergence = misread or an
  Emu/Ymir quirk worth a note). Never auto-adopt Emu's number.

### Increment Z4: port internal states + load-use (GREEN)
**Files:** `src/chips/cpu/sh2/sh2.cpp`, `sh2.hpp`.
- [ ] Z4.1 Replace scattered `account_cycles(N)` magic with a manual-derived
  per-opcode states table; model the baseline-overlap rule explicitly.
- [ ] Z4.2 Re-derive load-use from the manual rule: keep/repair the classifiers
  (`load_destination`/`source_reg_mask`/`consumes_t_bit`); **add explicit vectors
  for the load→load-same-dest and load→MAC exemptions** before trusting them; apply
  the +1 **before** the consumer's data access (fix the ordering bug §2).
- [ ] Z4.3 **Bump `sh2::save_state` version** to serialize `pending_load_reg_` +
  `pending_load_t_` (currently omitted) — add a save/load round-trip test.
- [ ] Z4.4 Iterate Z2 to green (internal + load-use). Flag-off bit-identical;
  format + build + ctest. **PR: "sh2(X2): manual-grounded internal + load-use timing".**

---

## Stage C — X3 (bus access + contention) + cache

### Increment Z5: single-CPU region access timing
**Files:** `src/chips/cpu/sh2/sh2.cpp` (`add_external_wait_cycles`,
`record_data_access`), `src/manifests/sega32x/sega32x_system.cpp`.
- [ ] Z5.1 Transcribe + confirm the 32X access-timing table (above) from the manual.
- [ ] Z5.2 Replace flat byte/word=1/long=2 with region-aware per-access cost; keep
  end-of-step charge discipline (PR #133 swallow hazard) AND model the data-access
  cycle offset (access_cycle = start + pre-access stall + MA offset, not elapsed-at-start).
- [ ] Z5.3 Bus-timing corpus (single CPU) green. **PR: "sh2(X3a): region access-cycle timing".**

### Increment Z6: two-SH-2 contention
- [ ] Z6.1 Extend the reservation across both SH-2 + DMAC with fixed tie-breaks;
  contention corpus green. Multi-master (68K/VDP) scoped + logged, not silently capped.
  **PR: "sh2(X3b): SH-2↔SH-2 + DMAC contention".**

### Increment Z7: cache (hit AND miss)
- [ ] Z7.1 Model CCR/LRU/direct-array/purge + **miss/fill timing** (the 32X SDRAM
  model is built around 8-word miss fills — hit-only is NOT enough for default-on);
  cache state in save/load. Vectors clear/serialize/prime cache per case.
- [ ] Z7.2 Cache corpus green. **PR: "sh2: SH7604 cache timing + state".**

---

## Stage D — default-on (gated)

### Increment Z8: regression + flip
- [ ] Z8.1 **Gate (all required):** cycle corpus green · functional corpus still
  green · sync-vs-threaded equivalence under X2/X3-on (`MNEMOS_32X_THREAD=0` and
  threaded) · save-state coverage (pending-load + cache) · 32X title-sweep
  frame-HASH regressions (not merely "no new boot regression").
- [ ] Z8.2 Default `model_load_use_` + `meter_shared_contention_` ON; env rollback kept.
- [ ] Z8.3 Update parity-gap-inventory (X2/X3→done, 32X 8/8), hard-problems board,
  sega32x-port memory; flip the ADR to ratified-accepted (Marius). Full verify + PR + CI.

---

## Risks / open questions
1. **Manual transcription accuracy** — the access-timing table is the linchpin;
   confirm against the manual PDF (Z5.1), cross-check vs Emu/Ymir, adjudicate divergence.
2. **Baseline-overlap** — getting the states/MA-overlap convention wrong skews every
   memory op; nail it in Z2 with worked examples before porting (Z4).
3. **Determinism** — timing changes can alter VALUES via fence/IRQ/DMAC/COMM/PWM
   progression; Z8.1 sync-vs-threaded equivalence + frame-hash is the guard. Save-state
   must carry pending-load + cache or load() desyncs timing.
4. **Cache-miss required** — do not ship default-on with hit-only (Codex).
5. **Codex = L5** — its review informed this plan; re-run it on the Z4/Z5 classifiers,
   but it never sets an expected value (so does Emu/Ymir).
