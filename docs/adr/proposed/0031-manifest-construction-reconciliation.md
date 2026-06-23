---
id: ADR-0031
title: "Manifest Construction Contract Reconciliation"
status: proposed
version: 0.1.0
supersedes: []
superseded_by: null
proposed: 2026-06-23
---

# ADR 0031: Manifest Construction Contract Reconciliation

## Context

ADR-0004 says the manifest layer instantiates chips by canonical factory ID and
never includes chip headers directly. The current source tree has since grown
typed per-system assemblers (`assemble_genesis`, `assemble_sms`, `assemble_nes`,
and others) that include concrete chip headers and wire buses, callbacks,
controllers, storage, and expansion devices directly.

Both paths now exist:

- `src/manifests/common/builder.*` provides factory-driven manifest construction.
- Per-system assemblers provide the compatibility path exercised by adapters and
  oracle-style tests.

This is an intent/source reconciliation issue. Rewriting every assembler into
the generic builder in one step would be high-risk because those assemblers are
where many cycle-sensitive and board-specific behaviors currently live.

## Decision

Until this proposal is ratified, do not silently reinterpret ADR-0004. Treat the
current typed assemblers as transitional source authority for existing systems
and keep them behavior-stable.

If ratified:

1. New systems should enter through factory-driven manifest construction unless a
   proposed ADR justifies a typed assembler.
2. Existing typed assemblers may remain while they are covered by parity tests
   against `manifests/common/builder`.
3. Direct chip-header includes in manifest modules are migration targets, not
   precedent for new modules.
4. Each migrated system should keep its public `assemble_*` facade until callers
   can move to the generic manifest path.
5. Builder gaps discovered during migration should be fixed in
   `src/manifests/common/`, not by adding new direct chip coupling.

## Migration Plan

1. Add an inventory test that lists manifest modules with direct chip-header
   includes and classifies them as existing transitional debt or new violations.
2. Migrate one low-complexity system first and keep its existing integration
   tests as the behavior gate.
3. For systems with board-specific peripherals or expansion chips, add builder
   extension points before removing assembler-local wiring.
4. Remove each direct chip include only after parity tests prove the generic
   builder path produces the same chip graph, memory map, and first-frame state.

## Consequences

- The project stops accumulating new direct manifest-to-chip coupling.
- Existing compatibility work remains stable while the generic builder matures.
- Reviewers get an explicit rule for distinguishing legacy assembler debt from
  new architecture drift.
