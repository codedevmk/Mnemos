# Amiga Manifest

This module assembles classic Amiga machines from reusable chips plus
Amiga-specific board glue. The concrete silicon implementations live under
`src/chips/`; this manifest wires them into model profiles.

Current public systems using this module:

- `amiga500`
- `amiga500plus`
- `amiga600`
- `amiga2000`

Layout:

- `amiga_system.hpp/.cpp` — shared machine assembly and remaining integration
  glue.
- `chipsets/` — OCS/ECS/AGA chipset profile policy around Agnus, Denise, and
  Paula. The chips themselves stay under `src/chips/`. `amiga_chipsets.*`
  currently owns Copper address-width policy.
- `expansions/` — Zorro/autoconfig and expansion-board helpers.
- `drives/` — Amiga drive and disk-controller glue.
- `devices/` — keyboard, joystick, mouse, and port-device glue.
- `models/` — model descriptors for A500, A500+, A600, A2000, and later
  machines. `amiga_models.*` currently owns base chip RAM, chipset selection,
  and expansion capability policy.

The current refactor has split out `chipsets/amiga_chipsets.*`,
`expansions/zorro2.*`, and `models/amiga_models.*`; remaining large blocks in
`amiga_system.cpp` should move into the matching folders as the next behavior
work touches them.
