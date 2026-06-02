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
