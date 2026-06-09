# sega32x_vdp — Sega 32X VDP (315-5788)

`mnemos::chips::video::sega32x_vdp` — the secondary video chip on the 32X
adapter, separate from the Genesis VDP. Owns a 256-entry 16-bit palette CRAM
(15bpp BGR + per-entry priority bit), the six-register file (bitmap mode,
screen shift, autofill length/address/data, frame-buffer control) and the
autofill engine. The 256 KiB frame-buffer byte store itself lives on the 32X
board (`manifests::sega32x::sega32x_system::framebuffer`) — the SH-2s write it
as plain memory — so buffer-touching operations take a span.

Output is composition, not frame generation: `compose_scanline` overlays the
32X pixels for a row onto an already-rendered Genesis row of `0x00RRGGBB`
pixels (packed 8bpp / direct 15bpp / run-length modes). The per-pixel priority
bit gates layer *order*, not visibility — a priority-0 pixel still shows
wherever the Genesis pixel is at backdrop (approximated as black).

Hardware behaviours modelled:
- **FS flip-flop**: a frame-select write latches and only commits to the
  displayed-bank bit (FBCR bit 0) on the V-blank rising edge via
  `set_blanking`, so double-buffered flips never tear; reads keep returning
  the *displayed* bank until then.
- **Autofill address quirk**: only address bits 0-7 increment during a fill
  (the top byte holds), so a fill sweeps one 256-word row and wraps; the final
  address latches back into AUTOFILL_ADDR. Fill count = length + 1, stores are
  big-endian, FEN reads as done (fills complete well inside a scanline).
- **HBLK/VBLK status** (FBCR bits 14/15) driven by the parent Genesis VDP's
  blanking state through `set_blanking`.

## Provenance

Ported from the Emu reference `chips/thirtytwox_vdp` (our own earlier project —
sanctioned per ADR-0006 / ADR-0011); clean-room per the Sega 32X VDP
Programmer's Manual and Mars hardware references. No third-party emulator
source consulted. Restructured to the Mnemos `ichip` contract (metadata / reset
/ save_state / introspection / factory registration); `tick` is a no-op — the
chip is slaved to the Genesis VDP's beam.

Deliberate divergence from the reference: the reference's bus glue fires the
autofill on *every* byte write to AUTOFILL_DATA (so a word store on its
byte-decomposed bus fills twice, the second time from the latched end
address). Mnemos fires once, when the low byte completes the word — the
hardware meaning of "writing AFDR triggers the fill" on a byte-level bus.

## Conformance

`tests/sega32x_vdp_test.cpp`: register write masks + mirror decode, palette
round-trip, autofill (count, row-wrap quirk, address latch-back, big-endian
store), FS commit on the V-blank rising edge + HBLK/VBLK bits, packed/direct
composition (transparency, priority over/behind, backdrop rule), save/load
round-trip.
