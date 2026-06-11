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
| **ZEXALL-SMS** — Z80 instruction exerciser as a Sega Master System ROM (renders to the SMS VDP / SDSC console) | _SMS-native; no committed harness yet_ | _n/a_ |

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
- **C64 PLA (906114)** — banking truth table from the published PLA equations;
  cross-checked against multiple open-source C64 emulators' memory maps.
- **C64 cartridge `.crt` container** — the de-facto community format
  originally defined by the CCS64 project and adopted by all subsequent C64
  emulators.

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
