# Commodore 1541 — port notes

## What is here

- **`d64_image`** — logical `.d64` decode: sectors-per-track zoning, track/sector
  offsets, BAM, the track-18 directory chain, name matching (`*` / `?`), PRG
  block-chain read, and the `LOAD"$"` BASIC-listing renderer. Pure byte-level (no
  GCR).
- **`synthetic_drive`** — protocol-level IEC device (devices 8-11). It answers the
  standard KERNAL serial routines and serves files straight from the mounted
  `.d64`: LISTEN/TALK/OPEN/CLOSE/secondary-address command FSM + channel serving
  (`$` directory, `*`/empty first PRG, else by name). This is the proven path that
  makes `LOAD"*",8,1` / `LOAD"$",8` work without the drive DOS ROM.

The IEC bus itself is `chips::iec_bus` (tier-2 header) and the MOS 6522 VIA is
`chips::bus_controller::via_6522`.

## Validation boundary

The command + channel-serving logic is exercised directly by the `debug_*`
helpers (named PRG, wildcard, directory, wrong-device, save/load). The
**bit-level IEC handshake** in `serial_tick` drives the bus the real KERNAL
speaks; end-to-end `LOAD` is validated with the C64 KERNAL ROM and is therefore
**data-gated**, exactly like the golden boot.

## Full cycle-accurate drive (`full_drive`)

The full 1541 is now ported: the drive 6502 (the 6510 core with its I/O port
disabled), two `via_6522` VIAs (VIA1 = IEC, VIA2 = mechanism + GCR head), 2 KB
RAM, the 16 KB DOS ROM, the `bind_gcr` GCR surface under a stepper head with
SYNC/byte-ready timing, and the IEC + auto-ATN-ack wiring. Registered as
`commodore.c1541.full`.

The memory map, VIA port wiring, stepper movement, motor, and head byte/SYNC
mechanics are unit-tested with a synthetic ROM (the drive boots to its reset
vector and runs the mechanism). **Running the real DOS ROM is data-gated** (the
16 KB ROM is copyrighted, never committed — see the C64 ROMS.md pattern), and the
GCR *read* path is the part that needs real-ROM validation to prove
out. The synthetic drive remains the reliable LOAD path; the full drive is for
fastloaders / copy-protection once the DOS ROM is supplied locally.
