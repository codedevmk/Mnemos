---
id: ADR-0001
title: "Monorepo Layout"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-22
---

# ADR 0001: Monorepo Layout

**Status:** Accepted for M0 scaffold
**Date:** 2026-05-22

## Context

Mnemos needs a clean standalone repository that supports reusable chip libraries,
a deterministic runtime, instrumentation, and multiple frontends without coupling
to any prior or sibling project's internals.

## Decision

Use one monorepo with product code under `src/`. Tier directories under `src/`
match the architecture TDS: `foundation`, `chips`, `topology`, `manifests`,
`runtime`, `instrumentation`, `frontend_sdk`, and `apps`.

Each tier has a CMake target and may depend only on lower-numbered tiers.
Supporting project surfaces live in `cmake`, `docs`, `extern`, `tests`, and
`tools` at the repository root.

## Consequences

- Code ownership and dependency direction are visible from the repository root.
- CI can validate the CMake graph before meaningful emulation code exists.
- Future source moves should preserve tier boundaries or add a new ADR.
