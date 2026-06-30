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
  Paula. The chips themselves stay under `src/chips/`.
- `expansions/` — Zorro/autoconfig and expansion-board helpers.
- `drives/` — Amiga drive and disk-controller glue.
- `devices/` — keyboard, joystick, mouse, and port-device glue.
- `models/` — model descriptors for A500, A500+, A600, A2000, and later
  machines.

The current refactor starts the split with `expansions/zorro2.*`; remaining
large blocks in `amiga_system.cpp` should move into the matching folders as
the next behavior work touches them.
