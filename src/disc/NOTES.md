# disc — CD image + media format utilities

TIER 2 data-format module (`mnemos::disc`) for the CD-based systems (Sega CD,
and later any CD platform). Peer to `mnemos::compression`; not a hardware chip.

## Provenance

Ported from the Emu reference (`C:\Users\mkrol\source\repos\Emu\Emu\chips\…`),
which is the user's own earlier project — cannibalizing it is sanctioned per
ADR-0006 (`docs/adr/0006-emu-reuse-and-conformance.md`) and
[[mnemos-port-emu-cores]]. Restructured into Mnemos C++23 conventions
(namespaces, `std::span`, compile-time tables); no third-party emulator source
consulted.

| Mnemos file | Emu source | Notes |
|---|---|---|
| `circ_ecc.{hpp,cpp}` | `chips/circ_ecc/circ_ecc.{h,c}` | CD-ROM Mode-1 EDC (CRC-32) + P/Q Reed-Solomon parity regeneration, clean-room per ECMA-130. CRC table is now a compile-time `constexpr`. |

## Conformance

`tests/circ_ecc_test.cpp` asserts **byte-for-byte equality with the reference**:
the golden ECC blocks were produced by compiling and running the reference
`circ_ecc` over two deterministic Mode-1 sectors (all-zero data and a
`data[i] = i*7+13` pattern), then baked into the test.

## Roadmap

- `disc_image` (BIN/CUE/ISO loader + sector read) — uses `circ_ecc` to synthesize
  full 2352-byte sectors from 2048-byte user data.
- `chd_image` (MAME compressed-hunk disc) — will use `mnemos::compression::lzma1`
  (already staged for this purpose) + `circ_ecc` for cdlz sector reconstruction.
