---
id: ADR-0030
title: "Capability Discovery Logical Schema"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-17
ratified: null
---

# ADR 0030: Capability Discovery Logical Schema

## Context

ADR-0029 proposes future game-capability contracts for paged media, game-state
watches, session input, and achievements. Those contracts need a common discovery
surface before any frontend or plugin can safely build UI around them.

This proposal deliberately defines a **logical schema**, not a serialization
format. ADR-0027 requires the wire serialization to be measured before Mnemos
commits to Cap'n Proto or an alternative. The shape below must therefore map to
an in-process C++ model first, and later to the chosen wire encoding without
semantic changes.

## Decision

Introduce one top-level discovery document, `capability_manifest`, produced for
the currently loaded system/game/session. The producer may be an adapter,
manifest builder, debug/instrumentation service, plugin host, or a composition of
those, but consumers see one merged document.

### Top-level shape

Logical fields:

- `schema`: integer schema version, starting at `1`.
- `producer`: stable component id and version that created the merged document.
- `system`: loaded system family id, manifest id/revision, display region, and
  game/media identity constraints when known.
- `capabilities`: ordered capability descriptors.
- `limits`: host-independent limits relevant to the current run.
- `diagnostics`: non-fatal reasons a capability is unavailable or degraded.

The document is a snapshot. It may be queried again after media swap, plugin load,
or game manifest load. Runtime-changing updates must be frame-tagged when they
affect deterministic evaluation.

### Capability descriptor

Every capability uses the same envelope:

```text
capability {
  id: stable string
  kind: media | watch | session | achievement | asset | audio | memory | debug
  provider: stable component id
  version: integer
  state: available | unavailable | degraded | experimental
  scope: system | game | media | session | host
  requires: list of dependency ids or host services
  diagnostics: list of diagnostic ids
  payload: kind-specific record
}
```

Rules:

1. `id` is stable within the provider and must not be derived from display text.
2. `kind` controls payload interpretation; unknown kinds are ignored unless a
   consumer explicitly requires them.
3. `state` is machine-actionable. User-facing explanation lives in diagnostics,
   not in ad-hoc string matching.
4. `scope` tells consumers whether a capability follows the system, the game,
   the media image, the multiplayer session, or only the host machine.
5. `version` is per-kind/provider and increments on breaking payload changes.

### Media capability payload

Fields:

- `mode`: `resident`, `paged`, or `streamed`.
- `logical_size`: full media size in bytes when known.
- `page_size`: page granularity when mode is `paged`.
- `hash`: full-media hash algorithm and digest when known.
- `page_hashes`: optional hash table identity for paged media.
- `regions`: logical ROM/media ranges exposed to the emulated system.
- `provider`: required page provider id when mode is `paged` or `streamed`.
- `cache_hints`: minimum useful pages, preferred prefetch window, and maximum
  deterministic lookahead.

Unavailable/degraded examples:

- full-media hash missing;
- provider unavailable;
- page hash table unavailable;
- media revision unsupported by the game manifest.

### Watch capability payload

Fields:

- `watch_id`: stable rule id.
- `sample_point`: `frame_end`, `scanline`, `instruction`, or named event.
- `sources`: memory views, register fields, trace events, or symbol ids.
- `decoder`: binary/BCD/text, endian, signedness, mask, shift, scale, and clamp.
- `predicate`: comparison or condition graph root.
- `result_type`: `counter`, `score`, `flag`, `timer`, `enum`, or `opaque`.
- `reset`: reset/latch policy.

High-score capture is a watch with `result_type = score` plus optional
`player_slot`, `ranking`, and `submit_policy` fields.

### Session capability payload

Fields:

- `input_format`: stable controller-state format id.
- `ports`: available input ports, device types, and player labels.
- `modes`: supported deterministic session modes: `local`, `lockstep`,
  `input_delay`, and later `rollback`.
- `frame_clock`: frame numbering origin and rate.
- `save_state`: whether bounded save/restore is available for rollback.
- `latency`: supported input-delay range in frames.

Transport details are excluded. Matchmaking, sockets, relays, and account
systems advertise host/plugin capabilities, not runtime capabilities.

### Achievement capability payload

Fields:

- `pack_id`: stable achievement pack id.
- `game_constraints`: media hash, manifest id, region/revision, and required
  adapter/capability versions.
- `rules`: list of achievement rule ids and condition graph roots.
- `progress`: optional progress counters mapped to watch ids.
- `persistence`: whether unlock state is frontend-local, host-profile, or
  external-service backed.

Achievements must reference watch/event ids instead of duplicating raw memory
addresses in a separate rule language.

### Diagnostics

Diagnostics are structured records:

```text
diagnostic {
  id: stable string
  severity: info | warning | error
  capability_id: optional stable id
  code: stable enum-like string
  detail: optional display string
}
```

Examples:

- `media.hash.missing`
- `media.provider.unavailable`
- `watch.source.missing`
- `session.save_state.unavailable`
- `achievement.game_hash_mismatch`

Consumers must branch on `code`, not `detail`.

### Evolution rules

1. Additive fields are allowed.
2. Unknown capability kinds, payload fields, and diagnostics are ignored unless a
   consumer declared them as required.
3. Existing enum values are never repurposed.
4. Breaking payload changes require a per-kind version bump and a compatibility
   adapter or a new capability id.
5. Wire serialization, once chosen, must preserve these semantics exactly.

## Consequences

- Frontends can ask one question, "what can this loaded thing do?", and render
  controls from capability state instead of system-family heuristics.
- Plugins can add providers, achievement packs, and session transports without
  changing the emulator core or frontend hard-coded lists.
- The schema leaves room for existing dump surfaces (`asset`, `audio`, `memory`,
  `debug`) so future UI can present screenshots, layers, audio, and memory dumps
  through the same discovery path.
- The proposal creates a narrow next implementation slice: an in-process
  `capability_manifest` model plus hermetic tests with fake providers, before any
  wire serialization is committed.

## Alternatives considered

- **Expose one boolean per frontend feature.** Rejected: it does not explain
  unavailable/degraded states and does not scale to plugin-provided packs.
- **Put capability fields directly on `player_system`.** Rejected: it would
  force frontend-facing policy into every adapter and make plugin merge logic
  hard to reason about.
- **Define the wire schema now.** Rejected: ADR-0027 intentionally defers the
  serialization choice until event-channel measurements exist.
- **Use free-form JSON from plugins.** Rejected: consumers need stable ids,
  states, diagnostics, and evolution rules to build dependable UI.

## Relationship to existing modules

Refines ADR-0029's capability-discovery section. It also aligns with ADR-0027 by
defining semantics before serialization. If ratified, the first code slice should
live at the instrumentation/debug boundary as an in-process logical model, with
frontend consumption added only after fake-provider tests prove merge and
diagnostic behavior.
