---
id: ARCH-003
title: "Rendering and Platform-Access Boundary"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
---

# ARCH-003 — Rendering and Platform-Access Boundary

Lifted from `docs/architecture/mnemos-architecture-tds-v0.1.md` section 14,
per `constitution/MIGRATION.md`. The TDS's "Vulkan-based renderer abstraction"
wording was adjudicated against repository fact at ratification: the platform
layer is SDL3 and GPU access goes through the SDL_GPU API (Vulkan is one of
its backends). ADR-0014 records the adjudication.

## Contract

The frontend SDK / app tier (tiers 7–8) provides:

- UI primitives, theming, and common widgets used by player and dev apps.
- GPU rendering through the SDL3 GPU API (`SDL_CreateGPUDevice`); backend
  selection (Vulkan, D3D12, ...) is SDL3's concern, never a lower tier's.
- Asset loading (images, fonts, audio).
- Input device abstraction (keyboard, mouse, gamepads).

**Tiers 7–8 are the only tiers allowed to do platform graphics, audio, or
windowing.** The runtime core remains headless: rendering and audio output
never enter tiers 1–6 (cross-reference ARCH-004 on the headless deterministic
core). SDL3 is consumed only by the apps (`THIRD_PARTY_NOTICES.md`: consumer
`mnemos_player`), built statically, pinned by
`cmake/modules/MnemosFetchSDL3.cmake`.

Detailed SDK design remains deferred to a separate Frontend SDK TDS; a future
move away from SDL_GPU is a supersession of this module, not a silent swap.
