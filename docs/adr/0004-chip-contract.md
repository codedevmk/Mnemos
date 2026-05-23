# ADR 0004: Chip Contract

**Status:** Accepted for M1 chip contract
**Date:** 2026-05-22

## Context

M1 introduces the chip tier (tier 2) and its first concrete chip, the MOS 6510.
Before a CPU can be implemented, the contract every chip implements must be
fixed: how chips are classified, how they advance time, how they reach memory,
how they are constructed by the manifest layer, and how they expose state. These
decisions are referenced by TDS §8 and are recorded here per the M1 acceptance
criterion.

## Decision

### Taxonomy and base interface

- `chip_class` enumerates the seven chip categories (cpu, audio_synth, video,
  bus_controller, storage, mapper, peripheral).
- All chips implement `i_chip` (metadata, `tick`, `reset`, save/load state,
  introspection accessor), per TDS §8.2.
- Per-class interfaces (`i_cpu`, `i_audio_synth`, …) inherit `i_chip` and each
  carry a `static_class` tag for compile-time class identification.

### Clock contract

- `tick(cycles)` advances a chip by exactly `cycles` of its own clock domain.
- Chips never tick each other; all inter-chip interaction goes through the bus
  (or, later, scheduler-mediated events). Master-to-chip clock division is the
  scheduler's responsibility (tier 5).

### Bus access and dependency direction

- Chips read and write memory through an abstract `i_bus` (`read8`/`write8`).
- `i_bus` is declared in tier 2 (`mnemos::chips::common`), not in tier 3
  (topology), even though the concrete bus lives in topology. This is deliberate
  dependency inversion: a CPU depends on the `i_bus` abstraction, while topology
  provides the implementation. It preserves the strict
  `foundation -> chips -> topology` direction; placing `i_bus` in topology would
  force tier 2 to depend on tier 3.
- A CPU receives its bus via `i_cpu::attach_bus(i_bus&)`, called once at attach
  time before the first `tick`. The CPU observes but does not own the bus, per
  TDS §8.4 ("Chips attach to buses; they do not own them").

### Construction and registration

- Chips are constructed with no constructor arguments and configured afterward
  (e.g. `attach_bus`), so the factory signature stays uniform.
- Each chip registers a factory at static-init time, keyed by canonical chip ID
  (`"mos.6510"`). The manifest layer (tier 4) instantiates chips by ID and never
  includes chip headers directly (TDS §8.6).

### Introspection

- `instrumentation::i_chip_introspection` remains a minimal forward-looking
  interface in M1. Its full surface (register snapshots, owned memory regions,
  event taps, cycle counter) is defined with the instrumentation tier in M4
  (TDS §8.5).

## Deferred

- **16-bit and 32-bit bus accessors.** TDS §8.4 sketches `read16`/`write16`
  width specializations for wide CPUs (68000, SH-2). They are intentionally left
  out of `i_bus` for now: the 6502 family is cycle-accurate and must perform
  byte-by-byte accesses anyway, so it has no use for them, and a default 16/32
  accessor cannot pick a byte order without a concrete bus-endianness contract.
  These accessors are added when the first wide-bus CPU lands (M8), alongside the
  endianness semantics they require.

## Consequences

- Tier 2 owns the chip-facing memory abstraction; tier 3 implements it. No tier
  inversion is introduced.
- The 6510 (and later CPUs) can be built and unit-tested against a mock `i_bus`
  before topology exists.
- When wide CPUs arrive, extending `i_bus` with width accessors is an additive
  change that does not disturb existing 8-bit chips.
