# Mnemos hardware parity-gap inventory & tracking checklist (2026-06-11)

Actionable checklist of every **missing or incomplete** hardware implementation in
Mnemos required to reach 100% hardware parity. Companion to
[`progress-analysis.md`](progress-analysis.md) — that file has the full per-component
parity tables and the Critical Risk Register (§5); this file is the trackable to-do
form. Each item carries a stable ID (e.g. `X1`) for reference in commits/PRs, and the
`R#` cross-reference to the risk register where one exists.

> **Porting mandate (ADR-0006 §1 · proposed ADR-0024):** every Emu→Mnemos port is a
> **re-architecture to Mnemos-or-better standards**, never a transcription of Emu C.
> Items here name *what* to port (behaviour); the *how* follows the mandate.

## How to use

- Tick `[x]` when the item is merged to master; reference the item ID in the PR title.
- Use `[~]` for in-progress.
- **Scope:** hardware only. Excludes anything Mnemos already does at FULL/EXCEEDS, and
  the developer/debug/game-dev tooling axis (GUI, disassemblers, scripting, save-state
  wiring, the C64 dev suite) — tracked in
  [`tooling-gap-inventory.md`](tooling-gap-inventory.md).
- **Do not regress toward Emu's stubs** (risk-register R19): where Mnemos already
  exceeds Emu (Sega CD stamp/1M, M72 rendering, 32X threading/PWM), the target is real
  hardware, not Emu. Items below are genuine gaps, not "make it look like Emu".

## Legend

- **State:** MISSING (absent) · PARTIAL (present but incomplete)
- **Sev:** CRIT · HIGH · MED · LOW (impact on game compatibility / development)
- **Effort:** S (small) · M (medium) · L (large)
- **Type:** `vs Emu` (Emu has it, Mnemos doesn't) · `beyond Emu` (real-hardware
  completeness neither has — needed for true 100%, not an Emu-parity gap)

## Progress

- **Implemented-system hardware items: 0 / 28 complete** — 2 CRIT · 8 HIGH · 12 MED · 6 LOW
- **Unbuilt systems: 0 / 9 complete**

---

## Sega 32X — 0 / 8

The heaviest cluster and the shortest path to 32X correctness: the SH-2 timing tail +
on-chip interrupt delivery. This is why Star Wars / Space Harrier / After Burner stay
black. Ties to the hard-problems board (SH-2 cycle-true).

#### CPU — core / timing
- [ ] **X2** Per-instruction cycle-accurate SH-2 timing (currently 1-cycle base, ADR-0011 deferral) · PARTIAL · **CRIT** · L · vs Emu · R2
- [ ] **X1** SH-2 address-error exception (vec 9, misaligned word/long access) · MISSING · HIGH · M · vs Emu · R6
- [ ] **X3** SH-2 ↔ SH-2 bus-lock / contention stall timing · MISSING · HIGH · L · vs Emu · R2

#### CPU — on-chip peripherals
- [ ] **X4** INTC full interrupt delivery — raise DMAC-end, DIVU-OVFI, WDT, SCI (today **only FRT fires**) · PARTIAL · **CRIT** · M · vs Emu · R1
- [ ] **X5** SCI serial controller (SMR/BRR/SCR/SSR/TDR/RDR + ERI/RXI/TXI/TEI) · MISSING · MED · M · vs Emu · R10
- [ ] **X6** DMAC transfer-end IRQ + DREQ + bus-wait metering (TE is set; IRQ/timing deferred) · PARTIAL · MED · M · vs Emu · R11
- [ ] **X7** WDT reset + interval-timer interrupt (ITI) · PARTIAL · MED · S · vs Emu · R11
- [ ] **X8** DIVU busy-cycle model + overflow-interrupt delivery (math done; instant completion, IRQ storage-only) · PARTIAL · MED · S · vs Emu

> Done (no action): SH-2 ISA (60 mnemonics), both CPUs (threaded), all VDP modes,
> palette/autofill/double-buffer, PWM, comm/adapter bridge, VINT/HINT/CMD/PWM, MARS, FRT.

---

## Genesis / Mega Drive — 0 / 7

#### CPU
- [ ] **G1** M68000 address-error / bus-error (group-0) exceptions *(also fixes the Sega CD sub-CPU — shared core)* · MISSING · HIGH · M · vs Emu · R7

#### Audio
- [ ] **G4** Per-console-revision mix model (model-1/2 mix gains + low-pass cutoff) · MISSING · MED · M · vs Emu · R13

#### Cartridge / mapper
- [ ] **G2** Sonic & Knuckles lock-on passthrough ($200000–3FFFFF intercept) · MISSING · MED · S–M · vs Emu · R12
- [ ] **G3** SVP coprocessor (Virtua Racing) — $390000 bus windows **+ real SSP1601 DSP core** (Emu's DSP is itself a stub) · MISSING · MED · L · vs Emu · R12

#### Peripherals / controllers
- [ ] **G5** J-Cart extra controller ports (4-player) · MISSING · MED · S · vs Emu · R12
- [ ] **G6** Multitap (4-Way Play / Team Player), Menacer & Justifier light guns, Sega Mouse · MISSING · LOW · M · beyond Emu

#### System / save
- [ ] **G7** Whole-system save state — assemble existing per-chip serialization into a machine path · PARTIAL · HIGH · M · vs Emu · R8

> Done: M68000 (except G1), Z80, VDP (render + FIFO + DMA timing), YM2612, SN76489,
> SRAM/EEPROM/SSF2 banking, region/IO, Z80 bus arbitration.

---

## SMS + Game Gear — 0 / 5

#### Audio
- [ ] **S1** YM2413 FM Sound Unit (Japanese SMS FM, ports $F0/$F1/$F2) · MISSING · HIGH · M · vs Emu · R4

#### Peripherals / IO
- [ ] **S3** Pause button → Z80 NMI wiring (NMI exists on the core; not wired at the system level) · MISSING · MED · S · vs Emu · R14
- [ ] **S4** Light Phaser, Sports Pad, Paddle controllers (scaffolded "ready", not confirmed complete) · PARTIAL · LOW · M · beyond Emu

#### Mapper / storage
- [ ] **S2** Sega-mapper $8000–$BFFF cart-RAM bank-select — verify / complete · PARTIAL · LOW · S · vs Emu · R15

#### System / glue
- [ ] **S5** Deep cart-header validation (checksum / product-code / claimed-size) · PARTIAL · LOW · S · vs Emu · R15

> Done (mostly exceeding Emu): Z80, SMS VDP, GG VDP (12-bit CRAM + crop), SN76489 (+GG
> stereo), all 8 mappers, 93C46 saves, GG $00–$06 handset, PAL/NTSC switch, controller IO.

---

## Sega CD — 0 / 2

#### Disc / media
- [ ] **D1** CHD compressed disc reader (v5 codec stack: cdfl / cdlz / cdzl / huff) — `.chd` currently returns `nullopt` · MISSING · HIGH · L · vs Emu · R3
- [ ] **D2** ISO 9660 file-system walker (PVD parse + directory records) · MISSING · MED · M · vs Emu · R16

> Done (console hardware exceeds Emu): sub-CPU, gate array, word-RAM 2M+1M, CDC, CDD,
> CD-DA, RF5C68, stamp/rotation ASIC, font expander, ECC, comm protocol, interrupts,
> backup RAM, BIOS, CUE/BIN/ISO. Saturn IP.BIN parser is out of scope here (Saturn-only).
> Note: the sub-CPU inherits **G1** (address-error) when the shared 68K core gains it.

---

## Irem M72 — 0 / 3

All "beyond Emu" — Emu's M72 is a non-rendering scaffold, so these are board-family
hardware completeness, not Emu-parity gaps.

#### CPU
- [ ] **M1** mcs51 (8051) protection MCU — validate / complete for protected sets (wired but dormant; R-Type needs none) · PARTIAL · HIGH · M · beyond Emu · R9

#### System / variants
- [ ] **M3** Additional M72 board variants beyond R-Type (`board_params` framework exists, roster has only `rtype`) · PARTIAL · MED · M–L per game · beyond Emu

#### Mapper / ROM
- [ ] **M2** Z80 $8000 sound-ROM banking · MISSING · MED · S · beyond Emu

> Done (exceeds Emu): V30, Z80 (shared-RAM boot handshake), full video (tilemaps /
> sprites / palette), YM2151, DAC/PCM sample playback, 8259 PIC, raster compare,
> shared sound RAM, inputs/DIPs.

---

## C64 — 0 / 3

Hardware only. The large C64 delta vs Emu is the ~4,400 LOC developer-tooling suite,
which is **out of scope** for this hardware inventory.

#### Storage
- [ ] **C1** 1541 GCR read path — harden / prove (self-flagged "still being proven" in code) · PARTIAL · HIGH · M · vs Emu · R5
- [ ] **C2** Datasette tape **write / save** (currently read-only `.tap`) · MISSING · LOW · M · beyond Emu

#### Mapper / cartridge
- [ ] **C3** Additional niche CRT cartridge + RAM-expansion types (Mnemos has 10 of dozens; GeoRAM etc.) · PARTIAL · LOW · varies · beyond Emu

> Done: 6510, VIC-II, SID (+dual-SID), both CIAs, PLA, 1541 architecture, IEC, REU,
> modem/RS-232, cartridge banking. C64 hardware is essentially at parity.

---

## Unbuilt systems — 0 / 9

Entirely absent from Mnemos. Domains to build per system; ordered easiest → hardest.
"✓" = the needed CPU/chip already exists in Mnemos and is reusable.

- [ ] **Spectrum** — *LOW.* CPU: Z80 ✓ · Video: ULA (inline) · Audio: AY-3-8910 · Glue: 48K contention / Timex TC2048/TS2068/TC2068 models. Easiest win.
- [ ] **CPS1** — *MED.* CPU: m68000 ✓ + z80 ✓ · Video: CPS-A/B GFX (bespoke, inline in Emu) · Audio: ym2151 ✓ + MSM6295 + QSound · Glue: ZIP set loading.
- [ ] **NES** — *MED.* CPU: 2A03 variant + glue (Mnemos m6510 ≠ 2A03) · Video: ppu2c02 · Audio: ricoh_2a03_apu · Glue: real mappers (MMC1/3…) beyond NROM.
- [ ] **CPS2** — *MED–HIGH.* CPU: m68000 ✓ + z80 ✓ · Video: reuse CPS1 path · Audio: QSound · Glue: CPS-2 keyed opcode decryption.
- [ ] **Amiga** — *HIGH.* CPU: m68000 ✓ · Video: agnus + denise · Audio: paula · Glue: cia8520, copper, floppy (Emu chip shells are shallow OCS/ECS — need finishing).
- [ ] **NeoGeo** — *HIGH.* CPU: m68000 ✓ + z80 ✓ · Video: **LSPC (greenfield — exists nowhere)** · Audio: ym2610 (ssg + adpcm_a + adpcm_b). Emu is a scaffold.
- [ ] **Taito F2** — *HIGH.* CPU: m68000 ✓ + z80 ✓ · Video: **TC0100SCN + TC0200OBJ customs (greenfield)** · Audio: ym2610. Emu is a scaffold (197 LOC).
- [ ] **SNES** — *VERY HIGH.* CPU: 65c816 · Video: s_ppu · Audio: spc700 + s_dsp · Glue: CPU IRQ servicing, DMA/HDMA, HiROM. Emu CPU is a scaffold.
- [ ] **Saturn** — *VERY HIGH.* CPU: sh2 ×2 ✓ + m68000 ✓ · Video: vdp1 + vdp2 · Audio: scsp + scu_dsp · Glue: SCU/SMPC/CD-block scheduler. Multi-month.

---

## Suggested critical path (correctness before breadth)

1. **32X timing/INTC cluster** — X4, X2, X1, X3 (2 CRIT + 2 HIGH in one subsystem).
2. **Sega CD CHD** — D1 (unblocks the common modern disc format).
3. **SMS YM2413** — S1 (restores FM audio; self-contained).
4. **Address-error exceptions** — G1 + X1 (shared 68K + SH-2 work; G1 also fixes Sega CD sub-CPU).
5. **Genesis whole-system save state** — G7 (unlocks save-state/rewind).
6. **C64 1541 GCR hardening** — C1 (gates disk-based software).
7. **Breadth, by ROI** — Spectrum → CPS1 → NES (6 → 9 systems for modest effort).
