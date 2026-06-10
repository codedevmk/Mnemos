---
id: ADR-0013
title: "Adopt MNE-CTX-PLAN-001 (Context-Engineering Pilot)"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0013: Adopt MNE-CTX-PLAN-001 (Context-Engineering Pilot)

## Context

`constitution/MNE-CTX-PLAN-001.md` proposes piloting the context-engineering
framework (authority hierarchy, executable truth, deterministic derived views,
entropy pump) on this repository. Its P0 phase — constitution lift, ADR
lifecycle scaffold, gates G1/G7/G9, week-0 baselines — was executed in this
change set. Per the plan's own intervention model, agents propose and humans
ratify, so this ADR lands as `proposed`; ratifying it (H1) constitutes
acceptance of the plan and of decisions D1–D8 below.

The pilot's motivating failure modes are already observable in this
repository:

- `docs/adr/README.md` and `src/disc/NOTES.md` reference
  `0006-emu-reuse-and-conformance.md`, which was never committed — a dead
  authority reference that gate G3 (P1) will detect mechanically.
- ADR-0011 (Sega 32X) has status Proposed while the 32X subsystem is fully
  implemented — an unratified decision that silently became load-bearing,
  exactly what the expiry rule and weekly packet exist to surface.
- The TDS prescribes a Vulkan-based renderer abstraction while the tree ships
  SDL3 window/GPU — a candidate intent-vs-fact axis conflict, flagged in
  ARCH-003 for adjudication rather than silently inherited.

## Decision

Adopt MNE-CTX-PLAN-001 with the §16 recommendations as proposed:

- **D1** pilot subsystems: `src/chips/cpu/z80` + SMS machine
- **D2** capsules co-located with their subsystem
- **D3** token budgets 8k (index) / 12k (module) / 6k (capsule) / 24k (pack)
- **D4** provenance trailers on `constitution/` + schema commits only
- **D5** proposal expiry window 14 days
- **D6** symbol extraction via libclang, grammar-parser fallback
- **D7** CI provider GitHub Actions (confirmed: `.github/workflows/ci.yml`)
- **D8** token telemetry via Claude Code OpenTelemetry export

### Amendments — plan text vs. repository reality

The plan's §3 layout was written against an idealized tree. The lift adapts
paths to existing conventions instead of churning the tree; the plan body is
kept verbatim and these amendments govern:

- **A1 — `src/`, not `source/`.** Product code stays under `src/` (TDS §5,
  AGENTS.md). The `public/`/`private/` split named in §3 does not exist
  either; modules are flat per ADR-0009. The capsule extraction boundary (P1)
  is therefore the module's headers, not a `public/` directory.
- **A2 — `docs/adr/`, not root `adr/`.** The lifecycle directories
  (`proposed/`, `accepted/`, `superseded/`, generated `INDEX.md`) live under
  the existing `docs/adr/`. Existing ADRs 0001–0012 were moved into lifecycle
  directories with machine-lintable front matter prepended; bodies untouched.
- **A3 — adoption ADR is 0013.** The plan says "ADR-0001" by construction;
  this repository already has ADRs 0001–0012. Numbering continues; 0006 stays
  reserved (referenced but never committed).
- **A4 — wire schemas.** No `.capnp` files exist yet; their designated home is
  `src/debug/wire/`, not a root `schemas/` tree. G8 (P2) targets that path.
- **A5 — G7 enforces tree-level licensing.** The repository declares licenses
  via `LICENSE`/`LICENSE-chips` (ADR-0003), not per-file headers, so the gate
  checks the tree declarations, the copyleft denylist, and FetchContent
  enumeration in `THIRD_PARTY_NOTICES.md`.
- **A6 — governance tools live in `tools/governance/`** to keep them apart
  from the C++ tooling targets in `tools/`.

## Consequences

- `CONSTITUTION.md` + `constitution/` modules exist with `status: proposed`;
  H2 ratification flips them to accepted (G4 invariant coverage applies from
  P2 only to accepted documents).
- Gates G1 (naming validator added to the existing format/-Werror suite), G7,
  and G9 (+advisory G11) run blocking in CI via the `governance` job.
- Week-0 baselines: repository-derivable values are captured in
  `metrics/snapshots/week0.json`; M6/M7/M15 require instrumented agent
  sessions and are recorded as `pending` with the capture procedure in
  `metrics/README.md` — no synthetic baselines, per the plan's epistemic rule.
- P1 (capsule machinery) starts on ratification of this ADR.

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session, together with
the constitution modules (H1/H2). P1 starts with this acceptance.
