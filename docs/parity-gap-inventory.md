# Mnemos hardware parity-gap inventory & tracking checklist (2026-06-11; re-audited 2026-06-12, 2026-06-13)

Actionable checklist of **missing or incomplete** hardware implementation found
by the 2026-06-11 parity audit. Companion to
[`progress-analysis.md`](progress-analysis.md) — that file has the full
per-component parity tables and the Critical Risk Register (§5); this file is
the trackable backlog form, not an oracle result or measured pass-rate report.
Each item carries a stable ID (e.g. `X1` or `U1`) for reference in commits/PRs,
and the `R#` cross-reference to the risk register where one exists.

> **Re-audit 2026-06-12:** every row re-verified against current source (the
> 32X X-items through PR #136). All confirmed accurate except **S2**, whose
> wording was freshened — the SMS cart-RAM bank-select is implemented and
> unit-tested, not merely "to verify". Counters (5/28, 0/9) and the severity
> tally are unchanged; the fix is wording, not a state change.

> **Update 2026-06-13 (the SH-2 X2/X3 cycle-true campaign closed at the opt-in
> stage):** **X2 and X3 flip `[~]`→`[x]`** (32X 6/8 → **8/8**; implemented 6/28 →
> **8/28**). The campaign (ADR-0026) resolved the long-standing "needs a
> cycle-accurate reference" blocker by making the **official manuals the authority**
> (Emu/Ymir are L5 cross-check only, never an expected value): X2 internal+load-use
> timing was validated complete vs the SH-1/SH-2 PM, X3 grew region access-cycle
> timing + a per-resource SH-2↔SH-2/DMAC contention model, and the SH7604 cache got
> an operand + instruction-fetch hit/miss timing shadow (PRs #139, #141-143, #146,
> #147, #148). All ship **OPT-IN, default-off → the 32X stays bit-identical**. The
> **default-ON flip (Z8) is deliberately deferred**, not a remaining gap: there is
> no cycle-accurate reference to validate a flip and no 32X frame-hash parity
> harness to A/B it, so opt-in is the honest end state. The multiplier-contention
> upper bound (X2) stays unmodeled — its magnitude has no manual grounding.

> **Update 2026-06-14 (Genesis VDP Mode-4 viewport geometry, PR #153):** a new
> Genesis item **G8** is tracked. The VDP reported `field_height` (224/240) as the
> framebuffer height regardless of **M5** (reg1 bit2); with M5 clear the hardware
> uses the legacy **192-line** viewport. PR #153 gates `visible_height()` on M5
> (192 when M5 clear) — a pure host-facing geometry change (`framebuffer()`-only),
> leaving the M5-independent raster-timing height untouched so the universal boot
> M5 0→1 transition does not perturb VBL/VINT phase. Full 2784 sweep vs a
> *current-master* baseline: **+6 byte-perfect, 0 regressions**. G8 stays `[~]`:
> full Mode-4 *content* rendering remains (deferred — a 2026-06-14 scope found NO
> corpus game renders M5-clear+display-on content, and a faithful implementation
> needs GPGX's high-risk 64K↔16K VRAM re-decode that runs on every boot). (Methodology
> note: the pinned `results_on.txt` baseline is weeks stale; isolating a change now
> requires a same-tree current-HEAD baseline player, not that file.)

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

- **Implemented-system hardware items: 8 / 29 complete** — 2 CRIT · 8 HIGH · 12 MED · 7 LOW
  (X2 + X3 completed 2026-06-13 as opt-in models; G8 added 2026-06-14 as `[~]`; see the update notes above)
- **New-system backlog: 0 / 9 parity-closed**

---

## Sega 32X — 8 / 8

All eight tracked SH-2 / 32X hardware items are implemented. The SH-2 cycle-true
timing tail (X2/X3) closed 2026-06-13 as **opt-in** models (ADR-0026,
manual-grounded; default-on deferred — no validation reference). The remaining 32X
correctness work is the boot/feature chain that keeps Star Wars / Space Harrier /
After Burner black — those are boot-sequence + VDP/feature bugs (tracked on the
hard-problems board / per-title tasks), NOT the opt-in timing tail.

#### CPU — core / timing
- [x] **X2** Per-instruction cycle-accurate SH-2 timing — **complete as an opt-in model (ADR-0026, manual-grounded).** Fixed-state costs for delayed/non-delayed branches, TRAPA/RTE/SLEEP, system-register memory forms, TAS, MAC/multiply minima, and GBR byte-immediate ops are wired and **validated complete vs the SH-1/SH-2 Programming Manual** (the only fix was the delayed-branch fold undercount — BRA/BSR/JMP/JSR/RTS=3, RTE=5, BT/S BF/S=3 taken; PR #139). The **load-use interlock** (`MNEMOS_32X_LOAD_USE`, PRs #134/#136) adds the +1 cycle when an instruction reads a GPR the previous one loaded, incl. the `LDC.L @Rn+,SR`→T-consumer case, and **cache-miss timing is now modelled** (the SH7604 operand + instruction-fetch cache shadow, PRs #146/#147). The reference blocker is RESOLVED by ADR-0026 (manuals are authority; Emu/Ymir L5 cross-check only); a manual-derived **cycle** conformance harness (`MNEMOS_SH2_CYCLE_TESTS_DIR`) joins the functional one (PR #131). Ships **OPT-IN** (default-off = bit-identical); default-ON is deferred by decision (no cycle-accurate reference / no parity harness). Only the multiplier-contention upper bound stays unmodelled — its magnitude has no manual grounding · DONE (opt-in) · **CRIT** · L · vs Emu · R2 · Evidence: `progress-analysis.md` R2 + `src/chips/cpu/sh2/sh2.cpp` + `src/chips/cpu/sh2/tests/sh2_cycle_conformance_test.cpp` + `docs/plans/2026-06-12-sh2-x2-x3-cycle-true.md`
- [x] **X1** SH-2 address-error exception — vectors odd fetches, normal and delay-slot misaligned word/long data accesses, PC-relative on-chip loads, SH7604 on-chip byte/long access-class faults, the stacking fault on a misaligned exception-frame SP (diverts to vector 9 once, no recurse/reset), and byte/word/PC-relative/TAS access to the cache purge (`$40000000`) and address-array (`$60000000`, longword-only) spaces — all through vector 9. The `$C0000000` data array (32X cache-as-RAM scratch) is correctly excluded · DONE · HIGH · M · vs Emu · R6 · Evidence: `src/chips/cpu/sh2/sh2.cpp`
- [x] **X3** SH-2 ↔ SH-2 bus-lock / contention stall timing — **complete as an opt-in model.** A board bus-wait hook charges `TAS.B` locks on shared 32X ranges; the opt-in model (`MNEMOS_32X_BUS_CONTENTION`, PRs #133/#141-143) adds **region access-cycle timing** (VDP regs = 5/word, SDRAM write = 2/word, COMM = 1, SDRAM read = the 12-clock cache line-fill burst) via a `data_access_kind` enum + address-matched store re-tag, and **per-resource cross-CPU + DMAC contention** (PR #148): the reservation is held per hardware block (SDRAM / frame buffer / VDP / COMM), so distinct-block accesses on the two SH-2s do not falsely serialize; cross-CPU ties resolve deterministically (master before slave; a CPU's instruction before its own DMAC). The 68000 and 32X VDP as bus masters are scoped-out + logged (not a silent cap); the intra-instruction MA offset is not modelled. Ships **OPT-IN** for the same reason as X2 (no cycle-accurate reference); default-ON deferred by decision · DONE (opt-in) · HIGH · L · vs Emu · R2 · Evidence: `progress-analysis.md` R2 + `src/chips/cpu/sh2/sh2.cpp` + `src/manifests/sega32x/sega32x_system.cpp`

#### CPU — on-chip peripherals
- [x] **X4** INTC full interrupt delivery — the on-chip INTC arbitrates and delivers FRT, DIVU-OVFI, DMAC transfer-end, WDT ITI, and SCI ERI/RXI/TXI/TEI by IPRA/IPRB priority + VCR vectors (a zero VCR is a valid vector, masked only by IPR). The 32X's VINT/HINT/CMD/PWM are the manifest's external `set_irq`/IRL path and NMI has no Mars consumer · DONE · **CRIT** · M · vs Emu · R1 · Evidence: `src/chips/cpu/sh2/sh2_peripherals.cpp` `pending_onchip_irq`
- [x] **X5** SCI serial controller — register-visible SMR/BRR/SCR/SSR/TDR/RDR, RDR receive/error/overrun via `sci_receive_byte`, ERI/RXI/TXI/TEI delivery, hardware-accurate SSR read-then-write-0 clear (TEND read-only), frame-level BRR/SMR transmit timing, and the master↔slave serial LINK (a completed TX frame delivers TDR to the peer's RX; wired in the manifest). Bit-level SCK waveform + external-clock peer gating are approximated at frame level (ample — titles use COMM, not SCI) · DONE · MED · M · vs Emu · R10 · Evidence: `src/chips/cpu/sh2/sh2_peripherals.cpp` + `src/manifests/sega32x/sega32x_system.cpp`
- [x] **X6** DMAC request/channel timing + bus-wait metering — local slices report per-unit source/destination bus waits through the board callback, pace cycle-steal as one transfer unit per tick, cap burst-mode units per tick (resuming until the block drains), arbitrate DMAOR fixed/round-robin channel priority, and latch normalized DREQ edges. The handshake tail is now modelled and unit-tested: DRCR0/1 request-source routing (DREQ / SCI RXI / SCI TXI gated on SCR.RIE/TIE; a DMAC RDR read / TDR write auto-clears RDRF/TDRE per the SCI chapter), the CHCR.AL/AM DACK handshake (per-unit strobe callback, AL-resolved polarity, AM-selected cycle), single-address (CHCR.TA) transfers with a board device port (external-request-only per manual §9.3.4), the CHCR.TE + DMAOR.NMIF/AE read-then-write-0 clear protocol, and DMAC access routing through the on-chip window (SCI-driven transfers reach RDR/TDR). Per-bus-cycle DACK electrical timing is below the abstraction and CHCR.DL is absorbed by the normalized-request contract (ample — the 32X wires no DREQ/DACK pins; its DMA is the 68000→SH-2 FIFO module-request path). Multi-master shared-bus contention is X3's arbiter, tracked there · DONE · MED · M · vs Emu · R11 · Evidence: `src/chips/cpu/sh2/sh2_peripherals.cpp` + `src/chips/cpu/sh2/tests/sh2_test.cpp`
- [x] **X7** WDT watchdog reset output — sets RSTCSR.WOVF, resets WTCNT/WTCSR when RSTE is clear, requests a power-on/manual SH-2 internal reset when RSTE is set (the 32X machine rebases its pacing anchors across that self-reset), and low-pulses the external WDTOVF pin for 128 cycles (pollable via `wdtovf_pin_asserted()`; the 32X leaves the pin unconnected) · DONE · MED · S · vs Emu · R11 · Evidence: `src/chips/cpu/sh2/sh2_peripherals.cpp`
- [x] **X8** DIVU — 39/6-cycle busy model, serialized in-flight divides, register-access stall, the SH7604 write-then-read +1-cycle penalty, and SBYCR.MSTP2 ($FFFFFE91 bit 2) module-stop that freezes the divider clock. Sub-32-bit CPU access faults per the on-chip access class (hardware leaves the byte-lane value undefined, so no contract is made) · DONE · MED · S · vs Emu · Evidence: `src/chips/cpu/sh2/sh2_peripherals.cpp`

> Done (no action): SH-2 ISA (60 mnemonics), both CPUs (threaded), all VDP modes,
> palette/autofill/double-buffer, PWM, comm/adapter bridge, VINT/HINT/CMD/PWM, MARS, FRT.
>
> Verification: TWO data-gated harnesses — `sh2_conformance_test`
> (`MNEMOS_SH2_TESTS_DIR`) cross-checks the SH-2 ISA *semantics* against the public
> SH4 single-step corpus, and `sh2_cycle_conformance_test`
> (`MNEMOS_SH2_CYCLE_TESTS_DIR`, ADR-0026) checks per-instruction **cycle counts**
> against manual-derived vectors. The "cycle timing needs a cycle-accurate hardware
> reference" blocker is RESOLVED by ADR-0026 (the official manuals are the
> authority; Emu/Ymir are L5 cross-check only). X2/X3 cycle timing is modelled +
> validated against the manual and ships OPT-IN; only the default-ON flip stays
> deferred (it would need a 32X frame-hash parity harness to A/B safely).

---

## Genesis / Mega Drive — 0 / 8

#### CPU
- [ ] **G1** M68000 address-error / bus-error (group-0) exceptions *(also fixes the Sega CD sub-CPU — shared core)* · PARTIAL (core mechanics complete) · HIGH · M · vs Emu · R7 · Address-error vector 3 handles odd instruction fetch plus odd word/long data accesses; explicit topology BERR windows now drive bus-error vector 2; both build MC68000 group-0 frames. Concrete Genesis/Sega CD BERR address-map policy and prefetch-exact corpus parity remain open · Evidence: `progress-analysis.md` R7 + `src/chips/cpu/m68000/tests/m68000_test.cpp` + `src/topology/tests/bus_test.cpp` + `src/chips/cpu/m68000/tests/m68000_conformance_test.cpp`

#### Audio
- [ ] **G4** Per-console-revision mix model (model-1/2 mix gains + low-pass cutoff) · MISSING · MED · M · vs Emu · R13 · Evidence: `progress-analysis.md` R13

#### Cartridge / mapper
- [ ] **G2** Sonic & Knuckles lock-on passthrough ($200000–3FFFFF intercept) · MISSING · MED · S–M · vs Emu · R12 · Evidence: `progress-analysis.md` R12
- [ ] **G3** SVP coprocessor (Virtua Racing) — $390000 bus windows **+ real SSP1601 DSP core** (Emu's DSP is itself a stub) · MISSING · MED · L · vs Emu · R12 · Evidence: `progress-analysis.md` R12

#### Peripherals / controllers
- [ ] **G5** J-Cart extra controller ports (4-player) · MISSING · MED · S · vs Emu · R12 · Evidence: `progress-analysis.md` R12
- [ ] **G6** Multitap (4-Way Play / Team Player), Menacer & Justifier light guns, Sega Mouse · MISSING · LOW · M · beyond Emu · Evidence: `progress-analysis.md` Genesis controller gap

#### Video — legacy / SMS-compat mode
- [~] **G8** Mode 4 (M5-clear / legacy) display — the M5-clear **192-line viewport geometry** is now reported correctly: `visible_height()` returns 192 when reg1 bit2 (M5) is clear (Mode 5 is required for the 224/240-line display), a pure `framebuffer()`-only change that leaves the M5-independent raster-timing height untouched (folding it into timing would shift VBL/VINT on every boot). Fixes the M5-clear size-mismatch cluster — Clue [h1], Bubba N Stix [h2], Wrestle War [h2], SegaCD BIOS, Pro Action Replay ×2 (+6 byte-perfect, **0 regressions** on the full 2784 sweep vs a current-master baseline; PR #153). **Remaining (deferred):** full Mode-4 (SMS-compat) BG/sprite **content** rendering for the M5-clear + display-on case (Mnemos still composites Mode-5 planes from non-Mode-5 VRAM, and stores CRAM in the MD 9-bit form rather than the Mode-4 6-bit `00BBGGRR`/`pixel_lut_m4` path). A 2026-06-14 scope found **no corpus game** renders M5-clear + display-on content (it is purely a few-frame boot transient every game leaves), and a faithful implementation needs GPGX's 64K↔16K VRAM address re-decode (`vdp_ctrl.c:1773-1896`) that runs on **every** boot M5 0→1 — a corpus-wide regression risk disproportionate to the (cosmetic, idle-frame-only) benefit. A "M5-down-toggle stale-height latch" was considered and **rejected as a non-gap**: GPGX also collapses to 192 on M5-clear (`system.c:1080-1100`, just frame-deferred), so there is no stale-height divergence to model · PARTIAL · LOW · M · vs Emu · Evidence: `src/chips/video/genesis_vdp/genesis_vdp.cpp` `visible_height()` + `tests/genesis_vdp_test.cpp`

#### System / save
- [x] **G7** Whole-system deterministic save target — assemble existing per-chip serialization into a Genesis machine path. **Increment 1 done:** `genesis_system::save_state/load_state` serialize the non-chip board state (I/O sub-controller regs, VDP word latches, Z80 bus-arbitration lines + bank window, SRAM enable/WP, SSF2 banks, EEPROM pins) with a version marker; loading into a system assembled from the same cart needs no bus re-wiring (every MMIO closure reads the latches live). Round-trip + version-reject unit tests pass. **Increment 2 done:** the machine-path assembly builds a `runtime::save_target` from the manifest-path Genesis (`genesis_runtime` — what the player runs): the 5 graph chips + work RAM via `build_save_target`, the Z80 RAM + battery SRAM as memory chunks, the board latches via `genesis_runtime::save_state` (manifest-path twin of `genesis_system::save_state`, same byte layout), the EEPROM device's in-flight I2C state via `eeprom_i2c::save_state/load_state`, and the scheduler pacing. Whole-machine round-trip tests (determinism, save/load transparency, rewind-ring replay, corruption/mismatch rejection, battery-SRAM variant) pass. · DONE · HIGH · M · vs Emu · R8 · ⇄ T4 · Evidence: `src/runtime/tests/genesis_save_state_roundtrip_test.cpp`, `src/manifests/genesis/genesis_runtime.cpp` save_state/load_state, `src/chips/storage/eeprom_i2c/eeprom_i2c.cpp` save_state/load_state

> Done: M68000 (except G1), Z80, VDP (render + FIFO + DMA timing; M5-clear 192-line
> viewport geometry via PR #153 — Mode-4 *content* rendering is the open part of G8),
> YM2612, SN76489, SRAM/EEPROM/SSF2 banking, region/IO, Z80 bus arbitration.

---

## SMS + Game Gear — 1 / 5

#### Audio
- [x] **S1** YM2413 FM Sound Unit (Japanese SMS FM, ports $F0/$F1/$F2) · DONE · HIGH · M · vs Emu · R4 · Existing `chips/audio/ym2413` is now wired into hand-built SMS, manifest-built SMS, and the player adapter behind `sms_config::fm_unit` / `mnemos_player --fm`; ports $F0/$F1/$F2 and audio capture/mixing are covered by focused tests · Evidence: `src/manifests/sms/tests/sms_system_test.cpp` + `src/manifests/sms/tests/sms_runtime_parity_test.cpp` + `src/apps/player/adapters/sms/tests/sms_adapter_test.cpp`

#### Peripherals / IO
- [ ] **S3** Pause button → Z80 NMI wiring (NMI exists on the core; not wired at the system level) · MISSING · MED · S · vs Emu · R14 · Evidence: `progress-analysis.md` R14
- [ ] **S4** Light Phaser, Sports Pad, Paddle controllers (scaffolded "ready", not confirmed complete) · PARTIAL · LOW · M · beyond Emu · Evidence: `progress-analysis.md` SMS peripheral notes

#### Mapper / storage
- [ ] **S2** Sega-mapper $8000–$BFFF cart-RAM bank-select — the slot-2 cart-RAM window, `$FFFC` RAM-enable/bank bits, both 16 KiB banks, and snapshot save/load are implemented and unit-tested (part of the "all 8 mappers" done set). Residual: real-ROM golden validation + on-cart RAM battery (`.srm`) persistence (only the GG 93C46 EEPROM persists today) · PARTIAL · LOW · S · vs Emu · R15 · Evidence: `src/chips/mapper/sms_mapper/sms_mapper.cpp` + `sms_mapper/tests/sms_mapper_test.cpp`

#### System / glue
- [ ] **S5** Deep cart-header validation (checksum / product-code / claimed-size) · PARTIAL · LOW · S · vs Emu · R15 · Evidence: `progress-analysis.md` R15

> Done (mostly exceeding Emu): Z80, SMS VDP, GG VDP (12-bit CRAM + crop), SN76489 (+GG
> stereo), YM2413 FM unit, all 8 mappers, 93C46 saves, GG $00–$06 handset, PAL/NTSC switch, controller IO.

---

## Sega CD — 0 / 2

#### Disc / media
- [x] **D1** CHD compressed disc reader (v5 codec stack: cdfl / cdlz / cdzl / huff). **inc1 (`451ed5bd`):** `chd_reader` decodes the v5 header, the two-level canonical-Huffman hunk map, and the `cdzl` (DEFLATE) / `cdlz` (LZMA) / `none` / `self` codecs into a flat raw-2352 image with CHTR/CHT2 track synthesis + ECC regen; `.chd` opens through `disc_image::open` and the player boots the Sega CD BIOS from it. **inc2 done:** clean-room `flac_decoder` (RFC 9639: header CRC-8 + footer CRC-16 validated, CONSTANT/VERBATIM/FIXED/LPC subframes, partitioned-Rice residual, L/R/mid-side decorrelation) decodes `cdfl` CD-DA audio. Also fixed a latent audio-track `data_offset` bug (subtracted start_lba, so every audio track's flat window overlapped the data track — harmless while audio was silent). Verified across all 9 real corpus CHDs: every FLAC frame's CRC validates and the decoded audio is correct little-endian CD-DA (waveform-smoothness check); synthetic FLAC unit tests cover CI. · DONE · HIGH · L · vs Emu · R3 · Evidence: `src/disc/flac_decoder.cpp`, `src/disc/chd_reader.cpp`, `src/disc/tests/{flac_decoder_test,chd_reader_test}.cpp`
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

## New-system backlog — 0 / 9

Systems absent or not yet parity-closed in Mnemos. Domains to build per system; ordered easiest → hardest.
"✓" = a reusable implementation exists in Mnemos, not that the chip is already
parity-grade for the new machine. Listed prerequisites still apply.

- [~] **U1 Spectrum** — *LOW.* CPU: Z80 ✓ · Video: ULA ✓ · Audio: AY-3-8910 (128K) · Glue. **inc 1 DONE (#215 ULA chip, #216 system):** `chips::video::ula` (256x192 bitmap+attr+FLASH+border render-at-vblank, 50 Hz /INT pulse) + `manifests::spectrum::assemble_spectrum` (Z80 + 16K ROM + 48K RAM + port-$FE keyboard/border) + `spectrum_adapter` + `--system spectrum`. **The real 48K ROM boots to the © 1982 Sinclair Research Ltd screen (verified visually).** The 7th running system. **Remaining (inc 2+):** beeper audio, keyboard input mapping, `.z80`/`.tap` loading (to run games), then 128K AY + Timex models + 48K contention. · Evidence: `src/chips/video/ula/`, `src/manifests/spectrum/`, `src/apps/player/adapters/spectrum/`
- [ ] **U2 CPS1** — *MED.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: CPS-A/B GFX (bespoke, inline in Emu) · Audio: ym2151 ✓ + MSM6295 + QSound · Glue: ZIP set loading. · Evidence: `progress-analysis.md` §4
- [ ] **U3 NES** — *MED.* CPU: 2A03 variant + glue (Mnemos m6510 ≠ 2A03) · Video: ppu2c02 · Audio: ricoh_2a03_apu · Glue: real mappers (MMC1/3…) beyond NROM. · Evidence: `progress-analysis.md` §4
- [~] **U4 CPS2** — *MED-HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: CPS-2 tile/sprite/palette pipeline · Audio: QSound behavioural mixer · Glue: CPS-2 keyed opcode decryption, ZIP/parent/key loading, checked-in game manifests, EEPROM, player input/media/battery surfaces, frame-exact player save/load, operator service/test inputs, and MAME-matched per-game input profiles for the checked-in roster. **inc 1 DONE:** authentic `1944.zip` and self-describing `1944_mn.zip` boot to lit frames; `scripts/cps2/run-corpus-smoke.ps1` now data-gates catalog-match and clone-suffix CPS2 zips from `MNEMOS_CPS2_ROM` / `MNEMOS_CPS2_SET_DIR`, with current local proof across four 1944-family zips. **inc 2 DONE:** the CPS2 adapter exposes a complete board+adapter `runtime::save_target` through `mnemos_player` (`--save-state`, `--load-state`, F5/F9) and round-trips `1944_mn.zip` after restore. **inc 3 DONE:** IN2 now models the active-low test switch and service-credit bits, exposes them through player/scripted input, and preserves them in CPS2 save states. **inc 4 DONE:** the adapter now distinguishes 1/2/3/4-button, 3P/4P, six-button, and Cybots-specific input layouts, including the CPS2 six-button P2 button-6-on-IN2 wiring. **inc 5 DONE:** Eco Fighters and Puzz Loop 2 now use board-visible spinner/paddle profiles, including CPU-selected analog/digital IN0 multiplexing and deterministic save-state coverage. **inc 6 DONE:** `$804041` now records coin-counter edges and coin-lockout lines with the Mars Matrix polarity exception and save-state coverage. **inc 7 DONE:** SFA3 Hispanic/Brazil ticket-dispenser six-button profiles now drive the active-high ticket-empty line inactive, while preserving P2 button 6 on IN2 bit 14. **inc 8 DONE:** QSound now consumes the DL-1425 per-voice echo send plus feedback/length registers in the behavioural mixer and preserves the echo delay line in save states. **Remaining:** wider real-set corpus proof, game-specific EEPROM default contents if required by real sets, and DSP16-level QSound only if required for cycle-grade fidelity. · Evidence: `src/manifests/capcom_cps2/`, `src/chips/video/cps2_video/`, `src/chips/audio/qsound/`, `src/apps/player/adapters/capcom_cps2/`, `scripts/cps2/run-corpus-smoke.ps1`
- [ ] **U5 Amiga** — *HIGH.* CPU: m68000 ✓ (G1 applies) · Video: agnus + denise · Audio: paula · Glue: cia8520, copper, floppy (Emu chip shells are shallow OCS/ECS — need finishing). · Evidence: `progress-analysis.md` §4
- [ ] **U6 NeoGeo** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **LSPC (greenfield — exists nowhere)** · Audio: ym2610 (ssg + adpcm_a + adpcm_b). Emu is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U7 Taito F2** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **TC0100SCN + TC0200OBJ customs (greenfield)** · Audio: ym2610. Emu is a scaffold (197 LOC). · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U8 SNES** — *VERY HIGH.* CPU: 65c816 · Video: s_ppu · Audio: spc700 + s_dsp · Glue: CPU IRQ servicing, DMA/HDMA, HiROM. Emu CPU is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [ ] **U9 Saturn** — *VERY HIGH.* CPU: sh2 ×2 ✓ (X1/X2/X4 apply) + m68000 ✓ (G1 applies) · Video: vdp1 + vdp2 · Audio: scsp + scu_dsp · Glue: SCU/SMPC/CD-block scheduler. Multi-month. · Evidence: `progress-analysis.md` §4

---

## Suggested critical path (correctness before breadth)

1. ~~**32X timing cluster** — X2, X1, X3~~ **DONE (2026-06-13):** X1 address-error + X4 INTC + the X2/X3 cycle-true timing tail are all implemented (X2/X3 as opt-in, manual-grounded models; default-on deferred). 32X is 8/8. The next 32X correctness work is the boot/feature chain (per-title tasks), not this cluster.
2. ~~**Sega CD CHD** — D1~~ **DONE** (`chd_reader` decodes cdzl/cdlz/none data tracks + cdfl/FLAC CD-DA audio; all 9 corpus CHDs verified).
3. ~~**SMS YM2413** — S1~~ **DONE (2026-06-17):** SMS FM is wired through the hand-built path, manifest runtime, and player `--fm`.
4. **Address-error exceptions** — G1 + X1 (shared 68K + SH-2 work; G1 also fixes Sega CD sub-CPU).
5. ~~**Genesis whole-system deterministic save target** — G7 ⇄ T4~~ **DONE (2026-06-18):** the manifest-path `runtime::save_target` (graph chips + work/Z80 RAM + battery SRAM + board latches + EEPROM I2C state + scheduler pacing) round-trips deterministically; unlocks save-state/rewind.
6. **C64 1541 GCR hardening** — C1 (gates disk-based software).
7. **Breadth, by ROI** — ~~Spectrum~~ **48K booting (#215/#216; 7th system)** → CPS1 → NES.
