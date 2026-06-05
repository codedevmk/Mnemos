# segacd — Sega CD (Mega CD) system

`mnemos::manifests::segacd` — the Sega CD sub side, layered on the Genesis
(M8 family). Ported from the Emu reference `systems/sega/segacd` per ADR-0006 /
[[mnemos-port-emu-cores]] and [[segacd-port]]. Mirrors the `genesis_system`
hand-wired oracle pattern (chips as value members, heap-pinned, `assemble_*`
factory wires a `topology::bus`).

## Status (staged port)

- **B1 (done):** `segacd_system` core — 2nd m68000 (sub-CPU) + `topology::bus`
  sub-bus mapping PRG-RAM (512K), word RAM (256K, 2M mode), the RF5C164 PCM
  register + wave-RAM windows, and the BIOS read-overlay; `run_cycles` /
  `release_sub_reset` / `reset`. Tested standalone: the sub-CPU boots from the
  PRG-RAM `$0/$4` vectors and runs (writes word RAM), bus maps reach PCM + RAM,
  reset gating + BIOS overlay behave.
- **B2 (next):** gate-array registers ($FF8000 / $A12000), word-RAM 2M↔1M/1M
  banking + RET/DMNA ownership, backup RAM ($FE0000 odd-lane), PRG-RAM bank
  window, inter-CPU comm registers.
- **B3:** sub-CPU IRQ controller (levels 1-6, mask, priority).
- **C:** CDD drive + CDC (LC8951) decoder/DMA + CD-DA + stamp ASIC.
- **D (⚠ APPROVAL GATE — touches the shipping Genesis):** main-side $A12000
  bridge + $000000-$03FFFF/$200000-$23FFFF windows, BIOS/region, sub-CPU
  scheduling, player `.cue/.iso` routing + `segacd` family, boot parity.

## Sub-CPU memory map (so far)

| Range | Backing |
|---|---|
| `$000000-$07FFFF` | PRG-RAM (512K). BIOS read-overlay on `$000000-$(bios-1)`. |
| `$080000-$0BFFFF` | Word RAM (256K, 2M mode). |
| `$FF0000-$FF0FFF` | RF5C164 registers ($00-$08). |
| `$FF1000-$FF1FFF` | RF5C164 wave-RAM window (bank-selected). |

Sub-CPU clock 12.5 MHz, 166,666 cycles / CD frame (75 Hz) — wired when the
scheduler is added (phase D). The sub-CPU is reused from `chips/cpu/m68000`
(instruction-stepped; boots from the `$0/$4` vectors on `reset`).
