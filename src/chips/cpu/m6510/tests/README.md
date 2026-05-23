# MOS 6510 Tests

## Unit + microtests (`mnemos_chips_cpu_m6510_test`)

Deterministic Catch2 tests covering identity, reset, the I/O port, every
addressing mode (including page-cross and indexed/indirect timing), the
documented opcodes, decimal mode, RMW, branches/jumps/subroutines, interrupts,
the stable illegals, and register-snapshot introspection. Always built and run.

## Conformance gate (`mnemos_chips_cpu_m6510_tomharte_test`)

Validates the core against the **Tom Harte / SingleStepTests 6502** corpus — for
each vector it checks the final CPU state *and* the exact per-cycle bus trace.
See ADR 0006.

The corpus is large and **never committed**. The test is data-gated: it reports
*skipped* (CTest `SKIP_RETURN_CODE`) when the corpus directory is not provided,
so normal CI and local builds stay green without it.

### Running it locally

Point `MNEMOS_M6510_TOMHARTE_DIR` at a directory of SingleStepTests 6502 JSON
files (one `<opcode>.json` per opcode, the ProcessorTests `6502/v1` layout):

```powershell
$env:MNEMOS_M6510_TOMHARTE_DIR = "C:/path/to/6502"
ctest --preset windows-msvc-debug -R m6510_tomharte_test --output-on-failure
```

```bash
MNEMOS_M6510_TOMHARTE_DIR=/path/to/6502 \
  ctest --preset linux-clang-release -R m6510_tomharte_test --output-on-failure
```

The corpus is available from the public SingleStepTests / ProcessorTests project
(`SingleStepTests/65x02`, `6502/v1`). JAM/KIL and the unstable illegal opcodes
(SHA/SHX/SHY/TAS/LAS/ANE/LXA) are out of scope for v0.1 and are skipped by the
harness; see `../NOTES.md`.

The core currently passes the full corpus (~2.4M vectors) across every documented
and stable-illegal opcode.
