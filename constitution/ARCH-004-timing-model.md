---
id: ARCH-004
title: "Timing Model and Determinism"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
invariants:
  - id: INV-TIM-002
    statement: "Identical manifest, ROM hashes, load point, and frame-tagged input produce identical framebuffer hashes."
    verified_by: [tests/golden]
---

# ARCH-004 — Timing Model and Determinism

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
sections 8.3, 11.1–11.3 and 16, per `constitution/MIGRATION.md`. ADR-0005
(fixed-divider master clock) refines the scheduling strategy and remains
accepted authority.

## Clock contract (TDS §8.3)

- A chip's `tick(cycles)` advances its internal state by exactly `cycles` of
  its own clock domain.
- The scheduler (tier 5) translates master clock ticks into per-chip clock
  ticks via the divider declared in the system manifest. The master clock is
  the sole time authority; no chip self-advances.
- A chip MUST NOT call `tick` on another chip directly. All inter-chip
  interactions go through the bus or through explicit event posts handled by
  the scheduler.

## Runtime ownership (TDS §11.1)

The runtime (tier 5) owns master clock progression, per-chip cycle dispatch,
input routing (frame-tagged buffer), save state and rewind ring, determinism
guarantees, and frame-boundary signaling. It does NOT own rendering, audio
output, UI state, or direct introspection hooks.

## Scheduling strategy (TDS §11.2, refined by ADR-0005)

v0.1 uses a fixed-divider master clock scheduler: a master tick advances all
chips by their declared dividers. Slice-based scheduling for systems with
dynamic clock ratios MAY be introduced later by amendment.

## Determinism guarantees (TDS §11.3)

Given identical manifest revision, identical ROM SHA-256s, identical
save-state load point (or power-on), and identical frame-tagged input
sequence, the runtime MUST produce identical output for: framebuffer contents
at every frame, audio sample sequence, and save-state byte stream.

## Determinism sources and validation (TDS §16)

1. Chip implementations are pure functions of (state, input): no undefined
   behavior, no uninitialized memory, no platform-dependent floating-point
   modes.
2. The scheduler is deterministic given the manifest and master clock.
3. Input is frame-tagged and replayed verbatim.

A replay is: (save state at frame F) + (input log from frame F onward) +
(manifest reference + ROM hashes). CI runs golden-frame regression tests per
known ROM; a divergence fails the build (today: `tests/golden/`; pilot phase
P2 extends this with the hash-every-N-frames harness, gate G6).

Optimization changes that risk determinism require an ADR (project plan §4.3).
