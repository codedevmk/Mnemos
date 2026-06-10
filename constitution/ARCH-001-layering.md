---
id: ARCH-001
title: "Layered Architecture"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-06-10
proposed: 2026-06-10
invariants:
  - id: INV-ARCH-001
    statement: "Each tier may depend only on tiers strictly below it; dependency direction is downward."
    verified_by: [cmake/modules/MnemosDeclareTier.cmake]
  - id: INV-ARCH-002
    statement: "The chip tier depends only on foundation and never knows which system it lives in."
    verified_by: [cmake/modules/MnemosDeclareTier.cmake]
---

# ARCH-001 — Layered Architecture

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
sections 3, 4, 5.1 and 6.2, per `constitution/MIGRATION.md`. ADR-0009 (module
placement) refines this module and remains accepted authority.

## Principles (TDS §3, layering-relevant subset)

1. **Library-first, not driver-first.** Chips are reusable libraries. Systems
   are compositions of chips plus topology. No system owns its own copy of a
   68000.
2. **Strict layered architecture.** Each tier may depend only on tiers below
   it. Lateral dependencies require explicit justification.
3. **Composition over inheritance.** Polymorphism only where it earns its
   keep; otherwise concrete types with value semantics.

## Tier map (TDS §4)

| Tier | Name | Responsibility |
|------|------|----------------|
| 1 | `foundation` | Containers, allocators, math, time, I/O, threading, logging, platform abstraction |
| 2 | `chips` | Silicon implementations (CPU, audio, video, bus controller, storage, mapper) |
| 3 | `topology` | Memory maps, bus primitives, address decoders, cartridge / mapper infrastructure |
| 4 | `manifests` | System declarations (TOML schemas + thin C++ glue) |
| 5 | `runtime` | Scheduler, clock, save state, rewind ring, determinism, input routing |
| 6 | `instrumentation` | Observability API, event subscription, wire protocol implementation |
| 7 | `frontend_sdk` | UI primitives, theming, asset loading, common widgets |
| 8 | `apps` | Player frontend, developer frontend (separate executables) |

- The chip library (tier 2) MUST NOT know which system it lives in. It depends
  only on foundation.
- The runtime (tier 5) MUST NOT depend on instrumentation; instrumentation
  observes the runtime via injection.
- Frontends (tier 8) MUST NOT bypass the instrumentation API to reach the
  runtime directly.
- The scripting subsystems (Lua embedding, Python IPC) live at tier 6
  alongside instrumentation, since they consume the same surface.

## Module layout (TDS §5.1)

Each module is self-contained and **flat**: public headers and implementation
sources live directly at the module root (no nested `include/mnemos/...`
tree), with unit tests under `tests/`. CMake exposes the module root as the
target's public include directory, so headers are included by **basename in
quotes** both within a module and across modules. Because there is no path
qualifier, every header filename MUST be globally unique across the repository
(see STD-001; enforced by `tools/governance/naming_validator.py`).

Product code lives under `src/`; root-level directories are reserved for
build, docs, tests, tools, and repository infrastructure.

## Dependency enforcement (TDS §6.2)

Each tier's CMakeLists MAY `target_link_libraries` against tiers strictly
below it. The CMake function `mnemos_declare_tier(NAME tier_n DEPENDS ...)`
validates this at configure time and fails the build on violation.

## Hygiene (TDS §5.1.1)

- All build artifacts MUST go under `build/`; the root directory stays clean.
- CI MUST fail on any compiler warning (`-Werror` / `/WX`, see
  `cmake/modules/MnemosCompilerFlags.cmake`).
- Pre-commit MUST run `clang-format --dry-run` and fail on diff.
