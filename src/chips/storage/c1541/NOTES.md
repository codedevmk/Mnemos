# Commodore 1541 — port notes

Ported from the Emu reference core (ADR 0006): `Emu/Emu/chips/c1541/`,
`Emu/Emu/chips/iec_bus/`, `Emu/Emu/chips/m6522/`.

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

## Deferred (full cycle-accurate drive)

The full 1541 (drive 6502 + two 6522 VIAs + 2 KB RAM + 16 KB DOS ROM + GCR
bit-stream + head/stepper) is a separate, larger component. In Emu its GCR read
path was still being debugged, so "parity" tracks that: the synthetic drive is
the reliable LOAD path; the full drive (for fastloaders / copy protection) is the
follow-up, built on the `via_6522` chip already in place.
