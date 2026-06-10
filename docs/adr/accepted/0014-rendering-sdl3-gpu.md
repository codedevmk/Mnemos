---
id: ADR-0014
title: "Rendering Reality: SDL3 GPU Layer Supersedes Vulkan-First Wording"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-10
ratified: 2026-06-10
---

# ADR 0014: Rendering Reality — SDL3 GPU Layer Supersedes Vulkan-First Wording

## Context

The TDS (§14) prescribes a "Vulkan-based renderer abstraction" for the
frontend SDK. The shipped player renders through SDL3's GPU API
(`SDL_CreateGPUDevice`, `src/apps/player/main.cpp`), with SDL3 pinned in
`cmake/modules/MnemosFetchSDL3.cmake` and enumerated in
`THIRD_PARTY_NOTICES.md`. ARCH-003 flagged this as a candidate intent-vs-fact
axis conflict at the P0 lift rather than silently inheriting either side.

## Decision

Adjudicated for fact: the platform layer is **SDL3**, and GPU access goes
through the **SDL_GPU API**, for which Vulkan is one backend among several
(D3D12, Metal). ARCH-003 v1.0.0 is amended accordingly. The substantive
architectural rule survives unchanged: tiers 7–8 are the only tiers allowed
to do platform graphics, audio, or windowing, and the runtime core stays
headless.

The TDS §14 text remains as written in the L3 design note; `constitution/`
takes precedence per `CONSTITUTION.md` §1.

## Consequences

- No code change; this records reality.
- A future move off SDL_GPU (e.g. a bespoke Vulkan renderer) is a
  supersession of ARCH-003, not a silent swap.
- The conflict was resolved at ratification time, so no `axis-conflict`
  defect needed to be opened against an accepted document.

## Ratification

Ratified 2026-06-10 by owner directive in the pilot session (H3
adjudication batched with the H1/H2 ratifications).
