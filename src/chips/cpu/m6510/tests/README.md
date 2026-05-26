# MOS 6510 Tests

## Unit + microtests (`mnemos_chips_cpu_m6510_test`)

Deterministic Catch2 tests covering identity, reset, the I/O port, every
addressing mode (including page-cross and indexed/indirect timing), the
documented opcodes, decimal mode, RMW, branches/jumps/subroutines, interrupts,
the stable illegals, and register-snapshot introspection. Always built and run.

## Conformance gate (`mnemos_chips_cpu_m6510_conformance_test`)

Validates the core against a public per-instruction 6502 test corpus — for each
vector it checks the final CPU state *and* the exact per-cycle bus trace. See
[`THIRD-PARTY.md`](../../../../../THIRD-PARTY.md) for the corpus reference.

The corpus is large and **never committed**. The test is data-gated: it reports
*skipped* (CTest `SKIP_RETURN_CODE`) when the corpus directory is not provided,
so normal CI and local builds stay green without it.

### Running it locally

Point `MNEMOS_M6510_TESTS_DIR` at a directory of per-opcode JSON files (one
`<opcode>.json` per opcode):

```powershell
$env:MNEMOS_M6510_TESTS_DIR = "C:/path/to/6502"
ctest --preset windows-msvc-debug -R m6510_conformance_test --output-on-failure
```

```bash
MNEMOS_M6510_TESTS_DIR=/path/to/6502 \
  ctest --preset linux-clang-release -R m6510_conformance_test --output-on-failure
```

JAM/KIL and the unstable illegal opcodes (SHA/SHX/SHY/TAS/LAS/ANE/LXA) are out
of scope for v0.1 and are skipped by the harness; see `../NOTES.md`.

The core currently passes the full corpus (~2.4M vectors) across every
documented and stable-illegal opcode.
