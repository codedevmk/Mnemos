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

- `amiga_memory_sizes.hpp` — shared binary size vocabulary for chip RAM, Fast
  RAM, Kickstart windows, and expansion sizing.
- `amiga_system.hpp/.cpp` — shared machine assembly and remaining integration
  glue.
- `chipsets/` — OCS/ECS/AGA chipset profile policy around Agnus, Denise, and
  Paula. The chips themselves stay under `src/chips/`. `amiga_chipsets.*`
  currently owns Copper address-width policy.
- `expansions/` — Zorro/autoconfig and expansion-board helpers.
- `drives/` — Amiga drive and disk-controller glue. `amiga_floppy.*`
  currently owns DD floppy geometry and drive-local stream/cache state.
- `devices/` — keyboard, joystick, mouse, and port-device glue.
  `amiga_input.*` currently owns controller-port masks, JOYDAT encoding, mouse
  counter wrapping, and POT counter composition. `amiga_keyboard.*` currently
  owns raw key constants, keyboard queue/matrix helpers, caps lock state, and
  keyboard SDR byte encoding, serial acknowledgement state transitions, and
  the keyboard save-state payload.
- `models/` — model descriptors for A500, A500+, A600, A2000, and later
  machines. `amiga_models.*` currently owns base chip RAM, chipset selection,
  and expansion capability policy.

The current refactor has split out `chipsets/amiga_chipsets.*`,
`devices/amiga_input.*`, `devices/amiga_keyboard.*`, `drives/amiga_floppy.*`,
`expansions/zorro2.*`, and `models/amiga_models.*`; remaining large blocks in
`amiga_system.cpp` should move into the matching folders as the next behavior
work touches them.
