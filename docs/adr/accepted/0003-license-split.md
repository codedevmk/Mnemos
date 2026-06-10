---
id: ADR-0003
title: "License Split"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-22
---

# ADR 0003: License Split

**Status:** Accepted for M0 scaffold
**Date:** 2026-05-22

## Context

Mnemos separates reusable chip implementations from the application/runtime
layers. The architecture TDS requires Apache-2.0 for most tiers and MIT for the
chip library.

## Decision

Use Apache-2.0 for `foundation`, `topology`, `manifests`, `runtime`,
`instrumentation`, `frontend_sdk`, `apps`, `tools`, and repository
infrastructure. Use MIT for `chips`.

Third-party dependencies must be listed in `THIRD_PARTY_NOTICES.md` and pinned
in CMake or an equivalent reproducible mechanism.

## Consequences

- Chip code remains permissive and easy to reuse independently.
- GPL emulator source cannot enter Apache- or MIT-licensed tiers.
- License-impacting changes require a new ADR.
