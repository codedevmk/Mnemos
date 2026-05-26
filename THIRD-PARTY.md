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

These corpora are the de-facto standard for verifying 8-/16-bit CPU cores; we
acknowledge their authors' work in producing and maintaining them.

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

## Project policy

- No GPL emulator source is lifted into Mnemos. Where multiple open-source
  emulators agree on a behavioural detail, that agreement is the evidence;
  no individual project's *code* is the source.
- Cartridge images, system ROMs, BIOS dumps, and conformance corpora are
  copyrighted and are **never** committed. Each data-gated test self-skips
  when its env var is unset.
