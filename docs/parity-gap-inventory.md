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

> **Update 2026-06-23 (Amiga 500 bootstrap branch):** **U5 moves `[ ]`→`[~]`**.
> The feature branch adds a first bootable Amiga 500 integration path: MC68000 +
> Agnus + Denise + Paula + two CIA 8520s, Kickstart reset overlay, chip RAM,
> OCS custom register routing, Paula audio DMA, Copper pointer/strobe routing
> plus frame-start `COP1LC` reload, MOVE/SKIP/WAIT instruction cadence,
> impossible terminal-WAIT handling, `BPLCON0` high-resolution 640-pixel
> fetch/view, `BPLCON1` playfield horizontal-scroll delay, and
> `BLTPRI` blitter-priority CPU chip-RAM wait states, high-resolution
> four-plane display-DMA CPU chip-RAM lockout, and
> board-owned custom-register MOVEs for raster palette/interrupt effects,
> ADF mounting with AmigaDOS MFM track streaming, DF0 motor/step/side control,
> player media metadata, and hardware-visible joystick input through
> JOY0DAT/JOY1DAT, CIAA fire bits, mouse counters, and POT secondary/middle
> button pins. This is not
> a compatibility-complete Amiga: a real Kickstart+ADF boot smoke gate now exists
> behind `MNEMOS_AMIGA500_KICKSTART`/`MNEMOS_AMIGA500_ADF`, but no local
> copyrighted-media pass is recorded here; floppy write/decode and exact
> sub-byte bitcell timing are simplified, and major OCS/ECS features remain partial (exact blitter bus
> timing, exact sprite DMA bus-slot/hires edge-priority behavior,
> non-saturated display DMA contention, exact non-nasty blitter/display CPU slot arbitration,
> and remaining Copper/display bus arbitration and bitplane timing). Follow-ups in the same branch wire an adapter-level
> runtime save target for the six chips, Amiga board state, chip RAM,
> mounted-media position, input state, and scheduler pacing, plus a CIA-A
> raw-key serial queue for Amiga keyboard down/up events gated by the host
> KDAT/SP handshake pulse, exact keyboard control-code injection including
> reset warning, and Caps Lock LED toggle semantics, and a first OCS
> sprite compositor slice for attach mode, DMA-list channel reuse, `BPLCON2`
> playfield/sprite priority, and `CLXCON`/`CLXDAT` collision latches. The branch also adds a bounded OCS
> area-blitter path: BLTCON0/1, A/B/C/D pointers, modulos, A first/last masks,
> minterm evaluation, inclusive/exclusive area fill, line mode with octant
> stepping, texture, and `SING`, BLTSIZE-triggered chip-RAM writes,
> color-clock-bounded BBUSY retirement, delayed BLIT interrupt request, and
> in-flight busy countdown save states.
> Controller-port follow-ups include `POTGO`-started, RC-calibrated raster-line
> POT counters with the reset-window delay and per-axis stop thresholds for
> `POT0DAT`/`POT1DAT`.
> Floppy status now includes the CIAA active-low disk-change latch, asserted on
> media insert/remove and cleared by a selected step pulse, plus a 300 RPM
> scanline-paced DF0 index pulse on CIAB FLAG. Disk write DMA now consumes chip
> RAM bytes at the paced raw-track stream position, mutates writable in-memory
> raw tracks, raises DSKBLK on completion, and preserves the raw track stream in
> save states. Disk read DMA now honors `ADKCON.WORDSYNC`, waiting for
> `DSKSYNC` before transferring the following disk word and preserving that wait in
> save states. Raw MFM bytes now reach `DSKBYTR` and disk DMA through an Agnus
> color-clock accumulator instead of scanline-start bursts. The player/frontend path now forwards host keyboard state as
> USB/HID usages to the dedicated Amiga keyboard port, where the adapter maps
> the US raw-key table including modifiers, keypad, cursor/function keys, and
> Caps Lock LED toggles. DF0-DF3 select lines now address independent mounted
> media, cylinders, change/status latches, raw-track streams, and save-state
> phase; the player adapter mounts the first four supplied ADFs as resident
> drives while keeping DF0 media swapping available. Writable AmigaDOS raw
> sectors are decoded back into the resident ADF image when disk write DMA
> completes with valid MFM header/data checksums. The adapter also advertises
> analog POT ports, the SDL player routes normalized gamepad stick axes into
> `POT0DAT`/`POT1DAT` targets, and the manifest converts those targets through
> the calibrated POT reset-window counter model.

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

## Irem M72 — 1 / 3

All "beyond Emu" — Emu's M72 is a non-rendering scaffold, so these are board-family
hardware completeness, not Emu-parity gaps.

#### CPU
- [~] **M1** mcs51 (8051) protection MCU — the optional `mcu` region is now player-loadable via `mcu.bin`, scheduled when present, and covered through the MCU MOVX sample/latch/shared-RAM path (`V30 $B0000` / MCU `$C000`) using both DPTR and P2-latched `@R0` / `@R1` external-data forms; declarative protected sets with a missing MCU dump now carry the ROM-set issue but clear the filled `mcu` region before board construction, so Mnemos does not schedule an all-`0xFF` fake protection CPU; port read-modify-write instructions now use the output latch rather than external pin levels, matching the 8051 behavior real i8751 code relies on when updating P0-P3; the MCS-51 core now implements the classic IE/IP two-level priority model so high-priority external/timer/serial interrupts can preempt a low-priority ISR while equal/lower-priority requests wait for RETI plus the following foreground instruction, including the same one-instruction deferral when an IE/IP access leaves a serviceable request pending, timer mode 0's 13-bit counter, timer mode 3's split TL0/TH0 behavior, TMOD GATE-controlled timers, T0/T1 external counter pins, serial RI/TI arbitration through the shared `0x0023` vector with firmware-owned flag clearing, frame-level SBUF transmit/receive timing, and a mechanical all-opcode operand-consumption regression covering the complete i8751 decoder surface including AJMP/ACALL page forms and call stack byte order; no-dump true-M72 sets can now declare explicit `[[hle]]` MCU profiles, with `dbreedm72` / `dkgensanm72` mapped to the startup protection RAM inversion surface, V30 command-latch acknowledge, and manifest-declared profile-bounded sample-trigger cursor setup; duplicate sample-trigger declarations are schema errors, and supported no-dump profiles without trigger metadata or with trigger starts outside the loaded `samples` region are reported and disabled instead of activating partial HLE; `MNEMOS_M72_PROTECTED_SET` provides a data-gated real-ROM player check for protected true-M72 sets with either a dumped MCU or an explicit no-dump HLE profile, and `MNEMOS_M72_PROTECTED_MCU_SET` now separately proves a CRC-clean protected set with a real dumped MCU by requiring the MCS-51 chip to be scheduled. Local proof with `D:\emu\irem\M72\nspirit.zip` passes that dumped-MCU golden. Remaining: validate / complete protected-game behavior against authentic per-game MCU + ROM-set artifacts, including the no-dump profile entry routines beyond startup RAM inversion, command-latch acknowledge, and sample trigger setup (R-Type needs none) · PARTIAL · HIGH · M · beyond Emu · R9 · Evidence: `src/chips/cpu/mcs51/mcs51.cpp` + `src/chips/cpu/mcs51/tests/mcs51_test.cpp` + `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` / `progress-analysis.md` R9

Loader note: no-dump HLE profiles now also disable when their declared `samples` region has missing file issues, preventing sample fill bytes from driving protection-HLE cursors.

Protection-HLE note: `dbreedm72` and `dkgensanm72` no-dump MCU profiles now also expose their profile-specific service-ROM checksum response bytes at the shared-RAM request trigger, matching the declared HLE surface instead of leaving service-test reads as stale RAM.

Sample-trigger note: the no-dump HLE sample-trigger metadata for `dbreedm72`
and `dkgensanm72` is now pinned by exact full-table assertions, covering all
nine Dragon Breed starts and all 28 Daiku no Gensan starts instead of a single
representative entry.

Sample-ROM note: the M72 Z80 sample-read port and protection-MCU MOVX sample
data port now use bounded direct sample-region addressing; reads past the
loaded sample ROM return open bus (`0xff`) instead of modulo-wrapping to the
first byte. Focused board tests cover both cursors.

Sample-pump note: the M72 board now owns a scheduled external sample pump at
the documented 32 MHz / 4096 cadence. It advances the same bounded sample-ROM
cursor, writes only nonzero bytes to the DAC, stays idle while an uploaded-RAM
sound CPU is held in reset, and is exposed as an Irem peripheral in the player
chip list. Focused scheduler tests pin the divider.

Split-source proof note: the per-set M72 data-gated hooks and corpus smoke
runner now accept platform path-list env vars and pass supplemental media
through the adapter, matching the player's multi-`--rom` route. Current local
proof passes Gallop directly with
`MNEMOS_M72_PROTECTED_SET=D:\emu\irem\M72\gallopm72.zip;D:\emu\irem\M72\gallop`,
Air Duel directly with
`MNEMOS_M72_VERTICAL_SET=D:\emu\irem\M72\airduelm72.zip;D:\emu\irem\M72\airduelm72`,
and Dragon Breed audio directly with
`MNEMOS_M72_PROTECTED_AUDIO_SET=D:\emu\irem\M72\dbreedm72.zip;D:\emu\irem\M81\dbreed.zip`.

#### System / variants
- [~] **M3** Additional M72 board variants beyond R-Type — `board_params_for` now records the known true-M72 work-RAM map families (`rtype*`, standard protected M72, `xmultiplm72`, and `dbreed*m72`) plus set-specific DIP defaults without admitting M81/M82/M84/M85/M92 boards into the M72 profile; the player resolves standard set zips, single-inner-set wrapper zips, or unpacked set directories by basename through the embedded checked-in game manifests, keeps source-local `game.toml` as a development override, resolves declarative clone `parent` zips/directories beside the clone set, reports missing/corrupt/unsafe/undeclared/mismatched parent resolutions as media validation issues, refuses fallback bytes from a parent zip whose manifest does not validate as the declared parent, and inherits parent manifest regions plus parent DIP/HLE metadata when the clone manifest omits replacements; source-local declarations for non-M72 boards now surface loader issues instead of falling through as empty M72 development sets; checked-in M72 game manifests now cover the true-M72 roster: `rtype`, `bchopper`, `mrheli`, `nspirit`, `nspiritj`, `loht`, `lohtj`, `lohtb2`, `lohtb3`, `imgfight`, `airduelm72`, `rtypej`, `rtypejp`, `rtypeu`, `rtypeb`, `imgfightj`, `imgfightjb`, `airdueljm72`, `xmultiplm72`, `dbreedm72`, `dbreedjm72`, `dkgensanm72`, and `gallopm72` with parser/region-contract coverage plus explicit no-dump MCU HLE declarations for `dbreedm72` / `dkgensanm72`; the ROM-set schema/player adapter now parse and retain roster-level true-M72 DIP option metadata, including conditional Irem coinage tables, unsupported MCU HLE profiles now report loader issues instead of silently booting as unprotected boards, unprotected boards leave the absent MCU latch port as open bus, and the player media descriptor publishes a CRC32 over resolved set metadata plus loaded resident ROM regions while carrying ROM-set validation issues so capability discovery degrades CRC-mismatched or incomplete M72 media instead of reporting it available; no-dump HLE profiles now cover the startup inversion surface, V30 command-latch acknowledge, and manifest-declared profile-bounded sample-trigger cursor; control-register coin-counter outputs now count rising edges, the CPU-visible sprite-DMA-complete bit stays asserted, flip-screen mirrors the composed frame while both round-trip through board/video state, mid-frame video save-state now preserves already-composed scanlines, the M72 board save-target manifest revision tracks the media-fingerprinted board-state schema, the player-adapter save/load overrides now expose that target to `mnemos_player`, and the target also captures adapter frame count, audio-drain cursor, DAC mix continuity, and frontend input snapshots, capability discovery exposes that frame-exact target as rollback-ready for the M72 player session, and visible scanlines compose at beam-line start so raster-time scroll writes affect later lines without repainting earlier lines; `MNEMOS_M72_VERTICAL_SET` provides a data-gated real-ROM orientation/framebuffer sanity hook for vertical true-M72 sets, `MNEMOS_M72_SET_DIR` now drives a full checked-in true-M72 roster gate from a mixed corpus root or platform path-list of roots containing `<set>.zip`, `<set>\`, or single-inner-set wrapper zips anywhere below each root per embedded manifest and validates CRC-clean loads, clone parent fallback, board wiring, orientation, sound release, and a non-blank frame, and `scripts/irem_m72/run-corpus-smoke.ps1` drives configured R-Type, protected, vertical, and roster artifacts through `mnemos_player` save/load/screenshot smoke proof with per-set fallback frame attempts for attract/blank intervals and per-set subsets for collection ZIPs. Current local proof treats `D:\emu\irem\M72` as the sorted board-local corpus and proves clean save/load/screenshot smoke for all 23 checked-in true-M72 sets from the current full smoke pass; the smoke runner refuses media-validation issues when deciding pass/fail, stale unpacked `D:\emu\irem\M72\nspirit` is bypassed in favor of the CRC-complete `D:\emu\irem\M72\nspirit.zip`, the current recursive artifact preflight reports `417/417` checked-in M72 manifest artifacts present from `D:\emu\irem\M72`, including `118/118` for the `gallopm72` / `nspirit` / `nspiritj` / `lohtj` / `lohtb2` blocker group, and the current full-roster CTest passes with `MNEMOS_M72_SET_DIR=D:\emu\irem\M72` using its default 900-frame roster window. Remaining: complete and validate no-dump HLE protection entry behavior beyond the covered startup/latch/sample surfaces, verify any board-manual corrections for MAME-assumed DIP locations, and resolve remaining set-specific protection/sample behavior · PARTIAL · MED · M–L per game · beyond Emu · Evidence: `src/manifests/irem_m72/games/*.toml` + `src/manifests/irem_m72/m72_game_manifests.hpp` + `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` + `scripts/irem_m72/run-corpus-smoke.ps1` / `progress-analysis.md` M72 board roster

Artifact note: stale extracted raw folders are no longer treated as sufficient
proof when a complete exact set ZIP is present. Direct scanning of the current
`D:\emu\irem\M72\nspirit.zip` reports `48/48` artifacts present for `nspirit`
plus `nspiritj`, including `nin_c-pr-b.ic1` (`0x0f7b2713`) and
`nspiritj/nin_c-pr-.ic1` (`0x802d440a`). The unpacked
`D:\emu\irem\M72\nspirit` folder is still incomplete, so the smoke runner now
ranks the exact `nspirit.zip` ahead of that same-name folder instead of failing
on stale extracted media.

Follow-up scan of the exact current M72 paths
`D:\emu\irem\M72\nspirit.zip`, `D:\emu\irem\M72\gallopm72.zip`, and the
unpacked `D:\emu\irem\M72\gallop` parent/share folder now reports the focused
`gallopm72` plus World/Japan `nspirit` artifact group present. Direct ZIP
inspection confirms `nspirit.zip` contains both the World `nin_c-pr-b.ic1` MCU
(`0x0f7b2713`) and the Japan `nspiritj/nin_c-pr-.ic1` MCU (`0x802d440a`),
while `gallopm72.zip` contains `cc_c-pr-.ic1` (`0xac4421b1`). Exact scans of
`lohtj.zip` plus `loht.zip` report `20/20`, and `lohtb2.zip` plus `loht.zip`
report `30/30`; the current exact `.7z` check of `lohtj.7z`, `lohtb2.7z`, and
the parent `loht.zip` reports `50/50`. The manifests
now alias the local `loht.zip` parent/shared filenames used by those clone
routes, and targeted `gallopm72` / `lohtj` / `lohtb2` corpus smoke passes
`3/3`.
The artifact scanner now tolerates unreadable entries while walking unrelated
`.7z` archives, so a broad mixed storage probe can skip bad archive members
instead of aborting before Irem evidence is collected; the patched scanner
finishes the previously failing
`D:\emu\Chaos Field (English v1.0)[Analog Stick Enabled][cdi].7z` case as a
clean 0/20 non-match. Current live recheck against `D:\emu\irem\M72` with
`-Recurse` reports `118/118` present for the prior `gallopm72` / `nspirit` /
`nspiritj` / `lohtj` / `lohtb2` blocker group and `417/417` present for the
full checked-in M72 manifest artifact preflight. The optional one-command roster
CTest with `MNEMOS_M72_SET_DIR=D:\emu\irem\M72` now passes after the source-ranking,
supplemental-media, held-MCU-latch, and late Air Duel audio-window fixes; this
turns the complete local artifact set into current full-roster player proof.

Corpus note: `scripts/irem_m72/run-corpus-smoke.ps1` now accepts multiple
`-RomDir` values, handles collection ZIP forms, and treats sorted non-M72 roots
such as `D:\emu\irem\M81`, `D:\emu\irem\m82`, and `D:\emu\irem\M84` as a clean
zero-candidate result instead of folding them into M72 proof; the
`D:\emu\irem\m72` / `-MaxSets 2` probe remains a positive smoke path. The player
adapter now also prefers a checked-in canonical M72-suffixed top-level directory
when a plain source stem is used, so collection ZIP forms are not mis-selected
as `airdueljm72` by shared program CRCs. The local mixed
`D:\emu\irem\M72\airduel.zip` is now tracked through the M82 `airduel` manifest
instead of being counted as true-M72 proof; when explicitly forced through
`--system irem_m72`, it remains excluded from clean M72 roster evidence. The
default fallback frame list includes 900 frames because `rtypeb` stays black at
the 300/600-frame attract probes but reaches a lit post-load frame at 900.
The runner now parses comma-separated `-FallbackFrames` values reliably under
`pwsh -File` and stops trying longer frame fallbacks once media-validation
issues appear, so stale or partial media still fails fast instead of launching
irrelevant long-frame probes. The current local M72 artifact preflight is clean.

Input note: the M72 player adapter now maps the shared frontend arcade inputs
directly to the board-visible system byte: `service` clears service 1/2 bits
4/5, legacy `mode` remains a service alias for older callers and v1 adapter
states, and `test` clears the operator-test bit 6. Adapter state version 2
persists the explicit `service`/`test` fields while accepting version 1 input
snapshots.

2026-06-25 artifact proof note: `MNEMOS_M72_RTYPE_SET=D:\emu\irem\R-Type_Arcade_EN.zip`,
`MNEMOS_M72_PROTECTED_SET=D:\emu\irem\imgfight.zip`, and
`MNEMOS_M72_VERTICAL_SET=D:\emu\irem\imgfight.zip` pass their dedicated CTest
golden gates. The recursive mixed-corpus smoke path now passes targeted
`nspirit` from `D:\emu\irem\M72\nspirit.zip`; the full open-roster proof should
be rerun against the now-clean local artifact corpus before quoting a current
full-roster player result.

2026-06-26 roster discovery note: the CTest roster gate now walks mixed corpus
roots recursively, so `MNEMOS_M72_SET_DIR=D:\emu\irem` discovers
`D:\emu\irem\i8751\Legend-of-Hero-Tonma_Arcade_EN.zip` as the `lohtb3`
single-inner wrapper without adding `D:\emu\irem\i8751` manually. The M72
PowerShell smoke runner also supports `-Set` filtering and the top-level
`scripts/run-data-gated-tests.ps1` entrypoint now passes `-Recurse` for M72
smoke runs; targeted proof with
`scripts\irem_m72\run-corpus-smoke.ps1 -BuildDir build/windows-msvc-debug -RomDir D:\emu\irem -Recurse -Set lohtb3`
passes `1/1`, using the nested `lohtb3` wrapper plus parent `loht` sources and
a lit 300-frame fallback screenshot with no media-validation issues. Targeted
proof with
`scripts\irem_m72\run-corpus-smoke.ps1 -BuildDir build/windows-msvc-debug -RomDir D:\emu\irem\M72 -Recurse -Set nspirit`
passes `1/1`, resolving `D:\emu\irem\M72\nspirit.zip` first and retaining the
stale same-name folder only as a secondary source. The current full artifact
scanner now reports `417/417` present, and targeted `gallopm72` / `lohtj` /
`lohtb2` corpus smoke passes `3/3`.

Full-roster runner note: `scripts\irem\run-local-corpus.ps1 -IncludeFullM72Roster`
now runs the strict checked-in-manifest artifact scanner before CTest. That
preflight now reports the current checked-in M72 artifact set as complete before
the roster CTest runs. The roster CTest now passes with
`MNEMOS_M72_SET_DIR=D:\emu\irem\M72` using its default 900-frame roster window.

Scanner evidence note: `scripts/irem_m72/find-missing-artifacts.ps1` now writes
a per-run `run_id` and a collision-resistant default report filename using
fractional timestamp, process id, and the run-id prefix. Rapid or parallel
artifact probes no longer overwrite each other when they start within the same
second; current same-second checks against `nspirit.zip` and `gallopm72.zip`
produce distinct `build/scratch/irem-m72-artifacts/*.json` reports.
The same scanner now accepts `-MissingFromReport <json>` to seed a focused
search from the prior report's missing targets, records that seed path in the
new report, tolerates malformed ZIPs, and uses `tar -tvf` sizes for `.7z`
members so broad archive walks do not need `-ScanAllSevenZipEntries` just to
find differently named same-size targets. Older missing-only scans are
superseded by the updated local ZIP set and the current `417/417` full M72
artifact preflight.

2026-06-26 category proof note: the current local M72 category proof uses
`MNEMOS_M72_RTYPE_SET=D:\emu\irem\M72\rtype.zip`,
`MNEMOS_M72_PROTECTED_SET=D:\emu\irem\M72\gallopm72.zip;D:\emu\irem\M72\gallop`,
`MNEMOS_M72_PROTECTED_AUDIO_SET=D:\emu\irem\M72\dbreedm72.zip;D:\emu\irem\M81\dbreed.zip`,
`MNEMOS_M72_PROTECTED_MCU_SET=D:\emu\irem\M72\nspirit.zip`, and
`MNEMOS_M72_VERTICAL_SET=D:\emu\irem\M72\airduelm72.zip;D:\emu\irem\M72\airduelm72`;
the common data-gated runner passes the R-Type, protected, protected-audio,
dumped-MCU protected, and vertical CTest goldens and the M72 player corpus smoke
resolves/passes `rtype`, `dbreedm72`, `nspirit`, and `airdueljm72` 4/4.
`tests/oracles/highwater.json` records those G6 goldens as `passed`.
`GLD-M72-ROSTER` is now current local proof when pointed at the board-local
corpus root: `MNEMOS_M72_SET_DIR=D:\emu\irem\M72` using its default 900-frame
roster window passes the roster CTest. The older mixed-root-only requirement is
superseded by the sorted board-local corpus: the recursive
`D:\emu\irem\M72` artifact scan now reports `417/417` checked-in M72 artifacts
present.

2026-06-26 rendered-audio smoke note: `scripts/irem_m72/run-corpus-smoke.ps1`
now accepts `-RequireRenderedAudio` plus `-AudioFrames` to run a separate
`mnemos_player --extract-audio` pass and require nonzero PCM in the rendered WAVE
payload. The current local proof
`scripts\irem_m72\run-corpus-smoke.ps1 -BuildDir build/windows-msvc-debug -RomDir D:\emu\irem\M72 -Set dbreedm72 -Frames 300 -FallbackFrames 600 -RequireRenderedAudio -AudioFrames 120`
passes `1/1`, including the grouped `dbreedm72` sources discovered under the
M72 corpus bucket. This proves a non-silent rendered-audio path for that protected
smoke route; it is not music parity or reference-audio certification.

2026-06-26 parity-hash oracle note: `GLD-M72-PARITY-HASH` now exposes an
opt-in deterministic final-frame/audio SHA-256 ratchet through
`MNEMOS_M72_PARITY_SET`, `MNEMOS_M72_PARITY_FRAME_SHA256`,
`MNEMOS_M72_PARITY_AUDIO_SHA256`, and optional `MNEMOS_M72_PARITY_FRAMES`.
The gate self-skips without trusted hashes, so it does not certify correct
graphics or music until a reference capture is supplied and the CTest passes.
`GLD-M72-PROTECTED-AUDIO` now registers the same protected audio requirement as
a G6 data-gated oracle through
`MNEMOS_M72_PROTECTED_AUDIO_SET`, including split-source path-list inputs such
as `D:\emu\irem\M72\dbreedm72.zip;D:\emu\irem\M81\dbreed.zip`; the standard
data-gated sweep includes `mnemos_apps_player_irem_m72_protected_audio_golden_test`
and passes it alongside the existing R-Type, protected, dumped-MCU, and vertical
goldens.

2026-06-26 dumped-MCU protection note:
`MNEMOS_M72_PROTECTED_MCU_SET=D:\emu\irem\M72\nspirit.zip` drives the new
`mnemos_apps_player_irem_m72_protected_mcu_golden_test`, which requires a
CRC-clean `mcu` region, `machine.mcu_present`, no protection-HLE fallback, and a
scheduled MCS-51 chip before it runs the lit-frame/sound-release proof. This is
separate from the generic protected golden so no-dump HLE sets cannot satisfy
the dumped-MCU requirement.

2026-06-27 M72 clone-subset smoke note:
`scripts/irem_m72/run-corpus-smoke.ps1` now preserves parent media when a
collection-style ZIP is subset for a clone route. This keeps the `nspiritj`
subset extracted from `D:\emu\irem\M72\nspirit.zip` paired with the exact-stem
parent `nspirit.zip`, so clone parent fallback can resolve the shared program,
graphics, samples, and MCU media instead of reporting a false incomplete clone.
Targeted proof with
`scripts\irem_m72\run-corpus-smoke.ps1 -BuildDir build\windows-msvc-debug -Rom D:\emu\irem\M72\nspirit.zip -Set nspiritj`
passes `1/1`, and the common local corpus runner's trailing M72 smoke now passes
`4/4`.

Corpus inventory note: `scripts/irem/inventory-corpus.ps1` with
`-Root D:\emu\irem -Recurse` records the local Irem tree as metadata only,
ignores archive-only container folders as unpacked sets, and currently reports
439 items across 24 top-level buckets. Direct files under `D:\emu\irem` are now
zero; ROM evidence is sorted into board/system buckets, while `for-delete` and
`non-irem` are ignored for support accounting even when file stems
match checked-in manifests. Board-local `name-collisions` folders are skipped
by both inventory and data-gated corpus source discovery. The inventory
separates manifest tracking, media loadability, and player support: 331 items
match a checked-in Irem manifest from non-ignored buckets, 179 are readable
through current ZIP / single-inner-ZIP / folder routes, all 179 are backed by an
executable player-supported route, 0 are tracked contract-only, and 153
manifest-backed items are metadata-only while awaiting ZIP/unpacked folders or
supplemental media; the M58 artwork package is still ignored as non-ROM proof. ZIPs whose entries are only
layout/images/docs now classify as `non_rom_artwork_package`, so packages such
as `rtypeleo (1).zip` and `travrusa.zip` no longer count as direct ROM-loadable
support. The M58 bucket
now holds nine ROM archives for `10yard`, `10yardj`, `vs10yard`, and
`vs10yardj`; `D:\emu\irem\M58\artwork\10yard-artwork.zip` is artwork/layout
only, is classified as metadata-only non-ROM artwork, and does not count as ROM
proof. Windows copy-suffixed checked-in set ZIPs such as `loht (1).zip` are
canonicalized to their embedded manifest IDs for player loading, M72
corpus-smoke grouping, and inventory grouping, and the local Air Duel M82
parent/US clone wrappers now route through `irem_m82`.
The report also carries per-item
`tracked_family`, `manifest_parent`, `set_role`, `archive_composition`, and
`load_readiness` fields plus `tracked_sets` and `known_corpus_items` groupings;
this currently separates
the local Image Fight material into `imgfight` as the M72 parent/standalone set
with two direct player-loadable routes plus one metadata-only `.7z`, and
`imgfightj` / `imgfightjb` as clones declaring parent `imgfight`, each with one
direct player-loadable ZIP route plus one metadata-only `.7z`.
The same grouping now tracks local M62 Kung-Fu Master / Spartan X, Kid Niki /
Yanchamaru, Lode Runner, Lot Lot, Spelunker II, Battle Road, Horizon, and
Youjyuden wrappers as first-pass player-loadable routes. `ldrun`, `ldrun2`,
`ldrun3`, and parent-backed clone `ldrun3j` have explicit
CPU/sound/video/PROM/timing region contracts, while `battroad`, `bkungfu`,
`horizon`, `kidniki`, `kungfum`, `ldruna`, `spartanx`, `yanchamr`, and
`youjyudn` stay as raw-media staging routes; the M62 bucket currently has 26
tracked archive items, 16 loadable/supported ZIP-style routes, and 10
metadata-only archives in `scripts/irem/inventory-corpus.ps1` output.
The local M14 grouping now tracks `D:\emu\irem\M14\ptrmj.zip` and
`D:\emu\irem\M14\ptrmj (1).zip` as first-pass player-loadable `ptrmj`
items; `ptrmj.7z` remains metadata-only until converted or unpacked.
The local M10 grouping now tracks `D:\emu\irem\M10\andromed.zip` and
`D:\emu\irem\M10\skychut.zip` as first-pass native-8085 player-loadable M10
items; `andromed.7z` and `skychut.7z` remain metadata-only until converted or
unpacked.
The local M27 grouping now tracks `D:\emu\irem\M27\panther.zip` and
`D:\emu\irem\M27\panther (1).zip` as first-pass player-loadable `panther`
items; `panther.7z` remains metadata-only until converted or unpacked.
The local M47 grouping now tracks `D:\emu\irem\M47\olibochu.zip` and
`D:\emu\irem\M47\punchkid.zip` as first-pass player-loadable M47 items, with
`punchkid` still resolving shared media from `olibochu`; `punchkid.7z` remains
metadata-only until converted or unpacked.
The local M57 grouping now tracks `D:\emu\irem\M57\newtangl.zip` and
`D:\emu\irem\M57\troangel.zip` as direct player-loadable first-pass New
Tropical Angel / Tropical Angel routes; `newtangl.7z`, `troangel.7z`, and the
combined `troangel (1).7z` collection remain metadata-only until converted or
unpacked.
The local M58 grouping now tracks `10yard`, `10yardj`, `vs10yard`, and
`vs10yardj` as first-pass player-loadable 10-Yard Fight manifests. The four ZIP
sets are direct player-loadable through `irem_m58`; the five 7z archives remain
metadata-only until converted or unpacked for the player route, and the
`10yard-artwork.zip` package is metadata-only non-ROM artwork.
The local M63 grouping now tracks `D:\emu\irem\M63\wilytowr.zip` as a
direct player-loadable first-pass `wilytowr` route; the two local Wily Tower
`.7z` archives remain metadata-only until converted or unpacked.
The local M75 grouping now also tracks the bootleg `vigilantbl` wrapper as a
direct player-loadable clone of `vigilant` and the standalone `kikcubic` ZIP as
a first-pass Meikyu Jima / Kickle Cubicle route; `kikcubic.7z` and
`kikcubic (1).7z` remain metadata-only until converted or unpacked. The M62
`lotlot` contract keeps the local Lot Lot wrapper out of
`classify_or_sort_corpus_item`. The local
travrusa grouping now tracks `travrusa`, `motorace`, `travrusab`, and
`travrusab2` from the M52-era local corpus bucket as first-pass player-supported under
`src/manifests/irem_travrusa` and `src/apps/player/adapters/irem_travrusa`;
`MNEMOS_TRAVRUSA_SET_DIR=D:\emu\irem\M52` proves four CRC-clean ZIP routes,
and the unsuffixed `travrusa.zip` is artwork/layout rather than parent ROM
proof, so it remains metadata-only for support accounting. The remaining known
untracked classifications are explicit rather than
generic sort work: `headon` (`sega/vicdual.cpp`) and `uniwars` / `uniwarsa`
(`galaxian/galaxian.cpp`) are non-Irem reference zips and should not be counted
as missing Irem implementation targets.
The local M119 grouping now tracks `D:\emu\irem\M119\scumimon.zip` as a
first-pass player-loadable M119 route and `D:\emu\irem\M119\scumimon.7z` as
metadata-only until converted or unpacked; `scumimon` no longer appears as an
M92 board-family candidate.
The local M78 grouping now tracks `D:\emu\irem\M78\bj92.zip` as a
first-pass player-loadable M78 route and `D:\emu\irem\M78\bj92.7z` as
metadata-only until converted or unpacked; `bj92` no longer appears as an M78
board-family candidate.
The local M102 grouping now tracks `D:\emu\irem\M102\hclimber.zip` as a
first-pass player-loadable M102 route and `D:\emu\irem\M102\hclimber.7z` as
metadata-only until converted or unpacked; `hclimber` no longer appears as an
M102 board-family candidate.
The local Red Alert / WW III grouping now tracks `D:\emu\irem\M27\ww3` as the
CRC-clean unpacked WW III player source and records `D:\emu\irem\M27\ww3.zip`
as a filename-matching contract-only route; `ww3.7z` remains metadata-only until
converted or unpacked for direct loading. The data-gated Red Alert player test
selects the unpacked directory because the local `ww3.zip` is a split/incomplete
archive, and `ww3` no longer appears as an unsupported M27 board-family
candidate.
The standard data-gated runner now also reports, runs, and oracle-registers
every implemented Irem player-family corpus golden: M10, M14, M15, M27, M47,
M52, M57, M58, M62, M63, travrusa, Red Alert, M72, M75, M78, M81, M82, M84,
M85, M90, M92, M102, M107, and M119. The
newest G6 high-water raises cover
`GLD-M10-CORPUS`, `GLD-M14-CORPUS`, `GLD-M15-CORPUS`, `GLD-M27-CORPUS`, `GLD-M47-CORPUS`, `GLD-M52-CORPUS`, `GLD-M57-CORPUS`, `GLD-M58-CORPUS`, `GLD-M62-CORPUS`, `GLD-M63-CORPUS`, `GLD-TRAVRUSA-CORPUS`, `GLD-M78-CORPUS`,
`GLD-M81-CORPUS`, `GLD-M82-CORPUS`, `GLD-M84-CORPUS`, `GLD-M85-CORPUS`, `GLD-M102-CORPUS`, `GLD-M107-CORPUS`, and `GLD-M119-CORPUS`, closing the previous gap where those
implemented player smoke gates existed but were absent from the common oracle
proof command.
M15 now has a checked-in `headoni` manifest plus an executable MOS 6502
board/player path with source-aligned Head On ROM/vector placement, RAM/MMIO
windows, frame IRQ coverage, active-high P1/P2 input wiring, coin-triggered NMI,
Head On DIP defaults, active-low flip control, and a tile/color/chargen renderer
using the M-15 tile scan order, 1bpp palette lookup, and scanline-paced
composition for frame-IRQ writes; `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`
proves the sorted local wrapper ZIP loads CRC-clean through the embedded
manifest, including the directory-prefixed entry aliases used by the local
nested ZIP, and the player route produces nonblank screenshot plus save/load
smoke for `--system irem_m15`.
M84 now has a ROM-contract layer for `hharryb`, `hharryu`, local Daiku no
Gensan split clones (`dkgensan`, `dkgensana`), local V35-profile `ltswords`,
its split Ken-Go clones (`kengo`, `kengoj`), local V35-profile Gallop
(`gallop`), and local Cosmic Cop (`cosmccop`);
`MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72` proves the
sorted Hammerin' Harry / Daiku no Gensan split ZIPs load CRC-clean when
composed with the M81 `hharry` parent, that `kengo` / `kengoj` load through the
local `ltswords` parent, and that the local `ltswords` ZIP loads its
CRC-verified program, sound, graphics, and samples as a standalone M84 set. The
same gate now validates the sorted `D:\emu\irem\M84\gallop.zip` archive and
keeps the unpacked `D:\emu\irem\M72\gallop` folder as true-M72 parent/share
media for `gallopm72`; `D:\emu\irem\M84\cosmccop.zip` loads as a
clone with its own program/sound/graphics/sample dumps and inherits Gallop's
PROM/PLD artifacts. The `ltswords` manifest explicitly declares the missing
small PROM/PLD artifacts, and that declaration is inherited by the Ken-Go clones
instead of treating the current route as final board-authentic video proof.
Gallop now also carries 10 checked-in DIP switch definitions, and the M84
adapter composes them into the board default (`0xf9bf`) while exposing the
switch count through the player system spec for both Gallop and Cosmic Cop.
M85 now has a ROM-contract and first-pass executable wrapper for `poundfor` and
`poundforj`: the embedded manifests cover the 1 MiB V30 program-pair layout,
sound CPU ROM, sample ROM, tile/sprite graphics, PLD artifacts, and Japan clone
parent fallback from the local `D:\emu\irem\M85` ZIPs. `MNEMOS_M85_SET_DIR`
proves both sets CRC-clean through the adapter; direct `mnemos_player --system
irem_m85` / `--system m85` smokes wrote 384x256 nonblank screenshots and
save-state bytes. This is still a compatibility-core bring-up, not board
authentic M85 graphics/music proof.
M81 now has a first-pass executable board/profile layer for `dbreed`, `hharry`,
and `xmultipl`: the embedded manifests preserve the three M81 ROM layouts,
`MNEMOS_M81_SET_DIR=D:\emu\irem\M81` proves those sorted wrapper ZIPs load
CRC-clean, and hermetic tests run a V30/Z80/YM2151/DAC/8259 frame plus reject
save states restored under the wrong board-layout profile. M81 is now also
player-routable through `--system irem_m81`, with resident media validation,
rollback-ready save-state, capability discovery, and real local wrapper-ZIP
player smoke for all three checked-in sets. The inventory also identifies
duplicate / misbucketed `dbreed` copies outside `M81`; M81 still lacks final
authentic video/raster priority, DIP, and board-timing parity. M107 now has a
first-pass executable V33/V35/YM2151/GA20 board and player route for `airass`,
`dsoccr94`, and `firebarr`; `MNEMOS_M107_SET_DIR=D:\emu\irem\M107` proves the
sorted ZIPs load CRC-clean, and direct Dream Soccer '94 player smoke writes a
384x256 nonblank PPM plus save state from `D:\emu\irem\M107\dsoccr94.zip`.
The board tests cover pending command/reply state from main
V33 latch write through V35 INTP1/vector-25 dispatch, explicit `$a8044`
acknowledge, sound-RAM storage, reply-port write, save-state persistence, the
YM2151 Timer A IRQ path through V35 INTP0/vector-24, simultaneous pending
YM/command IRQ arbitration selecting INTP0 before INTP1, and follow-on service
of a still-pending command IRQ through INTP1 after the YM2151 source is cleared.
The shared Fire Barrel / Air Assault SW1/SW2 DIP profile default (`0xffbf`), the
separate SW3 `COINS_DSW3` default (`0xebff`), and Dream Soccer's four-player DIP
profile with SW3 Player Power default (`COINS_DSW3=0xffff`) are now tracked. The
M107 adapter also maps frontend service/test input onto the
`COINS_DSW3` service-credit and operator-service bits. Root and `m72`-folder
duplicates are also visible as M107 artifacts; the two Fire Barrel `.7z` copies
remain metadata-only until converted or unpacked.
M82 is player-routable
through its own first-pass board profile, including scanline-composed video for
mid-frame palette writes and M72-style tile priority groups around sprites, but
not yet exact raster-phase or visual-priority authentic. The data-gated M82
artifact/player tests use `MNEMOS_M82_SET_DIR=D:\emu\irem\M82` to unwrap the local
Major Title and R-Type II collection ZIPs plus the unpacked standalone
`D:\emu\irem\M82\dkgensanm82` route and load `dkgensanm82`, `majtitle`,
`majtitlej`, `rtype2`, `rtype2j`, `rtype2jc`, and `rtype2m82b` CRC-clean through
the embedded manifests and clone-parent fallback where needed.
M90 now has checked-in manifests plus a first-pass executable V35/Z80 board and
player adapter for `atompunk`, `bbmanw`, `bbmanwj`, `bbmanwja`, `gussun`,
`hasamu`, `newapunk`, `quizf1`, and `riskchal`; `MNEMOS_M90_SET_DIR=D:\emu\irem`
proves the local M90 ZIPs load CRC-clean, including split-clone parent fallback
for Bomber Man World/Atomic Punk and Gussun Oyoyo, produce 384x256 nonblank
diagnostic frames, and save state through the M90 adapter. Loaded graphics and
sample regions now participate in the diagnostic frame/media hash, but this is
still route/topology proof rather than graphics/music-authentic proof.
M92 now has checked-in manifests plus a first-pass executable V33/V35 board and
player adapter for `bmaster`, `crossbld`, `dsoccr94j`, `geostorm`,
`geostorma`, `gunforce`,
`gunforcej`, `gunforceu`, `gunforc2`, `gunhohki`, `hook`, `hookj`, `inthunt`,
`inthuntu`, `inthuntk`, `kaiteids`, `leaguemn`, `leaguemna`, `lethalth`,
`majtitl2`, `majtitl2a`, `majtitl2b`, `majtitl2j`, `mysticri`, `mysticrib`,
`nbbatman`, `nbbatmanu`, `psoldier`, `rtypeleo`, `rtypeleoj`, `ssoldier`,
`thndblst`, `uccops`, `uccopsar`, `uccopsj`, and `uccopsu`;
`MNEMOS_M92_SET_DIR=D:\emu\irem\M92;D:\emu\irem\M107` proves the thirty-six
checked-in local M92 sets load CRC-clean, produce 320x240 nonblank diagnostic frames, and save state
through the M92 adapter, including clone-parent fallback for the local Gunforce
Japan/US split wrappers, Blade Master Japan, Geostorm alternate custom-sound,
Hook Japan, In the Hunt US/Japan/Korea, Mystic Riders Japan/bootleg split
wrappers, Lethal Thunder
Japan, Ninja Baseball Bat Man US, Yakyuu Kakutou League-Man parent/alternate
M92-Z-C wrappers, Superior/Perfect Soldiers Japan, Major Title 2
alternate-sound/World/Japan split wrappers, R-Type Leo Japan, and Undercover
Cops US/Japan/Alpha Renewal split wrappers, plus Dream Soccer '94 Japan M92
hardware using explicit `dsoccr94.zip` supplemental shared media.
They remain
diagnostic, not graphics/music-authentic, until encrypted V35 sound CPU handling
and GA21/GA22 video behavior are proven.
M10, M14, M15, M82, M84, M90, M92, M102, and M107 now have executable board/profile
layers, but all nine still need board-authentic video/priority, sound, exact
raster phase, and screenshot-parity closure before they can be called
authentic; M10, M14, M84, M90, M92, and M107 also retain memory/I/O and DIP
validation gaps.

Video note: the M72 sprite renderer now traverses the full 0x400-byte latched sprite RAM entry range, so single-width entries beyond the old 64-entry software cap remain visible. The M72 palette CPU map now models the disconnected A9 mirror and low-byte-only 5-bit gun writes/reads while preserving the renderer's canonical R/G/B plane storage.

#### Mapper / ROM
- [x] **M2** Z80 sound-program ROM path / `$8000` banked variants · DONE · MED · S · beyond Emu · ROM-backed sound maps activate when a set supplies a `soundcpu` region; public 64 KiB sound ROM declarations are accepted, with only `$0000-$EFFF` mapped as ROM and `$F000-$FFFF` shadowed by Z80 RAM, and the development zip `soundcpu.bin` path is covered. The suspected `$8000` bank-register variants are not part of the true-M72 board profile and must be routed to later M81/M84/M92 profiles or ADRs instead of folded into M72. · Evidence: `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` / `progress-analysis.md` R9

> Done (exceeds Emu): V30 (including hermetic 0F INS/EXT bitfield regressions
> and FPO2/BRKEM operand-consumption stubs), Z80 (shared-RAM boot handshake + ROM-backed sound map), full video (scanline-composed
> tilemaps / sprites / palette with raster-time scroll changes and CPU-visible disconnected-A9 palette mirrors), YM2151, DAC/PCM sample playback with sound-Z80-clocked
> write-boundary mixing, 8259 PIC, raster compare,
> shared sound RAM, inputs/DIPs.

---

## Irem M10 — 2 / 2

This section is split from M15 because the local Andromeda SS and Sky Chuter
artifacts are M10/M11-lineage 8085 routes rather than the later Head On M15
6502 route.

#### Manifests / board bring-up
- [x] **I10-1** Local M10 ROM-set contracts — `src/manifests/irem_m10` carries checked-in embedded ROM-contract manifests for `andromed` and `skychut`, with parser/region-contract coverage for their 8085 program ROM windows, GFX ROMs, local aliases, region sizes, offsets, and CRC32 values. `MNEMOS_M10_SET_DIR=D:\emu\irem\M10` data-gates the local M10 ZIPs and proves they load CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1` records four tracked M10 artifacts, two supported ZIP routes, and two metadata-only `.7z` routes · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m10/games/*.toml` + `src/manifests/irem_m10/tests/m10_system_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I10-2** Executable M10 board profile — First-pass route exists for Andromeda SS and Sky Chuter: `src/manifests/irem_m10/m10_system.cpp` assembles a native Intel 8085 board shell with main program placement at `$1000-$2FFF`, reset-vector mirror at `$FC00`, scratch/video/color/work RAM, input/DIP/control MMIO, mirrored I/O ports, GFX-ROM diagnostic video, beeper-backed sound latch, save-state identity, and player adapter registration. `src/apps/player/adapters/irem_m10` registers `--system irem_m10` / `m10`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local player smoke through `MNEMOS_M10_SET_DIR=D:\emu\irem\M10`. Direct 120-frame player smokes wrote 224x256 nonblank PPMs for `andromed` and `skychut`. This is smoke-playable, not authentic parity: deeper 8085 interrupt/timing proof, exact reset/bus map evidence, video/color, discrete sound, raster phase, and trusted visual/audio parity remain open · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/chips/cpu/i8080/*` + `src/manifests/irem_m10/m10_system.cpp` + `src/manifests/irem_m10/tests/m10_system_test.cpp` + `src/apps/player/adapters/irem_m10/*` + `MNEMOS_M10_SET_DIR=D:\emu\irem\M10` corpus golden + direct `mnemos_player --system irem_m10` / `--system m10` smoke

---

## Irem M14 — 2 / 2

This section is split from M10/M15 because the local P.T. Reach Mahjong artifact
is classified by current public driver-level evidence as M14 hardware rather
than the existing M15 Head On route.

#### Manifests / board bring-up
- [x] **I14-1** Local M14 ROM-set contract — `src/manifests/irem_m14` carries a checked-in embedded ROM-contract manifest for `ptrmj`, with parser/region-contract coverage for the eight 1 KiB 8085 program ROMs, two 1 KiB graphics ROMs, local nested-wrapper aliases, region sizes, offsets, and CRC32 values. `MNEMOS_M14_SET_DIR=D:\emu\irem\M14` data-gates the local `ptrmj` ZIPs and proves they load CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1` records the board-bucket wrappers as tracked M14 artifacts instead of leaving them as `classify_or_sort_corpus_item` · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m14/games/ptrmj.toml` + `src/manifests/irem_m14/tests/m14_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I14-2** Executable M14 board profile — First-pass route exists for P.T. Reach Mahjong: `src/manifests/irem_m14/m14_system.cpp` assembles a board shell with the native Intel 8085/8080 core configured as an 8085, program ROM `$0000-$1FFF`, scratch/video/color/work RAM, input/DIP/control MMIO, mirrored I/O ports, GFX-ROM diagnostic video, a beeper-backed sound latch, save-state identity, and player adapter registration. `src/apps/player/adapters/irem_m14` registers `--system irem_m14` / `m14`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local P.T. Reach Mahjong player smoke through `MNEMOS_M14_SET_DIR=D:\emu\irem\M14`. This is smoke-playable, not authentic parity: deeper NEC D8085AC/8085 interrupt/timing proof, exact M14 memory/I/O behavior, video/color, paddle/mahjong input behavior, sparse discrete/sample sound, raster phase, and trusted visual/audio parity remain open · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/chips/cpu/i8080/*` + `src/manifests/irem_m14/m14_system.cpp` + `src/manifests/irem_m14/tests/m14_system_test.cpp` + `src/apps/player/adapters/irem_m14/*` + `MNEMOS_M14_SET_DIR=D:\emu\irem\M14` corpus golden + direct `mnemos_player --system irem_m14` / `--system m14` smoke

---

## Irem M27 — 2 / 2

This section is split from the Red Alert-family source grouping because current
public driver-level evidence labels Panther as Irem M27 hardware, and the local
corpus includes a complete Panther wrapper.

#### Manifests / board bring-up
- [x] **I27-1** Local M27 Panther ROM-set contract — `src/manifests/irem_m27` carries a checked-in embedded ROM-contract manifest for `panther`, with parser/region-contract coverage for seven 2 KiB M6502 program ROMs mapped at `$8000-$B7FF`, one 2 KiB Panther audio-board ROM mapped at `$7000`, one 512-byte color PROM, local nested-wrapper aliases, region sizes, exact offsets, and CRC32 values. `MNEMOS_M27_SET_DIR=D:\emu\irem\M27` data-gates the local `panther` ZIPs and proves they load CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1` records the board-bucket wrappers as tracked M27 artifacts instead of leaving them as `classify_or_sort_corpus_item` · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m27/games/panther.toml` + `src/manifests/irem_m27/tests/m27_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I27-2** Executable M27 board profile — First-pass route exists for Panther: `src/manifests/irem_m27/m27_system.cpp` assembles a MOS 6502 board shell with program ROM `$8000-$B7FF`, reset-vector fallback for the local contract layout, scratch/video/color/work RAM, input/DIP/control MMIO, PROM/RAM diagnostic video, a beeper-backed sound latch, save-state identity, and player adapter registration. `src/apps/player/adapters/irem_m27` registers `--system irem_m27` / `m27`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local Panther player smoke through `MNEMOS_M27_SET_DIR=D:\emu\irem\M27`. This is smoke-playable, not authentic parity: exact M27 memory/I/O timing, bitmap/char video and color behavior, input/DIP behavior, Panther audio-board behavior, raster phase, and trusted visual/audio parity remain open · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m27/m27_system.cpp` + `src/manifests/irem_m27/tests/m27_system_test.cpp` + `src/apps/player/adapters/irem_m27/*` + `MNEMOS_M27_SET_DIR=D:\emu\irem\M27` corpus golden + direct `mnemos_player --system irem_m27` / `--system m27` smoke

---

## Irem Red Alert / WW III — 2 / 2

This section is split from M27 because Panther has a first-pass M27 player
route, while `ww3` is a Red Alert-family clone in `irem/redalert.cpp` with a
different program map, video config, and Irem M37B audio-board route. The local
files remain physically sorted under `D:\emu\irem\M27` as early M27-era corpus
material, but Mnemos tracks the ROM contract and first-pass player route under
`irem_redalert`.

#### Manifests / board bring-up
- [x] **IRED-1** Local Red Alert / WW III ROM-set contract — `src/manifests/irem_redalert` carries a checked-in embedded ROM-contract manifest for `ww3`, preserving the public M6502 `maincpu`, `soundboard:audiocpu`, and PROM region sizes, offsets, and CRC32 values for the local WW III files. `MNEMOS_REDALERT_SET_DIR=D:\emu\irem\M27` data-gates the unpacked `D:\emu\irem\M27\ww3` directory that was extracted from the complete local `ww3.7z` archive and proves those dumped regions load CRC-clean through the embedded manifest. `scripts/irem/inventory-corpus.ps1` now classifies the unpacked directory as a supported Red Alert first-pass route, keeps the filename-matching split `ww3.zip` as contract-only, leaves `ww3.7z` metadata-only, and no longer reports WW III as an unsupported M27 board-family candidate · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_redalert/games/ww3.toml` + `src/manifests/irem_redalert/tests/redalert_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **IRED-2** Executable Red Alert / WW III board profile — `redalert_system` and `irem_redalert_adapter` add a first-pass M6502 board/player route for WW III with the source-aligned `$5000-$bfff` program window, `$f000-$ffff` vector mirror from the `$8000` ROM window, `$c0xx` input/audio/video/color registers, bitmap/char/PROM-backed diagnostic frame composition, input/save-state support, beeper-backed sound-command evidence, and a data-gated real-corpus WW III adapter smoke. Remaining authenticity gaps are the Red Alert parent contract, M37B audio-board runtime, exact video/color/input/DIP behavior, timing/raster proof, and trusted visual/audio parity · PARTIAL · MED · S · beyond Emu · Evidence: `src/manifests/irem_redalert/redalert_system.*` + `src/apps/player/adapters/irem_redalert/*` + `src/manifests/irem_redalert/tests/redalert_system_test.cpp` + `src/apps/player/adapters/irem_redalert/tests/irem_redalert_adapter_test.cpp`

---

## Irem M15 — 1 / 2

This section is split from M72 so the early Head On hardware stays classified as
a 6502-era M15 profile instead of being folded into later V30 boards.

#### Manifests / board bring-up
- [x] **I15-1** Local M15 ROM-set contract — `src/manifests/irem_m15` carries a checked-in embedded ROM-contract manifest for `headoni`, with parser/region-contract coverage for the six 1 KiB program ROMs, the `e4.9d` reset-vector reload at `$fc00`, and aliases for the local nested wrapper ZIP's `headoni/` entry prefix. `MNEMOS_M15_SET_DIR=D:\emu\irem\M15` data-gates the sorted local artifact and proves it loads CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1` records the M15 bucket as tracked/loadable instead of an unsupported board-family candidate · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m15/games/headoni.toml` + `src/manifests/irem_m15/tests/m15_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I15-2** Executable M15 board profile — `src/manifests/irem_m15` now assembles an M15-owned MOS 6502 board shell at `733125 Hz` with the M-15 memory map: scratch RAM `$0000-$02ff`, ROM `$1000-$33ff` plus vectors `$fc00-$ffff`, video RAM `$4000-$43ff`, color RAM `$4800-$4bff`, chargen RAM `$5000-$57ff`, and `$a000/$a100/$a200/$a300/$a400` P2/sound/DIP/P1/control MMIO. It reuses the shared 6502-family CPU core in bare-6502 mode, pulses the IRQ vector during frame stepping, maps coin insertion to the NMI edge, uses Head On's active-high P1/P2 controls and `0x11` DIP default, preserves the active-low flip control bit in save states, exposes 6502 trace/register capability discovery, and renders the vertical 224x256 frame through M-15 tile scan order, color RAM lower-three-bit palette selection, runtime chargen RAM, and scanline-paced composition where frame-IRQ writes affect later visible scanlines without repainting earlier ones instead of the old program-ROM diagnostic fallback. Whole-board save/load identity rejects mismatched DIP/layout identity. `src/apps/player/adapters/irem_m15` registers `--system irem_m15` / `m15`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local player smoke through `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`; direct Head On screenshot and save/load smoke both produce 224x256 nonblank frames. Remaining: board-evidenced discrete sample mappings/analog sound behavior, analog color proof, exact raster phase proof, and authentic screenshot parity before calling the profile authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m15/m15_system.cpp` + `src/manifests/irem_m15/tests/m15_system_test.cpp` + `src/apps/player/adapters/irem_m15/irem_m15_adapter.cpp` + `src/apps/player/adapters/irem_m15/tests/irem_m15_adapter_test.cpp`

Continuation: `$a100` sound writes now persist total write count, per-bit rise/fall counters, active-low bit-6 speaker output state, and speaker output edge count through both board and adapter save-state paths. This narrows the sound gap to board-evidenced discrete sample mappings/analog sound behavior rather than raw latch observability.

Continuation: M15 frame stepping now slices CPU/audio/video by scanline and composes each visible line before that line's CPU slice; focused coverage proves the frame IRQ can change color RAM for later scanlines without repainting an earlier line. This narrows the raster gap to exact phase proof and external visual parity rather than raw mid-frame write visibility.

---

## Irem M47 — 2 / 2

This section is split from M52/M62 because public driver-level evidence labels
Oli-Boo-Chu / Punching Kid as isolated M47 hardware and explicitly separates it
from the later M52 lineage.

#### Manifests / board bring-up
- [x] **I47-1** Local M47 Oli-Boo-Chu / Punching Kid ROM-set contracts — `src/manifests/irem_m47` carries checked-in embedded ROM-contract manifests for `olibochu` and `punchkid`, preserving the local filenames, region sizes, offsets, and CRC32 values for `maincpu`, `audiocpu`, `samples`, `gfx8x8`, `gfx16x16`, and `proms`. `punchkid` declares `olibochu` as parent and inherits shared audio, sample, 16x16 graphics, and PROM regions from the sibling parent wrapper while replacing its program and 8x8 graphics dumps. `MNEMOS_M47_SET_DIR=D:\emu\irem\M47` data-gates the local `olibochu` and `punchkid` ZIPs and proves both load CRC-clean through the embedded manifests. `scripts/irem/inventory-corpus.ps1` now classifies the two ZIP archives as direct player-loadable M47 items and `punchkid.7z` as metadata-only, so M47 support accounting no longer reports Oli-Boo-Chu / Punching Kid as contract-only · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m47/games/*.toml` + `src/manifests/irem_m47/tests/m47_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I47-2** Executable M47 board profile — First-pass route exists for the M47 family: `src/manifests/irem_m47/m47_system.cpp` assembles a vertical Z80 main CPU, Z80 sound CPU, dual YM2149/AY-compatible SSG, `maincpu` / `audiocpu` / `samples` / `gfx8x8` / `gfx16x16` / `proms` media regions, video/color/sprite/work/sound RAM, input/DIP MMIO, scroll registers, flip latch, sound-command latch IRQ/ack, region-backed diagnostic 2-plane video, save-state identity, and player adapter registration. `src/apps/player/adapters/irem_m47` registers `--system irem_m47` / `m47`, supports ZIPs, folders, single-inner wrapper ZIPs, parent fallback, media validation, rollback-ready save-state, capability discovery, and vertical display metadata. `MNEMOS_M47_SET_DIR=D:\emu\irem\M47` proves both ZIP routes through the adapter; direct player smokes wrote 256x256 nonblack PPM screenshots for `olibochu` and `punchkid` and a rollback save state for `punchkid`. This is smoke-playable, not authentic parity: exact M47 memory/I/O timing, video/color PROM behavior, input/DIP behavior, AY/sample sound timing, and trusted visual/audio parity remain open · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m47/m47_system.cpp` + `src/manifests/irem_m47/tests/m47_system_test.cpp` + `src/apps/player/adapters/irem_m47/*` + `MNEMOS_M47_SET_DIR=D:\emu\irem\M47` corpus golden + direct `mnemos_player --system irem_m47` / `--system m47` smoke

---

## Irem M52 — 1 / 2

This section is split from both M15 and M72 so Moon Patrol-era Z80 hardware gets
its own board identity instead of being treated as either early 6502 hardware or
later V30 M72-family hardware.

#### Manifests / board bring-up
- [x] **I52-1** Moon Patrol ROM-set contract — `src/manifests/irem_m52` carries checked-in embedded ROM-contract manifests for `mpatrol` and `mpatrolw`, including program, sound, text, sprite, PROM regions, and 13 SW1/SW2 DIP definitions from the Moon Patrol Instruction Manual; the Williams clone inherits the parent DIP table. The local wrappers under `D:\emu\irem\M52` are now tracked M52 ZIP routes in `scripts/irem/inventory-corpus.ps1`; `mpatrolw` declares `mpatrol` as parent and resolves shared parent dumps from a sibling wrapper or supplemental media instead of accepting missing shared files as clean proof · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m52/games/*.toml` + `src/manifests/irem_m52/tests/m52_rom_contract_test.cpp` + `src/apps/player/adapters/irem_m52/tests/irem_m52_adapter_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `THIRD-PARTY-REFERENCES.md`
- [~] **I52-2** Executable M52 board profile — `src/manifests/irem_m52` now assembles a first-pass Irem M52 board route with Z80 main CPU, Moon Patrol memory windows, input/DIP MMIO, scroll/background control latches, deterministic board identity, rollback-ready save/load, player adapter registration under `--system irem_m52` / `m52`, capability discovery, service/test input mapping with save-state proof, real local corpus data-gate, and nonblank framebuffer output. The board now owns two native YM2149/AY-3-8910-compatible SSG instances and a native OKI MSM5205 ADPCM decoder with register introspection, save/load coverage, and adapter audio mixing. It also owns and schedules a second Z80 sound CPU with mapped `soundcpu` ROM/RAM, sound-command latch IRQ/ack state, save-state coverage, and adapter/capability exposure as `memory.z80_1.registers`; the sound CPU can read/ack the command latch and write the currently modeled AY/MSM ports. Main-CPU sound commands now only update the command latch and IRQ line; focused coverage proves a latch write leaves AY/MSM state untouched until the sound Z80 executes its port sequence. The adapter retains parsed Moon Patrol DIP metadata, folds the manual's active-high factory defaults to board-visible `dsw1=0x01` / `dsw2=0x02`, exposes `DIP switches=13`, and bumps the M52 board/save-target revision for the corrected sound-ownership identity. The compositor no longer uses executable program/sound ROM bytes or generic work RAM bytes as direct pixel entropy; focused tests prove executable-region changes leave identical video output, and a 4-byte sprite-RAM record pass now renders 16x16 object pixels through the declared `sprite_gfx` region before the text layer. `GLD-M52-PARITY-HASH` is registered as a skipped-until-pinned visual/audio SHA-256 oracle over a reference-captured M52 set, hashing the final RGBA framebuffer and interleaved s16le audio after a deterministic frame count. Remaining: replace the remaining first-pass background placeholder with board-evidenced parallax/road/text priority, verify the exact M52 sound CPU port map plus MSM5205 stream timing against board evidence, implement discrete-analog behavior beyond the currently modeled AY/MSM port surfaces, clarify the Moon Patrol / Tropical Angel board split against primary evidence, and prove runtime DIP behavior beyond current manual defaults, raster timing, and trusted screenshot/audio parity hashes before calling Moon Patrol authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/chips/audio/msm5205/*` + `src/manifests/irem_m52/m52_system.cpp` + `src/manifests/irem_m52/tests/m52_system_test.cpp` + `src/apps/player/adapters/irem_m52/irem_m52_adapter.cpp` + `src/apps/player/adapters/irem_m52/tests/irem_m52_adapter_test.cpp` + `tests/oracles/registry.yaml` + `docs/architecture/factsheets/irem-system-boards-reference.md`

Video note: the M52 text pass now uses the shared layer plotter, so flip-screen
mirrors final tile positions in addition to glyph pixel order.

Cabinet input note: the M52 adapter now consumes explicit frontend `service`
and `test` inputs, keeps `mode` as the service-credit alias for older callers,
maps service to system bit `0x08`, maps operator-test to system bit `0x10`,
and persists explicit `service` / `test` fields in adapter state version 1.

DIP note: `mpatrol` now carries 13 active-high SW1/SW2 DIP entries transcribed
from the Moon Patrol Instruction Manual. `mpatrolw` inherits the table; the
adapter folds the factory default to `dsw1=0x01`, `dsw2=0x02`, publishes
`DIP switches=13`, and still honors explicit `--dip` override.

---

## Irem M57 — 2 / 2

This section is split from M52/M58 because the local Tropical Angel / New
Tropical Angel evidence is sorted under M57 and should not inherit Moon Patrol
or 10-Yard Fight board behavior without primary board evidence.

#### Manifests / board bring-up
- [x] **I57-1** Local M57 Tropical Angel raw-media ROM contracts — `src/manifests/irem_m57` carries checked-in embedded raw-media manifests for `newtangl` and `troangel`, preserving the local filenames, region sizes, offsets, and CRC32 values from `D:\emu\irem\M57\newtangl.zip` and `D:\emu\irem\M57\troangel.zip`. `MNEMOS_M57_SET_DIR=D:\emu\irem\M57` data-gates both local ZIPs and proves they load CRC-clean through the embedded manifests. `scripts/irem/inventory-corpus.ps1` classifies `newtangl.zip` and `troangel.zip` as direct player-loadable while leaving the three local `.7z` archives metadata-only until converted or unpacked, so M57 no longer reports either Tropical Angel route as unimplemented in the local corpus accounting · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m57/games/*.toml` + `src/manifests/irem_m57/tests/m57_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I57-2** Executable M57 board profile — First-pass route exists for Tropical Angel / New Tropical Angel: `src/manifests/irem_m57/m57_system.cpp` derives a Z80 execution window and diagnostic graphics window from the raw-media artifact contract, owns scratch/video/color/work RAM, active-high arcade inputs, a sound latch, beeper-backed synthetic audio, deterministic 256x256 nonblank video, board identity save/load, player adapter registration, capability discovery, and local corpus smoke. `src/apps/player/adapters/irem_m57` registers `--system irem_m57` / `m57`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml`, resident media validation, rollback-ready save-state, and real local M57 smoke through `MNEMOS_M57_SET_DIR=D:\emu\irem\M57`. Direct player smokes wrote a nonblank screenshot for `newtangl.zip` and a rollback save state through the `m57` alias. This is smoke-playable, not authentic parity: exact M57 memory/I/O maps, video/color behavior, Irem Audio path, inputs/DIPs, Tropical Angel / New Tropical Angel timing, and trusted visual/audio parity remain open · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m57/m57_system.cpp` + `src/manifests/irem_m57/tests/m57_system_test.cpp` + `src/apps/player/adapters/irem_m57/*` + `MNEMOS_M57_SET_DIR=D:\emu\irem\M57` corpus golden + direct `mnemos_player --system irem_m57` / `--system m57` smoke

---

## Irem M58 — 2 / 2

This section is split from M52/M62 because 10-Yard Fight uses its own M58 board
identity and should not inherit Moon Patrol or later M62 behavior without board
evidence.

#### Manifests / board bring-up
- [x] **I58-1** Local M58 10-Yard Fight ROM-set contracts — `src/manifests/irem_m58` carries checked-in embedded ROM-contract manifests for `10yard`, `10yardj`, `vs10yard`, and `vs10yardj`, preserving the local filenames, region sizes, offsets, and CRC32 values for `maincpu`, `soundcpu`, `tiles`, `sprites`, and `proms`. Regional and Vs. sets declare `10yard` as parent and inherit shared media from the sibling parent wrapper. `MNEMOS_M58_SET_DIR=D:\emu\irem\M58` data-gates the four canonical local ZIPs and proves they load CRC-clean through the embedded manifests; `D:\emu\irem\M58\artwork\10yard-artwork.zip` is artwork/layout only and is classified as metadata-only non-ROM proof rather than a board-family candidate. `scripts/irem/inventory-corpus.ps1` now classifies the four ZIP archives as direct player-loadable M58 items, the five 7z archives as metadata-only conversion/unpack routes, and the artwork ZIP as ignored non-ROM evidence, so M58 support accounting no longer reports 10-Yard Fight as contract-only · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m58/games/*.toml` + `src/manifests/irem_m58/tests/m58_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I58-2** Executable M58 board profile — `src/manifests/irem_m58/m58_system.cpp` now assembles a first-pass M58 board route with main Z80, sound MC6803, 10-Yard Fight ROM regions, the `soundcpu` high-ROM window at `$8000-$ffff`, real MC6803 reset-vector fetch from `$fffe/$ffff`, video/color/sprite/work/sound RAM, input/DIP MMIO, row scroll writes, flip latch, sound-command latch IRQ/ack state, first-pass MC6803 direct-page latch/SSG MMIO, two YM2149-compatible SSGs, ROM-backed tile/sprite/PROM diagnostic rendering, board identity save/load, and focused tests for MC6803-owned PSG writes, deterministic video independent of executable ROM entropy, sprite RAM rendering, flip-screen tile mirroring, and save-state rejection across mismatched board identity. `src/chips/cpu/m6803` now includes reset-vector, reset-line parking, stack, branch, ALU, load/store, register-introspection, and save-state tests, and `src/manifests/irem_m58/tests/m58_rom_contract_test.cpp` proves the real local `10yard` `soundcpu` reset vector points to `$fa80` inside the mapped high-ROM window. `src/apps/player/adapters/irem_m58` registers `--system irem_m58` and alias `m58`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml`, clone-parent fallback, resident media validation, rollback-ready save-state, capability discovery, service/test input mapping, optional parity hash oracle, and real local corpus smoke for `10yard`, `10yardj`, `vs10yard`, and `vs10yardj`. Direct `mnemos_player --system irem_m58 --rom D:\emu\irem\M58\10yard.zip --screenshot build\scratch\m58_10yard.ppm --frames 60` produced a nonblank 256x256 frame, and `--system m58` save-state smoke wrote a state for `10yardj`. Remaining: replace diagnostic video/color/priority/radar assumptions with board-evidenced 10-Yard Fight behavior, finish exact MC6803 ports/timers and Irem Audio timing beyond the current first-pass latch/SSG MMIO, add board/manual-backed DIP behavior, and pin visual/audio parity hashes before calling M58 correct · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/chips/cpu/m6803/m6803.cpp` + `src/chips/cpu/m6803/tests/m6803_test.cpp` + `src/manifests/irem_m58/m58_system.cpp` + `src/manifests/irem_m58/tests/m58_system_test.cpp` + `src/manifests/irem_m58/tests/m58_rom_contract_test.cpp` + `src/apps/player/adapters/irem_m58/irem_m58_adapter.cpp` + `src/apps/player/adapters/irem_m58/tests/irem_m58_adapter_test.cpp` + `scripts/irem/run-local-corpus.ps1`

---

## Irem M62 — 2 / 2

This section is split from M52/M72 so the Lode Runner / Spelunker / Battle Road
Z80+M6803 hardware is tracked as its own board-family contract instead of being
folded into neighboring Irem profiles.

#### Manifests / board bring-up
- [x] **I62-1** Local M62 ROM-set contracts — `src/manifests/irem_m62` carries checked-in embedded manifests for `battroad`, `bkungfu`, `horizon`, `kidniki`, `kungfum`, `ldrun`, `ldruna`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, `spelunk2`, `spartanx`, `yanchamr`, and `youjyudn`. `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` now use explicit `maincpu`, `soundcpu`, graphics, PROM, and timing regions with the MC6803 reset vector proven in the `$8000-$ffff` sound-ROM window; `ldrun3j` declares parent `ldrun3` and inherits shared MC6803, tile, PROM, and timing artifacts through clone-parent fallback. The raw-media sets preserve exact local file names, sizes, CRC32 values, and contiguous raw-media offsets until their final CPU/video/audio placement is wired. `MNEMOS_M62_SET_DIR=D:\emu\irem\M62` data-gates the exact local ZIPs and proves all 16 checked-in set IDs load CRC-clean through the embedded manifests. `scripts/irem/inventory-corpus.ps1 -Root D:\emu\irem -Recurse` classifies the M62 bucket as 26 tracked items, 16 loadable/supported ZIP-style routes, and 10 metadata-only archives, so M62 support accounting no longer reports Kung-Fu Master / Kid Niki / Lode Runner / Spelunker / Battle Road / Youjyuden as contract-only · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m62/games/*.toml` + `src/manifests/irem_m62/tests/m62_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I62-2** Executable M62 board profile — First-pass route exists for the M62 family: `src/manifests/irem_m62/m62_system.cpp` derives a Z80 execution window and diagnostic graphics window, owns scratch/video/color/work RAM, active-high arcade inputs, a sound latch, beeper-backed synthetic audio, deterministic 256x256 nonblank video, board identity save/load, player adapter registration, capability discovery, and local corpus smoke. `ldrun`, `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` now also wire the MC6803 `$8000-$ffff` sound-ROM window; `ldrun` has direct-page sound latch/ack MMIO and dual SSG programming proof, while `ldrun2`, `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` have reset-vector and first-pass MC6803 residency proof. `src/apps/player/adapters/irem_m62` exposes Z80, MC6803, and both SSG and both MSM5205 register surfaces and drains mixed beeper/SSG/MSM audio. The adapter registers `--system irem_m62` / `m62`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml`, clone-parent fallback, resident media validation, rollback-ready save-state, and real local corpus smoke for all sixteen checked-in M62 set IDs. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\ldrun.zip --frames 90 --screenshot build\scratch\irem-m62\ldrun-msm.ppm` produced a nonblank 256x256 frame, `--system m62` save-state smoke wrote a 19,549-byte state for `ldrun`, and `--extract-audio build\scratch\irem-m62\ldrun-msm-audio --extract-frames 90` wrote a 298,324-byte rendered WAV. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\ldrun2.zip --frames 90 --screenshot build\scratch\irem-m62\ldrun2-regioned.ppm` produced a nonblank 256x256 frame, and the `ldrun2` save-state smoke wrote a 24,902-byte state. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\ldrun3.zip --frames 90 --screenshot build\scratch\irem-m62\ldrun3-regioned.ppm` produced a nonblank 256x256 frame, and the `ldrun3` save-state smoke wrote a 16,127-byte state. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\ldrun3j.zip --screenshot build\scratch\irem-m62\ldrun3j-regioned.ppm --frames 90` produced a nonblank 256x256 frame, `--system m62` save-state smoke wrote a 16,189-byte state, and `--extract-audio build\scratch\irem-m62\ldrun3j-regioned-audio --extract-frames 90` wrote a 298,324-byte rendered WAV. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\ldrun4.zip --screenshot build\scratch\irem-m62\ldrun4-regioned.ppm --frames 90` produced a nonblank 256x256 frame, `--system m62` save-state smoke wrote a 20,040-byte state, and `--extract-audio build\scratch\irem-m62\ldrun4-regioned-audio --extract-frames 90` wrote a 298,324-byte rendered WAV. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\lotlot.zip --screenshot build\scratch\irem-m62\lotlot-regioned.ppm --frames 90` produced a nonblank 256x256 frame, `--system m62` save-state smoke wrote an 11,282-byte state, and `--extract-audio build\scratch\irem-m62\lotlot-regioned-audio --extract-frames 90` wrote a 298,324-byte rendered WAV. Direct `mnemos_player --system irem_m62 --rom D:\emu\irem\M62\spelunk2.zip --screenshot build\scratch\irem-m62\spelunk2-regioned.ppm --frames 90` produced a nonblank 256x256 frame, `--system m62` save-state smoke wrote a 15,498-byte state, and `--extract-audio build\scratch\irem-m62\spelunk2-regioned-audio --extract-frames 90` wrote a 298,324-byte rendered WAV. This is smoke-playable, not authentic parity: exact M62 title memory/I/O maps, MC6803 port/timer behavior, exact dual MSM5205 stream/control timing, KNA custom video/priority, inputs/DIPs, and visual/audio parity remain open · PARTIAL · HIGH · L · beyond Emu · Evidence: `src/chips/cpu/m6803/m6803.cpp` + `src/chips/cpu/m6803/tests/m6803_test.cpp` + `src/manifests/irem_m62/m62_system.cpp` + `src/manifests/irem_m62/tests/m62_system_test.cpp` + `src/apps/player/adapters/irem_m62/irem_m62_adapter.cpp` + `src/apps/player/adapters/irem_m62/tests/irem_m62_adapter_test.cpp` + `MNEMOS_M62_SET_DIR=D:\emu\irem\M62` corpus golden + direct `mnemos_player --system irem_m62` / `--system m62` smoke

---

## Irem M63 — 2 / 2

This section is split from M62/M72 so Wily Tower and Fighting Basketball stay
tracked as sparse late-8-bit M63 evidence instead of inheriting a neighboring
board profile.

#### Manifests / board bring-up
- [x] **I63-1** Local M63 Wily Tower ROM-set contract — `src/manifests/irem_m63` carries a checked-in embedded ROM-contract manifest for `wilytowr`, preserving the local Wily Tower filenames, region sizes, offsets, and CRC32 values for `maincpu`, `soundcpu`, `gfx1`, `gfx2`, `gfx3`, `user1`, and `proms`. `MNEMOS_M63_SET_DIR=D:\emu\irem\M63` data-gates the local `wilytowr` ZIP and proves it loads CRC-clean through the embedded manifest. `scripts/irem/inventory-corpus.ps1` classifies `wilytowr.zip` as direct player-loadable and leaves the two local `.7z` archives metadata-only until converted or unpacked, so Wily Tower no longer contributes to contract-only support accounting · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m63/games/wilytowr.toml` + `src/manifests/irem_m63/tests/m63_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I63-2** Executable M63 board profile — First-pass route exists for Wily Tower: `src/manifests/irem_m63/m63_system.cpp` assembles a Z80 diagnostic board shell with program ROM `$0000-$7fff`, video/color/work/scratch RAM, input/DIP/control MMIO, sound-command latch, Wily Tower media regions, synthetic combined graphics media from `gfx1` / `gfx2` / `gfx3` / `proms`, nonblank diagnostic video, beeper-backed audio for synthetic programs, save-state identity, and player adapter registration. `src/apps/player/adapters/irem_m63` registers `--system irem_m63` / `m63`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local Wily Tower player smoke through `MNEMOS_M63_SET_DIR=D:\emu\irem\M63`. This is smoke-playable, not authentic parity: exact M63 Z80/8039 board maps, 8039-class sound CPU behavior, AY/sample/discrete audio, tile/sprite/video/color-PROM behavior, inputs/DIPs, Fighting Basketball manifest coverage, raster timing, and trusted visual/audio parity remain open · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m63/m63_system.cpp` + `src/manifests/irem_m63/tests/m63_system_test.cpp` + `src/apps/player/adapters/irem_m63/*` + `MNEMOS_M63_SET_DIR=D:\emu\irem\M63` corpus golden + direct `mnemos_player --system irem_m63` / `--system m63` smoke

---

## Irem Traverse USA / Zippy Race — 1 / 2

This section tracks the early Zippy Race / Traverse USA family separately from
the Moon Patrol M52 runtime profile because public metadata maps it to
`irem/travrusa.cpp`; the physical local corpus files now live under the M52-era
bucket instead of a top-level game-family folder.

#### Manifests / board bring-up
- [x] **ITRAV-1** Local travrusa ROM-set contracts — `src/manifests/irem_travrusa` carries checked-in embedded ROM-contract manifests for `travrusa`, `motorace`, `travrusab`, and `travrusab2`, preserving local filenames, region sizes, offsets, CRC32 values, parent fallback, and aliases for the split wrappers. `MNEMOS_TRAVRUSA_SET_DIR=D:\emu\irem\M52` data-gates the local ZIP corpus, selects the CRC-clean parent route instead of the artwork/layout-only `travrusa.zip`, and proves the single-inner wrapper ZIPs load through embedded manifests · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_travrusa/games/*.toml` + `src/manifests/irem_travrusa/tests/travrusa_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **ITRAV-2** Executable travrusa board profile — First-pass route exists for the travrusa family: Z80 main CPU, Z80 sound CPU, dual SSG, MSM5205, title memory/MMIO windows, tile/sprite/PROM compositor, inputs, save-state identity, player adapter registration, local corpus smoke, and direct player launch proof are wired. This is smoke-playable, not authentic parity: MotoRace encrypted ROM handling, exact video priority/scroll/color behavior, Irem Audio timing, DIP/input parity, and trusted visual/audio parity remain open · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_travrusa/*` board implementation + `src/apps/player/adapters/irem_travrusa/*` + `MNEMOS_TRAVRUSA_SET_DIR=D:\emu\irem\M52` corpus golden + direct `mnemos_player --system irem_travrusa` smoke

---

## Irem M75 — 1 / 2

This section is split from M72/M81 because the current Vigilante and Meikyu
Jima / Kickle Cubicle routes are Z80 main CPU plus Z80 sound CPU boards with
YM2151/DAC audio, not the V30 route used by true M72 and later M8x profiles.

#### Manifests / board bring-up
- [x] **I75-1** M75 ROM-set contracts — `src/manifests/irem_m75` carries checked-in embedded ROM-contract manifests for parent `vigilant`, official regional clones `vigilanta`, `vigilantb`, `vigilantc`, `vigilantd`, `vigilantg`, and `vigilanto`, the bootleg clone `vigilantbl`, and standalone `kikcubic`. The Vigilante parent includes the Z80 main program with banked `$8000-$bfff` reload area, sound CPU ROM, character graphics, sprite graphics with ROM-continue duplication, background tile graphics, samples, PROM, PLD regions, and 14 SW1/SW2 DIP definitions from the Vigilante Installation & Service Manual; clones declare their changed program/graphics regions and inherit shared parent media plus the DIP table. `vigilantbl` preserves its local bootleg program, split sprite, three-PROM, and PAL dumps while resolving shared sound/character/background/sample media through the `vigilant` parent. `kikcubic` preserves the local ZIP filenames, 0x30000 main program, sound, character, sprite, sample, 0x0140 PROM regions, and 13 SW1/SW2 DIP definitions; the public no-dump PALs are omitted rather than faked. `MNEMOS_M75_SET_DIR=D:\emu\irem\M75` data-gates the complete local parent wrapper, official clone wrappers, bootleg wrapper, and `D:\emu\irem\M75\kikcubic.zip`; `scripts/irem/inventory-corpus.ps1` now records 17 tracked M75 local artifacts, 9 direct player-loadable/supported ZIP routes, and no remaining M75 board-family candidate · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m75/games/*.toml` + `src/manifests/irem_m75/tests/m75_rom_contract_test.cpp` + `scripts/irem/inventory-corpus.ps1` + `THIRD-PARTY-REFERENCES.md`
- [~] **I75-2** Executable M75 board profile — `src/manifests/irem_m75/m75_system.cpp` now assembles a first-pass M75-owned Z80/Z80/YM2151/DAC board with fixed and banked main ROM windows, sprite/palette/video/work RAM, sound ROM/RAM, active-low inputs/DIPs, Vigilante bank/scroll/rear-color latches, kikcubic main-CPU DSW/input/bank/sound-latch ports, sound latch IRQ/ack, sample-address and DAC ports, a two-bank 5-bit KNA91-style palette bus, rear color/disable register semantics, whole-board save/load identity, and diagnostic video composition over the checked-in graphics/sample/PROM regions. Focused board tests now prove the sound Z80 programs the sample-address ports, reads consecutive sample ROM bytes through the modeled sample port, writes them through the DAC on the sound CPU elapsed-clock timeline, acknowledges the latch, and that `kikcubic` reads `dsw1=0xff` / `dsw2=0xd5` through its own port layout while writing bank and sound latch ports. `src/apps/player/adapters/irem_m75` registers `--system irem_m75` / `m75`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked folders, embedded or in-archive `game.toml` manifests, clone/parent fallback media resolution, resident media validation, rollback-ready save-state, capability discovery, service/test input mapping with save-state proof, parsed DIP metadata retention, per-set manifest-default folding, and real local player smoke through `MNEMOS_M75_SET_DIR=D:\emu\irem\M75`, including `kikcubic.zip`. Direct `mnemos_player --system irem_m75 --rom D:\emu\irem\M75\kikcubic.zip --frames 180 --screenshot build\scratch\kikcubic_m75.ppm` produced a nonblank 256x256 PPM, and `--system m75` wrote a 105312-byte save state for the same ZIP. Remaining: replace the diagnostic compositor with board-evidenced Vigilante and Meikyu Jima tile/sprite/background priority, verify exact memory/I/O, runtime DIP behavior beyond current defaults, and raster timing, prove sample/DAC timing against reference evidence, prove bootleg/PROM/color behavior, and collect screenshot/audio parity before calling any M75 game correct · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m75/m75_system.cpp` + `src/manifests/irem_m75/tests/m75_system_test.cpp` + `src/apps/player/adapters/irem_m75/irem_m75_adapter.cpp` + `src/apps/player/adapters/irem_m75/tests/irem_m75_adapter_test.cpp` + `docs/architecture/factsheets/irem-system-boards-reference.md`

Cabinet input note: the M75 adapter now consumes explicit frontend `service`
and `test` inputs, keeps `mode` as the service-credit alias for older callers,
maps Vigilante service to system bit `0x10`, maps Vigilante operator-test to
system bit `0x20`, maps kikcubic service/mode to the third coin bit `0x40`,
and persists explicit `service` / `test` fields in adapter state version 2.

DIP note: `vigilant` now carries 14 SW1/SW2 DIP entries transcribed from the
Vigilante Installation & Service Manual. M75 clones inherit the table; the
adapter folds the raw active-low default to `dsw1=0xff`, `dsw2=0xfd`, publishes
`DIP switches=14`, and still honors explicit `--dip` override. `kikcubic`
carries 13 public SW1/SW2 DIP entries and folds to `dsw1=0xff`,
`dsw2=0xd5`.

---

## Irem M78 — 2 / 2

This section tracks Black Jack (`bj92`) as an isolated sparse M78
gambling/satellite board instead of folding it into M72 or the V30 M8x family.

#### Manifests / board bring-up
- [x] **I78-1** Local M78 BJ92 ROM-set contract — `src/manifests/irem_m78`
  carries a checked-in embedded ROM-contract manifest for `bj92`, preserving the
  local dumped main Z80 program, sound Z80 program, duplicate tile ROM groups,
  and PROM filenames, region sizes, offsets, and CRC32 values. The public
  no-dump M72-audio sample placeholders `3.v0.ic46` and `4.v1.ic47` are modeled
  as an explicit zero-filled `m72_audio` region with no fake file entries.
  `MNEMOS_M78_SET_DIR=D:\emu\irem\M78` data-gates the local `bj92.zip` and
  proves the dumped regions load CRC-clean through the embedded manifest.
  `scripts/irem/inventory-corpus.ps1` classifies `bj92.zip` as a direct
  player-loadable first-pass route and leaves `bj92.7z` metadata-only until
  converted or unpacked, so BJ92 no longer appears as an unsupported M78
  board-family candidate · DONE · MED · S ·
  beyond Emu · Evidence: `src/manifests/irem_m78/games/bj92.toml` +
  `src/manifests/irem_m78/tests/m78_rom_contract_test.cpp` +
  `scripts/irem/inventory-corpus.ps1` + `scripts/irem/run-local-corpus.ps1`
- [~] **I78-2** Executable M78 board profile — First-pass route exists for
  Black Jack (`bj92`): `src/manifests/irem_m78` assembles a sparse dual-Z80
  board shell with YM2151/DAC save-state proof, media validation, player adapter
  registration, and direct nonblank 512x384 screenshot proof. This is
  smoke-playable, not authentic parity: exact BJ92 I/O/comms, video-register
  model, satellite/main-screen routing, sample-ROM/no-dump behavior, input/DIP
  behavior, and trusted visual/audio parity remain open.

---

## Irem M81 — 1 / 2

This section is split from M72 so Dragon Breed, Hammerin' Harry, and X Multiply
sets do not inherit true-M72 board wiring when they are local M81 artifacts.

#### Manifests / board bring-up
- [x] **I81-1** Local M81 ROM-set contract — `src/manifests/irem_m81` carries checked-in embedded ROM-contract manifests for `dbreed`, `hharry`, and `xmultipl`, with parser/region-contract coverage for the 1 MiB V30 program region, boot-chunk reload declarations, sound CPU ROM, samples, graphics regions, and PROM metadata. `MNEMOS_M81_SET_DIR=D:\emu\irem\M81` data-gates the sorted local wrapper ZIPs and proves all three sets load CRC-clean from single-inner ZIP artifacts; `scripts/irem/inventory-corpus.ps1` now records five total M81 manifest matches when duplicate / misbucketed corpus copies are included · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m81/games/*.toml` + `src/manifests/irem_m81/tests/m81_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I81-2** Executable M81 board profile — `src/manifests/irem_m81/m81_system.cpp` now assembles an explicit M81 V30/Z80/YM2151/DAC/8259 board with a 1 MiB V30 program map, sound ROM + Z80 work RAM, inputs/DIPs, frame stepping, sound-Z80-clocked DAC event mixing plus YM drain support, whole-board save/load, scanline-composed video, CPU-visible KNA91-style low-byte palette writes and disconnected-A9 mirrors, per-layout media identity, and save-state rejection across mismatched M81 board-layout profiles. `src/apps/player/adapters/irem_m81` registers `--system irem_m81`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, resident media CRC/validation reporting, rollback-ready save-state, capability discovery, and real local player smoke for `dbreed`, `hharry`, and `xmultipl`. `MNEMOS_M81_SET_DIR=D:\emu\irem\M81` proves all three embedded M81 sets through the adapter. Remaining: replace/verify first-pass video priority, raster phase/timing, DIP behavior, palette-bank rendering/decode if board evidence requires it, and visual parity against board/manual evidence before calling the profile authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m81/m81_system.cpp` + `src/manifests/irem_m81/tests/m81_system_test.cpp` + `src/apps/player/adapters/irem_m81/irem_m81_adapter.cpp` + `src/apps/player/adapters/irem_m81/tests/irem_m81_adapter_test.cpp` + `docs/architecture/factsheets/irem-system-boards-reference.md`

---

## Irem M82 — 1 / 2

This section is split from M72 so local M82 artifacts, including R-Type II and
Major Title, do not inherit true-M72 assumptions. M82 now has its own executable profile, and the video path has a
RAM-backed first-pass tile/sprite/palette renderer composed per visible
scanline plus scanline-paced vblank / raster-compare IRQ delivery, with
tile priority groups partitioned into below-sprite and above-sprite passes and
a diagnostic fallback for uninitialized development launches pending
board-accurate DIP / raster-phase proof.

#### Manifests / board bring-up
- [x] **I82-1** Local M82 ROM-set contract — `src/manifests/irem_m82` carries checked-in embedded manifests for `airduel`, `airduelu`, `dkgensanm82`, `majtitle`, `majtitlej`, `rtype2`, `rtype2j`, `rtype2jc`, and the local nested `rtype2m82b` artifact, with parser/region-contract coverage for clone-parent inheritance, both known 1 MiB main-program reset-vector reload layouts, sound CPU ROM, voice/sample ROM, tile/background/sprite graphics regions, PROM/PLD metadata, and vertical Air Duel orientation. `MNEMOS_M82_SET_DIR=D:\emu\irem\M82` data-gates real local artifacts and proves all nine embedded M82 sets load CRC-clean from standard wrapper ZIPs, unpacked folders, or parent fallback, including reset-vector reload equality and non-fill resident regions; the local Air Duel parent/US clone wrappers are `D:\emu\irem\M72\Air-Duel_Arcade_EN (1).zip` and `D:\emu\irem\M72\Air-Duel_Arcade_EN (2).zip`, Daiku no Gensan M82 is validated from `D:\emu\irem\M82\dkgensanm82` unpacked from the standalone `.7z` because the local split ZIP lacks shared sprite/tile/PAL dumps, the local Major Title parent wrapper is `D:\emu\irem\Major-Title_Arcade_EN.zip`, the Japan wrapper is `D:\emu\irem\Major-Title_Arcade_JA.zip`, and the data-gate source index keeps collection wrappers ahead of stale unpacked folders except for the verified complete `dkgensanm82` unpacked route · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m82/games/*.toml` + `src/manifests/irem_m82/m82_game_manifests.hpp` + `src/manifests/irem_m82/tests/m82_system_test.cpp`
- [~] **I82-2** Executable M82 board profile — `src/manifests/irem_m82/m82_system.cpp` now assembles an explicit M82 V30/Z80/YM2151/DAC/8259 board with a 1 MiB V30 program map, sound ROM + Z80 work RAM, inputs/DIPs, frame stepping, sound-Z80-clocked DAC event mixing plus YM drain timing, whole-board save/load, scanline-sliced V30 execution with one-line IR0 vblank and IR2 raster-compare pulses, and an M82-local video path that uses VRAM-backed 8x8 planar tilemaps, a dedicated `backgrounds` ROM region for the rear tilemap when present, rowscroll RAM, 5-bit palette RAM with CPU-visible KNA91-style low-byte writes and disconnected-A9 mirrors, sprite-DMA-latched 16x16 planar cells, flip-screen state, scanline composition before the CPU tick for that beam line, M72-style tile priority groups split into below-sprite and above-sprite passes, and save/load of the latched sprite buffer, while retaining a diagnostic fallback only when no hardware render state is initialized; focused board/video tests now prove a raster-compare V30 handler changing palette RAM affects later scanlines without repainting earlier scanlines, KNA91 low-byte palette bus behavior, group-0 front pens stay below sprites, group-2 front pens cover sprites, and the dedicated background graphics region renders even when foreground tile graphics are absent. `src/apps/player/adapters/irem_m82` registers `--system irem_m82`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, clone parent fallback, supplemental parent media, resident media CRC/validation reporting, rollback-ready save-state, capability discovery, and real M82 player smoke. `MNEMOS_M82_SET_DIR=D:\emu\irem\M82` proves all nine embedded M82 sets through the adapter; direct Air Duel parent/US clone, Major Title parent/Japan, R-Type II parent/clone, and Daiku no Gensan M82 routes produce nonblank frames in the data-gated adapter smoke, and direct `mnemos_player --system irem_m82 --rom D:\emu\irem\M82\dkgensanm82` wrote a 384x256 nonblank PPM plus save state after 180 frames. Remaining: prove Major Title/Air Duel/Daiku no Gensan background and sprite priority/parity against reference evidence, verify/replace first-pass M82 palette-bank rendering/decode, exact raster phase/timing, board-manual DIP behavior, and real visual-priority parity before calling any M82 game visually authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m82/m82_system.cpp` + `src/manifests/irem_m82/tests/m82_system_test.cpp` + `src/apps/player/adapters/irem_m82/irem_m82_adapter.cpp` + `src/apps/player/adapters/irem_m82/tests/irem_m82_adapter_test.cpp` + `docs/architecture/factsheets/irem-system-boards-reference.md`

---

## Irem M84 — 1 / 2

This section is split from M81 because the local Hammerin' Harry M84 artifacts
are split clone sets with M84-specific program/PAL dumps but inherited parent
graphics, sound, sample, and PROM assets. The local `ltswords`, `kengo`, and
`kengoj` routes are tracked here as M84 V35-profile sets, with the Ken-Go split
sets inheriting Lightning Swords media.

#### Manifests / board bring-up
- [x] **I84-1** Local M84 ROM-set contract — `src/manifests/irem_m84` carries checked-in embedded ROM-contract manifests for `cosmccop`, `dkgensan`, `dkgensana`, `gallop`, `hharryb`, `hharryu`, `kengo`, `kengoj`, and `ltswords`, with parser/region-contract coverage for the 1 MiB V30/V35 program reload layouts, M84-specific Hammerin' Harry / Daiku no Gensan PAL/PLD regions, corrected M84 second-program-pair placement at `0x60000` for the Hammerin' Harry-family M84 program layout, clone-parent inheritance from the M81 `hharry` parent, Ken-Go inheritance from the M84 `ltswords` parent, `cosmccop` inheritance from the M84 `gallop` parent, the standalone local `ltswords` program/sound/graphics/sample ROMs, the local Gallop program/sound/graphics/sample/PROM/PLD ROMs, the local Cosmic Cop program/sound/graphics/sample ROMs, and Gallop/Cosmic Cop's inherited 10 DIP switch definitions with composed default `0xf9bf`. `ltswords` declares an explicit `irem_m84_prom_pld` HLE profile because the local route lacks the small color PROM and PAL artifacts listed for the complete board, and `kengo` / `kengoj` inherit that declaration. `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72` data-gates the local M84 ZIP corpus plus the M81 parent route; `scripts/irem/inventory-corpus.ps1` now records eighteen M84 manifest matches in the M84 bucket with ten loadable/supported routes across the nine checked-in sets · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m84/games/*.toml` + `src/manifests/irem_m84/tests/m84_system_test.cpp` + `src/apps/player/adapters/irem_m84/tests/irem_m84_adapter_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I84-2** Executable M84 board profile — `src/manifests/irem_m84` now has an M84-owned executable wrapper for the local Hammerin' Harry / Daiku no Gensan M84 split sets plus the V35-profile `ltswords`, `kengo`, `kengoj`, `gallop`, and `cosmccop` routes, using the M81-compatible Z80/YM2151/DAC/KNA91-style board core behind an M84 manifest/save identity while selecting V30 for Hammerin' Harry / Daiku no Gensan profiles and V35 for `ltswords` / `kengo` / `kengoj` / `gallop` / `cosmccop`. The compatibility core exposes the shared KNA91-style low-byte palette bus through the M84 wrapper while preserving M84 save identity and rejecting save-states restored under the wrong M84 CPU/layout profile. `src/apps/player/adapters/irem_m84` registers `--system irem_m84` / `m84`, composes M84 child zips or folders with either M84 or M81 parent media when declared, loads standalone M84 folders/ZIPs, reports resident media validation, exposes rollback-ready save-state, applies parsed manifest DIP defaults into the board DIP register (`gallop` / `cosmccop` default to `0xf9bf`), and data-gates real local player smoke through `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81;D:\emu\irem\M72`. Direct Daiku no Gensan and Ken-Go player smoke now saves state and writes 384x256 nonblank screenshots for `dkgensan`, `dkgensana`, `kengo`, and `kengoj`, in addition to the earlier Gallop/Cosmic Cop proof. Remaining: replace/verify the compatibility-core assumptions with board evidence for M84-specific memory/I/O behavior, Hammerin' Harry/Cosmic Cop/Ken-Go video/priority, raster timing, board-authentic DIP behavior beyond the current manifest defaults, missing `ltswords`/Ken-Go PROM/PLD artifacts, and screenshot/audio parity before calling this authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m84/m84_system.cpp` + `src/manifests/irem_m84/tests/m84_system_test.cpp` + `src/apps/player/adapters/irem_m84/irem_m84_adapter.cpp` + `src/apps/player/adapters/irem_m84/tests/irem_m84_adapter_test.cpp` + `docs/architecture/factsheets/irem-system-boards-reference.md`

Cabinet input note: M81, M82, and M84 adapters now consume explicit frontend
`service` / `test` arcade inputs, keep `mode` as the service alias for older
callers, map operator test onto the active-low system bit 6, and persist those
fields in adapter state version 2.

---

## Irem M85 - 1 / 2

#### Manifests / board bring-up
- [x] **I85-1** Pound for Pound ROM-set contract - `src/manifests/irem_m85`
  carries checked-in embedded ROM-contract manifests for `poundfor` and
  `poundforj`, with parser/region-contract coverage for the 1 MiB V30 program
  pair layout, reset-vector reload, sound CPU ROM, sample ROM, tile/sprite
  graphics regions, PLD artifacts, and clone-parent inheritance from the local
  `D:\emu\irem\M85` parent ZIP. `MNEMOS_M85_SET_DIR=D:\emu\irem\M85` data-gates
  both local ZIPs and proves CRC-clean parent fallback for the Japan split set
  through the embedded manifests. `scripts/irem/inventory-corpus.ps1` now records
  the sorted M85 bucket as player-supported metadata for both checked-in sets ·
  DONE · MED · S · beyond Emu · Evidence:
  `src/manifests/irem_m85/games/*.toml` +
  `src/manifests/irem_m85/tests/m85_system_test.cpp` +
  `scripts/irem/inventory-corpus.ps1`
- [~] **I85-2** Executable M85 board profile - `src/manifests/irem_m85` now has
  an M85-owned executable wrapper for `poundfor` and `poundforj`, using the
  shared M81-compatible V30/Z80/YM2151/DAC/KNA91-style core behind M85 manifest
  and save identities. `src/apps/player/adapters/irem_m85` registers
  `--system irem_m85` / `m85`, composes the Japan split clone with the M85
  parent, reports resident media validation, exposes rollback-ready save-state,
  and participates in the common data-gated Irem runner. Direct player smoke
  wrote 384x256 nonblank screenshots and save-state bytes for both local ZIPs.
  Remaining: replace/verify the compatibility-core assumptions with M85-specific
  memory/I/O, video/priority, input/DIP, raster, audio, and visual/audio parity
  evidence before calling the route authentic · PARTIAL · HIGH · M-L · beyond
  Emu · Evidence: `src/manifests/irem_m85/m85_system.cpp` +
  `src/manifests/irem_m85/tests/m85_system_test.cpp` +
  `src/apps/player/adapters/irem_m85/irem_m85_adapter.cpp` +
  `src/apps/player/adapters/irem_m85/tests/irem_m85_adapter_test.cpp` +
  `docs/architecture/factsheets/irem-system-boards-reference.md`

---

## Irem M90 / M97 / M99 — 1 / 2

This section covers the M90-generation V35/Z80/YM2151/DAC board path used by
the local Atomic Punk / Bomber Man World artifacts. It remains separate from
M92 because M90 retains the classic Z80 sound CPU and DAC path while using the
GA25 graphics custom.

#### Manifests / board bring-up
- [x] **I90-1** Local M90 ROM-set contract — `src/manifests/irem_m90`
  carries checked-in embedded ROM-contract manifests for `atompunk`, `bbmanw`,
  `bbmanwj`, `bbmanwja`, `gussun`, `hasamu`, `newapunk`, `quizf1`, and
  `riskchal`, with parser/region-contract coverage for the 1 MiB interleaved
  V35 main program region, reset-vector reload slices, Z80 program regions,
  resident GA25 graphics regions, DAC samples, and `quizf1` banked program
  media. `MNEMOS_M90_SET_DIR=D:\emu\irem` data-gates the current local M90 ZIPs
  under `D:\emu\irem\M90`, including Bomber Man World/Atomic Punk and Gussun
  split-clone parent fallback, and proves all nine load CRC-clean;
  `scripts/irem/inventory-corpus.ps1` records direct player-loadable M90 matches
  instead of treating those ZIPs as unsupported candidates · DONE · MED · S · beyond Emu · Evidence:
  `src/manifests/irem_m90/games/*.toml` +
  `src/manifests/irem_m90/tests/m90_rom_contract_test.cpp` +
  `scripts/irem/inventory-corpus.ps1`
- [~] **I90-2** Executable M90 board profile — `src/manifests/irem_m90` now
  assembles a first-pass M90-owned V35/Z80 shell with the main CPU configured as
  NEC V35 at 14.318181 MHz, a Z80 sound CPU at 3.579545 MHz, YM2151 plus
  sound-Z80-clocked unsigned 8-bit DAC output, M90 work/video/sprite/palette/rowscroll RAM
  windows, sound RAM, input/DIP ports, whole-board save/load identity, and an
  M90-local GA25 diagnostic video path driven by loaded program/audio/sample/
  graphics bytes plus board RAM. `src/apps/player/adapters/irem_m90` registers
  `--system irem_m90` / `m90`, supports ZIPs, single-inner wrapper ZIPs,
  unpacked set folders, embedded or in-archive `game.toml` manifests,
  parent-fallback shared media, resident media validation, rollback-ready
  save-state, capability discovery, parsed
  DIP metadata retention/default folding, `DIP switches` player-spec exposure,
  P1/P2 service plus operator-test input mapping with save-state proof, and
  real local player smoke through `MNEMOS_M90_SET_DIR=D:\emu\irem`; all nine
  checked-in sets step one frame, produce nonblank 384x256 diagnostic output,
  and emit save-state bytes. Remaining: authentic GA25 tile/sprite/row-scroll
  behavior, V35 on-die interrupt/timer behavior, Quiz F-1 banked program
  mapping, board-authentic DIP tables/runtime behavior, raster timing, and
  authentic screenshot/audio parity before calling this profile authentic · PARTIAL · HIGH · L · beyond Emu · Evidence:
  `src/manifests/irem_m90/m90_system.cpp` +
  `src/manifests/irem_m90/tests/m90_system_test.cpp` +
  `src/apps/player/adapters/irem_m90/irem_m90_adapter.cpp` +
  `src/apps/player/adapters/irem_m90/tests/irem_m90_adapter_test.cpp` +
  `docs/architecture/factsheets/irem-system-boards-reference.md`

---

## Irem M92 — 1 / 2

- [x] **I92-1** Local M92 ROM-set contract — `src/manifests/irem_m92`
  carries checked-in embedded ROM-contract manifests for `bmaster`,
  `crossbld`, `dsoccr94j`, `geostorm`, `geostorma`, `gunforce`, `gunforcej`,
  `gunforceu`,
  `gunforc2`, `gunhohki`, `hook`, `hookj`, `inthunt`, `inthuntk`,
  `inthuntu`, `kaiteids`, `leaguemn`, `leaguemna`, `lethalth`, `majtitl2`,
  `majtitl2a`, `majtitl2b`, `majtitl2j`, `mysticri`, `mysticrib`, `nbbatman`,
  `nbbatmanu`, `psoldier`, `rtypeleo`, `rtypeleoj`, `ssoldier`, `thndblst`,
  `uccops`, `uccopsar`, `uccopsj`, and `uccopsu`,
  including
  declared 1 MiB main program regions, 128 KiB encrypted V35 sound-program
  regions, tile/sprite/sample/PLD regions, cabinet metadata, local wrapper-ZIP
  CRCs, and clone-parent inheritance for the local Blade Master Japan, Gunforce
  Japan/US, Geostorm alternate custom-sound, Hook Japan, In the Hunt
  US/Japan/Korea, Lethal Thunder Japan/Thunder Blaster, Mystic Riders
  Japan/bootleg, Major Title 2 alternate-sound/World/Japan split sets, Ninja
  Baseball Bat Man US, Yakyuu Kakutou League-Man parent/alternate M92-Z-C
  wrappers, Superior/Perfect Soldiers Japan, R-Type Leo Japan, and Undercover
  Cops US/Japan/Alpha Renewal split sets, plus Dream Soccer '94 Japan M92
  hardware using explicit `dsoccr94.zip` supplemental shared media from the
  local M107 bucket.
  `MNEMOS_M92_SET_DIR=D:\emu\irem\M92;D:\emu\irem\M107` data-gates the sorted
  local M92 ZIPs and proves all thirty-six checked-in sets load CRC-clean;
  `scripts/irem/inventory-corpus.ps1` records 82 tracked M92 artifacts in the
  sorted M92 bucket, 37 direct player-loadable/supported ZIP routes, and 45
  metadata-only routes, including `.7z` files and non-ROM artwork/layout
  packages · DONE · MED · S · beyond Emu · Evidence:
  `src/manifests/irem_m92/games/*.toml` +
  `src/manifests/irem_m92/tests/m92_rom_contract_test.cpp` +
  `scripts/irem/inventory-corpus.ps1`
- [~] **I92-2** Executable M92 board profile — `src/manifests/irem_m92`
  now assembles a first-pass M92-owned V-series shell with the main CPU
  configured as NEC V33 at 9 MHz, the sound CPU configured as NEC V35 at
  14.318181 MHz, a 320x240 / 60.011 Hz raster contract, M92
  work/video/sprite/palette RAM windows, sound RAM, memory-mapped YM2151/GA20/
  latch/reply windows, input/DIP ports, whole-board save/load identity, and an
  M92-local diagnostic video path driven by the checked-in
  tile/sprite/PLD/sample regions plus board RAM. The modeled sound-command path
  now tracks pending command/reply state, asserts the V35 command IRQ through
  INTP1/vector 25 on main V33 writes, leaves latch reads non-acknowledging,
  clears command pending only on sound-side writes to `$a8044`, routes YM2151
  Timer A through INTP0/vector 24, saves those bits in board-state version 3,
  and has synthetic V33-to-V35 command/reply proof through the IRQ handler,
  latch acknowledge, reply port, and sound RAM; simultaneous pending YM/command
  IRQs prefer INTP0 before INTP1, followed by proof that a still-pending command
  IRQ is serviced through INTP1 after the YM2151 source is cleared. The
  V30/V33/V35 core now fetches instruction bytes through the bus opcode path,
  and M92 can map an optional `soundcpu_opcodes` decrypted V35 opcode image over
  the 20-bit sound-bus fetch path while ordinary data reads still see the raw
  encrypted `soundcpu` ROM. `src/apps/player/adapters/irem_m92` registers
  `--system irem_m92` / `m92`, supports ZIPs, single-inner wrapper ZIPs,
  unpacked set folders, explicit supplemental media, embedded or in-archive
  `game.toml` manifests, clone-parent fallback beside the selected set path,
  resident media validation,
  rollback-ready save-state, capability discovery, and real local player smoke
  through `MNEMOS_M92_SET_DIR=D:\emu\irem\M92;D:\emu\irem\M107`; all thirty-six
  checked-in sets step one frame, produce nonblank 320x240 diagnostic output, and emit save-state
  bytes, with direct `mnemos_player` screenshot/save-state smokes for
  `crossbld`, `geostorm`, `inthuntu`, `gunforcej`, `gunforceu`, `mysticri`,
  `gunhohki`, `mysticrib`, `nbbatman`, `nbbatmanu`, `rtypeleo`, and
  `rtypeleoj`, plus direct Undercover Cops parent/US split-clone nonblank
  screenshots and Japan/Alpha Renewal save-state smokes, direct Major Title 2
  parent/alternate-sound nonblank screenshots and World set 3/Japan save-state
  smokes, direct In the Hunt Japan/Korea, Geostorm alternate-sound, and Hook
  Japan nonblank screenshots, direct League-Man parent/alternate save-state,
  load-state, and nonblank screenshot smokes, direct Superior/Perfect Soldiers
  parent/Japan save-state, load-state, and nonblank screenshot smokes, direct
  Dream Soccer '94 Japan M92 hardware save-state, load-state, and nonblank
  screenshot smoke with supplemental `dsoccr94.zip` media, data-gated Lethal Thunder /
  Thunder Blaster, R-Type Leo Japan, and Undercover Cops clone
  parent-fallback proof. Remaining: derive/verify the proprietary
  encrypted V35 decrypt transform/key and sound protocol, cycle-exact V35
  interrupt latency, exact M92 memory/I/O behavior, GA21/GA22 video and priority
  behavior, GA20 analog balance/filtering, DIP behavior, raster timing,
  protection details, and authentic screenshot/audio parity before calling the
  profile authentic · PARTIAL · HIGH · L · beyond Emu · Evidence:
  `src/chips/cpu/v30/v30.cpp` + `src/chips/audio/irem_ga20/irem_ga20.cpp` +
  `src/manifests/irem_m92/m92_system.cpp` +
  `src/manifests/irem_m92/tests/m92_system_test.cpp` +
  `src/apps/player/adapters/irem_m92/irem_m92_adapter.cpp` +
  `src/apps/player/adapters/irem_m92/tests/irem_m92_adapter_test.cpp`

## Irem M107 — 1 / 2

This section is split from M72/M81/M82 so M107 artifacts do not get counted as
older Irem board proof.

#### Manifests / board bring-up
- [x] **I107-1** Local M107 ROM-set contract — `src/manifests/irem_m107` carries checked-in embedded ROM-contract manifests for `airass`, `dsoccr94`, and `firebarr`, with parser/region-contract coverage for the 1 MiB main program region, reset-vector coverage by either explicit boot reloads or full high-ROM pairs, interleaved sound program ROMs, graphics ROM groups, Air Assault data/sample regions, Dream Soccer tile/sprite/sample regions, per-set DIP tables, and current local ZIP CRCs. `MNEMOS_M107_SET_DIR=D:\emu\irem\M107` data-gates the sorted local ZIPs and proves all three sets load CRC-clean; `scripts/irem/inventory-corpus.ps1` records eight tracked M107 local artifacts, five direct player-loadable/supported routes, and three metadata-only `.7z` routes · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m107/games/*.toml` + `src/manifests/irem_m107/tests/m107_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I107-2** Executable M107 board profile — `src/manifests/irem_m107` now assembles an M107-owned first-pass V-series board shell with the main CPU configured as NEC V33 at 14 MHz and the sound CPU configured as NEC V35 at 14.318181 MHz, the declared 1 MiB main program and 128 KiB sound program regions, M107 VRAM at `$d0000`, work RAM at `$e0000`, sprite RAM at `$f8000`, palette RAM at `$f9000`, sound RAM at `$a0000`, and sound-side GA20/YM2151/command-latch/reply MMIO at `$a8000`/`$a8040`/`$a8044`/`$a8046`, input/DIP ports, whole-board save/load identity, and an M107-local diagnostic video path driven by the checked-in M107 graphics/subdata/sample regions rather than by an older Irem board. The board-state identity now includes CPU model, clock semantics, the corrected map revision, separate `COINS_DSW3` defaults, and command-latch IRQ acknowledge semantics, so pre-V33/V35, pre-map-correction, pre-SW3, or pre-command-IRQ states are rejected instead of being silently loaded against the corrected board contract. The previous OKI6295 placeholder has been replaced by `chips::audio::irem_ga20`, with four GA20 channels, 16-byte-unit start/end registers, key-on/status behavior, zero-byte sample termination, save-state coverage, capture decimation to the YM output cadence, M107 player stereo mixing with clamping, M107 sound-side MMIO regression coverage, and port fallbacks for the current synthetic command path. Main V33 sound-command writes now assert the modeled V35 IRQ line through INTP1/vector 25; sound-side reads fetch the command without acknowledging it; sound-side writes to `$a8044` acknowledge/clear the command IRQ; YM2151 Timer A IRQ is routed through INTP0/vector 24; and synthetic V35 handler proof covers command IRQ dispatch, latch acknowledge, YM timer IRQ dispatch, simultaneous INTP0-over-INTP1 arbitration, follow-on INTP1 service after INTP0 clears, sound-RAM storage, and reply-port write. The checked-in `airass` and `firebarr` manifests carry the shared Fire Barrel / Air Assault SW1/SW2 and SW3 DIP profile; `dsoccr94` carries the four-player Dream Soccer Time, Difficulty, Game Mode, Starting Button, Cabinet, coinage, and SW3 Player Power profile. The adapter folds SW1/SW2 defaults to `0xffbf`, folds SW3 `COINS_DSW3` defaults to `0xebff` for `airass` / `firebarr` and `0xffff` for `dsoccr94`, surfaces per-set DIP counts in the player spec, maps service input to `COINS_DSW3` bit `0x10`, and maps operator-test input to bit `0x20`. `src/apps/player/adapters/irem_m107` registers `--system irem_m107` / `m107`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local player smoke through `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`; Air Assault and Dream Soccer direct `mnemos_player` smokes produce 384x256 nonblank frames and save-state bytes. Remaining: implement/verify V33/V35-specific timing and on-die peripheral behavior beyond the shared V30-compatible core, deeper M107 I/O behavior, GA21/GA22 video/priority behavior, cycle-exact V35 interrupt latency, remaining GA20 analog balance/filtering details, raster timing, and authentic screenshot parity before calling the profile authentic · PARTIAL · HIGH · L · beyond Emu · Evidence: `src/chips/cpu/v30/v30.cpp` + `src/chips/audio/irem_ga20/irem_ga20.cpp` + `src/manifests/irem_m107/m107_system.cpp` + `src/manifests/irem_m107/tests/m107_system_test.cpp` + `src/apps/player/adapters/irem_m107/irem_m107_adapter.cpp` + `src/apps/player/adapters/irem_m107/tests/irem_m107_adapter_test.cpp`

Cabinet input note: the M107 adapter now consumes explicit frontend `service`
and `test` inputs, keeps `mode` as the service-credit alias for older callers,
maps service/test onto the M107 `COINS_DSW3` service-credit/operator-service
bits, and persists explicit `service` / `test` fields in adapter state version 2.

---

## Irem M102 — 2 / 2

This section is split from M92/M107 because `hclimber` is a sparse
electromechanical medal-game board, not a V33/V35 video-game platform.

#### Manifests / board bring-up
- [x] **I102-1** Local M102 Hill Climber ROM-set contract — `src/manifests/irem_m102`
  carries a checked-in embedded ROM-contract manifest for `hclimber`, preserving
  the local D70008AC/Z80-class program ROM filename, GA20 sample ROM filenames,
  region sizes, offsets, CRC-32 values, the blank upper half of `hc-pr-c.ic23`,
  and the public no-dump PAL placeholder as an explicit zero-filled region.
  `MNEMOS_M102_SET_DIR=D:\emu\irem\M102` data-gates the sorted local ZIP and
  proves it loads CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1`
  records two tracked M102 local artifacts, one ZIP player-supported route, and one
  metadata-only `.7z` route · DONE · MED · S · beyond Emu · Evidence:
  `src/manifests/irem_m102/games/hclimber.toml` +
  `src/manifests/irem_m102/tests/m102_rom_contract_test.cpp` +
  `scripts/irem/inventory-corpus.ps1`
- [~] **I102-2** Executable M102 board profile — first-pass route exists for
  Hill Climber: `src/manifests/irem_m102/m102_system.cpp` assembles a
  D70008/Z80-compatible diagnostic board shell with fixed and banked program
  ROM windows, video/medal/work RAM, input/DIP/output ports, GA20 MMIO and
  captured sample output, diagnostic raster composition, save-state identity,
  and player adapter registration. `src/apps/player/adapters/irem_m102`
  registers `--system irem_m102` / `m102`, supports ZIPs, single-inner wrapper
  ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests,
  resident media validation, rollback-ready save-state, capability discovery,
  and local player smoke through `MNEMOS_M102_SET_DIR=D:\emu\irem\M102`.
  This is smoke-playable only: authentic medal/connector I/O, artwork and
  mechanical state, exact D70008/Z80 timing, title-specific video timing,
  DIP behavior, and visual/audio parity remain open · PARTIAL · HIGH · M-L ·
  beyond Emu · Evidence: `src/manifests/irem_m102/m102_system.cpp` +
  `src/manifests/irem_m102/tests/m102_system_test.cpp` +
  `src/apps/player/adapters/irem_m102/*` +
  `MNEMOS_M102_SET_DIR=D:\emu\irem\M102` corpus golden + direct
  `mnemos_player --system irem_m102` smoke

---

## Irem M119 — 2 / 3

This section is split from M92/M107 because `scumimon` runs on late isolated
M119 hardware, not the V33/V35/GA20 video-game boards.

#### Manifests / board bring-up
- [x] **I119-1** Local M119 Scumimon ROM-set contract — `src/manifests/irem_m119`
  carries a checked-in embedded ROM-contract manifest for `scumimon`, preserving
  the local SH-3 program, uPD94244 VDP, and YMZ sample ROM filenames, region
  sizes, offsets, even/odd VDP interleave, and CRC-32 values. `MNEMOS_M119_SET_DIR=D:\emu\irem\M119`
  data-gates the sorted local ZIP and proves it loads CRC-clean through the
  embedded manifest; `scripts/irem/inventory-corpus.ps1` records two tracked
  M119 local artifacts, one ZIP first-pass player route, and one metadata-only `.7z`
  route · DONE · MED · S · beyond Emu · Evidence:
  `src/manifests/irem_m119/games/scumimon.toml` +
  `src/manifests/irem_m119/tests/m119_rom_contract_test.cpp` +
  `scripts/irem/inventory-corpus.ps1`
- [x] **I119-2** Executable M119 first-pass board profile —
  `src/chips/cpu/sh3`, `src/chips/video/upd94244`, and
  `src/chips/audio/ymz280b` provide explicit first-pass SH7708/SH-3,
  uPD94244-210, and YMZ280B chip surfaces, while
  `src/manifests/irem_m119/m119_system.*` and
  `src/apps/player/adapters/irem_m119` wire the sorted `scumimon.zip` corpus
  into a player-selectable `irem_m119` route with diagnostics, frame stepping,
  audio capture, and save-state smoke coverage. SH-executed tests now prove
  SH7708 local register byte lanes through the CPU bus, big-endian 32-bit
  uPD94244 register MMIO, and SH-driven YMZ280B register writes rather than
  host-only helper writes. YMZ280B tests now cover the documented channel
  register map, 24-bit start/loop/end addresses, PCM8, MSB-first PCM16, ADPCM
  nibble playback, loop handling, and enabled end-status latching. This is
  executable first-pass support, not correct
  graphics/music parity · DONE · HIGH · L · beyond Emu ·
  Evidence: `src/chips/cpu/sh3/tests/sh3_test.cpp` +
  `src/chips/video/upd94244/tests/upd94244_test.cpp` +
  `src/chips/audio/ymz280b/tests/ymz280b_test.cpp` +
  `src/manifests/irem_m119/tests/m119_system_test.cpp` +
  `src/apps/player/adapters/irem_m119/tests/irem_m119_adapter_test.cpp`
- [ ] **I119-3** Authentic M119 silicon/parity — remaining work is real
  SH7708 MMU/cache/timer/interrupt behavior, uPD94244 raster/tile/sprite
  behavior, YMZ280B exact ADPCM edge cases, DSP/IRQ/bus-timing behavior, M119
  board I/O and timing, DIP/mechanical behavior, and verified correct
  visual/audio parity against hardware reference captures · MISSING · HIGH · L ·
  beyond Emu · Evidence:
  the first-pass tests intentionally assert deterministic diagnostics and corpus
  loadability only.

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
- [~] **U4 CPS2** — *MED-HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: CPS-2 tile/sprite/palette pipeline · Audio: QSound behavioural mixer · Glue: CPS-2 keyed opcode decryption, ZIP/parent/key loading, checked-in game manifests, EEPROM, player input/media/battery surfaces, frame-exact player save/load, operator service/test inputs, and MAME-matched per-game input profiles for the checked-in roster. **inc 1 DONE:** authentic `1944.zip` and self-describing `1944_mn.zip` boot to lit frames; `scripts/cps2/run-corpus-smoke.ps1` now data-gates catalog-match and clone-suffix CPS2 zips from `MNEMOS_CPS2_ROM` / `MNEMOS_CPS2_SET_DIR`, with current local proof across the checked-in 37-set CPS2 corpus baseline in `tests/golden/cps2_frame_hash_baseline.csv`. **inc 2 DONE:** the CPS2 adapter exposes a complete board+adapter `runtime::save_target` through `mnemos_player` (`--save-state`, `--load-state`, F5/F9) and round-trips `1944_mn.zip` after restore. **inc 3 DONE:** IN2 now models the active-low test switch and service-credit bits, exposes them through player/scripted input, and preserves them in CPS2 save states. **inc 4 DONE:** the adapter now distinguishes 1/2/3/4-button, 3P/4P, six-button, and Cybots-specific input layouts, including the CPS2 six-button P2 button-6-on-IN2 wiring. **inc 5 DONE:** Eco Fighters and Puzz Loop 2 now use board-visible spinner/paddle profiles, including CPU-selected analog/digital IN0 multiplexing and deterministic save-state coverage. **inc 6 DONE:** `$804041` now records coin-counter edges and coin-lockout lines with the Mars Matrix polarity exception and save-state coverage. **inc 7 DONE:** SFA3 Hispanic/Brazil ticket-dispenser six-button profiles now drive the active-high ticket-empty line inactive, while preserving P2 button 6 on IN2 bit 14. **inc 8 DONE:** QSound now consumes the DL-1425 per-voice echo send plus feedback/length registers in the behavioural mixer and preserves the echo delay line in save states. **inc 9 DONE:** the data-gated CPS2 corpus runner records default-gate RGB frame hashes, rendered-audio WAV stream hashes/shape, DL-1425 QSound register-dump counters, and post-save/load 93C46 EEPROM hashes/non-erased byte counts, checks save/load, screenshot, battery dump, audio-export output, and QSound register-dump presence for every selected set, and fails on mismatched or missing expected baseline rows. The current baseline records 9/37 sets with nonzero rendered audio and 28/37 with QSound-programmed-silent evidence at the default visibility gate; `armwar.zip` uses a 700-frame visibility gate, `dstlk.zip` uses a 1200-frame visibility gate, and the other rows stay at 600 frames. **inc 10 DONE:** the runner now has an Emu-derived gameplay input probe plus a separate longer rendered-audio window (`-AudioFrames`), and records first nonzero rendered-audio positions; `1944_mn.zip` is audible by the 2500-frame proof window even though it is silent at the default gate. **inc 11 DONE:** long corpus probes are resumable/filterable (`-OnlySets`, `-SkipSets`, `-StartAfter`) and flush artifacts per set; `-GameplayInput` applies to the audio proof only unless `-GameplaySaveInput` is supplied, fixing the `sfa.zip` gameplay-screenshot false failure; QSound register dumps now expose per-ADPCM-voice state and derived trigger/configuration columns. **inc 12 DONE:** Emu comparison supplied two correctness fixes: zero-filled expansion for partial expanded QSound Z80 ROM regions (`sfa3.zip` now reaches PCM volume writes by the 2500-frame proof) and the 32 KiB alternate object-RAM mirror (`cybots.zip` now exits startup RAM test and reaches PCM volume writes by the 2500-frame repeated-gameplay proof). One-at-a-time Emu comparisons now show `1944.zip`, `1944_mn.zip`, `armwar.zip`, `avsp.zip`, `choko.zip`, `ddtod.zip`, `dimahoo.zip`, `dstlk.zip`, and `gigawing.zip` reaching rendered audio plus nonzero QSound volume writes under comparable repeated auto-start/gameplay probes, and `xmcota.zip` becomes audible under repeated gameplay input. The runner also records thresholded first-significant-audio timing so rows like `ecofghtr.zip` do not confuse tiny echo residue with gameplay audio. **Remaining:** triage remaining late/silent QSound rows one at a time with the longer gameplay-audio probe and ADPCM state columns before claiming audio-complete compatibility, add game-specific EEPROM seed defaults only if a real set fails to self-initialize, and pursue DSP16-level QSound if required for cycle-grade fidelity. · Evidence: `src/manifests/capcom_cps2/`, `src/chips/video/cps2_video/`, `src/chips/audio/qsound/`, `src/apps/player/adapters/capcom_cps2/`, `scripts/cps2/run-corpus-smoke.ps1`, `tests/golden/cps2_frame_hash_baseline.csv`
- [~] **U5 Amiga** — *HIGH.* CPU: m68000 ✓ (G1 applies) · Video: agnus + denise wired for OCS bitplanes/palette/custom registers plus `BPLCON0` high-resolution 640-pixel fetch/view, HAM/EHB/dual-playfield colour decoding, dynamic bitplane pointer advancement requiring field rewrites, basic sprite register/DMA/attach rendering with bounded DMA-list channel reuse, active sprite-list DMA slot CPU/Copper stalls including stop-line `SPRxPOS/SPRxCTL` reloads, dynamic sprite pointer advancement requiring field rewrites, hardware low-resolution sprite scaling/edge clipping over high-resolution playfields, high-sprite slot suppression when display-owned fetch slots overlap sprite DMA, per-slot CPU/Copper ownership when display DMA steals only the neighboring high-sprite slot, per-word display-stolen sprite data/stop-control fetches preserving the next unfetched DMA pointer, BPLCON2 dual-playfield PF2PRI composition when higher-priority sprite groups mask PF2, sprite DMA channel reuse blank-scanline separation, and zero-height per-word control reload rejection for behind-beam reuse, `BPLCON1` playfield horizontal-scroll delay, `BPLCON2` playfield/sprite priority, `CLXCON`/`CLXDAT` collision latches, Copper pointer/strobe routing with frame-start `COP1LC` reload, MOVE/SKIP/WAIT cadence, impossible terminal-WAIT handling, high-resolution four-plane display-DMA CPU chip-RAM lockout, five-/six-plane low-resolution and three-plane high-resolution display-DMA CPU-open slot steals including second-clock residual waits, `BPLCON1` horizontal-scroll extra-word display-DMA contention, bounded non-nasty blitter CPU-slot release waits coalesced with display DMA delays, display-owned memory slots pausing blitter BBUSY/BLIT IRQ countdown, BLTPRI CPU lockout stacking after display-owned DMA slots, Copper memory-cycle stalls during full display-DMA lockout windows and covered partial display-DMA stolen slots, HRM control-register Copper write protection (`$080+` writable without CDANG, `$040-$07E` CDANG-gated, `$000-$03E` blocked), board-owned custom-register MOVEs for raster palette/interrupt effects, and a bounded blitter (`BLTCON0/1`, A/B/C/D pointers, modulos, first/last A masks, minterms, inclusive/exclusive area fill, line mode with texture and `SING`, `BLTSIZE`, BZERO/BBUSY, color-clock-bounded BLIT IRQ retirement, in-flight busy save state, `BLTPRI` CPU chip-RAM wait states while blitter DMA is busy) · Audio: paula DMA + capture · Glue: cia8520, Kickstart reset overlay, chip RAM, DF0-DF3 ADF mount + color-clock-distributed raw MFM byte stream with `ADKCON.WORDSYNC`/`DSKSYNC` start gating, raw-track sub-byte phase reads/writes, bitcell-granular raw-stream read/write shifters with partial-byte save state, raw-track write-DMA capture plus valid AmigaDOS-sector ADF writeback, custom/non-AmigaDOS raw-track retention across track movement/save state, rotational bit-phase and in-flight raw-byte preservation across side/track refreshes including cached custom track length changes, and deterministic weak-bit raw-track masks with save-state-stable sampling and write-clear, CIAB drive motor/step/side/select, CIAA active-low disk-change/status latches, 300 RPM scanline-paced CIAB disk-index FLAG pulse, player `--system amiga500`, ADF launch via `MNEMOS_AMIGA500_KICKSTART`, media swapping, joystick/POT/CIAA input, mouse JOYxDAT counters with CIAA/POTINP buttons, `POTGO`-started RC-calibrated raster-line POT counters with reset-window delay, programmable axis thresholds, and frontend analog axis routing, CIA-A keyboard raw-key serial queue gated by host KDAT/SP handshake, exact keyboard control codes/reset warning, duplicate raw-key edge filtering, adapter-side duplicate-source hold aggregation, bounded three-corner keyboard matrix jam filtering, pressed-state save/load, Caps Lock LED state, US/German-region QWERTZ/AZERTY/International-QWERTY physical keyboard USB/HID usage mapping, Brazilian/international layout aliases, and reserved national symbol-key HID usages for raw keys `$2B`/`$30`/`$3B`/`$0E`, player/runtime save target with frontend input snapshots, reported raw-key edge state, media index, writable mounted-floppy image bytes, mouse continuity, and audio phase, and data-gated real Kickstart+ADF boot smoke are in the bootstrap branch. Remaining for true compatibility: media-backed proof of a full Kickstart+Workbench boot, remaining cycle/beam-exact non-saturated display DMA contention beyond the covered 5-/6-plane low-resolution / 3-plane high-resolution slot steals and horizontal-scroll extra-word tail fetches, exact combined non-nasty CPU/display/blitter bus-slot arbitration beyond the display-owned blitter countdown pause and BLTPRI display-owned slot stacking, remaining sprite sub-line priority/reuse cases outside the covered per-word display-stolen fetch pointer preservation, dual-playfield sprite-masked PF2 composition, blank-scanline reuse suppression, and zero-height per-word reload rejection, deeper floppy analog PLL timing and full custom-track format coverage beyond preserved rotational phase and raw shifter continuity, national keycap legend text beyond the covered physical QWERTY/QWERTZ/AZERTY/reserved-symbol positions and deeper physical/electrical matrix ghosting edge cases, and remaining Copper/display edge cases around sprite priority, mixed DMA ownership, and exact beam-slot sequencing. · PARTIAL · HIGH · L · beyond Emu · Evidence: `progress-analysis.md` §4 + `src/manifests/amiga500/` + `src/apps/player/adapters/amiga500/`
- [ ] **U6 NeoGeo** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **LSPC (greenfield — exists nowhere)** · Audio: ym2610 (ssg + adpcm_a + adpcm_b). Emu is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [~] **U7 Taito F2** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Audio: ym2610 ✓ · Video: TC0100SCN/TC0200OBJ/TC0280GRD/TC0430GRW/TC0480SCP first pass ✓ (`taito_f2_video`: standard TC0100SCN BG/text RAM layout, dual-TC0100SCN Thunder Fox layer merge, RAM-generated text chars, board-selectable program-region 1bpp text chars for quiz maps, layer disable/priority swap, rowscroll/colscroll hooks, real 16-byte TC0200OBJ records, packed-LSB 16x16 sprites, banked sprite-register routing, TC0190FMC-style sprite-bank register-window routing, TC0280GRD + TC0430GRW 64x64 ROZ tilemap RAM/control registers, TC0480SCP four 16x16 BG planes + RAM text plane + control/priority registers, TC0480SCP rowscroll, layer zoom, BG2/BG3 row-zoom first-pass sampling, and BG/text offset synthetic golden coverage, board-selectable TC0480SCP priority decoding, board-configurable palette formats, board-configurable TC0200OBJ sprite-extension RAM windows, board-configurable active-area marker source including Footchmp-style Y-bit routing, board-specific TC0200OBJ hide-pixel offsets, board-configurable TC0200OBJ immediate/full/partial-delayed buffering policies including qzchikyu partial-word overlay, TC0200OBJ zoom and continuation chaining, marker disable/flip-screen handling, X bias + master/extra/absolute sprite scroll markers, frame-persistent active-area/disable/flip/master-scroll state, TC0360PRI-style tile/text/ROZ/sprite priority register routing and sprite/tile blend modes including ROZ-selector and all sprite-priority-group synthetic coverage, TC0200OBJ sprite-extension code policies) · Player adapter with frame-exact save/load ✓ · Gun Frontier World/Japan + Liquid Kids parent/clone + Quiz HQ + Quiz Torimon + Quiz Chikyu + Quiz Quest + Dondoko Don + Pulirula + Metal Black + Football Champ + Dead Connection + Dino Rex + Thunder Fox + Growl + Ninja Kids + Solitary Fighter ROM profiles, clone-parent zip fallback loading, real F2 address map/data-gated boot coverage, and a data-gated Taito F2 corpus smoke runner with wrapper-zip staging and optional screenshot SHA-256 pins ✓. Local recursive `D:\emu\arcade\Taito` F2 proof covers `dinorex`, `gunfront`, `gunfrontj`, and `growl` 4/4; the broad Taito inventory reports 4/25 local packages runnable by Mnemos today. **Remaining:** non-F2 Taito board families and media/container paths, broader F2 corpus proof beyond that local F2 set, real-board TC0360PRI priority proof, remaining TC0190FMC board-family profiles, and populated real-ROM visual/audio golden evidence. Emu is a scaffold (197 LOC). · Evidence: `progress-analysis.md` §4 + R19 + `scripts/taito-f2/run-corpus-smoke.ps1` + `scripts/taito/inventory-corpus.ps1`

  **2026-06-24 increment:** checked-in Taito F2 game manifests are now generated
  into the manifest target and consumed by the player adapter for plain set zips
  and one-level title-wrapper zips. Local proof covers direct `mnemos_player`
  launches and corpus smoke under `D:\emu\arcade\Taito`; the recursive runner
  recognized `dinorex`, `gunfront`, `gunfrontj`, and `growl`, synthesized
  self-describing zips under `build/scratch`, and passed save-state restore
  plus nonblank screenshot checks for all 4/4 candidates. The broad inventory
  script reports the same root as 4/25 supported today; the remaining local
  packages are 5 G-NET/ZN-2 CHD wrappers, 1 Type X2 CHD wrapper, 1 Type Zero
  CHD wrapper, 1 Namco System 246 Taito-published title, 8 Type X RAR packages,
  and 5 non-Taito Irem M92 wrappers under the local `F2` folder. Type X, G-NET,
  Type Zero, and other non-F2 boards remain outside this F2 coverage slice.

  **2026-06-24 G-NET prerequisite increment:** `src/chips/cpu/r3000a/`
  adds a native `sony.r3000a` MIPS I CPU bootstrap for the Taito G-NET / Sony
  ZN-2 path, and `src/manifests/taito_gnet/` adds package/media loading for
  ZIP-wrapped CHD flash-card images plus a first boot-ROM/main-RAM/flash-bank/
  PCMCIA-aperture/RF5C296-proxy board shell.
  Covered behaviour includes little-endian
  instruction/data access, branch delay slots, load delays, HI/LO
  multiply-divide, CP0 status/cause/EPC, exceptions, trace/register
  introspection, save-state round-trip, registry construction, CHD v5
  block-device decode (`lzma`/`zlib`/`huff`/`flac`/`none`/`self`),
  real-corpus package classification/loading, R3000A reset fetch from a
  caller-provided BIOS ROM, 2 MiB main RAM, mounted flash-card images,
  FC-board control/config registers, BIOS/firm/zoom/wave flash windows, direct
  PCMCIA card-data aperture access, a minimal RF5C296-style index/data IO
  register pair with reset-bit tracking and card-byte proxying, 1 KiB scratchpad
  RAM, first BIOS-facing memory/cache control latches, a GPU register/VRAM latch
  shell, COP2/GTE register-transfer and command-latch shell, limited GPU command
  and OTC DMA execution, interrupt status/mask
  delivery into the R3000A IRQ line, DMA channel/control/interrupt register
  latches, root-timer counter/mode/target latches with first-pass target/overflow IRQ delivery, board-shell save/load,
  a board-smoke player adapter registered as
  `taito_gnet`/`gnet`, and an optional BIOS+CHD adapter/system smoke gated by
  `MNEMOS_TAITO_GNET_BIOS`/`MNEMOS_TAITO_GNET_PACKAGE`.
  G-NET packages remain uncovered because the local packages carry CHDs only,
  not BIOS ROMs, and because the locked-card command/security protocol,
  GPU renderer/SPU/real GTE command math, full DMA transfer timing beyond GPU/OTC,
  exact root-timer sync/clock-source modes, JVS/I/O, playable
  video/audio/input presentation, and real boot proof are still missing.

  **2026-06-24 input increment:** the player adapter now publishes and saves
  four arcade panels for Growl and Ninja Kids, routes P3/P4 active-low panel
  bytes to the board latches, and extends the system START/COIN byte for P3/P4.
  Solitary Fighter remains a two-player frontend profile.

  **2026-06-25 conformance-gate increment:** `docs/plans/2026-06-25-taito-arcade-conformance-gates.md`
  is now the Taito arcade gate plan. F2 remains partial until chip-level gates
  and real-ROM capture/audio matrices prove TC0100SCN offsets and scroll
  origins, TC0200OBJ/TC0190FMC sprite placement, TC0360PRI priority/blend,
  TC0480SCP/ROZ variants, YM2610 sample cadence, TC0140SYT sound communication,
  board timing, inputs, save-state identity, and the broader corpus split.
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
