# Mnemos

Mnemos is a standalone multi-system emulator framework and developer toolkit. Its core thesis is a deterministic, headless runtime with first-class observability, used by both a player frontend and a developer frontend.

## Status

Mnemos is currently in M0 bootstrap. The repository contains the initial v0.1 planning specs, but the build system, source tree, CI, licenses, and tooling skeleton have not landed yet.

The specs are marked `Draft, awaiting review`. Treat them as the current design source of truth, not as automatic approval to implement every milestone.

## Read First

1. `docs/specs/mnemos-architecture-tds-v0.1.md`
2. `docs/specs/mnemos-project-plan-v0.1.md`
3. `docs/specs/mnemos-todos-v0.1.md`
4. `AGENTS.md` for Codex-specific contribution rules

## Planned Build Contract

The planned M0 toolchain is CMake 3.28+, Ninja, C++23, strict warnings-as-errors, and configure/build/test presets for Windows and Linux. Until `CMakeLists.txt` and `CMakePresets.json` exist, there are no canonical build or test commands to run.

Generated outputs belong under `out/`. ROMs, firmware dumps, logs, and build artifacts must not be committed.
