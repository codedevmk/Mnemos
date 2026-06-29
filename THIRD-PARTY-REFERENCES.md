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
- **MSX Korean mapper CRC database** — Korean MSX cartridge images likewise have
  no universal header signature, so exact known images that require the Korean
  MSX 8 KiB mapper are auto-detected by ROM CRC-32
  (`src/manifests/common/msx_cartridge_mapper.cpp`). The entries are catalogue
  facts from data-gated cartridge images and are cross-checked by forcing the
  implemented mapper in Mnemos; no cartridge data is committed.
- **MSX Generic8 / AshGuine mapper notes** — the four 8 KiB window Generic8
  mapper behaviour and the AshGuine family mapper assignment were cross-checked
  against public MSX mapper notes (`https://www.msx.org/wiki/MegaROM_Mappers`)
  and fMSX cartridge compatibility notes (`https://fms.komkon.org/fMSX/fMSX.html`).
  Mnemos implements the mapper from the observed bank-register protocol and
  local data-gated ROM traces; no emulator code or cartridge data is copied.
- **93C46 EEPROM cart CRC database** — the handful of Game Gear cartridges that
  carry a 93C46 serial EEPROM for saves are likewise auto-detected by ROM CRC-32
  (`src/manifests/sms/sms_system.cpp`), compiled from the community Game Gear
  cartridge database and cross-checked against open-source Sega emulators' game
  lists. The 93C46 Microwire protocol is the published Microchip datasheet.
- **Irem M72 ROM-set metadata** — declarative game manifests under
  `src/manifests/irem_m72/games/` use public MAME M72 driver metadata for dump
  filenames, offsets, reloads, CRC-32s, parent/clone relationships, cabinet
  orientation, and true-M72 DIP switch / coinage tables. The
  no-dump `dbreedm72` / `dkgensanm72` manifests also carry explicit HLE
  declarations from the public missing-i8751 notes and startup RAM inversion
  behavior. Their sample-trigger cursor starts are profile data cross-checked
  against MAME's public M72 driver and bounded by the declared sample ROM at
  runtime. No fake protection code arrays, emulator source code, or ROM data
  are copied into Mnemos.
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
  status is declared in the CPS2 manifests. The DL-1425 dry/wet/linear pan gain
  tables, FIR coefficient tables, filter-offset selection behaviour, echo path,
  and wet/dry delay-state model used by the QSound HLE were cross-checked
  against the BSD-3-Clause `qsound-hle` reference by superctr / Ian Karlsson and
  Valley Bell (2018), which documents behaviour derived from the DSP ROM. No
  third-party emulator source is vendored.

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
