# Third-Party Acknowledgments

Mnemos vendors no third-party emulator source. The reference materials below
were consulted as design guidance and as the inputs to data-gated conformance
tests; they are acknowledged here rather than scattered across individual
source files.

For *runtime* dependencies fetched at build time (test framework, etc.) see
[`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md).

---

## Conformance test corpora

The chip cores ship with data-gated conformance harnesses that, when pointed at
an external corpus via an environment variable, run the corpus and assert the
expected end state. None of these corpora are committed.

| Corpus | Used by | Env var |
|--------|---------|---------|
| Public per-instruction 6502 test corpus (one JSON file per opcode, ~10 000 cases each) | `m6510_conformance_test` | `MNEMOS_M6510_TESTS_DIR` |
| Public per-instruction 68000 test corpus (one JSON file per mnemonic) | `m68000_conformance_test` | `MNEMOS_M68000_TESTS_DIR` |
| Public Z80 instruction exerciser CP/M `.com` images (full-flag and documented-flag variants) | `z80_conformance_test` | `MNEMOS_Z80_TEST_ROM` |
| Public per-instruction Z80 test corpus (one JSON file per opcode, per-cycle bus trace) | `z80_singlestep_test` | `MNEMOS_Z80_TESTS_DIR` |
| Public per-instruction 8088/V20 test corpus (one JSON or gzipped JSON file per opcode, initial/final register + RAM images; the corpus metadata.json drives per-opcode flag masks) | `v30_singlestep_test` | `MNEMOS_V30_TESTS_DIR` |
| Public per-instruction **SH4** single-step corpus (one JSON file per encoding, four-instruction frame, initial/final register + memory) — used as a _functional_ cross-check for the shared SH-2/SH-4 integer ISA | `mnemos_chips_cpu_sh2_conformance_test` | `MNEMOS_SH2_TESTS_DIR` |
| **ZEXALL-SMS** — Z80 instruction exerciser as a Sega Master System ROM (renders to the SMS VDP / SDSC console) | _SMS-native; no committed harness yet_ | _n/a_ |
| Capcom CPS2 authentic ROM/key zip corpus (copyrighted game data; never committed) | `scripts/cps2/run-corpus-smoke.ps1` | `MNEMOS_CPS2_ROM`, `MNEMOS_CPS2_SET_DIR`, `MNEMOS_CPS2_EXTRA_ROMS` |

These corpora are the de-facto standard for verifying 8-/16-bit CPU cores; we
acknowledge their authors' work in producing and maintaining them.

**ZEXALL-SMS** (<https://www.smspower.org/Homebrew/ZEXALL-SMS>) is a Master
System port of Frank Cringle's ZEXALL (ported to CP/M and the Spectrum by
J.G. Harston, to the SMS by Brett K., and maintained by Maxim / Eric R. Quinn).
It is licensed **GPLv2** and is **not** committed. Unlike the CP/M `.com`
exercisers above, it reports results to the SMS VDP / SDSC debug console rather
than via BDOS, so it exercises the whole SMS (Z80 + VDP) rather than the bare
Z80 core — running it therefore needs the system harness, not the `.com`-loading
`z80_conformance_test`.

**SH4 single-step corpus** — there is no public SH-2 single-step corpus, so the
SH-2 core is cross-checked against the public **SH4** corpus, which was generated
by a software SH4 interpreter (Reicast) rather than from hardware. It is used
purely as a *functional* (instruction-semantics) reference for the integer
instructions the SH-2 and SH-4 share: the harness compares the final SH-2
register file (R0–R15, PC, PR, GBR, VBR, MACH, MACL, and SR masked to the
SH-2-defined bits) and memory writes, and **ignores** the SH-4 superscalar
cycle counts, FP/group-`F` encodings, SH-4-only control ops, and the SH-4-only
register state (FP banks, SSR/SPC/SGR/DBR, FPSCR). `MAC.L`/`MAC.W` are absent
from the corpus by construction. Cases where the SH-2 raises an address error
(misaligned word/long, on-chip, or cache-control accesses) that the SH4 source
does not model are excluded rather than counted, since our core is the more
correct of the two there. This is **not** a cycle-timing reference; SH-2
cycle-exact timing remains validated separately. The corpus is transcoded to
plain JSON by the corpus's own `transcode_json.py`; the `.json.bin` form is not
read. It is **not** committed.

## Hardware-reverse-engineering references

The following community reverse-engineering writeups, datasheets, and
open-source emulator notes informed the modelling of specific chips. No code
was copied; behavioural tables (envelope rates, H-counter sequences, PLA truth
tables) were derived from published hardware studies and cross-checked against
multiple independent open-source implementations.

- **YM2612 / OPN2 (Sega Genesis FM)** — envelope-rate increment pattern,
  operator slot order, and channel-mixing semantics derived from published
  reverse-engineering of the die and cross-checked against multiple
  open-source OPN2 implementations.
- **Sega Master System VDP / Genesis VDP** — H-counter and timing tables
  derived from community hardware tests and published timing notes;
  cross-checked against multiple open-source Sega emulators.
- **MSX2 / Yamaha V9938** — MSX2 slot, PPI, PSG, and VDP I/O-port layout
  checked against the MSX2 Technical Handbook / MSX Datapack references; the
  V9938 register, palette, status, and display-mode model is derived from the
  Yamaha V9938 MSX-VIDEO Technical Data Book. No emulator source was copied.
- **Konami 051649 / SCC** — SCC wavetable-channel count, 32-byte waveform RAM,
  12-bit frequency, 4-bit volume, channel-enable register, and original-SCC
  shared waveform for channels 4/5 checked against public MSX hardware notes
  (MSX Wiki and MSX Blue). No emulator source was copied.
- **MSX-MUSIC / Yamaha YM2413** — optional MSX2 FM expansion port decode
  (#7C address, #7D data) checked against public MSX hardware references. The
  YM2413 synthesis core is Mnemos-native and shared with other in-tree systems.
- **Ricoh RP-5C01 / MSX2 RTC** — MSX2 clock-chip port decode (#B4 index, #B5
  data), four CMOS blocks, mode/test/reset registers, and 1980-based BCD
  calendar behavior checked against public MSX hardware references. The RTC
  model is Mnemos-native and deterministic.
- **Western Digital WD1793 / MSX2 disk interface** — FDC register layout,
  MSX memory-mapped/I/O port decode, DRQ/INTRQ status behaviour, and standard
  512-byte-sector DSK geometries checked against public MSX hardware and disk
  format references. The controller model is Mnemos-native and sector-level.
- **C64 PLA (906114)** — banking truth table from the published PLA equations;
  cross-checked against multiple open-source C64 emulators' memory maps.
- **C64 cartridge `.crt` container** — the de-facto community format
  originally defined by the CCS64 project and adopted by all subsequent C64
  emulators.
- **NEC V20/V30 opcode references** — public NEC V-series user manuals, RBIL
  opcode notes, and assembler opcode-table discussions were used only to
  confirm the FPO2 `66/67 ModR/M` escape form and `0F FF imm8` BRKEM operand
  length. No assembler or emulator code was copied.
- **Taito F2 custom-chip behaviour** — TC0100SCN/TC0200OBJ/TC0280GRD/TC0430GRW/
  TC0480SCP/TC0190FMC board maps, register effects, and priority interactions are
  treated as behavioural facts to cross-check against public hardware notes and
  independent emulator behaviour, not as source material. Mnemos keeps those facts
  behind its own chip contracts, manifests, tests, and save-state model. ROM file
  names, byte counts, load offsets, and CRCs are catalog facts required to load
  user-provided sets; Mnemos records them in its own manifests and does not import
  MAME driver macros, implementation code, comments, or table structure.
- **Irem/Nanao GA20 PCM** — the M92/M107 GA20 register map and playback semantics
  were cross-checked against public MAME GA20 device notes and the local Irem
  board fact sheet: four channels, eight registers per channel, 16-byte-unit
  start/end registers, rate, hyperbolic volume, control bit 1 key-on/off, status
  bit 0 active, and zero-byte sample termination. Mnemos's implementation is a
  native chip model with its own tests and save-state format; no MAME source code
  is vendored or copied.

## Cartridge / mapper protocol notes

- **SMS mapper auto-detect signature** — header signature convention
  documented by the SMS Power community.
- **Codemasters SMS mapper** — bank-register layout documented in community
  hardware notes; cross-checked against multiple open-source Sega
  implementations.
- **SMS Korean mapper CRC database** — the Korean cartridge families carry no
  header signature, so they are auto-detected by ROM CRC-32. The CRC→mapper
  catalogue (`src/manifests/sms/sms_system.cpp`) was compiled from the community
  SMS cartridge database and cross-checked against open-source Sega emulators'
  game lists; only carts whose mapper Mnemos implements are listed.
- **93C46 EEPROM cart CRC database** — the handful of Game Gear cartridges that
  carry a 93C46 serial EEPROM for saves are likewise auto-detected by ROM CRC-32
  (`src/manifests/sms/sms_system.cpp`), compiled from the community Game Gear
  cartridge database and cross-checked against open-source Sega emulators' game
  lists. The 93C46 Microwire protocol is the published Microchip datasheet.
- **Irem M52 Moon Patrol manual and ROM-set metadata** — the checked-in
  `mpatrol` manifest uses the Moon Patrol Instruction Manual game-adjustment
  and diagnostic DIP switch tables for SW1/SW2 DIP names, options, conditions,
  active-high ON/OFF polarity, and factory defaults, and public set metadata
  for ROM dump filenames, offsets, region sizes, CRC-32s, and parent/clone
  relationships. The M52 adapter folds those DIP defaults into the
  board-visible `dsw1`/`dsw2` bytes and exposes the parsed switch count. No
  manual scans, emulator source, or ROM bytes are committed.
- **Irem M14 ROM-set metadata** — the checked-in `ptrmj` manifest uses public
  Irem M14 driver metadata for P.T. Reach Mahjong dump filenames, main CPU and
  graphics region offsets, region sizes, CRC-32s, orientation, and board-family
  classification. Mnemos now uses that metadata for a first-pass M14
  board/player route; it is still not visual/audio parity proof and currently
  uses an 8080-compatible surrogate until an authentic 8085 core lands. No MAME
  driver code or ROM bytes are copied into Mnemos.
- **Irem M27 ROM-set metadata** — the checked-in `panther` manifest uses public
  Irem M27 driver metadata for Panther dump filenames, M6502 program, audio CPU,
  and PROM region offsets, region sizes, CRC-32s, orientation, and board-family
  classification. Mnemos now uses that metadata for a first-pass M27
  board/player route; it is still not visual/audio parity proof. No MAME driver
  code or ROM bytes are copied into Mnemos.
- **Irem M47 ROM-set metadata** — the checked-in `olibochu` and `punchkid`
  manifests use public Irem M47 driver metadata for Oli-Boo-Chu and Punching
  Kid dump filenames, parent/clone relationship, region sizes, offsets, CRC-32s,
  orientation, and sparse board-family classification. Mnemos now uses that
  metadata for a first-pass M47 board/player route; it is still not
  visual/audio parity proof. No MAME driver code or ROM bytes are copied into
  Mnemos.
- **Irem M58 ROM-set metadata** — the checked-in `10yard`, `10yardj`,
  `vs10yard`, and `vs10yardj` manifests use public Irem M58 driver metadata for
  10-Yard Fight dump filenames, parent/clone relationships, region sizes,
  offsets, CRC-32s, orientation, and board-family classification. Mnemos uses
  that metadata for a first-pass M58 board/player route; it is still not
  visual/audio parity proof. No MAME driver code or ROM bytes are copied into
  Mnemos.
- **Irem M57 ROM-set metadata** — the checked-in `newtangl` and `troangel`
  manifests record the local M57/Tropical Angel-family wrapper filenames, region
  sizes, CRC-32s, orientation, and board-family classification. Mnemos routes a
  first-pass M57 player through those raw-media artifact contracts; this is not
  visual/audio parity proof for the authentic M57 board or its M52 sound PCB. No
  MAME driver code or ROM bytes are copied into Mnemos.
- **Irem M62 ROM-set metadata** — declarative manifests under
  `src/manifests/irem_m62/games/` use public set metadata for dump filenames,
  region sizes, CRC-32s, and board-family classification. `ldrun`, `ldrun2`,
  `ldrun3`, `ldrun3j`, `ldrun4`, `lotlot`, and `spelunk2` now record explicit
  Z80 program, MC6803 sound ROM, graphics, PROM, and timing regions; `ldrun3j`
  resolves its shared MC6803, tile, PROM, and timing artifacts through the local
  `ldrun3` parent fallback. `battroad`, `bkungfu`, `horizon`, `kidniki`,
  `kungfum`, `ldruna`, `spartanx`, `yanchamr`, and `youjyudn` stay in raw-media
  staging until their title-specific maps are wired.
  The Horizon local wrapper is stored under the local `M72`
  corpus folder, but public Irem M62 driver metadata classifies it with the M62
  Z80/M6803-era family; Mnemos records it as an M62 set and routes the first-pass
  player through that contract rather than treating folder placement as board
  proof. No MAME driver code or ROM bytes are copied into Mnemos.
- **Irem M63 ROM-set metadata** — the checked-in `wilytowr` manifest uses public
  Irem M63 driver metadata for Wily Tower dump filenames, region sizes, offsets,
  CRC-32s, orientation, and board-family classification. Mnemos routes the
  first-pass Wily Tower player through that local artifact contract, but this is
  not visual/audio parity proof for the authentic M63 board. No MAME driver code
  or ROM bytes are copied into Mnemos.
- **Irem M72 ROM-set metadata** — declarative game manifests under
  `src/manifests/irem_m72/games/` use public MAME M72 driver metadata for dump
  filenames, offsets, reloads, CRC-32s, parent/clone relationships, cabinet
  orientation, and true-M72 DIP switch / coinage tables. The
  no-dump `dbreedm72` / `dkgensanm72` manifests also carry explicit HLE
  declarations from the public missing-i8751 notes and startup RAM inversion
  behavior. Their sample-trigger cursor starts are profile data cross-checked
  against MAME's public M72 driver and bounded by the declared sample ROM at
  runtime. The no-dump HLE entry continuations synthesize Mnemos-native V30 far
  jumps from the same public profile targets; they are declared simulated
  behavior, not authentic dumped i8751 firmware. No wholesale fake protection
  routines, emulator source code, or ROM data are copied into Mnemos.
- **Irem M75 Vigilante manual and ROM-set metadata** — the checked-in `vigilant`
  manifest uses the Vigilante Installation & Service Manual game-options table
  for SW1/SW2 DIP names, options, conditions, and defaults, and public set
  metadata for ROM dump filenames, offsets, region sizes, CRC-32s, and
  parent/clone relationships, including the bootleg `vigilantbl` PROM/PAL
  grouping. The M75 adapter folds those DIP defaults into the board-visible
  `dsw1`/`dsw2` bytes and exposes the parsed switch count. No manual scans,
  emulator source, or ROM bytes are committed.
- **Irem M84 ROM-set metadata** — declarative game manifests under
  `src/manifests/irem_m84/games/` use public MAME Irem driver metadata for dump
  filenames, parent/clone relationships, region sizes, program reload offsets,
  CRC-32s, orientation, and PROM/PLD metadata for the Hammerin' Harry / Daiku no
  Gensan, Gallop / Cosmic Cop, and Lightning Swords / Ken-Go M84 sets. Mnemos
  uses that metadata for CRC-clean local corpus loading and a first-pass M84
  wrapper route; it is still not visual/audio parity proof. No MAME driver code
  or ROM bytes are copied into Mnemos.
- **Irem M85 ROM-set metadata** — the checked-in `poundfor` and `poundforj`
  manifests use public Irem M85 driver metadata for Pound for Pound dump
  filenames, parent/clone relationships, region sizes, offsets, reloads,
  CRC-32s, orientation, and PLD metadata. Mnemos uses that metadata for a
  first-pass M85 board/player route; it is still not visual/audio parity proof.
  No MAME driver code or ROM bytes are copied into Mnemos.
- **Irem M90 ROM-set metadata** — the checked-in `atompunk`, `bbmanw`,
  `bbmanwj`, `bbmanwja`, `gussun`, `hasamu`, `newapunk`, `quizf1`, and
  `riskchal` manifests use public Irem M90 driver metadata for dump filenames,
  parent/clone relationships, region sizes, offsets, reset-vector reloads,
  CRC-32s, orientation, graphics/sample media, and Quiz F-1 banked program
  media. Mnemos uses that metadata for a first-pass M90 board/player route; it
  is still not visual/audio parity proof. No MAME driver code or ROM bytes are
  copied into Mnemos.
- **Irem M92 ROM-set metadata and board constants** — declarative game manifests under
  `src/manifests/irem_m92/games/` use public MAME M92 driver metadata for dump
  filenames, offsets, region sizes, CRC-32s, cabinet/player metadata, and the
  encrypted V35 sound-program classification, including the Lethal Thunder /
  Thunder Blaster, R-Type Leo, and Undercover Cops parent/clone board variants.
  The first-pass M92 board constants and memory windows are cross-checked
  against public driver-level hardware notes; no MAME driver code or ROM bytes
  are copied into Mnemos. Current M92 execution remains diagnostic rather than
  parity-authentic until encrypted V35 sound and GA21/GA22 video behavior are
  implemented from acceptable evidence.
- **Irem M107 ROM-set metadata and board constants** — declarative game manifests under
  `src/manifests/irem_m107/games/` use public MAME M107 driver metadata for dump
  filenames, offsets, boot-vector coverage, region sizes, CRC-32s, cabinet
  metadata, SW1/SW2 DIP switch names/options/defaults, SW3 Rapid Fire /
  Continuous Play / Player Power DIP metadata, and local Air Assault / Fire
  Barrel / Dream Soccer '94 set identities. The first-pass
  M107 main RAM, video/sprite/palette RAM, sound RAM, YM2151, GA20,
  command-latch acknowledge path, reply, YM2151 Timer A IRQ routing, V35
  INTP0/INTP1 sound IRQ vector assignments, first-pass INTP0-over-INTP1
  arbitration, COINS_DSW3 window, and service/operator input-bit assignments
  are cross-checked against public
  driver-level hardware notes; no
  MAME driver code or ROM bytes are copied into Mnemos. Current M107 execution
  remains diagnostic rather than parity-authentic until V33/V35 peripheral
  timing, full V35 interrupt-controller priority/latency, GA21/GA22 video
  behavior, remaining operator I/O behavior, and visual/audio parity are proven
  from acceptable evidence.
- **Capcom CPS1 CPS-B config / gfx-mapper census** — each CPS1 board revision's
  CPS-B custom chip has a per-board scrambled register map (layer-control,
  priority, palette-control, layer-enable, protection ports) and a graphics-code
  mapper PAL. The hardware-keyed census (`src/manifests/capcom_cps1/cps_b_profiles.cpp`)
  was transcribed from the community reverse-engineering of those per-board
  register offsets and gfx bank ranges, via the author's first-party Emu
  reference core (see "Code adapted from sibling first-party projects"). It is
  keyed by the numeric CPS-B profile / PAL identity, never a game name, was
  transcribed mechanically to avoid error, and is cross-checked against an
  independent reimplementation of the mapper algorithm. No code was copied.
- **Capcom CPS2 68000 opcode cipher** — the CPS-2 board encrypts the 68000
  instruction stream with two 4-round Feistel networks keyed by the address and a
  64-bit master key. The algorithm and its s-box constants are the publicly
  documented result of the CPS-2 hardware reverse-engineering, originally
  published in MAME's `cps2crypt.cpp` under the **BSD-3-Clause** license
  (copyright-holders: Paul Leaman, Andreas Naive, Nicola Salmoria, Charles
  MacDonald; the encryption was broken by Andreas Naive). Mnemos's reimplementation
  (`src/manifests/capcom_cps2/cps2_crypto.cpp`) was transcribed from that
  documented algorithm via the author's first-party Emu reference core (see "Code
  adapted from sibling first-party projects"), restructured into Mnemos style, and
  cross-checked against the reference's output as a golden vector in its unit test.
  The BSD-3-Clause attribution above is retained here per that license.
- **Capcom CPS2 video, board glue, and QSound behaviour** — the CPS-2-specific
  object-RAM banking, graphics unshuffle, layer/sprite priority composition,
  input/service/test switch bits, coin counter/lockout output behaviour,
  per-game digital, ticket-dispenser, and spinner/paddle input profile variants,
  EEPROM pins, QSound sound-CPU map, and DL-1425 PCM/ADPCM/echo mixer behaviour
  were re-expressed in Mnemos C++ using the author's first-party Emu CPS2 core as the
  behavioural reference and cross-checked against MAME as an independent
  black-box reference. The QSound DSP is intentionally modelled at the
  behavioural mixer level rather than as a DSP16 instruction core; that HLE
  status is declared in the CPS2 manifests. No third-party emulator source is
  vendored.

## Cross-check emulators

Where a behavioural detail was ambiguous, it was validated against one or more
independent open-source emulators used purely as black-box references — read or
run to confirm a modelling decision against an independent implementation, never
as a source of code. Their authors' work materially helped Mnemos reach its
accuracy targets, and is acknowledged here:

- **Genesis-Plus-GX** — Sega Genesis / Mega Drive behavioural reference.
- **BlastEm** — Sega Genesis cycle-timing reference.
- **Nuked-OPN2** — die-accurate YM2612 FM reference.
- **MAME** — cross-system hardware behaviour and timing reference.

## Code adapted from sibling first-party projects

Two of the author's own separate projects are used as cannibalization sources
for non-emulation *utility* code: **Emu** (a C multi-system emulator) and the
**Eliot** engine (`eliot-mark2`). These are first-party (same author), not
third-party-owned, so no external license obligation attaches; they are
acknowledged here per the AGENTS.md rule that any lifted code carry its
provenance and a Mnemos re-review. Mnemos takes **no** runtime, namespace, or
allocator dependency on either project — only self-contained utility code,
re-expressed in Mnemos's own types, namespaces, and standards.

| Mnemos code | Source | Nature |
|---|---|---|
| `src/common/hex.hpp` | Eliot `Core/Encoding/Hex` | Adapted: the table-driven encode/decode, re-typed to `std` with an `std::optional` decode result. |
| `src/graphics/images/png_image.*` (PNG container) | Eliot `IML/Encoders/PNGEncoder` | Ported: the PNG signature + IHDR/IDAT/IEND chunk emission. The DEFLATE encoder it calls is independent clean-room work. |
| `src/compression/*` (inflate, deflate, zip, lzma1) | Emu compression + the RFCs | Reimplemented in C++ from RFC 1950/1951 and the container specs, with Emu's C as the design reference (no zlib/LZMA source consulted). |

Each was re-reviewed against Mnemos's architecture, determinism, licensing, and
test requirements before landing.

## Project policy

- No GPL emulator source is lifted into Mnemos. Where multiple open-source
  emulators agree on a behavioural detail, that agreement is the evidence;
  no individual project's *code* is the source.
- Cartridge images, system ROMs, BIOS dumps, and conformance corpora are
  copyrighted and are **never** committed. Each data-gated test self-skips
  when its env var is unset.
