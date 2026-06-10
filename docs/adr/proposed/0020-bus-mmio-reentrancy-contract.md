---
id: ADR-0020
title: "Bus Access Contract: MMIO Re-entrancy and Composed Wide-Access Order"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0020: Bus Access Contract: MMIO Re-entrancy and Composed Wide-Access Order

## Context

An MMIO write handler may legitimately remap the bus it lives on — the 32X
ADEN write maps the G BIOS overlay from inside the `$A15100` handler. The
bus dereferenced the resolved region pointer after invoking the handler
(feeding it into the fast-path cache), and a remap can reallocate
`regions_`, which also *moves the executing `std::function` itself* (SBO
closures relocate). Separately, the `ibus` composed wide-access defaults
evaluated their two `read8` calls as operands of `|` — indeterminately
sequenced, so MMIO side-effect order varied by compiler. Empty mappings
underflowed `end` to `0xFFFFFFFF` and claimed the whole address space over
an empty span.

## Decision

Options considered for re-entrancy: (a) forbid remapping from handlers;
(b) re-resolve the region by index after the call; (c) generation counters;
(d) copy the handler before invoking it and never touch the resolved region
afterwards.

Chosen: (d) — it is the only option that survives the function object
itself being moved, and it costs one handler copy on the (non-fast-path)
MMIO route. The contract is now: handlers may remap their own bus; the bus
guarantees it holds no region state across a handler call. Composed wide
accesses (BE and LE) are explicitly sequenced low-level-first per the
documented order; empty/zero-size mappings are rejected at `map_*`.

## Consequences

- The retail 32X boot path is UAF-free; regression tests pin the behaviour
  (`bus_test.cpp`).
- MMIO byte-access order in composed reads is compiler-independent — part
  of the determinism contract; any new composed accessor must sequence
  explicitly.
- A zero-byte ROM now fails cleanly at map time instead of crashing in the
  first vector fetch.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commits 481ebab, 4379816, f32f978, merged via e89e460).
