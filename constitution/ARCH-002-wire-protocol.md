---
id: ARCH-002
title: "Wire Protocol Contract"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
---

# ARCH-002 — Wire Protocol Contract

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
sections 12.2, 12.3 and 13.2, per `constitution/MIGRATION.md`.

**Factual status (2026-06-10):** no `.capnp` schema has been committed yet;
`src/debug/wire/` is the designated home (the TDS named
`src/instrumentation/wire/`; the debugger/wire tooling moved to `src/debug/`
during the ADR-0009 reorganization). This module is the contract any released
schema MUST satisfy. The schema-compatibility verifier
(`tools/schema_compat.py`, gate G8) lands in pilot phase P2 alongside the
first schemas.

## Transport and clients (TDS §12.2)

External clients (Python tooling, the developer frontend if run
out-of-process, CI agents) speak the same instrumentation surface via
Cap'n Proto over a stable transport:

- Default transport: local domain socket on Linux, named pipe on Windows.
- TCP transport for remote sessions (deferred to v0.2).
- All messages versioned by schema revision; backward compatibility maintained
  within a major version.
- Schemas generate C++ and Python bindings at build time.

## Evolution rules (TDS §12.3)

- The wire protocol uses semver. A breaking change requires a major bump and a
  deprecation window of one minor version.
- Released Cap'n Proto schemas evolve **append-only**: field renumbering, type
  changes, or removals on a released schema are breaking changes (gate G8
  fails them once live).
- Generated client bindings include a version-negotiation handshake.

## Python surface (TDS §13.2)

- Python clients consume the wire protocol exclusively; Python is never
  embedded in a Mnemos process.
- A `mnemos-py` package, versioned with the wire schema, provides idiomatic
  bindings.
