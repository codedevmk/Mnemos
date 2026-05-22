# Mnemos Codex Agent Guide

## First Read

- Read `README.md` first, then review the specs in this order:
  1. `docs/specs/mnemos-architecture-tds-v0.1.md`
  2. `docs/specs/mnemos-project-plan-v0.1.md`
  3. `docs/specs/mnemos-todos-v0.1.md`
- The v0.1 specs are marked `Draft, awaiting review`. Treat them as the current design source of truth, but do not treat them as blanket approval to implement every tier.
- If a request asks for implementation before a relevant contract is approved, confirm explicit sign-off or keep the work to review, documentation, or safe M0 repository initialization.

## Project Identity

- Mnemos is a standalone multi-system emulator framework and developer toolkit.
- It is not an Eliot Engine module and must not take Eliot runtime, UI, allocator, or namespace dependencies unless a future approved ADR introduces an integration boundary.
- Use the `mnemos` namespace and the tiered layout described in the architecture TDS.
- Emu is reference material only. Do not lift code from Emu without re-review against Mnemos architecture, licensing, determinism, and test requirements.

## Architecture Rules

- Preserve the eight-tier dependency direction: `foundation` -> `chips` -> `topology` -> `manifests` -> `runtime` -> `instrumentation` -> `frontend_sdk` -> `apps`.
- Lower tiers must not depend on higher tiers. Frontends must use instrumentation surfaces rather than reaching into runtime internals.
- The runtime core is headless and deterministic. UI, player behavior, and developer tooling are clients of that core.
- Cycle accuracy is the default. Any HLE behavior must be declared in manifests and justified in docs or ADRs.
- Observability is a product contract: chips and runtime work should expose state, events, and timing through the planned instrumentation model.

## Dependency Policy

- Follow TDS section 6.5 for v0.1 dependencies. Use pinned CMake `FetchContent` entries for approved third-party code.
- Do not add new third-party libraries without an ADR covering need, license, isolation, and maintenance risk.
- No GPL code may enter Apache- or MIT-licensed tiers. Emulator projects may be used as behavioral references only when provenance is documented.
- ROMs, firmware dumps, build outputs, and generated logs are never committed.

## Implementation Workflow

- Use `docs/specs/mnemos-todos-v0.1.md` as the task ledger until a tracker supersedes it.
- A task is done only when its acceptance criterion is met and the relevant CI/build/test evidence is available.
- Prefer Windows-first PowerShell commands, but keep Linux parity in every build-system and source-layout decision.
- Planned build tooling is CMake 3.28+, Ninja, C++23, strict warnings, and presets. Do not invent ad hoc build commands once presets exist.
- Keep generated artifacts under `out/`; keep the repository root clean.
- Add ADRs under `docs/adr/` for non-obvious architecture, dependency, license, scheduler, or contract decisions.

## Current Bootstrap State

- This repository is still in M0 bootstrap. At this point there may be no root `CMakeLists.txt`, `CMakePresets.json`, CI workflow, license files, or source directories.
- The current specs live under `docs/specs/`, even though the M0 target layout later calls for `docs/architecture/`. Do not move them unless the user asks for that migration.
- If `.gitignore` or the tracked documentation layout conflicts with the specs, report the conflict explicitly before broad cleanup.
