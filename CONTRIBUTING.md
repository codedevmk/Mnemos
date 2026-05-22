# Contributing

Mnemos is in M0 bootstrap. Contributions should follow `AGENTS.md`, the
architecture TDS, and the milestone task ledger under `docs/specs/`.

Required local checks once toolchains are available:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Every architectural exception, dependency addition, license decision, or
contract change requires an ADR under `docs/adr/`.
