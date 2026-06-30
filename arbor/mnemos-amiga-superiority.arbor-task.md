# Arbor Task — Mnemos Amiga Emulation Superiority vs. WinUAE 6.0.3

**Agent entry point:** `$arbor-research-agent` (Codex) / `/arbor-research-agent` (Claude Code)
**Companion files:** `research_config.yaml`, `mnemos_amiga.plugin.yaml`
**Competitive baseline:** WinUAE 6.0.3 (released 2026-03-04). Reference point: WinUAE's
custom-chipset rewrite (6.0.0) made Agnus/Alice and Denise/Lisa internally cycle-accurate,
moved Denise/Lisa to a separate thread for accurate-mode throughput, and the project
characterizes cycle-exact A500 as "100% accurate." That is the bar.

---

## 0. Read this first — why this is a measurement task before it is an optimization task

Arbor hill-climbs a scalar dev metric and merges only changes that clear a margin on a
**held-out** split. Two failure modes must be designed out up front, or the run produces
confident garbage:

1. **Oracle capture.** If the correctness oracle is WinUAE, the best Mnemos can do is
   *match* WinUAE, bugs included — superiority becomes impossible by construction. The
   oracle must be **hardware truth**, with WinUAE used only as a regression scaffold.
2. **Metric gaming.** A single accuracy scalar lets Arbor trade away performance, or
   regress N titles to fix M, for a net-positive number. The objective must be
   **lexicographic + regression-vetoed**, not a naive weighted sum.

Everything below exists to close those two holes. Do not relax them for convenience.

---

## 1. Arbor Research Contract (intake)

| Field | Value |
|---|---|
| **Objective** | Make Mnemos' Amiga subsystem measurably superior to WinUAE 6.0.3 in **accuracy**, **performance**, and **feature parity+**, proven on a held-out conformance corpus. |
| **Target (optimize)** | Mnemos Amiga subsystem source tree — *path to confirm* (§7). |
| **Primary metric** | **ACS** — Accuracy Conformance Score, 0–100, continuous, on the dev split. |
| **Constraint metric** | **PTR** — Performance Throughput Ratio (× realtime) at the active accuracy tier, on a pinned host. Lexicographically subordinate to ACS, floored. |
| **Gates** | Feature/Parity matrix vs WinUAE 6.0.3 — binary milestones that unlock corpus tiers. |
| **Baseline** | Current Mnemos ACS/PTR at tier A500-OCS-CE (measure in cycle 0). |
| **Held-out discipline** | Conformance corpus split dev/test; merge to trunk only on test-split gain ≥ `merge_threshold` **and** zero regression on previously-passing tests. |
| **Protected (read-only)** | Eval harness, golden references, WinUAE baseline traces. Executors may never edit these. |
| **Budget / run mode** | See `research_config.yaml`; recommended `interaction_mode: collaborative` (you approve direction + each merge). |
| **Out of scope** | Non-Amiga Mnemos cores; UI chrome; anything not on the conformance path. |

---

## 2. The metric, precisely

### 2.1 ACS — primary gradient signal (0–100, dev split)

ACS is the weighted aggregate of per-test conformance, chosen so partial correctness
yields partial credit (smooth gradient for the hypothesis tree to climb):

- **Trace conformance (weight 0.55).** Each micro-test runs to a fixed cycle count and
  emits a cycle-stamped event trace: register writes, DMA slot allocation, interrupt
  assertion cycles, beam (H/V) position at sampled cycles, audio period/volume reloads.
  Score = fraction of cycle-stamped events matching the golden trace. Tolerance band per
  signal class; for the cycle-exact tier the band is **zero** (exact cycle or it fails).
- **Frame/audio conformance (weight 0.35).** Reference scenes render to a fixed cycle
  count; framebuffer and audio sample stream are compared to golden via (a) exact hash —
  binary, and (b) a structural distance (per-pixel / per-sample L∞ + count of differing
  units) — continuous, providing gradient before exactness is reached.
- **Compatibility corpus (weight 0.10).** Curated ADF/WHDLoad/demo set; each title must
  reach a known checkpoint (title-screen or in-game frame hash at a fixed cycle). Pass
  rate. Low weight because it is a coarse, downstream signal — the first two drive the work.

### 2.2 PTR — constraint, not gradient

PTR = sustained emulated speed (× realtime, 50/60 Hz reference) at the active accuracy
tier, headless, fixed workload, on the **pinned** host (§7) with fixed CPU governor/clocks.
GPU is largely irrelevant to chipset-core throughput; it matters only on the RTG/display
path — measure those separately.

- **Hard floor:** `PTR ≥ PTR_floor` (default 1.0× at the active tier). A change that drops
  below the floor is rejected regardless of ACS gain.
- **Above floor:** bounded bonus `λ·clamp(PTR − PTR_winuae_parity, 0, cap)` folded into the
  dev objective, so speed beyond WinUAE parity is rewarded but cannot buy accuracy.

### 2.3 Composite dev objective Arbor optimizes

```
S_dev = ACS_dev + λ · clamp(PTR_dev − PTR_parity, 0, cap)
        with hard reject if PTR_dev < PTR_floor
        with hard veto  if any previously-passing conformance test regresses
```

**Merge rule (held-out):** promote to trunk only if `S_test` gain ≥ `merge_threshold`
**and** the regression veto is clean on the test split. The regression veto is independent
of the scalar and is non-negotiable: an emulator change that fixes some titles by breaking
others is not progress.

---

## 3. Golden-reference policy — the crux of "superior to WinUAE"

- **Tier-A oracle = hardware truth.** Published hardware-truth test-program results and/or
  real-hardware captures define correctness: CIA timer/TOD tests, Paula audio period &
  sub-period reload tests, Copper/Blitter timing tests, bitplane/sprite DMA-slot tests,
  beam-racing references, keyboard-MCU (low-level) tests, AGA fetch-mode tests. Superiority
  is *defined* as matching Tier-A where WinUAE diverges.
- **Tier-B baseline = WinUAE 6.0.3 cycle-exact traces.** Used **only** as a regression
  scaffold and coverage map — never as the correctness oracle. Every Mnemos↔WinUAE
  divergence is adjudicated against Tier-A. Where Tier-A confirms Mnemos correct and WinUAE
  wrong, that is a **logged superiority win**, not a bug to "fix" toward WinUAE.
- All golden + baseline data lives in the protected read-only dir. Generation of WinUAE
  baseline traces is an offline, one-time step done *outside* the optimized tree.

> **Legal invariant:** Kickstart ROMs, IPF/CAPS images, and commercial title images are not
> redistributable and are not part of the repo. The harness references them by path from a
> local, user-provided store. Mnemos itself stays Apache-2.0/MIT-clean.

---

## 4. Hypothesis-tree directions (depth-1 seeds)

Each maps to a subsystem with an independently runnable conformance sub-suite, so Executor
experiments in isolated worktrees stay clean and attributable.

1. **Agnus/Alice** — DMA engine, beam counters, Copper, Blitter, bitplane/sprite DMA slot
   allocation, programmed **and** hardwired display sync (the area WinUAE just rewrote).
2. **Denise/Lisa** — pixel pipeline, playfield priority, dual-playfield, HAM/EHB, sprites,
   AGA fetch modes, blanking/scandouble. Threaded design is a perf lever here.
3. **Paula** — audio DMA, period/volume, sub-DMA-period reload timing, interrupts, disk DMA,
   floppy MFM/flux decode, serial.
4. **CPU cores** — 68000/010 prefetch + cycle-exact; 68020–060 + caches/pipeline; FPU
   (softfloat + arithmetic exceptions); MMU; bus-cycle coupling to chipset timing.
5. **CIA / timing / glue** — CIA-A/B timers, TOD, low-level keyboard MCU + NKRO, interrupt
   prioritization, clock-domain crossing.
6. **Disk / storage** — ADF + extended-ADF, IPF/CAPS, SCP/flux, hardfile/RDB/GPT, CD32/CDTV.
7. **RTG / display path** — Picasso96/uaegfx, hardware RTG boards, output correctness/scaling.
8. **Performance architecture** — scheduler, threading model, dynarec/JIT, dirty-region
   rendering, cache-friendly layout. **Must not violate accuracy invariants** — perf wins
   are valid only if ACS and the regression veto hold.
9. **Compatibility breadth** — corpus pass-rate driver layered on 1–8.

Recommended depth-1 ordering follows the accuracy-tier phasing in §5.

---

## 5. Accuracy-tier phasing (corpus gates)

Pursue tiers in order; each gate unlocks the next tier's tests into dev/test:

1. **A500 / OCS / cycle-exact** — match WinUAE's stated "100% accurate" bar first. This is
   the densest, best-documented conformance target and the right place to prove the method.
2. **A500+/A600 / ECS** — ECS Agnus/Denise deltas, productivity/overscan modes.
3. **A1200 / AGA / cycle-exact chipset** — beat WinUAE's "good" (non-cycle-exact CPU-internal)
   tier by delivering tighter CPU-internal timing, a concrete superiority axis.
4. **A3000/A4000 + accelerators / RTG** — where WinUAE is "fast-CPU-only"; cycle-accurate
   chipset access + competitive throughput here is a clear differentiation lane.

---

## 6. Success criteria (definition of done)

- **Accuracy:** ACS_test at tier A500-OCS-CE ≥ Tier-A truth within the cycle-exact (zero)
  tolerance band on the full held-out suite; ≥ K logged superiority wins where Mnemos
  matches hardware and WinUAE 6.0.3 does not.
- **Performance:** PTR at each active tier ≥ WinUAE 6.0.3 on the pinned host, with the
  accuracy invariant intact (no accuracy traded for speed).
- **Features:** Parity matrix ≥ WinUAE 6.0.3 on the agreed scope, each feature backed by
  passing conformance tests (no feature counts until its tests pass).
- **Hygiene:** zero-warning build, deterministic harness, every merge regression-clean,
  every superiority win documented with the adjudicating Tier-A reference.

---

## 7. Open decisions — confirm before launch (do not assume)

Arbor's intake will ask for these if unset; resolving them now prevents a wrong-target run.

1. **Target path.** Exact Mnemos repo + Amiga subsystem directory to optimize.
2. **Harness precondition (blocking).** Arbor requires a *runnable eval script + dev/test
   data + clean git*. Confirm Mnemos exposes a **headless, deterministic, scriptable** run
   mode with: fixed cycle-count run, trace export, framebuffer+audio dump, and hashing. If
   not, **build that harness first** — it is prerequisite work, not part of the optimized
   loop, and the optimization cannot start without it.
3. **Golden corpus.** Which Tier-A hardware-truth references / test programs you have legal
   access to; confirmation that WinUAE 6.0.3 Tier-B traces will be generated locally.
4. **Pinned perf host.** CPU model + fixed governor/clock policy for PTR determinism. (GPU
   RTX 5090 is known; the chipset-core throughput number is CPU/memory-bound — pin the CPU.)
5. **Tier scope/order.** Confirm §5 phasing and the first tier to target.
6. **Budget.** `max_cycles`, `max_depth`, `executor.max_turns`, wall-clock ceiling, staged
   smoke→pilot→full profile (see plugin).
7. **Run mode.** Confirm `collaborative` (recommended) vs `review`/`direction`/`auto`.

---

## 8. Risks / failure modes to monitor

- **Determinism leaks** (host timing, threading races, uninitialized state) make ACS noisy
  and break dev/test discipline → enforce a deterministic, single-source-of-time harness;
  seed and pin everything; CI a repeat-run hash check.
- **Threaded Denise/Lisa-style perf work** introducing ordering races that pass intermittently
  → conformance must run under deterministic scheduling; perf threading validated against the
  same golden traces, not a relaxed mode.
- **Tier-B drift** — silently treating WinUAE as the oracle → adjudication against Tier-A is
  mandatory on every divergence; log, don't auto-converge.
- **Corpus overfit** — climbing dev compat % by special-casing titles → low corpus weight,
  held-out test split, and the regression veto are the guards; watch for narrow hacks in
  Executor diffs.
