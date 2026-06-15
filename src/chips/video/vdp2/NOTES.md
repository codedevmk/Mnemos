# vdp2 — Sega Saturn VDP2 background / colour processor (PARTIAL port)

## Provenance
Ported from the Emu C core `chips/vdp2/vdp2.{c,h}` (our own earlier project;
cannibalization sanctioned per ADR 0006). Re-reviewed for the Mnemos chip
contract (ADR 0004), determinism, and the flat-module / `i`-interface
conventions. Adopts Mnemos structure and namespaces (`mnemos::chips::video`).

## Status: PARTIAL
The hardware VDP2 is very large. This module ports a deliberately compiling
subset and registers as `sega.vdp2` (chip class `video`).

### Ported (faithful to the Emu reference)
- 256 word-aligned 16-bit register file with read-only TVSTAT / HCNT / VCNT.
- TVMD / BGON / RAMCTL decode (display, H/V resolution, interlace, per-plane
  enable + transparent-code, CRAM colour mode 0..2).
- 512 KiB VRAM + 4 KiB CRAM. Palette lookup across all three CRAM modes,
  converting Saturn-native 0BGR1555 / 24-bit to packed `0x00RRGGBB`.
- Per-scanline back-colour injection.
- NBG normal-scroll TILEMAP fetch: 4bpp / 8bpp / 16bpp cells, 1-word and
  2-word pattern names (with PNCN supplement), plane sizes (1x1 / 2x1 / 2x2),
  integer scroll, character H/V flip, transparent dot-code 0.
- Priority-ordered NBG0..NBG3 compositor over the back colour.

### Deferred (registers stored + read back, but not yet driving rendering)
- Rotation layers RBG0 / RBG1 and the whole rotation-matrix / coefficient-table
  machinery.
- Bitmap-mode NBGs (CHCTLA bitmap-enable path).
- The VDP1 sprite layer composite.
- Window masks (WCTLA/B/C/D + window positions), colour calculation / blending
  (CCCTL + ratios), mosaic (MZCTL), line colour (LNCLEN), line scroll / vertical
  cell scroll, coordinate-increment zoom, and colour offset (CLOFEN/COA/COB).

### Notes
- tick() advances a simple scanline beam over an NTSC-shaped frame and renders
  the completed frame when the beam re-enters vblank, bumping frame_index().
- introspection() exposes VRAM and CRAM as `memory_view`s.
- No third-party emulator or game names appear in the code (HARD RULE);
  hardware is described directly.
