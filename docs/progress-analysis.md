# Mnemos ⇄ Emu hardware-implementation parity & progress analysis (2026-06-11)

Cross-repository parity audit comparing the current **Mnemos** (C++23, tier/`ibus`
architecture) implementation against the legacy **Emu** (portable C11) reference, per
system and per hardware component. Both codebases are first-party (Emu is our own
earlier emulator; Mnemos cannibalizes/ports its cores — see ADR-0006).

- **Mnemos:** `C:\dev\emu\Mnemos\src\{chips,manifests,disc}`
- **Emu:** `C:\Users\mkrol\source\repos\Emu\Emu\{chips,systems}`

> **Porting mandate (ADR-0006 §1 · proposed ADR-0024):** every Emu→Mnemos port is a
> **re-architecture to Mnemos-or-better standards** (correct tier, chip/runtime
> contracts, STD-001/002 naming + error model, one reusable canonical home, determinism
> + instrumentation) — **never a transcription of Emu C**. The backlogs below name
> *what* to port (behaviour); the *how* follows the mandate.

**Method:** seven file-grounded passes (one per shared system + one backlog sweep).
Each read the actual system glue and chip cores on both sides, used `wc -l` for
implementation-mass signals, and grepped for feature markers. Every verdict below is
anchored to a real file. Parity numbers are engineering estimates, not measured test
pass-rates; where Mnemos has an empirical corpus result it is cited.

> **Read §5 (Critical Risk Register) before doing system bring-up work.** It
> consolidates every stub, deferred feature, and self-flagged weakness that can
> silently produce wrong behaviour. Skipping it is how we re-debug solved ground.

---

## How to read "parity %"

**Parity % = the share of Emu's _hardware_ implementation for a system that Mnemos
matches or exceeds.** Developer tooling (debuggers, disassemblers, GUI, scripting,
save-states, movie/rewind) is a **separate axis**, inventoried in
[`tooling-gap-inventory.md`](tooling-gap-inventory.md) — this audit is about hardware.
The two verdicts invert: **Mnemos leads on hardware, Emu leads on tooling.**

A **direction arrow** shows _net capability_ (who is actually ahead), which is **not**
the same as parity: Mnemos can be at 88% parity yet net-ahead because it adds timing
accuracy Emu never had. Conversely, several Emu systems are **scaffolds** — "matching
Emu" there is a low bar and Emu is not a trustworthy oracle (use GPGX / hardware).

Verdict legend used in component tables:
- **FULL** — Mnemos matches Emu's coverage
- **EXCEEDS** — Mnemos is more accurate/complete than Emu
- **PARTIAL** — Mnemos implements it but with gaps vs Emu
- **MISSING** — Emu has it, Mnemos lacks it
- **N/A** — neither implements it (or not applicable to the system)

---

## 1. Executive summary

- **Breadth:** Mnemos implements **6 of Emu's 15 systems (40%)**. Emu is the broader
  portfolio; Mnemos is the deeper one on what it covers.
- **Depth (shared systems):** average hardware parity **≈ 92%**. On all 6 shared
  systems Mnemos is at full-or-better parity on every core silicon block, and
  **net-ahead on 5 of 6** thanks to timing models Emu lacks.
- **The inversion:** for **SMS/GG, Sega CD, and Irem M72, Mnemos substantially
  _exceeds_ Emu** — Emu's M72 does not even render. The only system where Emu is
  clearly ahead is **C64 _tooling_** (~4,400 LOC of debuggers/format-IO not ported);
  its _hardware_ is at parity.
- **The real gap is breadth:** 9 unported systems. 6 are substantial in Emu (NES,
  Spectrum, CPS1, CPS2, Saturn, Amiga); 3 are scaffolds barely worth porting _from_
  (SNES, NeoGeo, Taito F2).

**Portfolio metrics:** system breadth coverage **40%** (6/15) · shared-system hardware
depth **≈ 92%** · total weighted portfolio (breadth × depth) **≈ 37%**.

---

## 2. Master parity table — all 15 Emu systems

| System | In Mnemos | HW parity vs Emu | Net | One-line gap / lead |
|---|---|---|---|---|
| **Genesis / Mega Drive** | yes | **88%** | ⬆ ahead | Leads on VDP FIFO/DMA timing; missing S&K lock-on, SVP, J-Cart, whole-system savestate, 68K address-error |
| **SMS + Game Gear** | yes | **95%** | ⬆⬆ far ahead | +7 mappers, YM2413 FM, real GG video, 93C46 saves, PAL switch; remaining misses are pause→NMI + deeper cart-header validation |
| **C64** | yes | **95% (HW)** | ⬌ even (HW) / ⬇ (tooling) | Every chip present + more cart types; Emu wins on dev tooling (disasm/debug/SID/sprite/movie) |
| **Sega CD** | yes | **95%** | ⬆ ahead | Real stamp-rotation ASIC, 1M word-RAM, font expander; CHD v5 disc format now supported |
| **Sega 32X** | yes | **88%** | ⬆ ahead | Threaded dual-SH2, PWM DC-block; missing addr-error, SCI, full INTC delivery, cycle-true timing |
| **Irem M72** | yes | **~100%** | ⬆⬆⬆ vastly ahead | Emu is a non-rendering scaffold; Mnemos renders R-Type with video/IRQ/sound/inputs |
| NES | no | 0% | ⬇ | Needs ppu2c02 + 2A03 APU — MEDIUM |
| SNES | no | 0% | ⬇ | Needs 65C816+SPC700+S-PPU+S-DSP — VERY HIGH (Emu scaffold) |
| Saturn | no | 0% | ⬇ | Needs VDP1/2+SCSP+SCU-DSP (SH-2 reusable) — VERY HIGH |
| Amiga | no | 0% | ⬇ | Needs Agnus/Denise/Paula/CIA8520 — HIGH |
| Spectrum | no | 0% | ⬇ | Z80 reused, ULA inline — LOW (easiest win) |
| NeoGeo | no | 0% | ⬇ | Needs YM2610+LSPC — HIGH (Emu scaffold) |
| CPS1 | no | 0% | ⬇ | Needs MSM6295+CPS-A/B GFX+QSound — MEDIUM |
| CPS2 | yes | ~92% | ⬆ | Keyed opcode crypto, CPS-2 video, QSound PCM/ADPCM/echo HLE, EEPROM, MAME-matched per-game digital/ticket/analog input profiles, coin output latches, ZIP manifest loading, player adapter, player save/load, and a data-gated CPS2 corpus smoke runner are wired; local proof covers the committed 37-set CPS2 frame/audio/QSound-register/EEPROM baseline, with 9/37 sets producing nonzero rendered audio and 28/37 carrying default-gate QSound-programmed-silent evidence for follow-up. The gate runs 35 sets at 600 frames, `armwar.zip` at 700 frames, and `dstlk.zip` at 1200 frames because Armored Warriors and Darkstalkers both remain black at the 600-frame checkpoint under the current Emu/Mnemos oracle behavior. The runner can now use an Emu-style gameplay input cadence plus a longer audio-only window; `1944_mn.zip` becomes audible by the 2500-frame proof window (first nonzero rendered-audio sample frame 147949, about CPS2 frame 367), and a 25-row 700-frame silent-set sweep converts 11/25 rows to audible evidence. Emu comparison lifted two concrete fixes into Mnemos: zero-filled expansion of partial QSound Z80 ROM regions and the 32 KiB alternate object-RAM mirror; with repeated gameplay input, `1944.zip`, `1944_mn.zip`, `armwar.zip`, `avsp.zip`, `choko.zip`, `cybots.zip`, `ddtod.zip`, `dimahoo.zip`, `dstlk.zip`, `gigawing.zip`, `sfa3.zip`, and `xmcota.zip` now produce rendered audio by 2500 frames, and `ecofghtr.zip` is audible with a new thresholded first-significant-audio metric separating tiny echo residue from gameplay audio. Remaining default-silent rows still need one-at-a-time Emu comparison before broad corpus expansion |
| Taito F2 | partial | 96% | ⬆ | Mnemos now has a first-pass F2 board, YM2610 path, TC0100SCN/TC0200OBJ video, dual-TC0100SCN Thunder Fox rendering, board-selectable TC0100SCN program-region 1bpp text glyphs for quiz maps, TC0280GRD + TC0430GRW ROZ tilemap rendering/control wiring, TC0480SCP four-BG-plane + RAM-text rendering/control wiring with rowscroll, layer zoom, BG2/BG3 row-zoom first-pass sampling, and synthetic golden coverage for board BG/text offsets plus BG2/BG3 row zoom, real 16-byte sprite records, board-configurable palette formats, board-configurable TC0200OBJ sprite-extension RAM windows, board-configurable active-area marker source including Footchmp-style Y-bit routing, board-specific TC0200OBJ hide-pixel offsets, board-configurable TC0200OBJ immediate/full/partial-delayed buffering policies including qzchikyu partial-word overlay, TC0200OBJ zoom/continuation chaining, marker disable/flip-screen handling, X bias + master/extra/absolute sprite scroll markers, frame-persistent active-area/disable/flip/master-scroll state, TC0360PRI-style tile/text/ROZ/sprite priority register routing and sprite/tile blend modes with ROZ-selector and all sprite-priority-group synthetic coverage, TC0200OBJ sprite-extension code policies, Gun Frontier bank-register routing, TC0190FMC-style sprite-bank register-window routing, board-selectable TC0480SCP priority decoding, player adapter with frame-exact save/load, Gun Frontier World/Japan, Liquid Kids parent/clone, Quiz HQ, Quiz Torimon, Quiz Chikyu, Quiz Quest, Dondoko Don, Pulirula, Metal Black, Football Champ, Dead Connection, Dino Rex, Thunder Fox, Growl, Ninja Kids, and Solitary Fighter ROM profiles, clone-parent zip fallback loading, real F2 map/data-gated boot coverage, and a data-gated Taito F2 corpus smoke runner that accepts plain set zips or one-level title-wrapper zips, synthesizes self-describing zips under `build/scratch`, and supports optional screenshot SHA-256 pins; local proof covers the recursive `D:\emu\arcade\Taito` F2 candidates (`dinorex`, `gunfront`, `gunfrontj`, `growl`) with save/load/nonblank restore 4/4 passing, while the broad Taito inventory reports 4/25 local packages runnable by Mnemos today; still needs non-F2 board families/media paths, corpus proof beyond that local F2 set, real-board TC0360PRI priority proof, remaining TC0190FMC board-family profiles, and populated real-ROM visual/audio golden evidence |

**Taito F2 update 2026-06-24:** the player adapter now resolves checked-in
Taito F2 manifests directly from plain set zips and from one-level title-wrapper
zips such as `Gun-Frontier_Arcade_EN.zip` -> `gunfront.zip`. Local proof used
`D:\emu\arcade\Taito` as a recursive corpus root; the runner recognized
`dinorex`, `gunfront`, `gunfrontj`, and `growl`, synthesized self-describing
zips under `build/scratch`, and passed save-state restore plus nonblank
screenshot checks for all 4/4 candidates. Gun Frontier World/Japan still share
the screenshot SHA-256
`e5d013cf630a8e9737408d673453ce022252505ea0553b3aacd6b05f665854de`; Dino Rex
now boots visibly through the player path after the D39 IRQ/palette/sprite
corrections. `D:\emu\arcade\Taito\Type X` and other non-F2 Taito boards are not
counted as Taito F2 coverage.

**Taito arcade corpus inventory 2026-06-24:** `scripts/taito/inventory-corpus.ps1`
now records broad local Taito coverage from a single corpus root. Against
`D:\emu\arcade\Taito -Recurse`, current output is **4/25 packages runnable by
Mnemos today**: the four F2 candidates above. The uncovered local packages are
5 G-NET/ZN-2 CHD wrapper zips, 1 Type X2 CHD wrapper, 1 Type Zero CHD wrapper,
1 Namco System 246 Taito-published title, 8 `Type X` RAR packages, and 5
non-Taito Irem M92 wrappers under the local `F2` folder. This is an executable
gap report, not an emulation claim: closing "100% Taito arcade" still requires
distinct non-F2 board implementations and media/container support.

**Taito G-NET prerequisite update 2026-06-24:** Mnemos now has a native
`sony.r3000a` CPU library for the PlayStation-derived Taito G-NET / Sony ZN-2
workstream. The first slice covers little-endian MIPS I integer execution,
branch delay slots, load delays, HI/LO multiply-divide paths, CP0 status/cause/
EPC basics, COP2/GTE register-transfer and command-latch shell, exceptions,
register/trace introspection, and save-state round-tripping. Follow-up slices add
`src/manifests/taito_gnet/`, which inspects
ZIP-wrapped G-NET packages, decodes bounded CHD v5 flash-card block-device
images from local corpus packages such as `chaoshea.zip`, and assembles a first
R3000A board shell with caller-provided BIOS ROM, 2 MiB main RAM, mounted
flash-card images, FC-board flash bank selection/control registers, a direct
PCMCIA data aperture, BIOS/firm/zoom/wave flash windows, a minimal
RF5C296-style index/data IO register pair with reset-bit tracking and card-byte
proxying, 1 KiB scratchpad RAM, first BIOS-facing memory/cache control latches,
a GPU register/VRAM latch shell, COP2/GTE register-transfer and command-latch shell,
limited GPU command and OTC DMA execution, interrupt status/mask delivery into
the R3000A IRQ line, DMA channel/control/interrupt register latches, root-timer
counter/mode/target latches with first-pass target/overflow IRQ delivery,
board-shell save/load coverage, and a data-gated BIOS+CHD assembly smoke behind
`MNEMOS_TAITO_GNET_BIOS`/`MNEMOS_TAITO_GNET_PACKAGE`.
The player now has a `--system taito_gnet`/`gnet` board-smoke adapter that
requires `MNEMOS_TAITO_GNET_BIOS`, preserves the package ZIP as the primary
media, assembles the native board shell, exposes CPU/RAM/flash/card metadata,
and round-trips adapter save state. This still does not make G-NET packages
playable: the local packages contain CHDs plus `readme.txt`, not BIOS ROMs,
and the remaining work is the locked-card command/security protocol, BIOS
sourcing, GPU renderer/SPU/real GTE command math, full DMA transfer timing beyond GPU/OTC,
exact root-timer sync/clock-source modes, JVS/I/O,
playable video/audio/input presentation, and real boot proof.

**Taito F2 input update 2026-06-24:** the player adapter now exposes four
arcade-panel ports for the multi-player Growl and Ninja Kids board profiles,
routes P3/P4 active-low panel bytes into the existing board latches, maps
P3/P4 START/COIN onto the shared system byte, and preserves those frontend
controller snapshots in adapter save states. Solitary Fighter stays advertised
as a two-player panel despite sharing the split-input board map.

---

## 3. Per-system deep dives (shared systems)

### 3.1 Genesis / Mega Drive — 88% ⬆

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| CPU M68000 | `chips/m68k/m68k.c` 3034 + dasm 1145; ~217 cycle markers; explicit address/bus-error group-0 exceptions | `chips/cpu/m68000/m68000.cpp`; instruction-atomic `cycle_debt_` model, prefetch/refresh-stall accounting, functional vector-2/3 group-0 frames | PARTIAL | Both cycle-aware. Mnemos now has address-error and explicit-BERR group-0 mechanics; concrete Genesis/Sega CD BERR maps and prefetch-exact corpus parity remain open |
| CPU Z80 | `chips/z80/z80.c` 1217 + dasm; ~20 undoc/MEMPTR markers | `chips/cpu/z80/z80.cpp` 1661 + 778 LOC tests; 48 undoc/MEMPTR/IM markers | EXCEEDS | More undoc-flag/WZ coverage + SingleStep JSON conformance |
| VDP (render) | `chips/genesis_vdp/genesis_vdp.c` 1707 + hcounter 486; in-chip FIFO ring | `chips/video/genesis_vdp/genesis_vdp.cpp` 1929 + hcounter 509 | FULL | Both H32/H40, hcounter tables, sprites, windows. Comparable |
| VDP write-timing / FIFO | system-side `sync_vdp_active_write_fifo`: release-period + 3-slot latency | in-chip 4-entry byte-accurate accept FIFO: `fifo_drain_[4]`, `next_accept_slot`; `write_accept` default-on | EXCEEDS | Mnemos byte-accurate FIFO (VRAM word = 2 slots) drove corpus 81%→87% |
| DMA timing | `service_pending_vdp_dma`, busy-bit hold, 68K-stall | `estimate_dma_transfer_cycles`; separate `dma_busy_master_cycles_` (status held, 68K free) vs `dma_stall_master_cycles_` (68K halted via `cpu_gate`) | FULL | Both split DMA-busy from 68K-stall; Mnemos cleaner two-timer model |
| YM2612 (FM) | `chips/ym2612/ym2612.c` 1257; ~171 env/LFO/SSG/ch3/DAC markers | `chips/audio/ym2612/ym2612.cpp` 1188 + 290 tests; SSG-EG, LFO, CSM, ch3 special, DAC ladder, 8 algorithms | FULL | Feature-equivalent |
| SN76489 (PSG) | `chips/sn76489/sn76489.c` 353 | `chips/audio/sn76489/sn76489.cpp` 300 + 184 tests | FULL | 3 tone + noise LFSR, atten, GG stereo. Equivalent |
| Cartridge / mapper | genesis.c header parse, ROM map, mirror | `genesis_cart.cpp` 86 + system.cpp map_rom; 4 MiB window clamp | FULL | Core cart loading equivalent |
| SRAM saves | genesis.c `sram_window_contains`/`sram_addr_to_index` | `genesis_cart.cpp` `wire_cart_sram`; 24-bit, priority-1 over ROM, `.srm` | FULL | Header-driven battery SRAM, $A130F1 map/WP. Parity |
| EEPROM saves | genesis.c full I2C 24Cxx state machine | `genesis_eeprom.cpp` 85; I2C 24Cxx, SDA/SCL pins | FULL | Pin-level I2C serial-EEPROM both sides. Parity |
| Banking / SSF2 | genesis.c bank handling | `genesis_banking.cpp` 52; >4 MiB $A130F3-FF page slots 1-7, slot 0 fixed | FULL | SSF2 byte-perfect in Mnemos |
| Region / IO ports | genesis.c `init_default_io_ports`, version reg, 3 ports + serial | `genesis_region.cpp` 77 + system.cpp $A10000-1F: version, data/ctrl A/B/C, market export bit | FULL | Both model version reg, 3 IO ports. Parity |
| Controllers / peripherals | genesis.c 3-button default; **J-Cart** adds ports 3&4 (4-player) | system.cpp pluggable `peripheral::device` ports, default 6-button MK-1653 | PARTIAL | Mnemos cleaner pluggable SDK + 6-button; Emu adds J-Cart. No multitap/Menacer/Justifier either side |
| Bus arbitration (Z80) | genesis.c BUSREQ/RESET, Z80 bank | system.cpp $A11100 BUSREQ / $A11200 RESET, 9-bit `z80_bank`, +1-cyc z80-bus latency | FULL | Both BUSREQ/RESET + banked window. Mnemos adds explicit z80-bus latency |
| **Lock-on (Sonic & Knuckles)** | genesis.c `lock_on_child_rom`, $200000-3FFFFF intercept | **none** (comment only) | **MISSING** | Emu supports S&K lock-on passthrough; Mnemos has none |
| **SVP coprocessor (Virtua Racing)** | genesis.c SVP bus intercepts $390000, DRAM/ctrl windows (DSP itself a no-op stub pending SSP1601) | **none** | **MISSING** | Emu has SVP MMIO scaffolding (DSP stubbed); Mnemos absent entirely |
| CD-DA mixing | genesis.c `step_cdda_audio` (Sega CD bridge) | **none in Genesis manifest** (lives in segacd subsystem) | PARTIAL | Architectural: Emu folds CDDA into Genesis core; Mnemos separates into Sega CD manifest |
| **Save states** | genesis.c `genesis_save_state` (full system serialize) | per-chip `save_state` exists; **no system-level Genesis savestate** in runtime | **PARTIAL** | Chips individually serializable, but no assembled-Genesis save/load path |
| Revision / mix model | genesis.c `genesis_set_revision`, `genesis_get_revision_mix_gains_q12`, low-pass cutoff | region (NTSC/PAL) only; no model-1/2 mix-gain selector found | PARTIAL | Emu models per-revision analog audio mix; Mnemos does not |

**Parity justification (88%):** the five core blocks every commercial cart depends on
(M68000, Z80, VDP render+FIFO+DMA timing, YM2612, SN76489) plus SRAM/EEPROM/banking/
region/IO/Z80-arbitration are FULL or EXCEEDS, empirically validated (87% byte-perfect
across 2784 titles). The gap is concentrated in peripheral cartridge silicon (lock-on,
SVP, J-Cart) and system conveniences (whole-machine savestate, per-revision mix),
none of which affect the common-case library. Dragged from ~95% to 88% by those plus
the remaining M68000 BERR-map/prefetch-exact gap.

**Mnemos EXCEEDS Emu in:** byte-accurate VDP write-accept FIFO; clean DMA-busy vs
68K-stall split; Z80 undoc/MEMPTR coverage + JSON conformance; co-located test suites
on every chip; pluggable peripheral SDK + explicit z80-bus latency.

**Gaps (Emu has, Mnemos lacks):** S&K lock-on; SVP bus scaffolding; J-Cart 4-player;
whole-system save state; concrete Genesis/Sega CD BERR maps and prefetch-exact M68000
group-0 parity; per-revision audio mix. CD-DA is Genesis-core in Emu but segacd-only in
Mnemos (architectural).

---

### 3.2 SMS + Game Gear — 95% ⬆⬆

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| CPU Z80 | `chips/z80/z80.c` 1217 + dasm 1832 | `chips/cpu/z80/z80.cpp` 1661; `set_irq_line`/`set_nmi_line` | FULL | Mnemos exposes finer line-level setters. Equivalent |
| SMS VDP (Mode 4) | `chips/sms_vdp/sms_vdp.c` 456; Mode-4 BG+sprites, line-IRQ `reg[10]`, 192/224/240 via `pal_mode` | `chips/video/sms_vdp/sms_vdp.cpp` 783; same + CRAM-write timing note | FULL | Same feature set; Mnemos larger (more detail) |
| **Game Gear VDP mode** | CRAM masked `& 0x1F` rendered as SMS 6-bit; **no 12-bit CRAM, no 160x144 crop** | `set_gg()`: real 12-bit `gg_cram_` (BGR444), central 160x144 crop, GG palette expand | **EXCEEDS** | Emu cannot render true GG color/viewport |
| SN76489 PSG | `chips/sn76489/sn76489.c` 353 | `chips/audio/sn76489/sn76489.cpp` 300 | FULL | Equivalent PSG |
| GG stereo PSG ($06) | `sn76489_update_stereo`: hi-nibble L / lo-nibble R | `write_stereo()` + `set_stereo_capture()`: bits 4-7 L / 0-3 R | FULL | Same bit layout. Parity |
| **YM2413 FM unit** | `chips/ym2413/` present; wired ports `$F0/$F1/$F2`, `fm_enabled`, per-frame FM buffer | `chips/audio/ym2413` + optional SMS `fm_unit`: ports `$F0/$F1/$F2`, manifest chip id `fm`, player `--fm`, capture mixed into the SMS adapter | FULL | Parity for the Japanese SMS FM Sound Unit; Mnemos keeps it opt-in so base SMS/GG behavior stays unchanged |
| Sega standard mapper | `sms.c` inline; `$FFFC`+`$FFFD/E/F` pages, cart-RAM, 1KB fixed page-0 | `mapper/sms_mapper` 247; `$FFFC-$FFFF` priority-1 overlay | FULL | Same mapper; Mnemos factors into chip + overlay |
| **Codemasters mapper** | **Absent** | `mapper/codemasters_mapper` 214 + checksum auto-detect + `.toml` | **EXCEEDS** | Emu SMS has none |
| **Korean mapper (std $A000)** | **Absent** | `mapper/korean_mapper` 173 | **EXCEEDS** | — |
| **Korean MSX 8K (+Nemesis)** | **Absent** | `mapper/korean_msx_mapper` 208 | **EXCEEDS** | — |
| **HiCom 188-in-1** | **Absent** | `mapper/hicom_mapper` 161 | **EXCEEDS** | — |
| **Janggun (bit-reversed)** | **Absent** | `mapper/janggun_mapper` 237 | **EXCEEDS** | — |
| **Multi 4×8K (XOR)** | **Absent** | `mapper/multi_4x8k_mapper` 192 | **EXCEEDS** | — |
| **Multi 16K (4-Pak)** | **Absent** | `mapper/multi_16k_mapper` 192 | **EXCEEDS** | — |
| **EEPROM / battery saves** | **Absent** (no SMS/GG 93C46) | `storage/eeprom_93c46` + `$8000`/`$FFFC` overlays + CRC auto-detect (5 carts) | **EXCEEDS** | Emu SMS/GG has no cart NVRAM |
| **GG IO handset ($00-$06)** | only `$06` (stereo) decoded; `$00-$05` open-bus | `gg_io.hpp` 94; `$00` START/region, `$01-$05` EXT link, `$06` stereo | **EXCEEDS** | Emu has no `$00` mode/region or EXT-link |
| **Region / PAL-NTSC** | VDP has `pal_mode` but `sms.c` **hardcodes NTSC** scanlines → NTSC-only | `set_pal()` 262↔313 lines; per-region `.toml`; `sms_region.cpp` cart-region parse | **EXCEEDS** | Emu's system layer is NTSC-locked |
| Cart-header parse | `sms_parse_cart_info` (full): "TMR SEGA", checksum, product code, region nibble, size code | `sms_region.cpp` 38: region nibble only; lighter | PARTIAL (Emu deeper) | Emu does checksum compute, product code, size match |
| **Pause button (NMI)** | `sms_pause()` → `z80_nmi()` | Z80 has `set_nmi_line` but **no SMS-system pause entry point wired** | PARTIAL (Emu ahead) | Mnemos pause button does nothing |
| Controller IO ($DC/$DD, $3F) | inline pad read + TR/TH latch | `read_pad_dc/dd` + `io_ctrl` + pluggable peripherals (MK-3020 default) | EXCEEDS | Same model; Mnemos adds peripheral abstraction |

**Parity justification (95%):** Mnemos covers everything Emu's SMS does at the
core-chip level (Z80, VDP, PSG, YM2413 FM, Sega mapper, GG stereo, controller IO all
FULL) and adds an 8-mapper family vs Emu's 1, real GG video, 93C46 saves, full GG
handset, and runtime PAL/NTSC that Emu hard-locks. Emu leads only in deeper
cart-header validation and a wired pause-NMI. Net capability strongly favours Mnemos.

**Mnemos EXCEEDS Emu in:** 8 mappers vs 1 (with CRC auto-detect); true 12-bit GG video
+ 160x144 crop; 93C46 battery saves; full GG `$00-$06` handset; runtime PAL/NTSC;
pluggable controller-port peripherals.

**Gaps (Emu has, Mnemos lacks):** pause→NMI entry point; deep cart-header validation
(checksum/product-code/size); Sega-mapper `$8000-$BFFF` cart-RAM bank-select detail
needs real-ROM validation and `.srm` persistence for flat cart RAM.

---

### 3.3 C64 — 95% hardware ⬌ / tooling ⬇

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| CPU 6510 | `chips/m6510_v2` 2002 (active, ADR-0005) + legacy `m6510` 987; NMOS, on-chip $00/$01 port | `cpu/m6510/m6510.cpp` 1587 + decode 333; single impl, doubles as 1541 CPU | FULL | Emu carries 2 parallel cores mid-cutover; Mnemos one |
| VIC-II video | `chips/vic2` 1307; per-line shadow | `video/vic_ii_6569` 964+261; PAL 6569 / NTSC 6567R8, open-bus | FULL | Both per-line accurate |
| SID audio | `chips/sid` 918 | `audio/sid_6581` 696+171; 6581/8580, paddle mux, dual-SID @ $D420 | FULL | Mnemos wires optional stereo 2nd SID |
| CIA #1 | `chips/cia6526` 1360 (shared) | `bus_controller/cia_6526` 823+170; TOD, kbd/joy, /FLAG, paddle mux | FULL | Equivalent |
| CIA #2 | same `cia6526` | same `cia_6526`; VIC bank sel, IEC ATN/CLK/DATA, RS-232 | FULL | Equivalent |
| PLA banking | `chips/pla` 251 | `mapper/c64_pla` 172+80; $01-port + /GAME//EXROM decode | FULL | Equivalent decode |
| Cartridge / bankcart | `c64_bankcart` 139 (Ocean+Magic Desk) + `c64_easyflash` 143 | `mapper/c64_cartridge` 326+101: 10 types incl. EasyFlash | EXCEEDS | Mnemos 10 cart types unified vs Emu's 2 + separate EasyFlash |
| EasyFlash | `c64_easyflash` 143 | folded into `c64_cartridge` | FULL | Same feature, different packaging |
| 1541 disk drive | `chips/c1541` 3539: full + synthetic + gcr + disk_bind + disk_image | `storage/c1541` 1464: full_drive (6502+2×VIA+GCR) + synthetic + d64 + prg + gcr | FULL | Same architecture; **Mnemos notes GCR read path "still being proven"**; Emu higher LOC (more format/edge coverage) |
| IEC bus | `chips/iec_bus` 133 | `shared/iec_bus.hpp` 52 (header-only) | FULL | Both ATN/CLK/DATA wired-OR |
| Datasette | `c64_datasette` 258+207; `.tap` playback | `storage/datasette` 146+69; `.tap` v0 playback | FULL | Both read-only `.tap`, no write/save |
| REU | `c64_reu` 221+136; 1700/1764/1750 DMA | `peripheral/reu` 228+73; 128/256/512K, DMA $DF00 | FULL | Both 3-model REC DMA |
| Modem / RS-232 | `c64_modem` 372+142 Hayes-AT + TCP/loopback | `peripheral/modem` 540+197 + `tcp_transport` 151 **and** separate `rs232` UART 153+106 | EXCEEDS | Mnemos splits bit-level UART from byte-level modem |
| **Sprite IO / mux** | `c64_sprite_io` 358 + `c64_sprite_mux` 87 | **none** | **MISSING** | Tooling: no sprite import/export or multiplex analyser |
| **SID MIDI / player / scope** | `c64_sid_midi` 193 + `c64_sid_player` 291 + `c64_sid_scope` 112 | **none** | **MISSING** | Tooling: no `.sid` playback, SMF export, scope |
| **Disasm / debug / symbols** | `c64_disasm` 221 + `c64_debug` 777 + `c64_symbols` 358 | **none** (C64-specific) | **MISSING** | Tooling: no C64 disassembler/debugger/labels |
| **Movie / rewind** | `c64_movie` 320 + `c64_rewind` 130 | none C64-specific (engine has generic save-state) | MISSING/PARTIAL | No C64-wired deterministic movie or rewind ring |
| **LHA / LNX archives** | `c64_lha` 599 + `c64_lnx` 147 | **none** | **MISSING** | No `.lzh`/`.lnx` loaders |
| **Charset / image IO** | `c64_charset_io` 430 + `c64_image_io` 359 | **none** | **MISSING** | No font/screen/bitmap import-export (Koala/.aas/.seq/PNG) |
| LUT-gen | `c64_lutgen` 87 | folded into palette TOMLs | N/A/FULL | Mnemos uses data-driven palette files |

**Parity justification:** **Core hardware ~95%** — every silicon part and physical
peripheral (6510, VIC-II, SID, both CIAs, PLA, full cycle-accurate 1541, IEC,
datasette, REU 3-model DMA, Hayes+TCP modem, RS-232 UART, cartridge incl. EasyFlash)
is present and equivalent, with Mnemos exceeding on cartridge breadth. **Tooling ~5%**
— Emu's C64 is its flagship dev platform with ~4,400 LOC of disasm/debugger/symbols/
SID-tooling/sprite-IO/charset-IO/LHA-LNX/movie/rewind, of which Mnemos has none.
Blended ≈ 55%; hardware-only ≈ parity.

**Mnemos EXCEEDS Emu in:** cartridge-type coverage (10 vs 2+separate); RS-232 layering
(dedicated bit-level UART); declarative dual-SID.

**Gaps — hardware:** none of consequence (only depth: Emu's 1541 has more format LOC,
and Mnemos self-flags its GCR read path as not yet proven; datasette read-only both
sides). **Gaps — tooling (the bulk of the delta):** 6502 disasm + 777-LOC debugger +
symbols; SID player/MIDI/scope; sprite IO + mux; charset/image IO; LHA/LNX loaders;
input record/replay movies + per-frame rewind ring.

> Emu's `chips/cia8520` 98 (Amiga part, unused by C64) and legacy `chips/m6510` 987
> (mid-sunset per ADR-0005, superseded by `m6510_v2`) are N/A or transitional.

---

### 3.4 Sega CD / Mega CD — 95% ⬆

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| Sub-CPU (m68k) | `segacd.c` `sys_segacd_run_cycles`; reuses m68k; reset gating SRES/BREQ | `segacd_system.cpp` `run_cycles`/`release_sub_reset`; reuses Genesis m68000; continuous-timeline base | FULL | Mnemos adds fractional-cycle carry timeline for exact 87.5/53.69 pacing |
| Gate array / register bridge | `segacd_gate_array_read8/write8` + main bridge; 256-byte block | `gate_read/gate_write_main/sub`; main bridge with per-access fence (catch_up_sub on every R/W) | FULL | Mnemos fences sub-CPU timeline on every gate access (fixed a comm deadlock); Emu has no such guard |
| Word RAM — 2M | flat 256 KB; main $200000-$23FFFF, sub $080000 | flat 256 KB; main $200000 + sub $080000 | FULL | Equivalent |
| **Word RAM — 1M** | flag only: `$03 bit 2` recognized; **no bank views, no cell-image, no dot-image** | full model: interleaved bank views, `cell_image_offset` $220000, `word_dot_read/write` $080000 with PM1:PM0 priority | **EXCEEDS** | Emu never implements 1M dual-bank semantics |
| CDC (LC8951) | indirect reg file + 16 KB ring + DMA; dest 4(PCM)/5(PRG)/word | `segacd_cdc.cpp` 434; full reg file + decoder + DMA dest 2/3(host)/4/5/word with 1M sub-bank routing | FULL+ | Mnemos word-RAM DMA is 1M-bank-aware; Emu 2M-flat only |
| CDD (CD drive) | `cdd_commit_status`/`cdd_report_toc`; CXD2500 cmd set, 75 Hz | `segacd_cdd.cpp` 513; same + RS1-RS8 TOC preservation + PLAY-warmup + TOC/STOP decouple | FULL | Mnemos has extra real-drive timing fidelity (the disc-read fix) |
| CD-DA audio | `sys_segacd_cdda_play_lba_range` + per-sample disc read | `cdda_play`/`cdda_next_sample` with per-sector cache | FULL | Mnemos caches the 2352-byte sector |
| RF5C68 / RF5C164 PCM | `chips/rf5c68/rf5c68.c` 194 | `chips/audio/rf5c68/rf5c68.cpp` 396 | FULL | Same silicon, both wired to CDC PCM-RAM DMA |
| **Graphics ASIC (stamp/rotation/scaling)** | `stamp_renderer_run`: naive nearest-neighbor; **no stamp-map indirection, no size modes, no HFLIP/rotation, no 4bpp, no priority** (header admits it doesn't really perform the rotation) | `segacd_stamp.cpp` 218: 4 size modes, stamp masking, HFLIP+ROT LUTs, 4bpp packing, PM priority, 13.3→13.11 trace vectors | **EXCEEDS** | Emu is a placeholder sampler; Mnemos is a real Super-GFX renderer |
| **Font expander ($4C-$57)** | **MISSING** | `gate_write_sub` $50-$57 1bpp→4bpp glyph expansion | **EXCEEDS** | Emu has no font expander at all |
| CD-ROM ECC/EDC (circ_ecc) | `chips/circ_ecc/circ_ecc.c` 187; EDC CRC-32 + P/Q, ECMA-130 | `disc/circ_ecc.cpp`; direct port — same EDC table + P/Q regen | FULL | Faithful port |
| Disc image formats | `chips/disc_image/disc_image.c` 1097: CUE (5 modes), BIN, ISO, IMG + **Saturn IP.BIN parser + ISO 9660 file walker + CHD** | `disc/disc_image.cpp`: CUE (same 5 modes), BIN/IMG, ISO, CHD; **no IP.BIN, no ISO9660 walker** | PARTIAL | Emu still broader on Saturn-oriented helpers; Mnemos now has CHD disc and block-device media support |
| **CHD (compressed disc/block media)** | `chips/chd_image/` 2541 across 6 files (cdfl/cdlz/cdzl/huff) — full CHD v5 reader | `disc/chd_reader.*`: v5 CD `cdzl`/`cdlz`/`cdfl` plus block-device `lzma`/`zlib`/`huff`/`flac`/`none`/`self` | FULL | Mnemos covers the common CHD v5 codec stack used by Sega CD and local G-NET flash-card packages |
| BIOS handling | `sys_segacd_attach_bios`; CD-BIOS at $000000-$01FFFF + $020000 | BIOS loaded main-side as the Genesis "cartridge"; sub boots from PRG-RAM (no separate sub-BIOS image) | FULL | Different placement, functionally equivalent boot path |
| Main↔sub comm protocol | comm flags $0E/$0F + words $10-$1F flat; IFL2 via $00→L2 | $0E/$0F lane-merge + $10-$1F; IFL2 as held level; per-access timeline fence | FULL | Mnemos corrects an Emu IRQ-bit off-by-one and models IFL2 as a held level |
| Interrupt routing | `segacd_sub_irq_raise/update`; levels 1-6; pending = `bit N-1` | `raise/update/acknowledge`; same 6 levels; pending = `bit N` (aligned with BIOS $33 mask) | FULL | Mnemos fixed the bit-convention mismatch, retires pending per-level on IACK |
| Backup (battery) RAM | 8 KB; main $FE0000 (odd-byte) | 8 KB `backup_ram`; gate/sub access; `.brm` persistence at player | FULL | Equivalent |

**Parity justification (95%):** every core Sega CD subsystem is at full functional
parity or better, and Mnemos additionally implements three things Emu only stubs or
omits — the real stamp/rotation GFX ASIC, the 1M word-RAM cell/dot/bank model, and
the font expander. Emu leads only in the Saturn-oriented disc helpers (IP.BIN +
ISO 9660 walker); Mnemos now covers the CHD v5 codec stack for compressed CD
images and bounded block-device media. Net: Mnemos exceeds on console hardware;
Emu still has broader legacy disc-container helpers.

**Mnemos EXCEEDS Emu in:** real Super-GFX stamp renderer; 1M word-RAM mode; font
expander; 1M-bank-aware CDC DMA; protocol/timing correctness fixes (per-gate fence,
corrected IRQ bit convention, IFL2-as-held-level, RS1-RS8 TOC + PLAY warmup).

**Gaps (Emu has, Mnemos lacks):** Saturn IP.BIN parser and ISO 9660 file-system
walker. CHD v5 compressed CD and bounded block-device decode are implemented.

> Open frontiers from the differential-trace work (not strictly Emu parity): license-
> screen artifacts and the intro freeze (main spins $FFA2F8) — tracked separately;
> GPGX is the oracle there, not Emu.

---

### 3.5 Sega 32X — 88% ⬆

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| SH-2 master CPU | `sh2.c` 7868; full core w/ bus cb, IRQ accept | `sh2.cpp` 1117; ported from Emu, instruction-atomic | FULL | Mnemos far more compact (Emu bundles disasm + per-op trace) |
| SH-2 slave CPU | second `sh2_t`, own bus cbs | second `sh2` instance, own bus; **worker thread** | EXCEEDS | Mnemos runs slave on a real OS thread (atomic handshake) |
| SH-2 ISA completeness | 60 mnemonics, all SH7604 groups | **identical 60 mnemonics** | FULL | Zero ISA gap incl. MAC.L/MAC.W, DIV0S/DIV1 |
| — Illegal-instr exception | vec4/vec6 general + slot-illegal | same (branch-delay guarded) | FULL | Parity |
| **— Address-error exception** | **Yes** — vec9, `sh2_latch_cpu_address_error`, misaligned-access fault | **Deferred** (explicit comment) | **MISSING** | Emu faults on odd-address word/long; Mnemos does not plumb it |
| **— Instruction cycle timing** | per-opcode cycle costs + bus-lock waits | 1-cycle base, timing deferred (ADR-0011) | **PARTIAL** | Mnemos not cycle-true |
| FRT (free-running timer) | FRC/OCRA/OCRB/OVF/CCLR + ICI/OCI/OVI IRQ delivery via INTC | FRT w/ prescale, OCRA/B, OVF, CCLR; FRT IRQ resolved by `pending_onchip_irq` | FULL | Both deliver FRT interrupts |
| **INTC (interrupt controller)** | IPRA+IPRB full decode; sources FRT, DIVU, DMAC, WDT, SCI, IRL | IPRB (FRT/SCI bits); `pending_onchip_irq` resolves **FRT only** | **PARTIAL** | Emu arbitrates all on-chip sources; Mnemos wires FRT only |
| **WDT (watchdog)** | interval + watchdog, OVF/WOVF, keyed writes, **reset + ITI** | interval + watchdog counter, OVF/WOVF, keyed writes; **reset + ITI deferred** | **PARTIAL** | Mnemos counts+flags but doesn't fire reset/interval IRQ |
| **DMAC** | 2-ch SAR/DAR/TCR/CHCR + DMAOR; auto + module req; **transfer-end IRQ + bus-wait** | 2-ch full; auto + module DREQ, units {1,2,4,16}, TE set; **end-IRQ + bus-wait deferred** | **PARTIAL** | Transfers complete; Mnemos omits TE interrupt + contention timing |
| **DIVU (hardware divider)** | 32/32 + 64/32, OVF, OVFIE IRQ, **busy-cycle model** | 32/32 + 64/32, OVF w/ partial-iteration + aborted-iteration modelling; **instant (no busy cycles), OVF IRQ storage-only** | **PARTIAL** | Mnemos div math arguably more rigorous, but completes instantly + no IRQ |
| **Serial (SCI)** | **Yes** — SMR/BRR/SCR/TDR/SSR/RDR, flags, TX timing, ERI/RXI/TXI/TEI | **Absent** — $FE00-$FE0F falls through to raw storage | **MISSING** | Emu models the serial link; Mnemos has no SCI behaviour |
| 32X VDP — framebuffer | 256 KB FB via `fb_control`; FS double-buffer latch on VBL | same; FS latch on VBL, access/display bank split | FULL | Parity |
| 32X VDP — packed mode | 8bpp, `y*256+x` direct row | 8bpp via line-table indirection (all modes) | EXCEEDS | Mnemos routes packed rows through line table (more HW-correct) |
| 32X VDP — direct mode | 15bpp, 320-wide, single-buffer | 15bpp, 320-wide, bank-relative w/ spill | FULL | Parity |
| 32X VDP — run-length mode | RLE w/ line table | same | FULL | Parity |
| 32X VDP — palette/CRAM | 256×16-bit, priority bit 15 | 256×16-bit, priority bit 15 | FULL | Parity |
| 32X VDP — autofill | length+1, low-byte addr wrap, FEN | same + FEN busy-read latch for poll idioms | EXCEEDS | Mnemos latches busy reads so both waiter idioms resolve |
| PWM audio | 3-deep L/R FIFOs, duty→PCM, TM IRQ, last-sample hold | 3-deep FIFOs, duty→PCM, TM IRQ, **DC-blocker + real-time capture queue** | EXCEEDS | Mnemos adds DC-block + interleaved sink |
| Comm registers / 68K↔SH2 bridge | 8×16-bit COMM, M_OK/S_OK pre-seed | 8×16-bit COMM, `fence_sh2` on writes | FULL | Both handle boot interlock |
| Adapter control ($A15xxx) | ADEN(bit1)/CPU-ID(bit0) per Emu layout | ADEN(bit0)/nRES(bit1) — **corrected bit order** (cold-boot fix), INTM/INTS, V-blank mirror | FULL | Mnemos fixed an ADEN/nRES bit swap inherited from Emu |
| Cycle scheduling (SH2:68K) | 3:1 integer, inline | 3:1 integer (`sh2_clock_multiplier=3`), threaded w/ fence/join | EXCEEDS | Same ratio; Mnemos tear-free batching across worker thread |
| VINT / HINT | from Genesis VDP, both SH-2 latches | from VDP vblank/hint callbacks → `raise_vint`/`raise_hint` | FULL | Parity |
| CMD interrupt | INTM/INTS → master/slave | same (`raise_cmd_master/slave`, $401A clear) | FULL | Parity |
| PWM interrupt | TM-divider wrap → unmasked SH-2s | TM-divider wrap → `raise_pwm` (level 6, vec 0x43) | FULL | Parity |
| Cartridge security / MARS | "MARS" id, security-block probe | "MARS" @$A130EC, $880000 security window, ADEN handshake | FULL | Parity |
| **Bus-lock / SH-2 contention timing** | **Yes** — `sh2_bus_lock_wait`, master/slave wait accumulators | **Absent** | **MISSING** | Emu models SH-2↔SH-2 bus arbitration stalls; Mnemos does not |

**Parity justification (88%):** full functional parity on everything that produces
visible/audible output — the complete 60-mnemonic SH-2 ISA, all three VDP bitmap modes
+ palette + autofill + double-buffer, PWM, the comm/adapter/DREQ bridge, cart banking,
MARS security, and all four interrupt sources (VINT/HINT/CMD/PWM) — consistent with VR
Deluxe rendering 3D. The shortfall is timing exactness and secondary interrupt delivery
that Emu models and Mnemos explicitly defers.

**Mnemos EXCEEDS Emu in:** threaded dual-SH-2 execution; PWM DC-block + capture queue;
VDP line-table correctness; autofill FEN poll-handling; DIVU edge-case math; corrected
adapter bit order; per-chip save/load state.

**Gaps (Emu has, Mnemos lacks):** SH-2 address-error exception (vec 9); SCI serial
controller; full INTC source delivery (DMAC-end, DIVU-OVFI, WDT, SCI interrupts set
flags but never fire — **only FRT is delivered**); DMAC transfer-end IRQ + DREQ/bus-wait
metering; WDT reset + ITI; SH-2 bus-lock/contention timing; per-instruction cycle
accuracy (ADR-0011 deferral).

---

### 3.6 Irem M72 — ~100% ⬆⬆⬆

> **Context:** Emu's M72 is explicitly a **scaffold** — its own header says
> "YM2151 + DAC + tile/sprite custom-chip integration follow in future commits" and
> calls VRAM a "scratch" buffer. Emu is therefore **not a useful oracle** for M72;
> hardware/MAME behaviour is. Mnemos is a real-ROM bring-up that renders R-Type.

| Component | Emu (LOC/notes) | Mnemos (LOC/notes) | Verdict | Difference |
|---|---|---|---|---|
| Main CPU (NEC V30) | `chips/v30/v30.c` 1977; wired in m72.c, 20-bit bus, `v30_step` | `chips/cpu/v30` 2314; `attach_bus`, IRQ-ack vectored via PIC | FULL | Both real V30; Mnemos drives V30 IRQ through PIC + ack vector; Emu has no V30 IRQ path |
| Sound CPU (Z80) | `chips/z80`; runs at c/2, ROM `$0000-7FFF` from a `sound_rom` blob | `chips/cpu/z80`; runs from **shared RAM** (`$0000`), V30 uploads program then releases `/RESET` via control bit 4 | FULL (Mnemos more accurate) | Real M72 has no separate sound ROM — Mnemos models the authentic RAM-upload + reset-release boot handshake |
| **M72 video — tilemaps** | **MISSING** — only a `video_ram[16K]` scratch buffer, zero rendering | `irem_m72_video` 326+201: two 64×64 4bpp playfields, `render_layer`, scroll ports `0x80-87` | **MISSING (Emu) / FULL (Mnemos)** | Emu has no tile renderer; Mnemos renders both playfields with priority groups |
| **M72 video — sprites + sprite-DMA buffer** | **MISSING** | `render_sprites`, multi-cell 16×16, `latch_sprites()` fired by port `0x04` | **MISSING / FULL** | Mnemos models the sprite-DMA double-buffer (renders from latched copy) |
| Palette | RAM buffer only, never decoded | 5-bit RGB guns at +0/+0x400/+0x800, `lookup_rgb`; sprite vs tile palette split | PARTIAL (Emu) / FULL | Emu stores palette bytes but never converts to color |
| YM2151 FM | `chips/ym2151/ym2151.c` 901; ports `$01/$02`, timer-IRQ→Z80 | `chips/audio/ym2151` 631; Z80 ports 0/1, status read, IRQ folded into IM0 vector | FULL | Both real OPM cores wired |
| **DAC / sample PCM** | **STUB** — port `$82` latches `dac_output` byte that feeds **no mixer** (only read by a unit test) | `dac8` 41 real output stage + auto-incrementing sample-ROM reader (Z80 ports `$80/81/84`) | **STUB / FULL** | Emu's DAC is a dead latch; Mnemos produces signed PCM + streams sample ROM |
| ADPCM (MSM6295) | core exists (`chips/msm6295` 197) but **wired only to CPS1, NOT M72** | not present (M72 uses DAC/MCU playback — correct for this board) | N/A | Neither wires MSM6295 to M72 (correct) |
| **8259 PIC scanline IRQs** | **MISSING** — no interrupt controller; V30 never interrupted | `pic_8259` 221 (µPD71059); IR0=vblank, IR2=raster, ports `$40/$42`, ack→V30 vector | **MISSING / FULL** | Emu has no scanline/vblank IRQ delivery to the main CPU |
| Raster compare | **MISSING** | ports `$06/$07`, `set_raster_compare` | MISSING / FULL | Mnemos-only |
| Shared sound RAM (V30↔Z80) | partial: 8-bit `sound_latch` + pending flag only | 64KB shared RAM at `$E0000` (V30 view) = Z80 program space + latch + ack | PARTIAL / FULL | Emu shares only a command byte; Mnemos shares the full 64K program RAM |
| **Protection / encryption (MCU)** | **MISSING** — header says encryption is "future"; no opcode table, no MCU | `mcs51` (8051) 1066 MCU, INT1-pulsed latch pair, MOVX sample bus; **dormant unless set has "mcu" region** | **MISSING / PARTIAL (scaffolded)** | Mnemos wires a real 8051 protection MCU (gated on ROM presence); R-Type needs none |
| ROM / banking | flat: main ROM `$A0000`, no Z80 bank, no V30 paging | 1 MiB program, even/odd interleave + boot-chunk reload; no live banking yet | PARTIAL (both) | Neither models Z80 `$8000` banking |
| Controls / I/O | **MISSING** — no input ports, no DIPs | P1/P2/system + DIP-lo/hi ports `$00-05`, per-game DIP defaults | MISSING / FULL | Emu reads no controls or dipswitches |
| Board variants / game count | **1** (R-Type implied; scaffold + 1 unit test) | **1** (`rtype.toml` only; `board_params_for` has just `"rtype"`) | PARITY (both = 1 game) | Both ship one game; only Mnemos's renders. Mnemos has the per-game framework to add more |

**Parity justification:** Emu's M72 boots V30+Z80, runs YM2151, and shuttles one
sound-latch byte; it renders **nothing**, delivers **no interrupts** to the main CPU,
reads **no inputs/DIPs**, has **no protection**, and its DAC is a dead latch. Mnemos
actually renders R-Type's title + attract + gameplay with a full tilemap/sprite/palette
renderer, PIC-driven scanline interrupts, the authentic shared-RAM sound boot, sample
playback, inputs/DIPs, and a dormant 8051 protection MCU. Mnemos ≈ 350-400% of Emu.

**Mnemos EXCEEDS Emu in:** complete video; PIC interrupts; sound-boot accuracy;
functional DAC/PCM; protection MCU; inputs/DIPs; ~417 LOC board tests + player adapter
(vs Emu's single ~145-LOC unit test).

**Gaps (Emu has, Mnemos lacks):** none of substance. Shared by both (not an Emu
advantage): no Z80 `$8000` ROM banking, and both expose only one game (R-Type).

---

## 4. Emu-only systems — porting backlog

| Emu System | Emu depth (files/LOC) | Chips needed | Already in Mnemos | Chips to port (new) | Effort |
|---|---|---|---|---|---|
| **nintendo/nes** | nes.c 274; ~399. Real (full CPU/PPU bus map, NROM mapper-0, controllers, sprite-DMA, scanline). Only mapper-0 | m6510_v2 (2A03), ppu2c02, ricoh_2a03_apu | **none** (Mnemos `m6510` is the C64 6510, not the 2A03 variant) | ppu2c02, ricoh_2a03_apu, 2A03 CPU glue, real mappers (MMC1/3) | MEDIUM |
| **nintendo/snes** | snes.c 246; ~347. Partial/scaffold (LoROM + APU mailbox + NMI gate; CPU "doesn't implement interrupt servicing in the scaffold") | wdc_65c816, spc700, s_ppu, s_dsp | **none** | all 4 cores + finish CPU IRQ + HiROM/DMA/HDMA | VERY HIGH |
| **sega/saturn** | ~70 files / **~22,763 LOC** (largest) but CD-block + MPEG heavy (`saturn_cdb_mpeg_command.c` 4736) + trace tooling. Cores: vdp1 1348, vdp2 2752, scsp 596, scu_dsp 1089, sh2 8895 | 2× sh2, vdp1, vdp2, scsp, scu_dsp, m68k (sound) | **sh2** (from 32X), **m68000** | vdp1, vdp2, scsp, scu_dsp + SCU/SMPC/CD-block bus/scheduler | VERY HIGH |
| **commodore/amiga** | amiga.c 566 + copper.c 276 + floppy.c 162; ~1,234. Real-ish shell, chip cores thin (predates full OCS/ECS) | m68k, agnus, denise, paula, cia8520 | **m68000** | agnus 297, denise 212, paula 565, cia8520 81 (shallow → real work to finish) | HIGH |
| **sinclair/spectrum** | spectrum.c 901; ~1,022. Real (48K contention/floating-bus timing, Timex TC2048/TS2068/TC2068, AY, palettes; ULA inline) | z80, (ULA inline), AY-3-8910 | **z80** | none as separate cores; AY-3-8910 PSG (Mnemos has sn76489, not AY) | LOW |
| **snk/neogeo** | neogeo.c 253; ~378. Explicit scaffold (LSPC regs return 0; YM2610 only latched) | m68k, z80, ym2610 (ssg+adpcm_a+adpcm_b) | **m68000, z80** | ym2610 (shell over ssg 198 / adpcm_a 178 / adpcm_b 186), **LSPC video (does not exist anywhere)** | HIGH |
| **capcom/cps1** | cps1.c 4216; ~4,431. NOT a scaffold — full tile/sprite/palette video + inline QSound + inline OKI MSM6295 | m68k, z80, ym2151, msm6295 (CPS-A/B bespoke inline) | **m68000, z80, ym2151** | msm6295 (Emu 225-line core, here inlined) + inline CPS-A/B GFX + QSound | MEDIUM |
| **capcom/cps2** | cps2.c 3667 + cps2_crypto.cpp 696; ~3,948. NOT a scaffold — keyed 68000 opcode decryption, ZIP loading, video, inline QSound | m68k, z80, QSound (inline) | **m68000, z80, cps2_video, qsound PCM/ADPCM/echo HLE, eeprom_93c46, CPS-2 crypto, player adapter, CPS2 corpus smoke runner, 37-set frame/audio/QSound-register/EEPROM baseline** | Corpus evidence now distinguishes rendered audio from QSound command activity: 28/37 selected rows have programmed-silent QSound at the default visibility gate, the runner supports Emu-style gameplay inputs plus a separate longer audio window, and it records ADPCM trigger/configuration categories plus thresholded first-significant-audio timing. `1944.zip`, `1944_mn.zip`, `armwar.zip`, `avsp.zip`, `choko.zip`, `ddtod.zip`, `dimahoo.zip`, `dstlk.zip`, and `gigawing.zip` were checked one-at-a-time against Emu at the comparable repeated auto-start probe and reach nonzero rendered audio plus QSound volume writes in Mnemos. Emu comparison resolved `sfa3.zip` through zero-filled partial expanded QSound Z80 ROM regions and resolved `cybots.zip` through the 32 KiB alternate object-RAM mirror plus the repeated input cadence; `xmcota.zip` also becomes audible under repeated gameplay input. `ecofghtr.zip` reaches rendered audio too, but its raw first-nonzero timing is dominated by Mnemos's echo path while the thresholded signal lands in the same gameplay window, so it remains an echo-HLE fidelity follow-up rather than a silent-row blocker. Remaining default-silent rows need per-title late-audio/input or sound-driver triage before being treated as QSound core failures. DSP16-level QSound and/or command-timing fidelity remains a real audio follow-up, while game-specific EEPROM seed defaults remain conditional on real-set self-init failures | LOW follow-up |
| **taito/f2 + local Taito corpus** | taito_f2.c 197; ~282. Explicit scaffold (bare RAM + comm latch, no video, no sound) | m68k, z80, ym2610 (+TC0100SCN/TC0200OBJ); local non-F2 corpus also needs R3000A/ZN-2, PowerPC Type Zero, x86 Type X, flash/HDD media presentation | **m68000, z80, ym2610, TC0100SCN/TC0200OBJ first pass including board-selectable program-region 1bpp text glyphs for quiz maps, TC0280GRD + TC0430GRW ROZ first pass, TC0480SCP first pass with rowscroll/layer-zoom/BG2-BG3 row-zoom sampling and BG/text offset synthetic golden coverage, player frame-exact save/load, Gun Frontier World/Japan + Liquid Kids parent/clone + Quiz HQ + Quiz Torimon + Quiz Chikyu + Quiz Quest + Dondoko Don + Pulirula + Metal Black + Football Champ + Dead Connection + Dino Rex + Thunder Fox + Growl + Ninja Kids + Solitary Fighter profiles, real F2 map, clone-parent zip fallback loading, Taito F2 corpus smoke runner with wrapper-zip staging and optional screenshot SHA-256 pins, local recursive `D:\emu\arcade\Taito` F2 proof (`dinorex`, `gunfront`, `gunfrontj`, `growl`) 4/4, broad Taito inventory showing 4/25 local packages runnable today, `sony.r3000a` CPU bootstrap for the G-NET/ZN-2 workstream, G-NET ZIP/CHD flash-card package decode and boot-ROM/main-RAM/scratchpad/flash-bank/PCMCIA-aperture/RF5C296-proxy shell with first BIOS-facing memory/cache-control, GPU register/VRAM latch shell, COP2/GTE register-transfer and command-latch shell, limited GPU command and OTC DMA execution, and IRQ/root-timer latches with first-pass target/overflow IRQ delivery, optional BIOS+CHD assembly smoke, and a board-smoke player adapter, banked TC0200OBJ records, TC0190FMC-style sprite-bank register-window routing, board-configurable palette formats, board-configurable TC0200OBJ sprite-extension RAM windows, board-configurable active-area marker source including Footchmp-style Y-bit routing, board-specific TC0200OBJ hide-pixel offsets, board-configurable TC0200OBJ immediate/full/partial-delayed buffering policies including qzchikyu partial-word overlay, TC0200OBJ zoom/continuation chaining, marker disable/flip-screen handling, X bias + master/extra/absolute sprite scroll markers, frame-persistent active-area/disable/flip/master-scroll state, TC0360PRI-style tile/text/ROZ/sprite priority register routing and sprite/tile blend modes with ROZ-selector and all sprite-priority-group synthetic coverage, TC0200OBJ sprite-extension code policies, board-selectable TC0480SCP priority decoding** | G-NET/ZN-2 BIOS sourcing, locked-card PCMCIA command/security protocol beyond the minimal RF5C296-style proxy, GPU renderer/SPU/real GTE command math, full DMA timing/exact timer sync/JVS I/O beyond GPU/OTC, Type Zero PowerPC/3D/ATA, Type X x86/Windows/JVS/HDD/container support, non-F2 playable board adapters, broader F2 corpus proof beyond the local F2 set, real-board TC0360PRI priority proof, remaining TC0190FMC board-family profiles, and populated visual/audio golden compatibility evidence | HIGH |

**Easiest → hardest to port next:**
1. **Spectrum (LOW)** — Z80 done; ULA inline (no separate core); only gap is AY-3-8910. Cleanest win.
2. **CPS1 (MEDIUM)** — 3 of 4 chips present; only MSM6295 + already-inline CPS-A/B GFX + QSound. High payoff (fighting library).
3. **NES (MEDIUM)** — real, working; needs ppu2c02 + 2A03 APU + proper 2A03 CPU variant + real mappers.
4. **CPS2 follow-up (LOW)** — keep the 37-set frame/audio/QSound-register/EEPROM corpus gate refreshed only for intentional video/audio/NVRAM changes, continue triaging remaining silent QSound rows with `-AudioFrames` plus audio-only `-GameplayInput`, use the ADPCM trigger/configuration columns to separate trigger-only driver state from real mixer failures, fill game-specific EEPROM defaults only if a real set fails to self-initialize, and pursue DSP16-level QSound if cycle-grade audio fidelity is required.
5. **Amiga (HIGH)** — only m68000 reused; port Agnus/Denise/Paula/CIA8520 (shallow OCS/ECS shells need completion).
6. **NeoGeo (HIGH)** — scaffold; needs YM2610 + from-scratch LSPC.
7. **Taito F2 / Taito corpus (HIGH)** — first-pass F2 implementation exists, including Gun Frontier World/Japan + Liquid Kids parent/clone + Quiz HQ + Quiz Torimon + Quiz Chikyu + Quiz Quest + Dondoko Don + Pulirula + Metal Black + Football Champ + Dead Connection + Dino Rex + Thunder Fox + Growl + Ninja Kids + Solitary Fighter real-set profiles, clone-parent zip fallback loading, real F2 map coverage, player frame-exact save/load, a data-gated Taito F2 corpus smoke runner with wrapper-zip staging and optional screenshot SHA-256 pins, local recursive `D:\emu\arcade\Taito` F2 proof (`dinorex`, `gunfront`, `gunfrontj`, `growl`) 4/4, broad Taito inventory showing 4/25 local packages runnable today, `sony.r3000a` CPU bootstrap for the G-NET/ZN-2 path, G-NET ZIP/CHD flash-card package decode, a boot-ROM/main-RAM/scratchpad/flash-bank/PCMCIA-aperture/RF5C296-proxy shell, first BIOS-facing memory/cache-control, a GPU register/VRAM latch shell, COP2/GTE register-transfer and command-latch shell, limited GPU command and OTC DMA execution, IRQ/root-timer latches with first-pass target/overflow IRQ delivery, and a board-smoke player adapter with optional BIOS+CHD assembly smoke; next work is BIOS sourcing, locked-card PCMCIA command/security protocol, GPU renderer/SPU/real GTE command math integration, full DMA timing/exact timer sync/JVS I/O beyond GPU/OTC, Type Zero, Type X, non-F2 playable board adapters, broader F2 corpus proof beyond the local F2 set, real-board TC0360PRI priority proof, remaining TC0190FMC board-family profiles, and populated real-ROM visual/audio golden evidence. Little Emu code to lift (197 LOC).
8. **SNES (VERY HIGH)** — four new complex cores; CPU IRQ still a scaffold; DMA/HDMA/HiROM missing.
9. **Saturn (VERY HIGH)** — biggest, but most LOC is CD/MPEG/trace; still needs VDP1+VDP2+SCSP+SCU-DSP + dual-SH2 + SCU/SMPC/CD-block. Multi-month.

**Scaffolds in Emu (low actual loss — near-greenfield either way):** Taito F2 (RAM +
2-byte latch only), NeoGeo (LSPC returns 0, YM2610 latched), SNES (CPU IRQ unwritten).
**Genuine ported value:** NES, Spectrum, Amiga, CPS1, CPS2, Saturn — CPS1/CPS2 and
Spectrum especially are substantial and mature.

---

## 5. ★ Critical Risk Register — "detrimental to development"

Consolidated failure modes: stubs that silently no-op, deferred features, self-flagged
weaknesses, and architectural divergences. **These are the things that, if forgotten,
cause re-debugging of solved ground or wrong-but-plausible behaviour.** Severity =
likely impact on game compatibility / development if relied upon.

| # | System | Risk | Severity | Detail / failure mode |
|---|---|---|---|---|
| R1 | 32X | **INTC delivers ONLY FRT** | **CRITICAL** | DMAC-end, DIVU-OVFI, WDT, SCI interrupts set their flags but **never fire**. Any 32X title relying on DMAC transfer-end IRQ or SCI will hang or misbehave. Blocks 32X game-coverage expansion |
| R2 | 32X | **Not cycle-true; DIVU completes instantly (no busy cycles); no bus-lock/contention timing** | **CRITICAL** | Timing-sensitive 32X titles may never converge. Star Wars / Space Harrier / After Burner already black (per project notes). On the hard-problems board (SH-2 cycle-true). ADR-0011 deferral |
| R3 | Sega CD | **CHD disc images unsupported** | **HIGH** | `.chd open()` returns `std::nullopt` — any CHD file **fails to load entirely**. Emu has the full v5 codec stack to port. Most common modern disc-image format |
| R4 | SMS | **YM2413 FM Sound Unit wired** | **RESOLVED** | SMS FM now routes ports `$F0/$F1/$F2` through the existing YM2413 chip, exposes player `--fm`, and mixes captured FM audio through the SMS adapter. Covered by focused SMS/runtime/player tests |
| R5 | C64 | **1541 GCR read path "still being proven"** | **HIGH** | Self-flagged in code. Disk loading may be unreliable; could block disk-based software. Emu's 1541 is 3539 LOC vs Mnemos 1464 (more format/edge coverage) |
| R6 | 32X | **SH-2 address-error exception deferred** | **HIGH** | Misaligned word/long access does not trap (Emu faults at vec 9). Software that deliberately faults, or buggy paths, behave wrong |
| R7 | Genesis | **M68000 BERR map / prefetch-exact group-0 parity incomplete** | **HIGH** | Functional vector-2/3 group-0 frames exist, but concrete Genesis/Sega CD BERR address maps and prefetch-exact corpus parity remain open. Affects software that probes/relies on bus errors |
| R8 | Genesis | **No whole-system save state** | **HIGH** | Per-chip serialization exists but there is **no assembled-Genesis save/load path**. Blocks save-state/rewind at the system level (Emu has `genesis_save_state`). Infra gap on hard-problems board |
| R9 | M72 | **Protection MCU (8051) dormant + untested; only 1 game** | **HIGH** | Only R-Type (no protection) works. Other M72 boards need the 8051 protection MCU, which is wired but gated/unexercised. No Z80 `$8000` banking yet |
| R10 | 32X | **SCI serial controller absent** | MEDIUM | `$FE00-$FE0F` is raw storage; no SMR/BRR/SCR/SSR behaviour, no link interrupts |
| R11 | 32X | **DMAC transfer-end IRQ + DREQ/bus-wait metering; WDT reset + ITI deferred** | MEDIUM | Transfers complete and set TE, but the end-interrupt never fires; watchdog counts but won't reset/interrupt |
| R12 | Genesis | **S&K lock-on, SVP, J-Cart absent** | MEDIUM | Specific titles unsupported: Sonic & Knuckles lock-on, Virtua Racing (SVP — note Emu's DSP is itself a stub pending SSP1601), 4-player J-Cart games |
| R13 | Genesis | **No per-revision audio mix** | MEDIUM | Emu models model-1/2 mix gains + low-pass; Mnemos handles region only. Audio fidelity differs from specific console revisions |
| R14 | SMS | **Pause button not wired (no pause→NMI)** | MEDIUM | Z80 supports NMI but the SMS manifest exposes no pause entry point — the pause button does nothing |
| R15 | SMS | **Shallow cart-header validation; cart-RAM mapper detail unverified** | MEDIUM | Only region nibble parsed (no checksum/product-code/size); Sega-mapper `$8000-$BFFF` cart-RAM bank-select not separately verified |
| R16 | Sega CD | **No ISO 9660 walker / Saturn IP.BIN** | LOW-MED | Mostly Saturn-relevant; some CD tooling/inspection paths unavailable |
| R17 | C64 / all | **Dev-tooling axis is a broad gap** (the C64 suite ~4,400 LOC is just the visible part) | LOW (scope) | Out of *hardware* scope but real: no GUI debugger, no disassemblers, scripting is README-stubs, and save-state/rewind player coverage is still uneven outside the new CPS2 path. Full inventory + T#/N# checklist in [`tooling-gap-inventory.md`](tooling-gap-inventory.md) |
| R18 | Sega CD / Genesis | **CD-DA & sub-BIOS placement differ from Emu** | LOW (architectural) | CD-DA lives in segacd subsystem (not Genesis core); sub-BIOS runs as main cart (not a dedicated sub image). Functionally equivalent — note before "comparing to Emu" |
| R19 | Multiple | **Emu is NOT a trustworthy oracle for its scaffolds/stubs** | META | Emu's M72, NeoGeo, Taito F2, SNES are scaffolds; its SVP DSP, Sega CD 1M mode + stamp ASIC are stubs. "Match Emu" is wrong for these — use GPGX/MAME/hardware. Do not regress Mnemos's superior implementations toward Emu |

> **R19 is the most important meta-risk.** In several places Mnemos is *correct* and Emu
> is the stub (Sega CD stamp/1M, M72 rendering). A naive "reach Emu parity" pass could
> *regress* Mnemos. Parity is the floor for mature Emu systems (C64, Genesis, CPS1/2,
> Spectrum), not the ceiling.

---

## 6. Prioritized recommendations

**Close hardware gaps (by impact):**
1. **32X interrupt delivery + cycle timing (R1, R2)** — the single biggest correctness
   risk; already the marquee item on the hard-problems board. Required to push 32X
   game coverage past the current rendering wins.
2. **Sega CD CHD (R3)** — full codec stack exists in Emu to port; unblocks the common
   modern disc format. Self-contained.
3. **1541 GCR read path (R5)** — prove/harden it; it gates disk-based C64 software.
4. **Genesis whole-system save state (R8)** — assemble the per-chip serialization into
   a machine-level path; unlocks save-state/rewind.
5. **Genesis exotics (R12)** — S&K lock-on is cheap + high-recognition; SVP needs the
   SSP1601 core (Emu also only stubs it).

**Grow breadth (by ROI):** Spectrum (LOW) → CPS1 (MEDIUM) → NES (MEDIUM). These three
take Mnemos from 6 → 9 systems for modest effort.

**Scope decision for the user:** C64 developer tooling (R17) is the largest single line
item in the whole comparison and is *not* hardware. Match it only if Mnemos's C64 must
be a dev platform, not just a game runner.

---

## 7. Appendix

### 7.1 Reproduction

This audit is reproducible by re-reading the cited files. Key roots:
- Emu systems: `C:\Users\mkrol\source\repos\Emu\Emu\systems\{capcom,commodore,irem,nintendo,sega,sinclair,snk,taito}`
- Emu chips: `C:\Users\mkrol\source\repos\Emu\Emu\chips\*`
- Mnemos chips: `C:\dev\emu\Mnemos\src\chips\{cpu,video,audio,mapper,peripheral,storage,bus_controller}`
- Mnemos manifests: `C:\dev\emu\Mnemos\src\manifests\{c64,genesis,irem_m72,sega32x,segacd,sms}`
- Mnemos disc: `C:\dev\emu\Mnemos\src\disc`

### 7.2 System↔chip mapping (Mnemos)

| System | CPU | Video | Audio | Other |
|---|---|---|---|---|
| Genesis | m68000, z80 | genesis_vdp | ym2612, sn76489 | cart/banking/eeprom/sram, region |
| SMS/GG | z80 | sms_vdp | sn76489 | 8 mappers, eeprom_93c46, gg_io |
| C64 | m6510 | vic_ii_6569 | sid_6581 | cia_6526, via_6522, c64_pla, c64_cartridge, c1541, datasette, reu, rs232, modem |
| Sega CD | m68000 (sub) | (stamp ASIC) | rf5c68 | disc/{disc_image,circ_ecc}, cdc, cdd, gate array |
| Sega 32X | sh2 ×2 (+sh2_peripherals) | sega32x_vdp | (PWM) | comm bridge, adapter ctrl |
| Irem M72 | v30, z80, mcs51 | irem_m72_video | ym2151, dac8 | pic_8259 |

### 7.3 Notes

- Parity %s are engineering estimates, not measured pass-rates. Genesis's 87%
  byte-perfect corpus figure (2784 titles) is the one empirical anchor.
- Point-in-time snapshot (2026-06-11). Re-verify file:line claims before acting; the
  codebase moves.
- **Dev-environment note (not a Mnemos defect):** the governance PostToolUse hook
  (`tools/governance/claude_hooks.py`) resolves its script path relative to the edited
  file's directory rather than the repo root, so it errors on writes outside
  `C:\dev\emu\Mnemos`. It does not block the operation. Fix is a one-line `settings.json`
  change to an absolute path (`$CLAUDE_PROJECT_DIR/tools/governance/claude_hooks.py`).
