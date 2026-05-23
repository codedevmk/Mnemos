# MOS 6569 / 6567 (VIC-II) — Implementation Notes

The Commodore 64 video interface controller. PAL 6569 (63 cyc × 312 lines) is the
default; NTSC 6567 geometry is selectable via `set_revision`.

## Provenance

Ported from the Emu reference core (`Emu/Emu/chips/vic2/`), relicensed into Mnemos
per ADR 0006. The port is a re-review into the Mnemos tier / C++23 / `i_video`
architecture, not a copy.

## Behavioral references

- MOS 6569 / 6567 datasheets.
- Christian Bauer, *The MOS 6567/6569 video controller (VIC-II) and its
  application in the Commodore 64* (1996) — raster timing, bad lines, the
  VC/VCBASE/RC/VMLI video-matrix address generator (§3.7.2).
- Pepto's calibrated C64 palette (RGB888).

## What this port covers

- Full `$D000`–`$D02E` register read/write path; `$D02F`–`$D03F` read `$FF`; the
  window mirrors every 64 bytes (`address & 0x3F`).
- Pepto 16-colour palette.
- Raster counter + 9-bit raster compare; beam-position tracker (PAL/NTSC).
- Raster-IRQ source latch + mask + master presentation via `$D019`/`$D01A`, with
  write-1 acknowledge; light-pen IRQ source.
- Bad-line detection with the BA-low and CPU-read-stall windows.
- SCROLY/SCROLX mode decode; sprite X (9-bit)/Y latches; collision read-to-clear.
- Internal video-matrix address generator (VC/VCBASE/RC/VMLI, display state).
- Register-snapshot introspection.

## Deferred (net-new — not in the Emu core either)

- Scanline rendering (text / bitmap / MCM / ECM / dual-mode).
- Sprite fetch + compositor + priority + sprite-sprite / sprite-data collisions.
- Border generation (open-border trick), sprite-DMA BA/AEC timing.
- Bus-side memory fetch (video matrix / char ROM / bitmap) — needs an `i_bus`
  attachment, as the CPU has, once rendering lands.
- Save/load state — deferred to the M3 runtime save-state format.

## Intentionally omitted from the port

Emu's opt-in dev-tool surfaces — raster-time profile, copper-list write log,
beam-renderer scaffolding, and the per-line compositor shadow — are not ported.
Those are M4-instrumentation / renderer concerns that Mnemos will model through
its own instrumentation tier.

## Configuration / wiring

The `$00/$01`-style bus routing for MMIO chips (how the topology bus dispatches
register reads/writes to this chip) is an M3 topology concern; for now `read`/
`write` are concrete public methods exercised directly by tests. IRQ output is
exposed via `irq_asserted()`; wiring it to the CPU IRQ line is M3.
