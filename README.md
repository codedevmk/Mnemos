# Mnemos

Mnemos is a standalone multi-system emulator framework and developer toolkit. Its core thesis is a deterministic, headless runtime with first-class observability, used by both a player frontend and a developer frontend.

## Status

Mnemos is currently in M0 bootstrap. The repository contains the initial v0.1 planning specs plus the first monorepo scaffold: CMake presets, `src/` tier targets, source hygiene files, CI wiring, documentation indexes, and a foundation smoke test.

The specs are marked `Draft, awaiting review`. Treat them as the current design source of truth, not as automatic approval to implement every milestone.

## Read First

1. `docs/specs/mnemos-architecture-tds-v0.1.md`
2. `docs/specs/mnemos-project-plan-v0.1.md`
3. `docs/specs/mnemos-todos-v0.1.md`
4. `AGENTS.md` for Codex-specific contribution rules

## Build

The M0 toolchain is CMake 3.28+, Ninja, C++23, strict warnings-as-errors, and configure/build/test presets for Windows and Linux.

Windows MSVC debug:

```powershell
# Run from a Visual Studio developer PowerShell or command prompt.
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Linux GCC debug:

```sh
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Generated build outputs belong under `build/`. ROMs, firmware dumps, logs, and build artifacts must not be committed.
