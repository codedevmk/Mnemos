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
- [~] **M1** mcs51 (8051) protection MCU — the optional `mcu` region is now player-loadable via `mcu.bin`, scheduled when present, and covered through the MCU MOVX sample/latch/shared-RAM path (`V30 $B0000` / MCU `$C000`) using both DPTR and P2-latched `@R0` / `@R1` external-data forms; declarative protected sets with a missing MCU dump now carry the ROM-set issue but clear the filled `mcu` region before board construction, so Mnemos does not schedule an all-`0xFF` fake protection CPU; port read-modify-write instructions now use the output latch rather than external pin levels, matching the 8051 behavior real i8751 code relies on when updating P0-P3; the MCS-51 core now implements the classic IE/IP two-level priority model so high-priority external/timer/serial interrupts can preempt a low-priority ISR while equal/lower-priority requests wait for RETI plus the following foreground instruction, including the same one-instruction deferral when an IE/IP access leaves a serviceable request pending, timer mode 0's 13-bit counter, timer mode 3's split TL0/TH0 behavior, TMOD GATE-controlled timers, T0/T1 external counter pins, serial RI/TI arbitration through the shared `0x0023` vector with firmware-owned flag clearing, frame-level SBUF transmit/receive timing, and a mechanical all-opcode operand-consumption regression covering the complete i8751 decoder surface including AJMP/ACALL page forms and call stack byte order; no-dump true-M72 sets can now declare explicit `[[hle]]` MCU profiles, with `dbreedm72` / `dkgensanm72` mapped to the startup protection RAM inversion surface, V30 command-latch acknowledge, and manifest-declared profile-bounded sample-trigger cursor setup; duplicate sample-trigger declarations are schema errors, and supported no-dump profiles without trigger metadata or with trigger starts outside the loaded `samples` region are reported and disabled instead of activating partial HLE; `MNEMOS_M72_PROTECTED_SET` provides a data-gated real-ROM player check for protected true-M72 sets with either a dumped MCU or an explicit no-dump HLE profile. Remaining: validate / complete protected-game behavior against authentic per-game MCU + ROM-set artifacts, including the no-dump profile entry routines beyond startup RAM inversion, command-latch acknowledge, and sample trigger setup (R-Type needs none) · PARTIAL · HIGH · M · beyond Emu · R9 · Evidence: `src/chips/cpu/mcs51/mcs51.cpp` + `src/chips/cpu/mcs51/tests/mcs51_test.cpp` + `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` / `progress-analysis.md` R9

Loader note: no-dump HLE profiles now also disable when their declared `samples` region has missing file issues, preventing sample fill bytes from driving protection-HLE cursors.

Protection-HLE note: `dbreedm72` and `dkgensanm72` no-dump MCU profiles now also expose their profile-specific service-ROM checksum response bytes at the shared-RAM request trigger, matching the declared HLE surface instead of leaving service-test reads as stale RAM.

#### System / variants
- [~] **M3** Additional M72 board variants beyond R-Type — `board_params_for` now records the known true-M72 work-RAM map families (`rtype*`, standard protected M72, `xmultiplm72`, and `dbreed*m72`) plus set-specific DIP defaults without admitting M81/M82/M84/M85/M92 boards into the M72 profile; the player resolves standard set zips, single-inner-set wrapper zips, or unpacked set directories by basename through the embedded checked-in game manifests, keeps source-local `game.toml` as a development override, resolves declarative clone `parent` zips/directories beside the clone set, reports missing/corrupt/unsafe/undeclared/mismatched parent resolutions as media validation issues, refuses fallback bytes from a parent zip whose manifest does not validate as the declared parent, and inherits parent manifest regions plus parent DIP/HLE metadata when the clone manifest omits replacements; source-local declarations for non-M72 boards now surface loader issues instead of falling through as empty M72 development sets; checked-in M72 game manifests now cover the true-M72 roster: `rtype`, `bchopper`, `mrheli`, `nspirit`, `nspiritj`, `loht`, `lohtj`, `lohtb2`, `lohtb3`, `imgfight`, `airduelm72`, `rtypej`, `rtypejp`, `rtypeu`, `rtypeb`, `imgfightj`, `imgfightjb`, `airdueljm72`, `xmultiplm72`, `dbreedm72`, `dbreedjm72`, `dkgensanm72`, and `gallopm72` with parser/region-contract coverage plus explicit no-dump MCU HLE declarations for `dbreedm72` / `dkgensanm72`; the ROM-set schema/player adapter now parse and retain roster-level true-M72 DIP option metadata, including conditional Irem coinage tables, unsupported MCU HLE profiles now report loader issues instead of silently booting as unprotected boards, unprotected boards leave the absent MCU latch port as open bus, and the player media descriptor publishes a CRC32 over resolved set metadata plus loaded resident ROM regions while carrying ROM-set validation issues so capability discovery degrades CRC-mismatched or incomplete M72 media instead of reporting it available; no-dump HLE profiles now cover the startup inversion surface, V30 command-latch acknowledge, and manifest-declared profile-bounded sample-trigger cursor; control-register coin-counter outputs now count rising edges, the CPU-visible sprite-DMA-complete bit stays asserted, flip-screen mirrors the composed frame while both round-trip through board/video state, mid-frame video save-state now preserves already-composed scanlines, the M72 board save-target manifest revision tracks the media-fingerprinted board-state schema, the player-adapter save/load overrides now expose that target to `mnemos_player`, and the target also captures adapter frame count, audio-drain cursor, DAC mix continuity, and frontend input snapshots, capability discovery exposes that frame-exact target as rollback-ready for the M72 player session, and visible scanlines compose at beam-line start so raster-time scroll writes affect later lines without repainting earlier lines; `MNEMOS_M72_VERTICAL_SET` provides a data-gated real-ROM orientation/framebuffer sanity hook for vertical true-M72 sets, `MNEMOS_M72_SET_DIR` now drives a full checked-in true-M72 roster gate from a platform path-list of roots containing `<set>.zip`, `<set>\`, or single-inner-set wrapper zips per embedded manifest and validates CRC-clean loads, clone parent fallback, board wiring, orientation, sound release, and a non-blank frame, and `scripts/irem_m72/run-corpus-smoke.ps1` drives configured R-Type, protected, vertical, and roster artifacts through `mnemos_player` save/load/screenshot smoke proof with per-set fallback frame attempts for attract/blank intervals and per-set subsets for collection ZIPs. Current local proof treats `D:\emu\irem` as a mixed Irem corpus (`m72`, `M81`, `m82`, `M84`, `M15`, `i8751`, plus root archives) and proves clean save/load/screenshot smoke for 19 true-M72 sets: `airdueljm72`, `airduelm72`, `bchopper`, `dbreedjm72`, `dbreedm72`, `dkgensanm72`, `imgfight`, `imgfightj`, `imgfightjb`, `loht`, `lohtb3`, `mrheli`, `nspiritj`, `rtype`, `rtypeb`, `rtypej`, `rtypejp`, `rtypeu`, and `xmultiplm72`; the smoke runner now refuses media-validation issues when deciding pass/fail, so incomplete discovered local artifacts are rejected for `gallopm72` (`cc_c-pr-.ic1` missing) and `nspirit` (`nin_c-pr-b.ic1` missing); `scripts/irem_m72/find-missing-artifacts.ps1` now records a repeatable name/CRC scan across `D:\emu\irem` plus `D:\emu\Darksoft Apocalypse M72 2020-12-30.7z`, finding 83/94 artifacts for `gallopm72`, `nspirit`, `lohtj`, and `lohtb2` but no matching `gallopm72` or `nspirit` MCU dumps, while `lohtj` lacks both set-specific program ROMs plus its MCU and `lohtb2` lacks four set-specific program ROMs, its 8 KiB MCU dump, and one sprite ROM. Remaining: complete and validate no-dump HLE protection entry behavior beyond the covered startup/latch/sample surfaces, verify any board-manual corrections for MAME-assumed DIP locations, exercise the full roster real-ROM hook with complete authentic artifacts, and resolve remaining set-specific protection/sample behavior · PARTIAL · MED · M–L per game · beyond Emu · Evidence: `src/manifests/irem_m72/games/*.toml` + `src/manifests/irem_m72/m72_game_manifests.hpp` + `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` + `scripts/irem_m72/run-corpus-smoke.ps1` / `progress-analysis.md` M72 board roster

Artifact note: extracting the Darksoft raw `gallop`/`nspirit` folders proves
the raw-name aliases load through `mnemos_player`; the extracted-folder scan is
42/44 present and still missing only `gallopm72:mcu:cc_c-pr-.ic1:ac4421b1` and
`nspirit:mcu:nin_c-pr-b.ic1:0f7b2713`; the artifact scanner reports suggested
local placements under `D:\emu\irem\m72\gallop\` and
`D:\emu\irem\m72\nspirit\` for lawful dumps. `nspirit` can reach a lit fallback
frame with the MCU disabled, but it is still not counted as protected-board
complete because the media remains incomplete. A bounded CRC scan of
`D:\emu\arcade` finds the Cosmic Cop/Gallop non-MCU files but still no
`cc_c-pr-.ic1` MCU dump, and no matching Ninja Spirit set.

Corpus note: `scripts/irem_m72/run-corpus-smoke.ps1` now accepts multiple
`-RomDir` values and treats sorted non-M72 roots such as `D:\emu\irem\M81`,
`D:\emu\irem\m82`, and `D:\emu\irem\M84` as a clean zero-candidate result
instead of folding them into M72 proof; `D:\emu\irem\m72 -MaxSets 2` remains a
positive smoke path. The default fallback frame list includes 900 frames because
`rtypeb` stays black at the 300/600-frame attract probes but reaches a lit
post-load frame at 900.

Input note: the M72 player adapter now maps the shared frontend arcade inputs
directly to the board-visible system byte: `service` clears service 1/2 bits
4/5, legacy `mode` remains a service alias for older callers and v1 adapter
states, and `test` clears the operator-test bit 6. Adapter state version 2
persists the explicit `service`/`test` fields while accepting version 1 input
snapshots.

2026-06-25 artifact proof note: `MNEMOS_M72_RTYPE_SET=D:\emu\irem\R-Type_Arcade_EN.zip`,
`MNEMOS_M72_PROTECTED_SET=D:\emu\irem\imgfight.zip`, and
`MNEMOS_M72_VERTICAL_SET=D:\emu\irem\imgfight.zip` pass their dedicated CTest
golden gates. The recursive mixed-corpus smoke path still rejects
`gallopm72`/`nspirit` until their missing MCU dumps are present.

Corpus inventory note: `scripts/irem/inventory-corpus.ps1` with
`-Root D:\emu\irem -Recurse` records the local Irem tree as metadata only,
ignores archive-only container folders as unpacked sets, and currently reports
121 items across `root`, `m72`, `M15`, `M81`, `m82`, `M84`, `M107`, and `i8751`.
One item matches a checked-in M15 manifest contract, thirty-seven items match
checked-in playable M72 manifests, five items match checked-in M81 manifest
contracts, five items match checked-in M82 R-Type II manifests, two items match
checked-in M84 manifest contracts, and eight items match checked-in M107
manifest contracts. The inventory now separates manifest tracking from player
loadability: 58 items match a checked-in Irem manifest, 52 are loadable through
current ZIP / single-inner-ZIP / folder routes, and six `.7z` matches remain
metadata-only until they are converted to ZIP or unpacked folders. No sorted
top-level board bucket is completely untracked anymore; `board_family_candidates`
now only keeps 19 untracked / misbucketed `M72`-folder items visible instead of
silently treating them as true-M72 proof.
M15 now has a checked-in `headoni` manifest plus an executable MOS 6502
board/player path with source-aligned Head On ROM/vector placement, RAM/MMIO
windows, frame IRQ coverage, active-high P1/P2 input wiring, coin-triggered NMI,
Head On DIP defaults, active-low flip control, and a tile/color/chargen renderer
using the M-15 tile scan order and 1bpp palette lookup; `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`
proves the sorted local wrapper ZIP loads CRC-clean through the embedded
manifest, including the directory-prefixed entry aliases used by the local
nested ZIP, and the player route produces nonblank screenshot plus save/load
smoke for `--system irem_m15`.
M84 now has a ROM-contract-only manifest
layer for `hharryb` and `hharryu`;
`MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81` proves the sorted split
wrapper ZIPs load CRC-clean when composed with the M81 `hharry` parent, while
keeping the M84-specific program/PAL regions separate from the inherited parent
graphics, sound, samples, and PROMs.
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
ROM-contract-only manifest layer for `airass` and `firebarr`;
`MNEMOS_M107_SET_DIR=D:\emu\irem\M107` proves the sorted ZIPs load CRC-clean,
while root and `m72`-folder duplicates are also visible as M107 artifacts; the
two Fire Barrel `.7z` copies remain metadata-only until converted or unpacked.
M82 is player-routable
through its own first-pass board profile, including scanline-composed video for
mid-frame palette writes and M72-style tile priority groups around sprites, but
not yet exact raster-phase or visual-priority authentic. The data-gated M82
artifact/player tests use `MNEMOS_M82_SET_DIR=D:\emu\irem` to unwrap the local
R-Type II collection ZIPs and load `rtype2`, `rtype2j`, `rtype2jc`, and
`rtype2m82b` CRC-clean through the embedded manifests and clone-parent fallback.
M15, M84, and M107 now have executable board/profile layers, but all three still
need board-authentic video/priority, sound, raster, and screenshot-parity closure
before they can be called authentic; M84 and M107 also retain memory/I/O and DIP
validation gaps.

Video note: the M72 sprite renderer now traverses the full 0x400-byte latched sprite RAM entry range, so single-width entries beyond the old 64-entry software cap remain visible. The M72 palette CPU map now models the disconnected A9 mirror and low-byte-only 5-bit gun writes/reads while preserving the renderer's canonical R/G/B plane storage.

#### Mapper / ROM
- [x] **M2** Z80 sound-program ROM path / `$8000` banked variants · DONE · MED · S · beyond Emu · ROM-backed sound maps activate when a set supplies a `soundcpu` region; public 64 KiB sound ROM declarations are accepted, with only `$0000-$EFFF` mapped as ROM and `$F000-$FFFF` shadowed by Z80 RAM, and the development zip `soundcpu.bin` path is covered. The suspected `$8000` bank-register variants are not part of the true-M72 board profile and must be routed to later M81/M84/M92 profiles or ADRs instead of folded into M72. · Evidence: `src/manifests/irem_m72/m72_system.cpp` + `src/manifests/irem_m72/tests/m72_system_test.cpp` + `src/apps/player/adapters/irem_m72/tests/irem_m72_adapter_test.cpp` / `progress-analysis.md` R9

> Done (exceeds Emu): V30 (including hermetic 0F INS/EXT bitfield regressions
> and FPO2/BRKEM operand-consumption stubs), Z80 (shared-RAM boot handshake + ROM-backed sound map), full video (scanline-composed
> tilemaps / sprites / palette with raster-time scroll changes and CPU-visible disconnected-A9 palette mirrors), YM2151, DAC/PCM sample playback with sound-clocked
> write-boundary mixing, 8259 PIC, raster compare,
> shared sound RAM, inputs/DIPs.

---

## Irem M15 — 1 / 2

This section is split from M72 so the early Head On hardware stays classified as
a 6502-era M15 profile instead of being folded into later V30 boards.

#### Manifests / board bring-up
- [x] **I15-1** Local M15 ROM-set contract — `src/manifests/irem_m15` carries a checked-in embedded ROM-contract manifest for `headoni`, with parser/region-contract coverage for the six 1 KiB program ROMs, the `e4.9d` reset-vector reload at `$fc00`, and aliases for the local nested wrapper ZIP's `headoni/` entry prefix. `MNEMOS_M15_SET_DIR=D:\emu\irem\M15` data-gates the sorted local artifact and proves it loads CRC-clean through the embedded manifest; `scripts/irem/inventory-corpus.ps1` records the M15 bucket as tracked/loadable instead of an unsupported board-family candidate · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m15/games/headoni.toml` + `src/manifests/irem_m15/tests/m15_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I15-2** Executable M15 board profile — `src/manifests/irem_m15` now assembles an M15-owned MOS 6502 board shell at `733125 Hz` with the M-15 memory map: scratch RAM `$0000-$02ff`, ROM `$1000-$33ff` plus vectors `$fc00-$ffff`, video RAM `$4000-$43ff`, color RAM `$4800-$4bff`, chargen RAM `$5000-$57ff`, and `$a000/$a100/$a200/$a300/$a400` P2/sound/DIP/P1/control MMIO. It reuses the shared 6502-family CPU core in bare-6502 mode, pulses the IRQ vector during frame stepping, maps coin insertion to the NMI edge, uses Head On's active-high P1/P2 controls and `0x11` DIP default, preserves the active-low flip control bit in save states, exposes 6502 trace/register capability discovery, and renders the vertical 224x256 frame through M-15 tile scan order, color RAM lower-three-bit palette selection, and runtime chargen RAM instead of the old program-ROM diagnostic fallback. Whole-board save/load identity rejects mismatched DIP/layout identity. `src/apps/player/adapters/irem_m15` registers `--system irem_m15` / `m15`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local player smoke through `MNEMOS_M15_SET_DIR=D:\emu\irem\M15`; direct Head On screenshot and save/load smoke both produce 224x256 nonblank frames. Remaining: discrete sample/sound behavior, analog color proof, raster timing, and authentic screenshot parity before calling the profile authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m15/m15_system.cpp` + `src/manifests/irem_m15/tests/m15_system_test.cpp` + `src/apps/player/adapters/irem_m15/irem_m15_adapter.cpp` + `src/apps/player/adapters/irem_m15/tests/irem_m15_adapter_test.cpp`

---

## Irem M81 — 1 / 2

This section is split from M72 so Dragon Breed, Hammerin' Harry, and X Multiply
sets do not inherit true-M72 board wiring when they are local M81 artifacts.

#### Manifests / board bring-up
- [x] **I81-1** Local M81 ROM-set contract — `src/manifests/irem_m81` carries checked-in embedded ROM-contract manifests for `dbreed`, `hharry`, and `xmultipl`, with parser/region-contract coverage for the 1 MiB V30 program region, boot-chunk reload declarations, sound CPU ROM, samples, graphics regions, and PROM metadata. `MNEMOS_M81_SET_DIR=D:\emu\irem\M81` data-gates the sorted local wrapper ZIPs and proves all three sets load CRC-clean from single-inner ZIP artifacts; `scripts/irem/inventory-corpus.ps1` now records five total M81 manifest matches when duplicate / misbucketed corpus copies are included · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m81/games/*.toml` + `src/manifests/irem_m81/tests/m81_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I81-2** Executable M81 board profile — `src/manifests/irem_m81/m81_system.cpp` now assembles an explicit M81 V30/Z80/YM2151/DAC/8259 board with a 1 MiB V30 program map, sound ROM + Z80 work RAM, inputs/DIPs, frame stepping, sound-clocked YM/DAC drain support, whole-board save/load, scanline-composed video, per-layout media identity, and save-state rejection across mismatched M81 board-layout profiles. `src/apps/player/adapters/irem_m81` registers `--system irem_m81`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, resident media CRC/validation reporting, rollback-ready save-state, capability discovery, and real local player smoke for `dbreed`, `hharry`, and `xmultipl`. `MNEMOS_M81_SET_DIR=D:\emu\irem\M81` proves all three embedded M81 sets through the adapter. Remaining: replace/verify first-pass video priority, raster phase/timing, DIP behavior, and visual parity against board/manual evidence before calling the profile authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m81/m81_system.cpp` + `src/manifests/irem_m81/tests/m81_system_test.cpp` + `src/apps/player/adapters/irem_m81/irem_m81_adapter.cpp` + `src/apps/player/adapters/irem_m81/tests/irem_m81_adapter_test.cpp`

---

## Irem M82 — 1 / 2

This section is split from M72 so R-Type II does not inherit true-M72
assumptions. M82 now has its own executable profile, and the video path has a
RAM-backed first-pass tile/sprite/palette renderer composed per visible
scanline plus scanline-paced vblank / raster-compare IRQ delivery, with
tile priority groups partitioned into below-sprite and above-sprite passes and
a diagnostic fallback for uninitialized development launches pending
board-accurate DIP / raster-phase proof.

#### Manifests / board bring-up
- [x] **I82-1** R-Type II ROM-set contract — `src/manifests/irem_m82` carries checked-in embedded manifests for `rtype2`, `rtype2j`, `rtype2jc`, and the local nested `rtype2m82b` artifact, with parser/region-contract coverage for clone-parent inheritance, the 1 MiB main-program reset-vector reload, sound CPU ROM, voice/sample ROM, graphics regions, and PROM metadata. `MNEMOS_M82_SET_DIR=D:\emu\irem` data-gates real local artifacts and proves all four embedded R-Type II sets load CRC-clean from standard wrapper ZIPs through parent fallback, including reset-vector reload equality and non-fill resident regions · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m82/games/*.toml` + `src/manifests/irem_m82/m82_game_manifests.hpp` + `src/manifests/irem_m82/tests/m82_system_test.cpp`
- [~] **I82-2** Executable M82 board profile — `src/manifests/irem_m82/m82_system.cpp` now assembles an explicit M82 V30/Z80/YM2151/DAC/8259 board with a 1 MiB V30 program map, sound ROM + Z80 work RAM, inputs/DIPs, frame stepping, audio drain timing, whole-board save/load, scanline-sliced V30 execution with one-line IR0 vblank and IR2 raster-compare pulses, and an M82-local video path that uses VRAM-backed 8x8 planar tilemaps, rowscroll RAM, 5-bit palette RAM, sprite-DMA-latched 16x16 planar cells, flip-screen state, scanline composition before the CPU tick for that beam line, M72-style tile priority groups split into below-sprite and above-sprite passes, and save/load of the latched sprite buffer, while retaining a diagnostic fallback only when no hardware render state is initialized; focused board/video tests now prove a raster-compare V30 handler changing palette RAM affects later scanlines without repainting earlier scanlines, group-0 front pens stay below sprites, and group-2 front pens cover sprites. `src/apps/player/adapters/irem_m82` registers `--system irem_m82`, supports direct ZIPs, single-inner wrapper ZIPs, unpacked set folders, clone parent fallback, supplemental parent media, resident media CRC/validation reporting, rollback-ready save-state, capability discovery, and real R-Type II player smoke. `MNEMOS_M82_SET_DIR=D:\emu\irem` proves all four embedded R-Type II sets through the adapter; direct parent and clone+parent `mnemos_player --system irem_m82` smokes both produce 384x256 nonblank frames. Remaining: verify/replace first-pass M82 palette banking, exact raster phase/timing, board-manual DIP behavior, and real visual-priority parity before calling R-Type II visually authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m82/m82_system.cpp` + `src/manifests/irem_m82/tests/m82_system_test.cpp` + `src/apps/player/adapters/irem_m82/irem_m82_adapter.cpp` + `src/apps/player/adapters/irem_m82/tests/irem_m82_adapter_test.cpp`

---

## Irem M84 — 1 / 2

This section is split from M81 because the local Hammerin' Harry M84 artifacts
are split clone sets with M84-specific program/PAL dumps but inherited parent
graphics, sound, sample, and PROM assets.

#### Manifests / board bring-up
- [x] **I84-1** Local M84 ROM-set contract — `src/manifests/irem_m84` carries checked-in embedded ROM-contract manifests for `hharryb` and `hharryu`, with parser/region-contract coverage for the 1 MiB V30 program reload layout, M84-specific PAL/PLD regions, and clone-parent inheritance from the M81 `hharry` parent. `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81` data-gates the sorted local split wrapper ZIPs and proves both clone sets load CRC-clean when composed with the checked-in M81 parent manifest/provider; `scripts/irem/inventory-corpus.ps1` now records two total M84 manifest matches and no unsupported top-level board buckets · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m84/games/*.toml` + `src/manifests/irem_m84/tests/m84_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I84-2** Executable M84 board profile — `src/manifests/irem_m84` now has an M84-owned executable wrapper for the local Hammerin' Harry M84 split sets, using the M81-compatible V30/Z80/YM2151/DAC board core behind an M84 manifest/save identity; `src/apps/player/adapters/irem_m84` registers `--system irem_m84` / `m84`, composes M84 child zips or folders with the M81 `hharry` parent supplied as supplemental media, reports resident media validation, exposes rollback-ready save-state, and data-gates real local player smoke through `MNEMOS_M84_SET_DIR=D:\emu\irem\M84;D:\emu\irem\M81`. Remaining: replace/verify the compatibility-core assumptions with board evidence for M84-specific memory/I/O behavior, Hammerin' Harry video/priority, raster timing, DIP behavior, and screenshot parity before calling this authentic · PARTIAL · HIGH · M-L · beyond Emu · Evidence: `src/manifests/irem_m84/m84_system.cpp` + `src/manifests/irem_m84/tests/m84_system_test.cpp` + `src/apps/player/adapters/irem_m84/irem_m84_adapter.cpp` + `src/apps/player/adapters/irem_m84/tests/irem_m84_adapter_test.cpp`

Cabinet input note: M81, M82, and M84 adapters now consume explicit frontend
`service` / `test` arcade inputs, keep `mode` as the service alias for older
callers, map operator test onto the active-low system bit 6, and persist those
fields in adapter state version 2.

---

## Irem M107 — 1 / 2

This section is split from M72/M81/M82 so Air Assault / Fire Barrel artifacts
do not get counted as older Irem board proof.

#### Manifests / board bring-up
- [x] **I107-1** Local M107 ROM-set contract — `src/manifests/irem_m107` carries checked-in embedded ROM-contract manifests for `airass` and `firebarr`, with parser/region-contract coverage for the 1 MiB main program region, boot-chunk reload declarations, interleaved sound program ROMs, graphics ROM groups, Air Assault data/sample regions, and current local ZIP CRCs. `MNEMOS_M107_SET_DIR=D:\emu\irem\M107` data-gates the sorted local ZIPs and proves both sets load CRC-clean; `scripts/irem/inventory-corpus.ps1` now records eight total M107 manifest matches when duplicate root and misbucketed `m72` copies plus metadata-only `.7z` copies are included · DONE · MED · S · beyond Emu · Evidence: `src/manifests/irem_m107/games/*.toml` + `src/manifests/irem_m107/tests/m107_system_test.cpp` + `scripts/irem/inventory-corpus.ps1`
- [~] **I107-2** Executable M107 board profile — `src/manifests/irem_m107` now assembles an M107-owned first-pass V-series board shell with separate main and sound V-series CPU instances, the declared 1 MiB main program and 128 KiB sound program regions, M107 RAM/shared/video/sprite/palette windows, YM2151 plus PCM command/status surfaces, input/DIP ports, whole-board save/load identity, and an M107-local diagnostic video path driven by the checked-in M107 graphics/subdata/sample regions rather than by an older Irem board. `src/apps/player/adapters/irem_m107` registers `--system irem_m107` / `m107`, supports ZIPs, single-inner wrapper ZIPs, unpacked set folders, embedded or in-archive `game.toml` manifests, resident media validation, rollback-ready save-state, capability discovery, and real local player smoke through `MNEMOS_M107_SET_DIR=D:\emu\irem\M107`; Air Assault direct `mnemos_player` screenshot and save/load smoke both produce 384x256 nonblank frames. Remaining: replace/verify the first-pass V33 configuration, M107 memory and I/O map, GA20/GA21/GA22 video/priority and PCM behavior, sound-CPU details, DIP behavior, raster timing, and authentic screenshot parity before calling the profile authentic · PARTIAL · HIGH · L · beyond Emu · Evidence: `src/manifests/irem_m107/m107_system.cpp` + `src/manifests/irem_m107/tests/m107_system_test.cpp` + `src/apps/player/adapters/irem_m107/irem_m107_adapter.cpp` + `src/apps/player/adapters/irem_m107/tests/irem_m107_adapter_test.cpp`

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
- [~] **U5 Amiga** — *HIGH.* CPU: m68000 ✓ (G1 applies) · Video: agnus + denise wired for OCS bitplanes/palette/custom registers plus `BPLCON0` high-resolution 640-pixel fetch/view, HAM/EHB/dual-playfield colour decoding, basic sprite register/DMA/attach rendering with bounded DMA-list channel reuse, `BPLCON1` playfield horizontal-scroll delay, `BPLCON2` playfield/sprite priority, `CLXCON`/`CLXDAT` collision latches, Copper pointer/strobe routing with frame-start `COP1LC` reload, MOVE/SKIP/WAIT cadence, impossible terminal-WAIT handling, high-resolution four-plane display-DMA CPU chip-RAM lockout, board-owned custom-register MOVEs for raster palette/interrupt effects, and a bounded blitter (`BLTCON0/1`, A/B/C/D pointers, modulos, first/last A masks, minterms, inclusive/exclusive area fill, line mode with texture and `SING`, `BLTSIZE`, BZERO/BBUSY, color-clock-bounded BLIT IRQ retirement, in-flight busy save state, `BLTPRI` CPU chip-RAM wait states while blitter DMA is busy) · Audio: paula DMA + capture · Glue: cia8520, Kickstart reset overlay, chip RAM, DF0-DF3 ADF mount + color-clock-distributed raw MFM byte stream with `ADKCON.WORDSYNC`/`DSKSYNC` start gating, raw-track write-DMA capture plus valid AmigaDOS-sector ADF writeback, CIAB drive motor/step/side/select, CIAA active-low disk-change/status latches, 300 RPM scanline-paced CIAB disk-index FLAG pulse, player `--system amiga500`, ADF launch via `MNEMOS_AMIGA500_KICKSTART`, media swapping, joystick/POT/CIAA input, mouse JOYxDAT counters with CIAA/POTINP buttons, `POTGO`-started RC-calibrated raster-line POT counters with reset-window delay, programmable axis thresholds, and frontend analog axis routing, CIA-A keyboard raw-key serial queue gated by host KDAT/SP handshake, exact keyboard control codes/reset warning, Caps Lock LED state, US physical keyboard USB/HID usage mapping, adapter-level runtime save target, and data-gated real Kickstart+ADF boot smoke are in the bootstrap branch. Remaining for true compatibility: media-backed proof of a full Kickstart+Workbench boot, cycle/beam-exact non-saturated display DMA contention, exact non-nasty CPU/display/blitter bus-slot arbitration, sprite bus-slot timing/hires edge behavior, floppy exact sub-byte bitcell timing and broader custom-track/non-AmigaDOS write behavior, international keymap selection/phantom-key matrix behavior, and remaining Copper/display bus arbitration edge cases. · PARTIAL · HIGH · L · beyond Emu · Evidence: `progress-analysis.md` §4 + `src/manifests/amiga500/` + `src/apps/player/adapters/amiga500/`
- [ ] **U6 NeoGeo** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Video: **LSPC (greenfield — exists nowhere)** · Audio: ym2610 (ssg + adpcm_a + adpcm_b). Emu is a scaffold. · Evidence: `progress-analysis.md` §4 + R19
- [~] **U7 Taito F2** — *HIGH.* CPU: m68000 ✓ (G1 applies) + z80 ✓ · Audio: ym2610 ✓ · Video: TC0100SCN/TC0200OBJ/TC0280GRD/TC0430GRW/TC0480SCP first pass ✓ (`taito_f2_video`: standard TC0100SCN BG/text RAM layout, dual-TC0100SCN Thunder Fox layer merge, RAM-generated text chars, layer disable/priority swap, rowscroll/colscroll hooks, real 16-byte TC0200OBJ records, packed-LSB 16x16 sprites, banked sprite-register routing, TC0190FMC-style sprite-bank register-window routing, TC0280GRD + TC0430GRW 64x64 ROZ tilemap RAM/control registers, TC0480SCP four 16x16 BG planes + RAM text plane + control/priority registers, TC0480SCP rowscroll, layer zoom, and BG2/BG3 row-zoom first-pass sampling, board-selectable TC0480SCP priority decoding, board-configurable palette formats, board-configurable TC0200OBJ sprite-extension RAM windows, board-configurable active-area marker source including Footchmp-style Y-bit routing, board-specific TC0200OBJ hide-pixel offsets, board-configurable TC0200OBJ immediate/full/partial-delayed buffering policies including qzchikyu partial-word overlay, TC0200OBJ zoom and continuation chaining, marker disable/flip-screen handling, X bias + master/extra/absolute sprite scroll markers, frame-persistent active-area/disable/flip/master-scroll state, TC0360PRI-style tile/text/ROZ/sprite priority register routing and sprite/tile blend modes, TC0200OBJ sprite-extension code policies) · Player adapter ✓ · Gun Frontier + Liquid Kids parent/clone + Quiz HQ + Quiz Torimon + Quiz Chikyu + Quiz Quest + Dondoko Don + Pulirula + Metal Black + Football Champ + Dead Connection + Dino Rex + Thunder Fox + Growl + Ninja Kids + Solitary Fighter ROM profiles, clone-parent zip fallback loading, and real F2 address map/data-gated boot coverage ✓. **Remaining:** broader board-family coverage, board-specific TC0360PRI priority variants, remaining TC0190FMC board-family profiles, TC0480SCP golden offset/row-zoom validation, and real-ROM golden visual/audio compatibility evidence. Emu is a scaffold (197 LOC). · Evidence: `progress-analysis.md` §4 + R19
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
