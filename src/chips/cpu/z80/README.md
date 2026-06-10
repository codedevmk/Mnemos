# Z80

Zilog Z80 CPU core (factory `zilog.z80`), ported from the Emu reference per
ADR-0006 and restructured to the Mnemos chip contract. Complete instruction
set — unprefixed, CB, ED, DD/FD and DDCB/FDCB — including the common
undocumented opcodes and the full flag model with the XF/YF bits. Memory goes
through `ibus`; the separate Z80 I/O space uses injected port callbacks. NMI
plus interrupt modes 0/1/2 are modeled. Execution is instruction-stepped with
a catch-up `tick`, with save/load state and a register snapshot for
instrumentation. Shared across the SMS, Game Gear, and Genesis (sound CPU)
machines.

## Conformance

Unit tests live in `tests/`. ZEXALL/ZEXDOC run through a data-gated CP/M
harness (`z80_conformance_test.cpp`) that loads an exerciser `.com` at `$0100`
and traps BDOS console output; it self-skips without `MNEMOS_Z80_TEST_ROM`.
