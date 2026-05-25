# SMS Manifests

`assemble_sms()` (in `sms_system.hpp` / `sms_system.cpp`) wires a Sega Master System
from its chips: the Zilog Z80, the SMS VDP, the SN76489 PSG, and the Sega mapper,
plus 8 KiB of work RAM and the Z80 memory + I/O buses.

- **Memory map:** `$0000-$BFFF` banked ROM / cart RAM via the mapper; `$C000-$DFFF`
  8 KiB work RAM mirrored to `$E000-$FFFF`; the `$FFFC-$FFFF` mapper-register window
  overlays the mirror (writes hit RAM *and* the mapper).
- **Z80 I/O:** `$3F` I/O-control latch, `$7E/$7F` V/H counters (read) and PSG (write),
  `$BE/$BF` VDP data/control, `$DC/$DD` joypads.
- **Interrupts:** the VDP `/INT` line is ORed into the Z80 IRQ.
- **Region:** `sms_config::region` selects NTSC (262 lines) or PAL (313).

`set_pad()` / `set_reset_button()` drive the controller state. A cartridge image is
moved into `assemble_sms`; the mapper borrows it.

The headless CLI run path (scheduler wiring) and golden-frame tests are follow-ups.
