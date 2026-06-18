---
id: ADR-0029
title: "Game Capability Contracts: Paged Media, Watches, Sessions, and Achievements"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-17
ratified: null
---

# ADR 0029: Game Capability Contracts: Paged Media, Watches, Sessions, and Achievements

## Context

Mnemos is adding richer dump and introspection surfaces, and the same product
shape will be needed for future user-facing capabilities:

- loading only the ROM pages needed during play for very large games and
  memory-constrained devices;
- high-score capture;
- network play for games that already support multiple local players; and
- achievements built from game-state conditions.

These features are tempting to implement as player shortcuts because they are
visible in the frontend. That would violate the core thesis. The runtime must
remain deterministic and headless; frontends and plugins must consume declared
capabilities instead of reaching through a loaded system's private state.

This ADR does not authorize implementation of all four features immediately. It
records the contract boundaries each future slice must respect.

## Decision

Define four additive capability contracts. Each is discoverable at runtime so a
frontend can adapt its UI to the loaded system and game without hard-coded
system branches.

### 1. Media paging contract

Large media may be represented as paged, content-addressed regions instead of a
single fully resident byte vector. The emulated bus still observes ordinary ROM
semantics: reads return the byte at the mapped address, writes are ignored or
handled by the mapped hardware, and page cache policy has no emulated timing
effect.

Rules:

1. The manifest declares paged regions explicitly: logical address range,
   backing media identity, page size, per-page hashes, byte order/interleave, and
   any bank or mapper relationship.
2. A page provider may stream from disk, package files, network storage, or a
   host cache, but a page miss is a host availability event, not an emulated bus
   value. The runtime may block host wall-clock before executing the affected
   frame or surface a recoverable host error; it must not inject nondeterministic
   data into emulated memory.
3. Deterministic replay is keyed by manifest revision plus full-media identity,
   not by the current cache contents. Save states record the media identity and
   mapped paging metadata; cached page bytes are an optimization and are not
   required for correctness when the provider can rehydrate them by hash.
4. Prefetch and eviction policy live below the emulation contract. A frontend may
   choose a small cache for low-memory devices, but the chosen cache size must not
   change emulated output for the same input log.

Non-goal: this is not "partial ROM support" for unknown or damaged dumps. A game
may run with a sparse local cache, but the full logical media identity remains
known and validated.

### 2. Game-state watch contract

High scores, counters, flags, and game-specific milestones are expressed as
typed watches over the existing instrumentation surface.

Rules:

1. A watch names one or more stable observations: memory view bytes, register
   fields, frame events, audio/video events, or future symbolized game-state
   values. It does not reach into concrete adapter internals.
2. Value decoders are explicit: binary or BCD, endian, signedness, scale, mask,
   latch/reset semantics, and sampling point. The default sampling point is a
   completed video frame so capture is deterministic and frontend-independent.
3. High-score capture is a specialization of watches. It records score value,
   player/slot when known, source locations, validation predicates, and reset
   conditions.
4. Watches may be system-level or game-level. System-level watches come from the
   adapter/manifests; game-level watches come from game manifests or external
   plugin packs with their own provenance and versioning.

Non-goal: Mnemos does not infer score addresses by heuristic scanning at runtime
as part of the core contract. Tooling may help author watches, but committed or
loaded rules must be explicit.

### 3. Session input contract

Network multiplayer for existing local-multiplayer games is modeled as
frame-indexed controller input, not as system-specific online code in the
emulator core.

Rules:

1. The runtime consumes a deterministic input log: frame number, port, controller
   state, and optional device type. Local and remote players produce the same log
   shape.
2. The first supported network mode is lockstep/input-delay. Rollback is a later
   extension that depends on bounded save-state/restore cost and deterministic
   replay windows.
3. Transport, matchmaking, NAT traversal, lobbies, encryption, voice chat, and
   account identity live in apps/plugins outside the runtime. They deliver
   validated input packets to the session contract.
4. Native emulated networking hardware remains a separate peripheral/chip
   problem. This ADR covers remote control of local-player ports, not emulating a
   modem, link cable, or LAN adapter.

### 4. Achievement contract

Achievements reuse the game-state watch contract with an unlock lifecycle.

Rules:

1. An achievement is a named condition graph over watches and events, evaluated
   at deterministic sample points.
2. The evaluator reports unlocked/locked/progress states to the frontend. User
   profiles, cloud sync, platform APIs, and social presentation live outside the
   runtime.
3. Achievement packs carry game identity constraints: media hash, manifest id,
   region/revision, and any required adapter capability version.
4. The same condition engine must support high-score capture and achievements so
   Mnemos does not grow two incompatible rule languages.

### 5. Capability discovery

Expose a high-level discovery surface that reports:

- media capabilities: fully resident, paged, required provider, page size, cache
  hints, and validation status;
- watch capabilities: available memory/register/event sources, built-in watches,
  high-score definitions, and achievement packs;
- session capabilities: controller ports, deterministic input format, supported
  local/remote modes, and rollback readiness; and
- limitations: missing full-media hashes, unavailable page provider, no save-state
  support, no game manifest, or unsupported timing mode.

The frontend uses this surface to enable, hide, or explain UI controls. It must
not guess from system family names.

## Consequences

- Low-memory large-game support has a deterministic contract: page streaming is
  host-side availability, never emulated timing.
- High scores and achievements share one watch/condition model and can ship as
  game-level data instead of adapter-specific code.
- Network play starts with a realistic deterministic input-log model and leaves
  rollback until save-state costs are measured.
- Plugins can add providers, watch packs, achievement packs, and session
  transports without bloating the emulator core.
- Future implementation slices should be narrow: first define the discovery
  schema, then add one capability at a time with hermetic tests.

## Alternatives considered

- **Implement each feature directly in `mnemos_player`.** Rejected: it would
  duplicate system knowledge in the frontend and bypass instrumentation.
- **Treat partial ROM loading as transparent file I/O inside bus reads.**
  Rejected: cache misses in the hot bus path would make timing and failures
  host-dependent.
- **Use separate rule engines for high scores and achievements.** Rejected:
  both are deterministic predicates over the same game-state observations.
- **Build rollback networking first.** Rejected: rollback needs measured
  save-state cost and replay determinism across the target systems; lockstep
  validates the session input contract with less risk.

## Relationship to existing modules

This proposal refines `ARCH-001` (layering), `ARCH-004` (determinism), and
ADR-0004 (chip/introspection direction) for future feature work. It does not
modify the accepted runtime contract by itself. Ratification should be followed
by a concrete schema proposal for capability discovery before product code lands.
