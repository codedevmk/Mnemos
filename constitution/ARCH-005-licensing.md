---
id: ARCH-005
title: "Licensing Boundaries"
status: accepted
version: 1.0.1
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
invariants:
  - id: INV-LIC-001
    statement: "No code or build file in src/, tests/, tools/, cmake/, or extern/ declares a copyleft (GPL/LGPL/AGPL) license."
    verified_by: [tools/governance/license_audit.py]
  - id: INV-LIC-002
    statement: "Every FetchContent dependency is enumerated in THIRD_PARTY_NOTICES.md."
    verified_by: [tools/governance/license_audit.py]
---

# ARCH-005 — Licensing Boundaries

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
section 7.4, per `constitution/MIGRATION.md`. ADR-0003 (license split) is the
ratified refinement and remains accepted authority.

## License split (TDS §7.4, ADR-0003)

- **Apache-2.0** for `foundation`, `topology`, `manifests`, `runtime`,
  `instrumentation`, `frontend_sdk`, `apps`, `tools`, and repository
  infrastructure (`LICENSE`).
- **MIT** for the chip library, `src/chips/` (`LICENSE-chips`).
- Licenses are declared at tree level by the two LICENSE files, not by
  per-file headers (existing ADR-0003 convention; gate G7 enforces the tree
  declarations and the denylist below).

## Copyleft denylist

- No GPL code may enter Apache- or MIT-licensed tiers. Any plugin that links
  GPL code MUST live under a clearly demarcated subdirectory and ship under
  GPL accordingly.
- No third-party emulator source is vendored in this repository. Open-source
  emulator projects are behavioral references only, with provenance
  acknowledged in `THIRD-PARTY-REFERENCES.md` (and they are L5 authority — advisory,
  never an oracle; see `CONSTITUTION.md` standing rules).

## Dependency policy

- Third-party dependencies enter only through pinned CMake `FetchContent`
  entries and MUST be enumerated in `THIRD_PARTY_NOTICES.md` with version,
  license, and consumer.
- Adding a third-party library requires an ADR covering need, license,
  isolation, and maintenance risk.
- ROMs, firmware dumps, build outputs, and generated logs are never committed.
- License-impacting changes require a new ADR.
