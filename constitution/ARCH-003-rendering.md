---
id: ARCH-003
title: "Rendering and Platform-Access Boundary"
status: proposed
version: 0.1.0
supersedes: []
superseded_by: null
ratified: null
proposed: 2026-06-10
---

# ARCH-003 — Rendering and Platform-Access Boundary

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
section 14, per `constitution/MIGRATION.md`.

## Contract (TDS §14)

The frontend SDK (tier 7) provides:

- A custom retained-mode UI toolkit.
- Vulkan-based renderer abstraction.
- Theme system.
- Asset loading (images, fonts, audio).
- Common widgets used by both player and dev apps.
- Input device abstraction (keyboard, mouse, gamepads).

**The SDK is the only tier above the instrumentation API allowed to do
platform graphics or audio.** Detailed SDK design is deferred to a separate
Frontend SDK TDS.

The runtime core remains headless: rendering and audio output live in the
frontend SDK / apps, never in tiers 1–6 (cross-reference ARCH-004 on the
headless deterministic core).

## Ratification note (candidate axis conflict)

The TDS prescribes a Vulkan-based renderer abstraction; the repository
currently fetches **SDL3** ("Window / GPU / audio / input for the player
frontend", `THIRD_PARTY_NOTICES.md`, pinned in
`cmake/modules/MnemosFetchSDL3.cmake`). Whether SDL3's GPU path satisfies or
supersedes "Vulkan-based renderer abstraction" is an open intent-vs-fact
question. The ratifier of this module should either amend this text to match
the SDL3 reality or file an `axis-conflict` defect (H3) — this note exists so
the conflict is adjudicated rather than silently inherited.
