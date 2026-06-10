---
id: STD-001
title: "Naming Conventions"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
invariants:
  - id: INV-STD-001
    statement: "C++ source and header basenames are snake_case."
    verified_by: [tools/governance/naming_validator.py]
  - id: INV-STD-002
    statement: "Header basenames are globally unique across the repository."
    verified_by: [tools/governance/naming_validator.py]
---

# STD-001 — Naming Conventions

Lifted from `docs/architecture/mnemos-architecture-tds-v0.1.md` section 5.1
and `docs/architecture/mnemos-project-plan-v0.1.md` section 4.4, per
`constitution/MIGRATION.md`.

## Identifiers

- snake_case naming throughout, in the `mnemos` namespace, with tier
  sub-namespaces (`mnemos::chips::cpu`, `mnemos::runtime`, ...).
- Interface types carry an `i` prefix on the type name (`ichip`, `ibus`,
  `icpu`), per the chip contract (ADR-0004).

## Files

- Source and header basenames are snake_case (`[a-z0-9_]+` plus extension).
- Headers are included by **basename in quotes** (`#include "ibus.hpp"`), so
  every header filename MUST be globally unique across the repository. This is
  why the bus *interface* is `ibus.hpp` while the topology *implementation* is
  `bus.hpp` (TDS §5.1).

## Targets and chip IDs

- Public CMake targets are exported as `mnemos::<tier>`; per-chip targets as
  `mnemos::chips::<category>::<part>` (TDS §6.1).
- Canonical chip IDs follow `vendor.part[.variant]` (`"mos.6510"`,
  `"yamaha.ym2612"`, `"mos.vic_ii.6569"`), enforced by the chip factory
  registry (project plan §4.4, ADR-0004).

## Documents

- Constitution modules: `ARCH-NNN-<slug>.md`, `STD-NNN-<slug>.md`.
- ADRs: `NNNN-<slug>.md` under `docs/adr/<lifecycle>/`, numbered
  chronologically; numbers are never reused (0006 stays reserved).
