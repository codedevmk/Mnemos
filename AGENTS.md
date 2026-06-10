# Mnemos Codex Agent Guide

## First Read

- Read `CONSTITUTION.md` first — the L0 authority index (precedence rules,
  standing rules, global invariants, module table). It points into
  `constitution/` modules that load on demand.
- Then `README.md`, and for design background:
  1. `docs/architecture/mnemos-architecture-tds-v0.1.md`
  2. `docs/architecture/mnemos-project-plan-v0.1.md`
- The v0.1 specs are L3 design notes: their normative content was lifted into
  `constitution/` (see `constitution/MIGRATION.md`); on any divergence the
  constitution wins. Do not treat them as blanket approval to implement every
  tier.
- If a request asks for implementation before a relevant contract is approved, confirm explicit sign-off or keep the work to review, documentation, or safe M0 repository initialization.

## Project Identity

- Mnemos is a standalone multi-system emulator framework and developer toolkit.
- It is not an Eliot Engine module and must not take Eliot runtime, UI, allocator, or namespace dependencies unless a future approved ADR introduces an integration boundary.
- Use the `mnemos` namespace and the tiered layout described in the architecture TDS.
- Any external code, sample, or test corpus brought in is reference material only. Do not lift code into Mnemos without re-review against the architecture, licensing, determinism, and test requirements, and acknowledge the source in `THIRD-PARTY.md`.

## Architecture Rules

- Product code lives under `src/`; keep root-level directories for build, docs, tests, tools, and repository infrastructure.
- Preserve the eight-tier dependency direction: `foundation` -> `chips` -> `topology` -> `manifests` -> `runtime` -> `instrumentation` -> `frontend_sdk` -> `apps`.
- Lower tiers must not depend on higher tiers. Frontends must use instrumentation surfaces rather than reaching into runtime internals.
- The runtime core is headless and deterministic. UI, player behavior, and developer tooling are clients of that core.
- Cycle accuracy is the default. Any HLE behavior must be declared in manifests and justified in docs or ADRs.
- Observability is a product contract: chips and runtime work should expose state, events, and timing through the planned instrumentation model.

## Dependency Policy

- Follow TDS section 6.5 for v0.1 dependencies. Use pinned CMake `FetchContent` entries for approved third-party code.
- Do not add new third-party libraries without an ADR covering need, license, isolation, and maintenance risk.
- No GPL code may enter Apache- or MIT-licensed tiers. Open-source emulator projects may be used as behavioral references only when provenance is acknowledged in `THIRD-PARTY.md`.
- ROMs, firmware dumps, build outputs, and generated logs are never committed.

## Implementation Workflow

- The working todo ledger is local-only (gitignored); ongoing tracker decisions stay in PR descriptions and ADRs.
- A task is done only when its acceptance criterion is met and the relevant CI/build/test evidence is available.
- Prefer Windows-first PowerShell commands, but keep Linux parity in every build-system and source-layout decision.
- Planned build tooling is CMake 3.28+, Ninja, C++23, strict warnings, and presets. Do not invent ad hoc build commands once presets exist.
- Keep CMake build trees under `build/`; never commit build outputs.
- Add ADRs under `docs/adr/` for non-obvious architecture, dependency, license, scheduler, or contract decisions.
- Do not overgenerate comments. Add comments only when they explain a non-obvious invariant, constraint, or tradeoff.
- No low-signal generated filler: avoid placeholder prose, broad abstractions, and scaffolding beyond the current milestone's acceptance needs.

## Scratch artifacts

- Every debug artifact a session produces goes under the git-ignored `scratch/` dir at the repo root: headless `--screenshot` framebuffers, VRAM/CRAM dumps, trace CSVs, ad-hoc logs, coverage `profraw`. Do not write them to the repo root.
- Examples: `mnemos_player --screenshot scratch/boot.ppm --frames 120`; redirect logs with `... > scratch/dma.log`; for ad-hoc coverage set `LLVM_PROFILE_FILE=scratch/cov/%p.profraw` (the foundation coverage script already scopes its own profraw under the build dir).
- Clear it with `scripts/clean-scratch.ps1` (supports `-OlderThanDays N` and `-Deep`). The whole dir is git-ignored, so a stale `scratch/` never dirties `git status`.

## Current Bootstrap State

- This repository is still in M0 bootstrap. Root CMake presets, `src/` tier placeholder targets, CI wiring, licenses, hooks, ADR seeds, and a foundation smoke test are expected to exist.
- The canonical specs live under `docs/architecture/` (migrated from the original `docs/specs/` location).
- Use the CMake presets in `README.md` for local validation. Windows Ninja builds require a Visual Studio developer environment with `cl` available on `PATH`.
