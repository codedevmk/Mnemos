---
id: ADR-0011
title: "Sega 32X Subsystem"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-09
ratified: 2026-06-10
---

# ADR 0011: Sega 32X Subsystem

**Status:** Proposed
**Date:** 2026-06-09

## Context

The 32X (Mega Drive 32X / Genesis 32X) is a mushroom-shaped add-on that plugs
into the Genesis cartridge slot and layers two RISC CPUs and a second video
path on top of the existing Genesis hardware. Unlike the Sega CD — which adds a
*second* 68000 of the same family Mnemos already implements (`chips/cpu/m68000`)
— the 32X introduces a CPU family Mnemos does not yet have: the Hitachi SH-2
(SH7604). This ADR fixes how the 32X subsystem is structured before any of it
is built, so the work is gated the same way the Sega CD port was (ADR-0006,
staged phases A–D) rather than landing as one unreviewable drop.

The 32X is additive to the Genesis the same way the Sega CD is: a plain
Genesis machine never traverses any 32X path, so the byte-parity floor for the
base system is untouched by this work.

### Hardware summary (public documentation)

- **Two Hitachi SH7604 (SH-2) CPUs** — a "master" and a "slave", 32-bit
  big-endian RISC, fixed 16-bit instruction width, ~23 MHz. They share the
  32X's address space and coordinate through a comm-register block. Each has an
  on-chip set of peripherals (the cache, the free-running timer / WDT, the DMA
  controller, the serial/comm interface, and the interrupt controller) — only
  the subset the 32X actually drives needs to be modelled.
- **32X VDP** — a framebuffer-oriented video unit (not a tile/sprite engine
  like the Genesis VDP). It owns a 256 KB *frame buffer* (two banks, displayed
  via a line table) in either packed-pixel (8 bpp into a 256-entry CRAM) or
  direct-color (15 bpp RGB) mode, plus an "auto-fill" register block. Its
  output is **priority-composited with the Genesis VDP's output** to make the
  final picture.
- **PWM audio** — a stereo pulse-width-modulation sound source clocked off the
  SH-2 timer, mixed with the Genesis YM2612 + SN76489.
- **Bank / comm / interrupt registers** — a shared register file (the "system
  registers") visible to both SH-2s and to the Genesis 68000, carrying the comm
  ports, interrupt-control/clear bits, the 68000↔SH-2 handshake, the cartridge
  ROM bank window, and the framebuffer-access arbitration bits.
- **Genesis-bus integration** — the 32X intercepts the cartridge slot: it
  remaps `$000000` (a 32X security/vector ROM + the cartridge), exposes its
  system registers and a Genesis-side framebuffer/palette window in the
  `$A130xx` / `$A15xxx` region, and feeds its composited video back out.

## Decision

### SH-2 is a new tier-2 chip, not a manifest detail

A new chip lands at `src/chips/cpu/sh2` (`mnemos::chips::cpu::sh2`), an `icpu`
implementation built the same way the m68000 was: instruction-stepped
(`step_instruction()` returns the cycle cost; `tick(cycles)` catches up by
whole instructions), reaching memory through the abstract `ibus`
(`read8`/`write8`, 16/32-bit accesses assembled **big-endian** — the SH-2 on
the 32X runs big-endian), and exposing a `register_view` + `trace_target`
through `introspection_surface` exactly like `m68000`. The two CPUs on the 32X
are two *instances* of this one chip configured master/slave; the chip itself
carries no 32X knowledge.

Rationale: this mirrors how `m68000` serves both the Genesis main CPU and the
Sega CD sub-CPU. A CPU core is system-agnostic; the system wiring lives in the
manifest tier. It also keeps the SH-2 independently unit-testable (and
conformance-testable) before any 32X topology exists — the same affordance ADR
0004 calls out for wide CPUs and that the m68000 conformance harness already
uses.

### The on-chip SH-2 peripherals the 32X drives are part of the SH-2 chip

The free-running timer/WDT (PWM clock source), the DMA controller (framebuffer
fills, SH-2↔sound transfers), the serial/comm port, the cache, and the on-chip
interrupt controller are SH7604-internal and addressed through the SH-2's own
on-chip register region. They live with the SH-2 chip (configurable / trimmable
to the subset the 32X uses), not in the 32X manifest, because they are part of
the CPU package, not the 32X board.

### 32X board parts are manifest-tier subsystems

The 32X VDP, the PWM unit, and the shared system/comm/bank register file are
32X-board hardware, not reusable CPUs. They land under
`src/manifests/sega32x` (`mnemos::manifests::sega32x`), following the
`segacd_system` hand-wired-oracle pattern: chips/components as heap-pinned value
members, an `assemble_*` factory wiring a `topology::bus`, layered on the
existing Genesis (`M8` family). The 32X VDP that has a true framebuffer output
implements `ivideo`; its composite-with-Genesis step is a manifest-tier
concern.

### Genesis composition stays additive

The Genesis VDP and base bus are not modified. The 32X manifest composes the
two video outputs (Genesis VDP + 32X VDP, per-pixel priority) at the manifest
tier and presents the combined `ivideo` to the runtime. A plain Genesis machine
instantiates none of this and its existing byte-parity tests are untouched —
the same guarantee the Sega CD port documented.

### Player + family registration

`src/apps/player/adapters/sega32x/` provides the player adapter and registers a
`sega32x` family id, mirroring the existing `segacd` adapter. ROM routing
(`.32x` images, and the security/vector ROM) lives there.

## Deferred

- **Cycle-exact SH-2 pipeline and cache timing.** The first SH-2 increment is
  instruction-stepped with documented per-instruction cycle costs, like the
  m68000's initial phase. The 5-stage pipeline, cache-hit/miss timing, and
  bus-contention between the two SH-2s and the 68000 over the shared
  framebuffer are deferred to a later phase once functional correctness and a
  conformance corpus are in place.
- **SH-2 conformance corpus.** As with the m68000, any third-party
  SH-2 test corpus is never committed; the conformance test skips
  (CTest `SKIP_RETURN_CODE 4`) when its corpus directory env var is unset.
- **Exact 32X↔Genesis video-composition priority edge cases** (the
  Genesis-priority vs 32X-priority per-region bits) are refined against real
  software during phase C/D, not fixed up front here.

## Consequences

- Mnemos gains a reusable big-endian RISC CPU (`chips/cpu/sh2`) that is useful
  beyond the 32X.
- The 32X is built and reviewed in gated phases (see
  `docs/plans/2026-06-09-sega-32x-port.md`), not as one drop.
- 100% behavioural parity cannot be *validated* in this repository without 32X
  BIOS/security ROMs and commercial ROM images; the boot/parity tests skip in
  their absence, exactly as the Sega CD and Genesis parity suites already do.
  Parity is a claim that requires those artifacts at validation time; this ADR
  fixes the structure that makes reaching it possible, not the validation
  itself.

## Note on ADR-0006

The Sega CD `NOTES.md` and this work refer to **ADR-0006
(emu-reuse-and-conformance)**, which is listed in `docs/adr/README.md` but whose
file is not present in the repository. The Emu-reuse-and-conformance *policy*
(port from the Emu reference, gate behind conformance/parity tests) is the
governing policy for both the Sega CD and this 32X effort; the missing file
should be restored or rewritten so the policy this ADR depends on is actually
recorded.

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session. The subsystem
was already implemented while this record sat unratified — the drift case
cited in ADR-0013; acceptance records reality.
