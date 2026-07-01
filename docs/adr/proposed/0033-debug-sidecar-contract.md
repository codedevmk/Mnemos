---
id: ADR-0033
title: "Debug Sidecar Contract"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-07-01
ratified: null
---

# ADR 0033: Debug Sidecar Contract

**Status:** Proposed
**Date:** 2026-07-01

## Context

Mnemos already has the lower-level pieces of a system-agnostic debug substrate:

- `frontend_sdk::player_system` hides concrete systems behind a common
  boot/frame/input/audio/media interface.
- `instrumentation::ichip_introspection` exposes optional memory, register,
  trace, register-write, debug-layer, asset, and audio surfaces without
  downcasting chips.
- `debug::dump` can discover and export many of those surfaces headlessly.
- ADR-0030 proposes one logical capability-discovery document for frontends,
  plugins, and tools.

Recent Amiga work exposed the need to turn that substrate into an interactive
debugger architecture. Turrican can boot and run, but the player body is
invisible while bullets and world layers are visible. Diagnosing that class of
bug needs live memory views, active palettes, sprite/playfield/layer toggles,
register banks, and graphics asset views. The same need exists for C64, ZX
Spectrum, NES, Genesis, MSX, CPS1, Taito, Irem, and other systems; only the
system-specific decoding differs.

The master architecture draft supplied for review describes the same direction:
a cold inspection plane, an out-of-process wire service, observability with zero
cost when detached, and deterministic event injection. This ADR records the
debug-sidecar contract slice only. It does not ratify the entire draft.

## Decision

Adopt a three-layer debugger architecture:

```text
Mnemos Player / Sidecar UI
  -> System Adapter debug session
    -> Concrete System / chips / runtime
```

### 1. Mnemos Player / Sidecar UI

The player and any sidecar debugger are clients. They must not include concrete
system headers, reach into runtime internals, or branch on private chip types.
They consume:

- a discovery manifest;
- frame-tagged snapshots;
- memory/register/layer/asset/audio views by stable id;
- timeline-control commands exposed by the debug session.

The UI may present system-specific names and categories, but it must not depend
on concrete implementation types such as Amiga Agnus, NES PPU, Genesis VDP, or
CPS1 tilemap classes.

The first interactive debugger UI should be a thin Dear ImGui sidecar hosted by
the existing SDL player process or by a separate sidecar process once the wire
service is available. ImGui is an implementation detail of the sidecar frontend:
the debug contract must remain UI-toolkit neutral so a later Eliot UIKit client
can replace the ImGui surface without changing adapters, chips, snapshot
formats, or capability ids. Mnemos product code must not take an Eliot runtime
or UIKit dependency unless a future approved ADR defines that integration
boundary.

### 2. System Adapter debug session

The system adapter is the contract bridge. It owns stable ids, merges chip and
system surfaces into a single session view, and maps generic requests to the
concrete system.

The initial implementation should extend the existing `player_system` boundary
with an in-process debug-session facade rather than adding per-system UI hooks.
That facade is responsible for:

- publishing an ADR-0030-compatible capability manifest;
- assigning stable ids to every memory space, register bank, debug layer, asset
  source, audio source, event stream, and control;
- producing frame-consistent snapshots when a tool needs multiple views from the
  same emulated instant;
- translating commands such as frame step, pause, memory read, register read,
  layer capture, and asset capture to existing chip/runtime surfaces;
- rejecting unavailable or degraded capabilities with structured diagnostics,
  not string-matched errors.

Adapters may provide system-specific decoders, but only behind generic
capability kinds. For example, Amiga can expose "bitplanes", "playfields",
"sprites", "copper", "Paula channels", and "CIA registers"; Genesis can expose
"plane A", "plane B", "window", "sprites", and "CRAM"; NES can expose pattern
tables, nametables, palettes, and OAM sprites. The player sees capability
records, not bespoke adapter APIs.

### 3. Concrete system / chips / runtime

The concrete system remains deterministic and headless. It exposes information
through existing or additive instrumentation contracts only:

- `memory_view` for RAM, VRAM, CRAM, OAM, register files, and system memories;
- `register_view` for architectural register banks;
- `debug_layer` for whole rendered debug surfaces;
- `asset_source` for decoded palettes, tile sheets, sprites, fonts, and bitmaps;
- `audio_source` and `reg_write_trace` for channel and music-analysis tooling;
- trace/event hooks for CPUs, buses, DMA, interrupts, and media when available.

Chips and systems do not know whether the consumer is the player overlay, a GUI
debugger, a headless test, a Python client, or a future remote process.

## Capability model

ADR-0030 remains the envelope. This ADR specializes the `debug`, `memory`,
`asset`, and `audio` capability payloads for debugger use.

Required debugger capability kinds:

- `memory_space`: stable id, display name, address width, byte length,
  mutability, access granularity, owner, optional source media id, and optional
  bank/page metadata.
- `register_bank`: stable id, owner, fields, bit widths, masks, enum labels,
  read timing, and write support if any.
- `video_frame`: the primary composited output, frame tag, dimensions, pixel
  format, and region timing.
- `debug_layer`: stable id, owner, category, dimensions, pixel format, source
  memory ids, palette ids, transparency policy, and whether the layer is
  host-filterable.
- `asset_source`: palette, tile sheet, sprite, font, or bitmap source, with
  source memory references and palette relationships.
- `audio_source`: channel or mixer source, sample format, clock/rate metadata,
  mute/solo host controls when available, and register-write stream ids.
- `trace_stream`: CPU instruction, bus, DMA, IRQ, register-write, media, or
  scheduler event stream with ordering and timestamp semantics.
- `timeline_control`: pause, resume, frame step, scanline step, instruction
  step, breakpoint, watchpoint, and replay/event injection support.

All ids are stable within their provider and must not be derived from display
text. Category names are generic and additive; consumers must ignore unknown
categories unless explicitly required.

## Determinism rules

Read-only observation is not part of the emulated timeline and must not change
machine state.

Host-only display controls, such as showing or hiding a debug layer in the UI,
are view filters. They must not change emulated hardware registers or memory.

Any command that mutates emulated state is a deterministic timeline event or a
documented debugger break action:

- input, reset, media insert/eject, fault injection, and register/memory pokes
  are ordered `ExternalEvent` entries when they are meant to be replayable;
- breakpoints and watchpoints may halt the host-side run loop, but the halted
  emulated state must be exactly the state at the selected timestamp;
- a non-replayable debugger action must be marked as such and must invalidate
  deterministic replay/rollback for that session unless explicitly recorded.

## Snapshot and lifetime rules

Live spans returned by chips remain short-lived, as today. The debug session
adds a snapshot layer for tools that need consistency across multiple views:

- every snapshot has a frame tag and, when available, a scheduler timestamp;
- all views in one snapshot are from the same completed frame or explicit
  debugger stop point;
- retained sidecar data is copied out of chip-owned spans by the adapter or
  debug session;
- expensive decoded views may be generated lazily on request, but the generated
  data must be tied to a snapshot tag.

## Wire and process boundary

The first slice is in-process C++ because the current wire directory is only a
placeholder and ADR-0027 defers serialization commitment. The logical contract
must be written so it can map to the ARCH-002 wire protocol later without
semantic changes.

Once the logical session model is stable, the out-of-process sidecar speaks the
same contract over the wire service. The wire service is a transport for the
debug-session contract, not a second debugger API.

## Implementation sequence

1. Add an in-process `debug_session` logical model at the frontend/debug
   boundary, backed by `player_system`, `chips()`, `memory_views()`, and chip
   introspection.
2. Extend capability discovery so debugger capabilities are first-class records
   instead of dump-only facts.
3. Add headless commands to list capabilities and capture specific memory,
   register, layer, asset, and audio views by stable id.
4. Use Amiga as the first proving system: publish Agnus/Denise/Paula/CIA memory
   and register banks, active palettes, bitplane/playfield/sprite debug layers,
   sprite asset views, Copper state, Paula channels, and disk/CIA event streams.
5. Add a thin Dear ImGui sidecar UI behind a build option after the headless
   contract can diagnose the Amiga sprite-visibility failure without concrete
   adapter branching. The initial panels should cover primary frame zoom/pan,
   debug-layer show/hide/solo, memory views, register banks, active palettes,
   asset sheets, audio channels, and timeline controls.
6. Promote the same contract through the wire service after schema and event
   throughput measurements satisfy ADR-0027.

## Consequences

- Mnemos gains a uniform debugger surface without making a lowest-common-
  denominator UI. Systems can publish rich, native facts through generic
  capability records.
- The Amiga debugger work directly benefits later C64, Spectrum, NES, Genesis,
  MSX, CPS1, Taito, and Irem work because the player and sidecar do not change.
- Layer visibility in the debugger is explicitly a host presentation filter,
  avoiding accidental mutation of emulated state.
- The debug UI becomes a consumer of discovery and snapshots, not an owner of
  per-system knowledge.
- The architecture keeps the production path cold: no per-cycle UI work, no
  concrete frontend dependencies in chips, and no wire overhead when detached.

## Alternatives considered

- **Per-system debugger panels.** Rejected: they would solve Amiga quickly but
  recreate the same UI and tooling for every machine family.
- **Put all debugger methods on `player_system`.** Rejected: that would make the
  frontend adapter interface grow without a coherent capability envelope.
- **Expose only raw memory and let the UI decode everything.** Rejected: tile,
  sprite, palette, and audio decoding is system-specific and belongs behind the
  adapter/chip contract.
- **Commit the wire schema first.** Rejected for now: ADR-0027 requires
  validation before a serialization commitment, and an in-process logical model
  is enough to prove the contract.
