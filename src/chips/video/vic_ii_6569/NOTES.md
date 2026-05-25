# MOS 6569 / 6567 (VIC-II) — Implementation Notes

The Commodore 64 video interface controller. PAL 6569 (63 cyc × 312 lines) is the
default; NTSC 6567 geometry is selectable via `set_revision`.

## Provenance

Ported from the Emu reference core (`Emu/Emu/chips/vic2/`), relicensed into Mnemos
per ADR 0006. The port is a re-review into the Mnemos tier / C++23 / `ivideo`
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

## Renderer (net-new — not in the Emu core)

A per-frame scanline renderer now lands pixels into an owned framebuffer
(`cycles_per_line()*8` x `total_lines()`, 0x00RRGGBB). `render_line` runs as each
raster line completes inside `tick`; `frame_index()` increments on the raster
wrap so the runtime can detect frame boundaries; `framebuffer()` returns a
borrowed `frame_buffer_view`.

The VIC fetches via `attach_memory` (64K RAM + 4K char ROM + 1K colour RAM) and
`set_bank` (the inverted CIA2 port-A bank select); the character ROM shadows VIC
$1000-$1FFF in banks 0 and 2. Implemented modes: standard (hi-res) and
multicolour text + border, gated by the DEN line-$30 latch and CSEL/RSEL
geometry. The display window uses a canonical CSEL=1/RSEL=1 origin.

## Still deferred (net-new — not in the Emu core either)

- Standard / multicolour bitmap and extended-colour text modes.
- Cycle-exact beam alignment (cycle->X), open-border / sprite-in-border tricks,
  mid-line raster splits (renderer currently latches per-line, not per-cycle).
- Sprite fetch + compositor + priority + sprite-sprite / sprite-data collisions.
- Sprite-DMA BA/AEC timing.
- Save/load state — deferred to the M3 runtime save-state format.

The /IRQ output now drives an edge callback (`set_irq_callback`) and the C64
shell ORs it with CIA1 into the 6510 /IRQ; the 16K fetch bank tracks CIA2 port A
at runtime (`set_bank` from the CIA2 port-A callback).

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
