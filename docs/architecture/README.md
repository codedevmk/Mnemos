# Architecture Docs

This directory contains the canonical v0.1 architecture drafts.

Reading order:

1. `mnemos-architecture-tds-v0.1.md`
2. `mnemos-project-plan-v0.1.md`
3. `mnemos-todos-v0.1.md`

## Subsystem references

- `genesis-cycle-and-timing-model.md` — how Mnemos processes cycles on the Genesis
  (master clock + scheduler dividers, the 68000 instruction-atomic execution model, VDP
  frame/line timing, and the VDP→CPU bus-stall mechanisms). A living reference for cycle
  work; enrich as the timing model is refined.
- `apm-tracer-sidecar-spec.md` — design of record for the self-contained APM-style tracer:
  a separate host process that loads the emulator as a plugin, injects a tagged memory
  allocator, and observes execution from the outside (page-protection watchpoints, boundary
  diffs, lockstep cross-engine diff). Folder/module layout, dependency direction, and build
  order. Companion to `apm-tracer-tasks.md`.
- `factsheets/irem-system-boards-reference.md` — Irem M-series board-family reference used
  to keep M72/M81/M82/M84/M107 work classified by board lineage and shared custom silicon.
