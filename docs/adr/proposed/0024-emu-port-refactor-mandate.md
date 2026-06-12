---
id: ADR-0024
title: "Emu Port Refactor Mandate (amends ADR-0006)"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-11
ratified: null
---

# ADR 0024: Emu Port Refactor Mandate (amends ADR-0006)

## Context

ADR-0006 §1 already states that Emu ports are "restructured into Mnemos C++23
conventions ... never copied wholesale." The 2026-06-11 parity and tooling audits
(`docs/progress-analysis.md`, `docs/parity-gap-inventory.md`,
`docs/tooling-gap-inventory.md`) enumerate a large, concrete Emu→Mnemos migration
backlog — CHD codec, CPU disassemblers, whole-system save-state wiring, audio A/B
analysis, the C64 developer suite, and entire unported systems. With a sizable backlog
now named item-by-item, there is a real risk that "port from Emu" is misread as "lift
Emu's C." The owner directs (2026-06-11) that the refactor requirement be elevated from
a clause inside ADR-0006 to an explicit, unmissable mandate so the backlog cannot be
executed as transcription.

This ADR **amends — it does not supersede** — ADR-0006, sharpening its Decision §1.
ADR-0006 stays in force; on ratification it receives a version-bumped cross-reference to
this mandate.

## Decision

**Every artifact cannibalized, ported, or migrated from Emu into Mnemos MUST be
re-implemented to Mnemos's standards, or better — never transcribed.** "Port against a
proven reference" (ADR-0006 §2) means reuse Emu's *behaviour and hard-won correctness*,
re-expressed in Mnemos's architecture. The Emu source is a behavioural reference and
test oracle, not a code donor.

A port is conformant only when it satisfies all of:

1. **Architecture & tiering** — lands in the correct tier with strictly-downward
   dependencies (ARCH-001); one canonical module home, never buried under a consumer
   (ADR-0009).
2. **Contracts & patterns** — implements the chip / runtime contracts (ADR-0004), not
   Emu's callback-vtable shape; modern C++23 idioms (RAII, `std::span` / `std::optional`
   / `std::expected`, compile-time tables) replace raw pointers, manual memory, untyped
   buffers, and global mutable state.
3. **Standards** — STD-001 naming (snake_case, globally-unique header basenames, chip
   IDs) and STD-002 errors (`std::expected`, no exceptions / RTTI in core, `-Werror`).
   Emu's C names, macros, and conventions are not carried over.
4. **Organization & reusability** — shared behaviour goes to a shared module (DRY); a
   port that duplicates logic another module already owns is rejected. Observation and
   serialization use the existing `introspection_views` contract, not bespoke per-port
   hooks.
5. **Determinism & observability** — deterministic, headless core (ARCH-004); state,
   events, and timing exposed through the instrumentation surface (constitutional
   invariant), never engine internals.
6. **Conformance & provenance** — passes the public conformance corpus / oracle entry
   (ADR-0006 §3); records Emu provenance in the module `NOTES.md` (ADR-0006 §4).

**"Or better" is a duty, not a permission.** Where Emu's structure is weaker than
Mnemos's standards, the port improves it. Inheriting an Emu design weakness because
"that is how Emu did it" is a defect, not a port.

### In practice

- DO: read the Emu core for behaviour, timing, and edge cases; re-design the data flow
  onto Mnemos contracts; reuse existing Mnemos modules; cover with conformance / golden
  tests; cite provenance in `NOTES.md`.
- DO NOT: paste Emu C and adjust syntax; reproduce Emu's global state, raw-pointer
  buffers, macro tables, or callback-only seams; stand up a parallel utility that
  duplicates an existing Mnemos module; widen a tier dependency to make a lift compile.
- Review heuristic: **if a port's structure is recognizably Emu's rather than Mnemos's,
  it is not done.**

## Consequences

- The migration backlogs in the 2026-06-11 audits are scoped as *behavioural* targets;
  each item's implementation follows this mandate, and reviewers gain a concrete
  checklist to reject non-conforming ports.
- Slightly higher per-port effort than transcription, traded for architectural
  consistency, reusability, and determinism — the standing strategic posture (ADR-0006
  Context: "pure rewrite, no code lift without re-review").
- On ratification: move this file to `docs/adr/accepted/`, set `status: accepted` with a
  `ratified` date, regenerate `docs/adr/INDEX.md`, and bump ADR-0006 to version 1.1.0
  with a one-line cross-reference from its Decision §1 to this mandate.
