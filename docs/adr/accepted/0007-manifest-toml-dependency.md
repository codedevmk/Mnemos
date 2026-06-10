---
id: ADR-0007
title: "Manifest TOML Dependency (tomlplusplus)"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-25
---

# ADR 0007: Manifest TOML Dependency (tomlplusplus)

**Status:** Accepted for M3 manifest loader
**Date:** 2026-05-25

## Context

M3 introduces the system manifest: a TOML file (TDS §10) that declares a machine's
clock, chips, buses, and memory regions. The manifest loader (tier 4,
`mnemos::manifests::common`) must parse TOML with strict validation and surface
errors with file/line/column. TDS §6.5 already names `tomlplusplus` as the v0.1
TOML dependency; this ADR records adopting it per the dependency policy.

## Decision

Add **`tomlplusplus`** (toml++) via pinned `FetchContent` (`v3.4.0`), consumed by
the manifest tier.

- **Need:** robust TOML 1.0 parsing with source-position diagnostics (line/column)
  for the strict validation TDS §10.3 requires. Hand-rolling a TOML parser is
  out of scope and error-prone.
- **License:** MIT — compatible with both the Apache-2.0 core and MIT chip tiers.
- **Isolation:** header-only; linked only by `mnemos::manifests::common` (tier 4)
  and its tests, never by chips, topology, or the runtime core.
- **Maintenance risk:** low — widely used, header-only, stable API, pinned tag.

## Consequences

- The manifest tier gains one header-only third-party dependency, pinned and
  isolated to tier 4.
- The loader returns structured diagnostics (message + source + line + column) so
  malformed manifests fail loudly at load time rather than mis-instantiating a
  machine.
- No product tier below manifests gains a TOML dependency.
