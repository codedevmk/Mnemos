---
id: ADR-0006
title: "Emu Reuse and Conformance (reconstructed)"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0006: Emu Reuse and Conformance (reconstructed)

> **Reconstruction.** The original ADR-0006 was listed in `docs/adr/README.md`
> and cited across the tree but its file was never committed. This text was
> reconstructed on 2026-06-10 (per ADR-0011's directive and ADR-0013's
> findings) from the policy's visible applications: `src/disc/NOTES.md`,
> `src/chips/audio/rf5c68/NOTES.md`, `src/chips/video/sega32x_vdp/NOTES.md`,
> `src/manifests/segacd/NOTES.md`, `THIRD-PARTY.md` §Emu, ADR-0011 ("Note on
> ADR-0006"), ADR-0012, and `docs/architecture/mnemos-todos-v0.1.md` (M1–M8
> entries). It records the policy as practiced; it does not invent new policy.
> If the original text resurfaces and differs, supersede this document.

## Context

The owner's earlier project **Emu** (a C multi-system emulator) predates
Mnemos and contains proven chip cores and utility code. Mnemos's strategic
posture is a pure rewrite with no code lift without re-review (project plan
§2). A policy is needed for how Emu — the owner's own code, so no third-party
license question — may be used, and how ported cores are kept honest.

## Decision (as practiced)

1. **Emu is a sanctioned port source.** Porting cores and utility code from
   the Emu reference is permitted. Ports are restructured into Mnemos C++23
   conventions (namespaces, `std::span`/`std::optional`, compile-time tables,
   the chip contract of ADR-0004) — never copied wholesale.
2. **No third-party emulator source is consulted or vendored.** Where Emu
   lacks something, implementation is clean-room from datasheets, standards
   (e.g. ECMA-130), and published hardware notes. "Use an existing proven
   core" means *port against a proven reference*, not link or transcribe one
   (restated by ADR-0012).
3. **Conformance gates the port.** A ported core is not done because it
   compiles; it must pass its public conformance corpus (per-cycle 6502
   corpus, ZEXALL/ZEXDOC, 68000 SingleStepTests, system parity suites).
   Corpora and ROMs are never committed; conformance tests are data-gated
   (env-var paths, CI skip code 4) and validated locally or on data-equipped
   runners.
4. **Provenance is recorded per module** in the module's `NOTES.md` and, for
   utility code, in `THIRD-PARTY.md`.

## Consequences

- Mature cores land quickly without compromising the no-GPL / no-vendored-
  emulator rules (ARCH-005).
- Conformance corpora act as the regression floor for ported behavior; the
  pilot's oracle registry (MNE-CTX-PLAN-001 §7.2, P2) formalizes exactly this
  with `ORC-LEGACY-*` entries.
- Every Emu-derived module is traceable via its NOTES provenance table.
