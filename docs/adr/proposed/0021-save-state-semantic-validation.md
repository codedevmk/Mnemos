---
id: ADR-0021
title: "Save-State Semantic Validation and Pre-Release Layout Evolution"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0021: Save-State Semantic Validation and Pre-Release Layout Evolution

## Context

ADR-0008's `state_reader` bounds-checks the chunk bytes, but chips trusted
the decoded *values*: the modem restored raw ring indices (wild writes), the
1541 restored an unclamped table index, and the REU silently kept its
current RAM when a blob sized for a different model arrived — a "successful"
load into an inconsistent machine, with no channel to report rejection.
Separately, completing the m68000 / Genesis VDP / Z80 / CIA chunks required
extending serialized layouts, raising the question of version gating.

## Decision

Three rules adopted (commit 4379816, a8f3037):

1. **Chips must validate decoded values** — clamp or reject anything that
   indexes a buffer, selects a table entry, or names an enumerator. The
   reader is the bounds defence; the chip is the semantics defence.
2. **`state_reader::fail()`** is the rejection channel: a chip that cannot
   apply a semantically valid-looking chunk poisons the reader so the
   caller's existing `ok()` gate rejects the restore (chosen over changing
   `load_state` to a failure-returning signature across every chip).
3. **While the format is pre-release (M0), layouts may be extended without
   version gates**, provided save and load change symmetrically in the same
   commit. States from older builds fail the `ok()` gate rather than load
   wrong. A version bump becomes mandatory the first time the format is
   declared an interchange surface. The loader also caps the zstd frame's
   self-declared content size (256 MiB), since the CRC covers only the
   compressed body.

## Consequences

- A corrupt or hostile state file can no longer produce out-of-bounds
  writes or a silently inconsistent machine; it fails the load.
- `load_state` implementations that assign reader values directly into
  index-like fields are a review flag.
- Old save states break across layout-extending commits until 1.0 — accepted
  while no compatibility promise exists.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commits 4379816, a8f3037, merged via e89e460).
