---
id: ADR-0005
title: "Runtime Scheduler Strategy (Fixed-Divider Master Clock)"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-25
---

# ADR 0005: Runtime Scheduler Strategy (Fixed-Divider Master Clock)

**Status:** Accepted for M3 runtime
**Date:** 2026-05-25

## Context

The runtime (tier 5) must advance a whole machine deterministically: each chip
exposes `tick(cycles)` and runs at some fraction of a master clock (TDS §8.3,
§11.1). The scheduler turns master-clock progression into per-chip ticks and
detects frame boundaries, and it MUST be deterministic — identical manifest,
ROMs, and inputs must yield an identical framebuffer, audio, and save-state byte
stream every run, on every platform (TDS §11.3, §16).

The v0.1 target systems (C64, later SMS/Genesis) have static clock ratios: every
chip's rate is a fixed integer divider of a single master clock. TDS §11.2 calls
for a **fixed-divider master-clock scheduler** for v0.1 and defers slice-based
scheduling (for dynamic ratios — Saturn, 32X) to v0.2.

## Decision

Adopt the **fixed-divider master-clock scheduler** (`mnemos::runtime::scheduler`).

- **Model:** each scheduled chip carries a `divider` = master cycles per one chip
  cycle. Advancing the master clock by N cycles ticks each chip whenever its
  divider boundary is reached, one chip cycle at a time, in the order the chips
  were supplied. A `divider` of 1 means the chip ticks every master cycle.
- **Dispatch order is the chip-supply order.** For the C64 the VIC-II is listed
  before the CPU so the CPU observes the freshly advanced beam each cycle (φ1/φ2
  ordering). The C64 runs every chip at φ2, i.e. all dividers are 1.
- **Lockstep fast path:** when every divider is 1, `run_master_cycles(n)` issues a
  single batched `tick(n)` per chip — exactly equivalent to n single-cycle ticks
  because no chip observes another mid-`tick` — while `run_frame` still steps one
  cycle at a time to preserve cross-chip interleave to the frame boundary.
- **Frame detection stays rendering-agnostic:** `run_frame` / `run_frames` advance
  until a designated `i_video` chip's `frame_index()` increments. The scheduler
  knows nothing about pixels.

## Consequences

- Determinism is structural: dispatch is integer-divider driven with a fixed
  order and no wall-clock or threading input, so output is bit-identical across
  platforms and runs. Validated by the save/load trajectory test and (data-gated)
  golden-frame hashing.
- Interrupts cross chips through level/edge lines set in chip callbacks (e.g. the
  C64 ORs VIC + CIA1 /IRQ into the 6510), not through the scheduler.

### Limitations (accepted for v0.1)

- **No sub-cycle bus arbitration.** Chips advance a full cycle independently; the
  scheduler does not model intra-cycle contention. The C64 VIC-II bad-line
  cycle-stealing (BA/AEC stalling the CPU) is therefore not enforced by the
  scheduler — the chips expose the signals but the CPU is not halted mid-line.
  Cycle-exact contention is follow-up work alongside the cycle-exact VIC renderer.
- **Static ratios only.** A single integer-divider master clock cannot express
  dynamic clock ratios; slice-based scheduling for those systems is deferred to
  v0.2 (TDS §20).
- **Frame boundary = one video chip.** Multi-video systems would need an explicit
  primary-frame-source policy; v0.1 systems have a single video chip.
