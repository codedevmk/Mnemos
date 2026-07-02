---
id: ADR-0032
title: "Amiga Composite Manifest Layout"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-30
---

# ADR 0032: Amiga Composite Manifest Layout

**Status:** Proposed
**Date:** 2026-06-30

## Context

The initial Amiga integration landed under `src/manifests/amiga500/` because
the Amiga 500 was the first boot target. The same implementation now assembles
Amiga 1000, Amiga 500, Amiga 500+, Amiga 600, and Amiga 2000 profiles. Keeping
shared OCS, ECS, floppy, keyboard, and Zorro II work under an `amiga500`
directory makes non-A500 changes appear misplaced and invites duplicate machine
implementations.

ADR-0009 currently prefers flat modules with public headers at the module root.
Classic Amiga is different enough to justify a scoped exception: it is one
machine family composed from reusable chips, chipset profiles, expansion buses,
drives, devices, and model descriptors.

## Decision

Use `src/manifests/amiga/` as the canonical home for classic Amiga machine
assembly.

Actual silicon remains in `src/chips/`:

- `m68000`
- `agnus`
- `denise`
- `paula`
- `cia8520`

The Amiga manifest owns board-level composition and model descriptors:

```text
src/manifests/amiga/
  amiga_system.hpp/.cpp
  chipsets/amiga_chipsets.hpp/.cpp
  expansions/zorro2.hpp/.cpp
  drives/amiga_floppy.hpp/.cpp
  devices/amiga_input.hpp/.cpp
  devices/amiga_keyboard.hpp/.cpp
  models/amiga_models.hpp/.cpp
```

Subdirectories are allowed inside this manifest only for Amiga family
building-blocks. They do not create new tiers and do not allow higher-tier
dependencies. Public CLI IDs and ROM environment variables remain model-specific
(`amiga500`, `amiga2000`, `MNEMOS_AMIGA2000_KICKSTART`, and so on).

Model descriptors select base chip RAM, chipset profile, and expansion-bus
capabilities. Chipset descriptors own chipset-specific policy such as the
address width used by Copper pointers. The shared system assembly consumes
these descriptors rather than hard-coding per-model branches.

Drive descriptors and state helpers own Amiga floppy geometry and drive-local
stream/cache state. The system assembly still owns the disk DMA register
contract and CIA wiring until those responsibilities can be split without
changing timing behavior.

Device helpers own controller-port button masks, joystick register encoding,
mouse counter wrapping, POT counter composition, keyboard constants, keyboard
queue/matrix helpers, caps lock state, keyboard SDR byte encoding, and keyboard
serial acknowledgement state transitions. Keyboard helpers also own the
keyboard payload inside the system save-state stream. The system assembly still
owns CIA pin routing until that can be moved without changing timing behavior.

## Consequences

- Amiga 2000-specific expansion logic no longer lives under an Amiga 500 path.
- Additional models can add descriptors without duplicating the shared
  Kickstart, custom-chip, floppy, keyboard, and input glue.
- The refactor creates a contained exception to ADR-0009's flat-module rule;
  if this layout proves useful beyond Amiga, ADR-0009 should be amended rather
  than copying the exception ad hoc.
