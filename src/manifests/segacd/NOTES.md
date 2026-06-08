# segacd — Sega CD (Mega CD) system

`mnemos::manifests::segacd` — the Sega CD sub side, layered on the Genesis
(M8 family). Ported from the Emu reference `systems/sega/segacd` per ADR-0006 /
[[mnemos-port-emu-cores]] and [[segacd-port]]. Mirrors the `genesis_system`
hand-wired oracle pattern (chips as value members, heap-pinned, `assemble_*`
factory wires a `topology::bus`).

## Status (staged port)

Phases A–D are implemented (PR #58): the real CD BIOS boots and renders its logo.
The in-game disc-read (a game loading itself off the disc) is WIP — see
[[gpgx-differential-trace]].

- **A:** leaf modules — `circ_ecc`, `disc_image` (`src/disc`), `rf5c68` (`src/chips`).
- **B:** `segacd_system` core — 2nd m68000 (sub-CPU) + `topology::bus` sub-bus mapping
  PRG-RAM (512K), word RAM (256K), the RF5C164 PCM register + wave-RAM windows; the
  gate-array registers, word-RAM 2M/1M RET/DMNA ownership, backup RAM ($FE0000
  odd-lane), PRG-RAM bank window, and the sub-CPU IRQ controller (levels 1-6).
- **C:** CDD drive + CDC (LC8951) decoder/DMA + CD-DA + stamp ASIC.
- **D:** main-side $A12000 bridge + $000000-$03FFFF/$200000-$23FFFF windows, sub-CPU
  scheduling, player `.cue/.iso` routing + the `segacd` family. Additive — a plain
  Genesis never traverses this, so the byte-parity floor is untouched.

The sub-CPU boots from the PRG-RAM `$0/$4` vectors that the main BIOS loads there;
there is intentionally NO BIOS overlay on the sub bus (one would shadow those vectors
with the MAIN reset vector and crash the sub).

## Sub-CPU memory map (so far)

| Range | Backing |
|---|---|
| `$000000-$07FFFF` | PRG-RAM (512K). Sub-CPU `$0/$4` reset vectors live here (loaded by the main BIOS; no overlay). |
| `$080000-$0BFFFF` | Word RAM (256K, 2M mode). |
| `$FF0000-$FF0FFF` | RF5C164 registers ($00-$08). |
| `$FF1000-$FF1FFF` | RF5C164 wave-RAM window (bank-selected). |

Sub-CPU clock 12.5 MHz, 166,666 cycles / CD frame (75 Hz) — wired when the
scheduler is added (phase D). The sub-CPU is reused from `chips/cpu/m68000`
(instruction-stepped; boots from the `$0/$4` vectors on `reset`).
