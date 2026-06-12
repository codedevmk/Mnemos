# Mnemos hardware parity-gap inventory & tracking checklist (2026-06-11)

Actionable checklist of **missing or incomplete** hardware implementation found
by the 2026-06-11 parity audit. Companion to
[`progress-analysis.md`](progress-analysis.md) — that file has the full
per-component parity tables and the Critical Risk Register (§5); this file is
the trackable backlog form, not an oracle result or measured pass-rate report.
Each item carries a stable ID (e.g. `X1` or `U1`) for reference in commits/PRs,
and the `R#` cross-reference to the risk register where one exists.

> **Active porting mandate (ADR-0006 §1):** every Emu→Mnemos port is
> restructured into Mnemos C++23 conventions and is never copied wholesale.
> Proposed ADR-0024 sharpens this into an explicit "Mnemos-or-better"
> re-architecture rule, but remains non-binding until ratified. Items here name
> *what* to port or prove (behaviour); the *how* follows accepted authority.

## How to use

- Tick `[x]` when the item is merged to master; reference the item ID in the PR title.
- Use `[~]` for in-progress.
- **Scope:** hardware only. Excludes anything Mnemos already does at FULL/EXCEEDS, and
  the developer/debug/game-dev tooling axis (GUI, disassemblers, scripting,
  the C64 dev suite) — tracked in
  [`tooling-gap-inventory.md`](tooling-gap-inventory.md). Cross-axis
  save-state/runtime items stay here only when they gate deterministic
  hardware-state parity, and are mirrored to tooling with `⇄ T#`.
- **Do not regress toward Emu's stubs** (risk-register R19): where Mnemos already
  exceeds Emu (Sega CD stamp/1M, M72 rendering, 32X threading/PWM), the target is real
  hardware, not Emu. Items below are genuine gaps, not "make it look like Emu".

## Legend

- **State:** MISSING (absent) · PARTIAL (present but incomplete)
- **Sev:** CRIT · HIGH · MED · LOW (impact on game compatibility / development)
- **Effort:** S (small) · M (medium) · L (large)
- **Type:** `vs Emu` (Emu has it, Mnemos doesn't) · `beyond Emu` (real-hardware
  completeness neither has — needed for true 100%, not an Emu-parity gap)
- **Evidence:** short authority pointer for the row. `progress-analysis.md` §5
  entries are audit-derived estimates, not measured pass rates. Rows with only
  audit/source-comment evidence must be re-verified against current source and
  oracle status before implementation.

## Progress

- **Implemented-system hardware items: 0 / 28 complete** — 2 CRIT · 8 HIGH · 12 MED · 6 LOW
- **Unbuilt systems: 0 / 9 complete**

---

## Sega 32X — 0 / 8

The heaviest cluster and the shortest path to 32X correctness: the SH-2 timing tail +
on-chip interrupt delivery. This is why Star Wars / Space Harrier / After Burner stay
black. Ties to the hard-problems board (SH-2 cycle-true).

#### CPU — core / timing
- [~] **X2** Per-instruction cycle-accurate SH-2 timing — local slice wires fixed-state costs for delayed/non-delayed branches, TRAPA/RTE/SLEEP, system-register memory forms, TAS, MAC/multiply minima, and GBR byte-immediate ops; load-use/cache/bus waits and variable multiplier upper bounds remain · PARTIAL · **CRIT** · L · vs Emu · R2 · Evidence: `progress-analysis.md` R2 + `src/chips/cpu/sh2/sh2.cpp`
- [~] **X1** SH-2 address-error exception — local slices vector odd fetches, normal and delay-slot misaligned word/long data accesses, PC-relative on-chip loads, and SH7604 on-chip byte/long access-class faults through vector 9; cache/address-array spaces and stacking-fault recursion remain unmodeled · PARTIAL · HIGH · M · vs Emu · R6 · Evidence: `progress-analysis.md` R6 + `src/chips/cpu/sh2/sh2.cpp`
- [~] **X3** SH-2 ↔ SH-2 bus-lock / contention stall timing — local slices add a board-provided bus-wait hook, charge `TAS.B` locks on shared 32X SDRAM/framebuffer/COMM ranges, and deterministically arbitrate same-cycle dual-SH-2 TAS locks; ordinary memory waits, full DMA/VDP contention, and cache-hit penalties remain · PARTIAL · HIGH · L · vs Emu · R2 · Evidence: `progress-analysis.md` R2 + `src/chips/cpu/sh2/sh2.cpp` + `src/manifests/sega32x/sega32x_system.cpp`

#### CPU — on-chip peripherals
- [~] **X4** INTC full interrupt delivery — local slice wires DMAC-end, DIVU-OVFI, WDT ITI, and raw SCI flag delivery · PARTIAL · **CRIT** · M · vs Emu · R1 · Evidence: `progress-analysis.md` R1 + `src/chips/cpu/sh2/sh2_peripherals.hpp`
- [~] **X5** SCI serial controller — local slice models SMR/BRR/SCR/SSR/TDR/RDR defaults/storage, coarse TDR transmit completion flags, RDR receive/error/overrun latching through `sci_receive_byte`, and ERI/RXI/TXI/TEI delivery; exact BRR/baud pacing, serial pin/link integration, and full SSR read-clear semantics remain · PARTIAL · MED · M · vs Emu · R10 · Evidence: `progress-analysis.md` R10 + `src/chips/cpu/sh2/sh2_peripherals.hpp`
- [~] **X6** DMAC request/channel timing + bus-wait metering — local slices report per-unit source/destination bus waits through the board callback, pace cycle-steal as one transfer unit per tick, cap burst-mode units per tick (resuming until the block drains), arbitrate DMAOR fixed/round-robin channel priority, and latch normalized DREQ edges; external pin electrical timing/DACK handshakes and full 32X DMA/VDP/shared-bus contention policy remain · PARTIAL · MED · M · vs Emu · R11 · Evidence: `progress-analysis.md` R11 + `src/chips/cpu/sh2/sh2_peripherals.cpp`
- [~] **X7** WDT watchdog reset output — local slice sets RSTCSR.WOVF, resets WTCNT/WTCSR when RSTE is clear, and requests a power-on/manual SH-2 internal reset when RSTE is set; external 128-cycle WDTOVF pin pulse/system-reset wiring remains · PARTIAL · MED · S · vs Emu · R11 · Evidence: `progress-analysis.md` R11 + `src/chips/cpu/sh2/sh2_peripherals.cpp`
- [~] **X8** DIVU busy-cycle model — local slice delays normal results for 39 cycles and overflow results/OVFI for 6 cycles, serializes in-flight divides, and stalls SH-2 DIVU register accesses until completion; undefined sub-32-bit DIVU accesses, post-write one-cycle read extension, and module-stop behavior remain · PARTIAL · MED · S · vs Emu · Evidence: `src/chips/cpu/sh2/sh2_peripherals.hpp`

> Done (no action): SH-2 ISA (60 mnemonics), both CPUs (threaded), all VDP modes,
> palette/autofill/double-buffer, PWM, comm/adapter bridge, VINT/HINT/CMD/PWM, MARS, FRT.

---

## Genesis / Mega Drive — 0 / 7

#### CPU
- [ ] **G1** M68000 address-error / bus-error (group-0) exceptions *(also fixes the Sega CD sub-CPU — shared core)* · MISSING · HIGH · M · vs Emu · R7 · Evidence: `progress-analysis.md` R7 + `src/chips/cpu/m68000/tests/m68000_conformance_test.cpp`

#### Audio
- [ ] **G4** Per-console-revision mix model (model-1/2 mix gains + low-pass cutoff) · MISSING · MED · M · vs Emu · R13 · Evidence: `progress-analysis.md` R13

#### Cartridge / mapper
- [ ] **G2** Sonic & Knuckles lock-on passthrough ($200000–3FFFFF intercept) · MISSING · MED · S–M · vs Emu · R12 · Evidence: `progress-analysis.md` R12
- [ ] **G3** SVP coprocessor (Virtua Racing) — $390000 bus windows **+ real SSP1601 DSP core** (Emu's DSP is itself a stub) · MISSING · MED · L · vs Emu · R12 · Evidence: `progress-analysis.md` R12

#### Peripherals / controllers
- [ ] **G5** J-Cart extra controller ports (4-player) · MISSING · MED · S · vs Emu · R12 · Evidence: `progress-analysis.md` R12
- [ ] **G6** Multitap (4-Way Play / Team Player), Menacer & Justifier light guns, Sega Mouse · MISSING · LOW · M · beyond Emu · Evidence: `progress-analysis.md` Genesis controller gap

#### System / save
- [ ] **G7** Whole-system deterministic save target — assemble existing per-chip serialization into a Genesis machine path · PARTIAL · HIGH · M · vs Emu · R8 · ⇄ T4 · Evidence: `progress-analysis.md` R8 + `tooling-gap-inventory.md` T4

> Done: M68000 (except G1), Z80, VDP (render + FIFO + DMA timing), YM2612, SN76489,
> SRAM/EEPROM/SSF2 banking, region/IO, Z80 bus arbitration.

---

## SMS + Game Gear — 0 / 5

#### Audio
- [ ] **S1** YM2413 FM Sound Unit (Japanese SMS FM, ports $F0/$F1/$F2) · MISSING · HIGH · M · vs Emu · R4 · Evidence: `progress-analysis.md` R4

#### Peripherals / IO
- [ ] **S3** Pause button → Z80 NMI wiring (NMI exists on the core; not wired at the system level) · MISSING · MED · S · vs Emu · R14 · Evidence: `progress-analysis.md` R14
- [ ] **S4** Light Phaser, Sports Pad, Paddle controllers (scaffolded "ready", not confirmed complete) · PARTIAL · LOW · M · beyond Emu · Evidence: `progress-analysis.md` SMS peripheral notes

#### Mapper / storage
- [ ] **S2** Sega-mapper $8000–$BFFF cart-RAM bank-select — verify / complete · PARTIAL · LOW · S · vs Emu · R15 · Evidence: `progress-analysis.md` R15

#### System / glue
- [ ] **S5** Deep cart-header validation (checksum / product-code / claimed-size) · PARTIAL · LOW · S · vs Emu · R15 · Evidence: `progress-analysis.md` R15

> Done (mostly exceeding Emu): Z80, SMS VDP, GG VDP (12-bit CRAM + crop), SN76489 (+GG
> stereo), all 8 mappers, 93C46 saves, GG $00–$06 handset, PAL/NTSC switch, controller IO.

---

## Sega CD — 0 / 2

#### Disc / media
- [ ] **D1** CHD compressed disc reader (v5 codec stack: cdfl / cdlz / cdzl / huff) — `.chd` currently returns `nullopt` · MISSING · HIGH · L · vs Emu · R3 · Evidence: `progress-analysis.md` R3
- [ ] **D2** ISO 9660 file-system walker (PVD parse + directory records) · MISSING · MED · M · vs Emu · R16 · Evidence: `progress-analysis.md` R16

> Done (console hardware exceeds Emu): sub-CPU, gate array, word-RAM 2M+1M, CDC, CDD,
> CD-DA, RF5C68, stamp/rotation ASIC, font expander, ECC, comm protocol, interrupts,
> backup RAM, BIOS, CUE/BIN/ISO. Saturn IP.BIN parser is out of scope here (Saturn-only).
> Note: the sub-CPU inherits **G1** (address-error) when the shared 68K core gains it.

---

## Irem M72 — 0 / 3

All "beyond Emu" — Emu's M72 is a non-rendering scaffold, so these are board-family
hardware completeness, not Emu-parity gaps.

#### CPU
- [ ] **M1** mcs51 (8051) protection MCU — validate / complete for protected sets (wired but dormant; R-Type needs none) · PARTIAL · HIGH · M · beyond Emu · R9 · Evidence: `progress-analysis.md` R9

#### System / variants
- [ ] **M3** Additional M72 board variants beyond R-Type (`board_params` framework exists, roster has only `rtype`) · PARTIAL · MED · M–L per game · beyond Emu · Evidence: `progress-analysis.md` M72 board roster

#### Mapper / ROM
- [ ] **M2** Z80 $8000 sound-ROM banking · MISSING · MED · S · beyond Emu · Evidence: `progress-analysis.md` R9 / M72 shared gap

> Done (exceeds Emu): V30, Z80 (shared-RAM boot handshake), full video (tilemaps /
> sprites / palette), YM2151, DAC/PCM sample playback, 8259 PIC, raster compare,
> shared sound RAM, inputs/DIPs.

---

## C64 — 0 / 3

Hardware only. The large C64 delta vs Emu is the ~4,400 LOC developer-tooling suite,
which is **out of scope** for this hardware inventory.

#### Storage
- [ ] **C1** 1541 GCR read path — harden / prove (self-flagged "still being proven" in code) · PARTIAL · HIGH · M · vs Emu · R5 · Evidence: `progress-analysis.md` R5
- [ ] **C2** Datasette tape **write / save** (currently read-only `.tap`) · MISSING · LOW · M · beyond Emu · Evidence: `progress-analysis.md` C64 datasette notes

#### Mapper / cartridge
- [ ] **C3** Additional niche CRT cartridge + RAM-expansion types (Mnemos has 10 of dozens; GeoRAM etc.) · PARTIAL · LOW · varies · beyond Emu · Evidence: `progress-analysis.md` C64 cartridge notes

> Done: 6510, VIC-II, SID (+dual-SID), both CIAs, PLA, 1541 architecture, IEC, REU,
> modem/RS-232, cartridge banking. C64 hardware is essentially at parity.

---

## Unbuilt systems — 0 / 9

Entirely absent from Mnemos. Domains to build per system; ordered easiest → hardest.
"✓" = a reusable implementation exists in Mnemos, not that the chip is already
parity-grade for the new machine. Listed prerequisites still apply.

- [ ] **U1 Spectrum** — *LOW.* CPU: Z80 ✓ · Video: ULA (inline) · Audio: AY-3-8910 · Glue: 48K contention / Timex TC2048/TS2068/TC2068 models. Easiest win. · Evidence: `progress-analysis.md` §4
- [ ] **U2 CPS1** — *MED.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: CPS-A/B GFX (bespoke, inline in Emu) · Audio: ym2151 ✓ + MSM6295 + QSound · Glue: ZIP set loading. · Evidence: `progress-analysis.md` §4
- [ ] **U3 NES** — *MED.* CPU: 2A03 variant + glue (Mnemos m6510 ≠ 2A03) · Video: ppu2c02 · Audio: ricoh_2a03_apu · Glue: real mappers (MMC1/3…) beyond NROM. · Evidence: `progress-analysis.md` §4
- [ ] **U4 CPS2** — *MED–HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: reuse CPS1 path · Audio: QSound · Glue: CPS-2 keyed opcode decryption. · Evidence: `progress-analysis.md` §4
- [ ] **U5 Amiga** — *HIGH.* CPU: m68000 ✓ (G1 applies) · Video: agnus + denise · Audio: paula · Glue: cia8520, copper, floppy (Emu chip shells are shallow OCS/ECS — need finishing). · Evidence: `progress-analysis.md` §4
- [ ] **U6 NeoGeo** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **LSPC (greenfield — exists nowhere)** · Audio: ym2610 (ssg + adpcm_a + adpcm_b). Emu is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U7 Taito F2** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **TC0100SCN + TC0200OBJ customs (greenfield)** · Audio: ym2610. Emu is a scaffold (197 LOC). · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U8 SNES** — *VERY HIGH.* CPU: 65c816 · Video: s_ppu · Audio: spc700 + s_dsp · Glue: CPU IRQ servicing, DMA/HDMA, HiROM. Emu CPU is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U9 Saturn** — *VERY HIGH.* CPU: sh2 ×2 ✓ (X1/X2/X4 apply) + m68000 ✓ (G1 applies) · Video: vdp1 + vdp2 · Audio: scsp + scu_dsp · Glue: SCU/SMPC/CD-block scheduler. Multi-month. · Evidence: `progress-analysis.md` §4

---

## Suggested critical path (correctness before breadth)

1. **32X timing/INTC cluster** — X4, X2, X1, X3 (2 CRIT + 2 HIGH in one subsystem).
2. **Sega CD CHD** — D1 (unblocks the common modern disc format).
3. **SMS YM2413** — S1 (restores FM audio; self-contained).
4. **Address-error exceptions** — G1 + X1 (shared 68K + SH-2 work; G1 also fixes Sega CD sub-CPU).
5. **Genesis whole-system deterministic save target** — G7 ⇄ T4 (unlocks save-state/rewind).
6. **C64 1541 GCR hardening** — C1 (gates disk-based software).
7. **Breadth, by ROI** — Spectrum → CPS1 → NES (6 → 9 systems for modest effort).
