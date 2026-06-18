# Capability Discovery and Game Feature Contracts

Status: Draft, awaiting review
Date: 2026-06-17

Plan for implementing the future capability surface proposed by ADR-0029 and
ADR-0030: paged large media, high-score capture, network session input, and
achievements. This document is sequencing and implementation guidance only; it
implements nothing and does not ratify the proposed ADRs.

---

## Scope

Build the smallest durable substrate first:

1. an in-process `capability_manifest` model with merge and diagnostics tests;
2. discovery for existing dump surfaces (assets, audio, memory, debug layers);
3. watch definitions that can power high scores and achievements;
4. deterministic session input metadata for local/remote players;
5. paged-media metadata without streaming bytes through bus reads; and
6. optional plugin/provider hooks after the core model is stable.

Out of scope for the first wave: actual MP4/WebM codecs, matchmaking, rollback
netcode, heuristic score-address scanning, cloud profile sync, or a new wire
serialization dependency.

## Ground truth today

- ADR-0029 and ADR-0030 are proposed, not accepted. Product code should not make
  them load-bearing until human ratification.
- `src/instrumentation` is currently the observation contract README plus types
  declared in `chips/shared/introspection_views.hpp`; debug tooling consumes the
  surface from `src/debug`.
- `src/debug` already exports screenshots, memory/register sidecars, graphics
  assets, audio samples, register-write logs, animated GIFs, and movie frame
  sequences through `frontend_sdk::player_system`.
- `frontend_sdk::player_system` already exposes the current frame, chips,
  system-level memory views, media count/swap, input, audio, and system spec
  fields.
- ADR-0027 defers the wire serialization choice. Any discovery model must remain
  serialization-neutral until the wire-event prototype chooses a format.

---

## Architecture

### Logical model

Create a cold-path discovery model with value types:

```text
capability_manifest
  schema
  producer
  system_identity
  capabilities[]
  limits[]
  diagnostics[]

capability_descriptor
  id
  kind
  provider
  version
  state
  scope
  requires[]
  diagnostics[]
  payload
```

The first implementation should not use `std::variant` payloads if that makes
wire projection harder. Prefer a compact typed record per capability family plus
explicit builder helpers; all fields are cold path and allocation-friendly.

### Placement

Recommended first home: `src/debug/capability_discovery.*`.

Reasoning:

- discovery merges runtime/player state with debug/introspection surfaces;
- it consumes `frontend_sdk::player_system`, so it cannot live in chips,
  topology, manifests, runtime, or instrumentation without a tier inversion;
- it is a debug/tooling surface first, and frontends already link `mnemos::debug`;
- once the instrumentation boundary is clarified, the logical types can be
  promoted or mirrored without changing the contract.

Do not put the first version directly on `player_system`; adapters should expose
raw facts, not frontend policy.

### Merge rules

Multiple providers may contribute descriptors. The merger must be deterministic:

1. sort by `(kind, id, provider)`;
2. reject duplicate `(provider, id)` pairs as diagnostics;
3. let unavailable/degraded descriptors remain visible;
4. keep unknown kinds as opaque descriptors when possible;
5. never derive stable ids from display text.

---

# Phase C0 - Ratification gate

No product-code implementation should begin until a human explicitly chooses one:

- ratify ADR-0029 and ADR-0030; or
- approve a prototype as non-authoritative experiment.

If ratified, move the ADRs to `docs/adr/accepted/`, set `status: accepted`,
add `ratified`, regenerate `docs/adr/INDEX.md`, and run
`python tools/governance/adr_lint.py --check-index`.

---

# Phase C1 - In-process model and merge tests

Files:

```text
src/debug/capability_discovery.hpp
src/debug/capability_discovery.cpp
src/debug/tests/capability_discovery_test.cpp
src/debug/CMakeLists.txt
```

Deliverables:

- enums for `capability_kind`, `capability_state`, `capability_scope`, and
  `diagnostic_severity`;
- structs for `diagnostic`, `capability_descriptor`, and `capability_manifest`;
- builder helpers for common descriptors;
- deterministic merge function over provider manifests;
- tests for ordering, duplicate diagnostics, unavailable/degraded states, and
  unknown/opaque capability preservation.

Acceptance:

- focused `mnemos_debug_dump_test` passes;
- full Windows preset passes;
- no wire serialization or frontend UI changes.

---

# Phase C2 - Discovery for existing dump surfaces

Map current capabilities before adding new feature domains.

Files:

```text
src/debug/capability_discovery.cpp
src/debug/tests/capability_discovery_test.cpp
src/debug/asset_export.hpp
src/debug/audio_export.hpp
src/debug/debug_dump.hpp
src/debug/animation_export.hpp
```

Deliverables:

- `asset` descriptors for asset sources and debug layers;
- `audio` descriptors for PCM sample and register-write/VGM export;
- `memory` descriptors for chip/system memory views and register sidecars;
- `debug` descriptors for screenshot, CPU trace, GIF, and movie frame sequence;
- diagnostics for systems with no framebuffer or no chip introspection.

Acceptance:

- fake `player_system` tests prove descriptor presence/absence without loading
  real ROMs;
- no system-family branching in the discovery code.

---

# Phase C3 - Watch model for high scores and achievements

This phase adds data structures and tests only, not game packs.

Files:

```text
src/debug/game_watch.hpp
src/debug/game_watch.cpp
src/debug/tests/game_watch_test.cpp
```

Deliverables:

- watch sources: memory view byte ranges, register fields, frame events;
- decoders: unsigned/signed binary, BCD, masks, shifts, scale;
- predicates: comparison, range, changed, latched, all/any condition nodes;
- score specialization with player slot, reset policy, and submit policy;
- achievement rule nodes that reference watch ids instead of raw addresses.

Acceptance:

- hermetic tests over fake memory/register sources;
- deterministic sampling point is explicit in every watch;
- malformed definitions produce diagnostics, not silent disabled rules.

---

# Phase C4 - Session input capability

Describe deterministic multiplayer support before implementing networking.

Files:

```text
src/debug/capability_discovery.cpp
src/debug/tests/capability_discovery_test.cpp
src/frontend_sdk/player_system.hpp
```

Deliverables:

- session descriptors for available controller ports and device formats;
- local and lockstep/input-delay mode metadata;
- save-state/rollback readiness flag derived from actual system support;
- diagnostics for missing save-state support or unknown input device shape.

Acceptance:

- no sockets, matchmaking, or transport code;
- fake systems prove local-only, lockstep-ready, and rollback-not-ready states.

Open question: `player_system` may need a small additive query for controller
port metadata. Keep it raw and factual; discovery derives policy.

---

# Phase C5 - Paged-media metadata

Describe paged media without changing bus read behavior.

Files:

```text
src/debug/capability_discovery.cpp
src/debug/tests/capability_discovery_test.cpp
src/manifests/common/...
```

Deliverables:

- media descriptors for resident vs paged vs streamed media;
- full-media hash and optional page-hash table metadata;
- provider id and cache hints;
- diagnostics for missing full-media hash, provider unavailable, or unsupported
  media revision.

Acceptance:

- tests use synthetic metadata, not large ROMs;
- no host file I/O in bus hot paths;
- no behavior change to existing resident ROM loading.

---

# Phase C6 - Plugin/provider seam

Only after C1-C5 are stable, add an opt-in provider registry.

Deliverables:

- provider registration API for capability descriptors;
- plugin pack provenance fields;
- deterministic merge order across built-in and plugin providers;
- diagnostics for stale provider version and unsatisfied requirements.

Acceptance:

- no exception crossing ABI boundaries;
- provider failure degrades to diagnostics;
- plugin-provided descriptors can be disabled without changing emulator output.

---

# Phase C7 - Frontend consumption

Add UI only after discovery is test-covered.

Deliverables:

- player/developer frontend queries capability manifest once per load and after
  media/plugin changes;
- controls are enabled/hidden/explained from `state` and diagnostics;
- no hard-coded system-family guesses for supported dump/watch/session features.

Acceptance:

- snapshot tests or CLI output tests prove stable user-facing capability summaries;
- existing headless export commands continue to work.

---

## Validation matrix

Each implementation phase should close with:

```powershell
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset windows-msvc-debug --target mnemos_debug_dump_test && ctest --preset windows-msvc-debug -R "mnemos_debug_dump_test" --output-on-failure'
cmd.exe /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --preset windows-msvc-debug --output-on-failure'
```

Docs-only phases use:

```powershell
python tools\governance\adr_lint.py --check-index
git diff --check
```

## Open questions needing sign-off

1. Should `capability_manifest` initially live in `src/debug` as proposed, or do
   we want a new tier-6 in-process instrumentation target first?
2. What exact raw controller-port metadata should `player_system` expose for
   session discovery?
3. Should game-level watch/achievement packs live beside per-game manifests or
   in a separate plugin directory?
4. Should paged-media providers be host-only plugins, or may manifest builders
   provide built-in providers for packaged media?
5. What minimum save-state performance target gates rollback readiness?

## Non-goals

- No direct ROM streaming in `topology::bus` read paths.
- No runtime dependency on frontend UI or plugin transport code.
- No network transport, rollback, account service, or cloud sync in these
  phases.
- No heuristic high-score scanner in core.
- No wire schema until ADR-0027's serialization validation is complete.
