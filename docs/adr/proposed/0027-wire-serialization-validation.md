---
id: ADR-0027
title: "Wire-Protocol Serialization: Validate Before Committing to Cap'n Proto"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-13
ratified: null
---

# ADR 0027: Wire-Protocol Serialization — Validate Before Committing to Cap'n Proto

## Context

`ARCH-002` (Wire Protocol Contract) names **Cap'n Proto** as the serialization,
"lifted verbatim" from TDS §12.2. That choice was frozen before any wire code
existed: there is no `.capnp` schema, no transport, and no client (the entire
`src/debug/wire/` tree is a README). The instrumentation review found the
differentiating "single API, multiple surfaces" thesis has, today, exactly one
in-process consumer and zero external clients, and flagged that the serialization
library was fixed by name before a prototype validated it (review C4).

Three facts make this worth a deliberate decision rather than inheriting the
default:

1. **Cap'n Proto is a non-trivial build dependency** (`capnp` compiler + `kj`
   runtime, plus the Python plugin for bindings) entering a tier — instrumentation
   / `src/debug` — that currently has none. Every contributor and CI build would
   pay for it.
2. **The requirements `ARCH-002` actually states are met by several options.**
   The contract demands: versioned messages, append-only schema evolution,
   C++ **and** Python binding generation, a version-negotiation handshake, and
   framing over a local socket / named pipe. Cap'n Proto, FlatBuffers,
   length-prefixed Protobuf, and a gdb-remote-style hand-rolled framing all
   satisfy those; they differ on cost axes, not on whether they can meet the
   contract.
3. **The demanding axis is the per-instruction trace-event stream**, not the
   control channel. Breakpoints, stepping, and state queries are low-rate
   request/response where any option is adequate. The `trace_target` /
   `reg_write_trace` taps fire per instruction / per register write — the hottest
   paths in the emulator — so events/sec and per-event copy cost dominate there.
   This is exactly where Cap'n Proto's zero-copy design genuinely wins, and
   exactly the case that should be measured before the schema freezes.

## Decision

Treat `ARCH-002`'s "Cap'n Proto" as a **recommendation to validate, not a settled
fact**, and gate the serialization choice on a measurement:

1. **Split the surface into two channels** and let them choose independently:
   - **Control channel** (breakpoints, stepping, state queries): request/response,
     low rate. Any of the candidates is acceptable; pick for build-cost simplicity.
   - **Event channel** (the trace/`reg_write` stream): high rate, copy-sensitive.
     The serialization is chosen here on measured throughput.

2. **Prototype the event hot path before committing.** Build a throwaway that
   serializes a representative trace-event stream (e.g. a Genesis or SH-2 run) and
   measure: events/sec, per-event allocation/copy cost, and encoded size, for
   Cap'n Proto vs at least one alternative (FlatBuffers or length-prefixed
   Protobuf). The control channel does not need this; it is decided by elimination.

3. **Decide against fixed criteria**, in priority order: (a) event-channel
   throughput and copy cost; (b) C++ **and** Python codegen quality (the Python
   client is the thesis's payoff); (c) append-only schema evolution that satisfies
   `ARCH-002`'s evolution rules and the future G8 verifier; (d) build-dependency
   weight on the instrumentation tier; (e) handshake / versioning ergonomics.

4. **If Cap'n Proto is chosen, vendor it the way `zstd` is vendored** — pinned
   `FetchContent` (commit SHA, per ADR-0027's sibling D4 change) — and **gate the
   Python-binding codegen behind a CMake feature flag** so non-Python builds (CI
   core, headless) do not pay for the Python toolchain.

5. **Amend `ARCH-002`** from "clients speak the surface via Cap'n Proto" to
   "clients speak the surface over the contract below; the serialization is chosen
   per ADR-0027 and recorded here once measured." The transport (domain socket /
   named pipe), versioning, append-only evolution, and handshake requirements are
   unchanged — only the named library becomes a recorded outcome rather than a
   premature mandate.

This ADR does **not** itself pick the serialization; it removes the premature
commitment and defines how the pick is made. The measured outcome is recorded as
a follow-up amendment to this ADR (and to `ARCH-002`) before the first schema is
committed.

## Consequences

- The wire-protocol implementation (review C1) gains a short, bounded validation
  step before its schema freezes — cheap now, versus a breaking schema/toolchain
  change after a `mnemos-py` client exists.
- The instrumentation tier does not inherit a build dependency by default; if it
  takes one, that is a measured decision with a feature-flagged cost.
- `ARCH-002` stops over-specifying an unvalidated implementation detail while
  keeping every behavioural guarantee it actually makes.
- A small risk: the prototype could confirm Cap'n Proto and make this look like
  ceremony. That is an acceptable price for not freezing a toolchain dependency on
  an unmeasured assumption, and the split-channel framing is useful regardless.

## Alternatives considered

- **Keep Cap'n Proto as mandated, build it now.** Rejected: commits the toolchain
  dependency and the schema shape before a single measurement or client exists —
  the exact premature-decision pattern this ADR exists to correct.
- **Hand-rolled binary framing only (gdb-remote style).** Viable for the control
  channel and minimal-dependency, but forgoes schema-driven codegen and the
  append-only evolution tooling `ARCH-002` requires for the Python surface;
  reasonable as the control-channel choice, not as the whole answer.
- **Decide purely from documentation, no prototype.** Rejected: the event hot path
  is precisely where the candidates diverge, and that divergence is empirical.

## Relationship to existing modules

Amends `ARCH-002` (Wire Protocol Contract) on the serialization point only;
ratification flips that module's named-library mandate to a recorded outcome.
Sequenced before review item C1 (build the wire protocol) and informs C2 (the APM
tracer's vocabulary should converge with whatever the wire schema describes).
