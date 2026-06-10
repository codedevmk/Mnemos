---
id: STD-002
title: "Error Handling and Language Discipline"
status: proposed
version: 0.1.0
supersedes: []
superseded_by: null
ratified: null
proposed: 2026-06-10
---

# STD-002 — Error Handling and Language Discipline

Lifted verbatim from `docs/architecture/mnemos-architecture-tds-v0.1.md`
section 7.1, per `constitution/MIGRATION.md`. No executable verifier exists
yet; until one lands, these are prose-only claims counted by metric M4.

## Rules (TDS §7.1)

- C++23 throughout. No C++26 features in v0.1; promote later, deliberately.
- **No exceptions in the chip library or runtime hot paths.** Errors propagate
  via `std::expected` (C++23). Frontends MAY use exceptions.
- **No RTTI in the chip library or runtime.** Frontends MAY use it.
- `constexpr`/`consteval` aggressively where it improves correctness or
  compile-time validation.
- Zero warnings under strict flags (`-Werror -Wall -Wextra -Wpedantic`,
  `/W4 /WX`); no implicit conversions in chip code (`-Wconversion` where
  feasible). Enforced by `cmake/modules/MnemosCompilerFlags.cmake` (gate G1).

## Error surfaces

- Manifest validation errors surface with file/line/column (TDS §10.1).
- Loaders and runtime entry points return `std::expected`-style results;
  failures carry actionable diagnostics rather than aborting.
