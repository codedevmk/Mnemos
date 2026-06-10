---
id: ADR-0019
title: "Monotone Pacing Positions Across CPU Reset Edges"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0019: Monotone Pacing Positions Across CPU Reset Edges

## Context

Multi-CPU machines (Sega 32X, Sega CD) pace a secondary CPU against the
68000 with cumulative cycle anchors, but `cpu.reset()` zeroes the CPU's
`elapsed_cycles()` on every /RES (32X) or SRES (Sega CD) release edge. Any
post-boot reset toggle therefore replayed every cycle the CPU had ever run
as one burst, destroying the clock-ratio contract. Separately, the 32X
worker thread's reset path forged its completion atomics from the main
thread, breaking the join invariant, and the synchronous and threaded
schedules used different anchor arithmetic (per-slice re-baselining vs
cumulative targets), so the two modes fenced at different SH-2 positions.

## Decision

Options considered: (a) make CPU `elapsed_cycles()` a monotone lifetime
counter that survives `reset()`; (b) machine-level offset bookkeeping;
(c) a system-level monotone position (`sh2_position()` / `sub_position()`)
that folds the discarded elapsed count into a base on each release edge.

Chosen: (c). Option (a) was rejected because the m68000's IRQ-idle table
and DRAM-refresh schedule key off the *absolute* value of `elapsed_`, so
changing reset semantics would perturb the tuned Genesis boot parity. All
pacing anchors now read the system position, never raw `elapsed_cycles()`.
Additionally: only the worker thread may write `sh2_done_`, published
targets are monotone, and both 32X schedules share the same cumulative
target arithmetic (`begin_slice()` removed), so threaded and synchronous
execution are emulated-state equivalent (commit 481ebab, 4379816).

## Consequences

- Reset toggles after boot (Sega CD BIOS loading game code, 32X soft
  resets) resume in step instead of bursting; the 3:1 and 87.5/53.693175
  ratio contracts hold across edges.
- The join invariant ("join returned implies worker parked") is restorable
  from the code alone; main-thread stores to worker atomics are a review
  flag.
- New multi-CPU machines must pace against a monotone system position;
  pacing on raw `elapsed_cycles()` is a review flag.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commits 481ebab, 4379816, merged via e89e460).
