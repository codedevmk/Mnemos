---
id: ADR-0022
title: "Debugger Surface Uses 64-Bit Addresses"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: null
---

# ADR 0022: Debugger Surface Uses 64-Bit Addresses

## Context

`breakpoint_spec::address`, `event::pc`, `stop_event::pc`, and the
`cpu_probe` were `std::uint16_t`, while the tree already runs a 68000
(24-bit PC) and SH-2s (32-bit PC). A 68000 breakpoint at `$FF0100` was
unrepresentable and probes aliased addresses mod 64 KiB. The M7 wire
protocol and the scripting layers will freeze on these types.

## Decision

Options considered: (a) per-CPU templated address types; (b) `uint32_t`;
(c) a single `debug::debug_address = std::uint64_t` alias used across the
debugger surface.

Chosen: (c) — one width covers every CPU in the tree and any plausible
future system, costs nothing at debugger call rates, and avoids
re-breaking the wire schema if a 64-bit guest ever appears. Done now,
while the only consumers are in-tree tests (commit b4292e0).

## Consequences

- The wire protocol / scripting schema can freeze on `u64` addresses.
- CPU probe wirings return the natural PC width and widen implicitly.
- Narrowing an address anywhere in the debug path is a review flag.

Session: https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF
(commit b4292e0, merged via e89e460).
