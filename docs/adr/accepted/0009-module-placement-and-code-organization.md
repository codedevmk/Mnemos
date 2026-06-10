---
id: ADR-0009
title: "Module Placement & Code Organization Policy"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-31
---

# ADR 0009: Module Placement & Code Organization Policy

**Status:** Accepted
**Date:** 2026-05-31

## Context

The tier layout (ADR 0001) fixes the dependency *direction* but not *where* a
given piece of functionality belongs. Without an explicit placement policy,
generic capabilities drift to wherever they were first needed and get buried
under a consumer. Concrete example: the DEFLATE/ZIP codecs were first written
under `src/apps/player/adapters/common/` because the player was the first
caller, then had to be relocated to `src/compression/`. Each such misplacement
makes a capability hard to find, hard to reuse, and couples it to an unrelated
tier.

This ADR adds a standing policy: every kind of functionality has exactly one
canonical home, named for the capability, independent of who first consumes it.

## Decision

### Governing principles

1. **Single canonical home.** Each capability lives in exactly one module. Do
   not duplicate it or scatter helpers across consumers.

2. **Place by capability, not by first consumer.** A module is named and located
   for *what it does*, never for who calls it first. If the player needs to
   unzip a ROM, the unzip code is a *compression* capability (`src/compression/`),
   not a player detail. The test: "if a second, unrelated subsystem needed this,
   where would it go looking?" — that is its home.

3. **Generic capabilities are low-tier and dependency-light.** A reusable
   primitive depends only on the standard library and lower tiers, so any tier
   above can use it. A generic capability acquiring a dependency on a *higher*
   tier is the symptom of misplacement.

4. **Flat modules, globally-unique names.** Within a module, headers sit beside
   their sources, includes are by basename, and type/file names are globally
   unique across the tree (existing convention).

5. **One module = one CMake target + tier declaration + `tests/`.** A new
   capability *category* means a new module, not extra files bolted onto an
   unrelated target.

### Placement catalog

Canonical homes by capability. When a new category appears, add a row here
rather than improvising a location.

| Capability | Canonical home | Namespace |
|---|---|---|
| std/language/runtime primitives — bit ops, type-safe ids, time, logging, filesystem helpers, span/expected extensions, allocators, threading | `src/foundation/` | `mnemos::foundation` |
| general-purpose base-type operations — string manipulation, parsing, formatting | `src/common/` (e.g. `string.{hpp,cpp}`) | `mnemos::common` |
| character encoding / decoding — UTF-8, ASCII, Shift-JIS, region text codecs | `src/text/` (`encoding.{hpp,cpp}`, `decoding.{hpp,cpp}`) | `mnemos::text` |
| hashing + encryption — CRC, SHA, MD5; AES, … | `src/security/cryptography/` | `mnemos::security::cryptography` |
| compression codecs — DEFLATE, LZMA, ZIP, … | `src/compression/` | `mnemos::compression` |
| audio / signal DSP (resampling, mixing, fixed-point) | `src/dsp/` | `mnemos::dsp` |
| file read / write I/O | `src/io/` | `mnemos::io` |
| image encode / decode (PPM, PNG, BMP, ...) | `src/graphics/images/` (base `image` + per-format subclasses) | `mnemos::graphics::images` |
| cross-cutting framework services — logging, configuration, DI, options | `src/extensions/<name>/` | `mnemos::<name>` (flat; see note) |
| emulated chips | `src/chips/<category>/<chip>/` | `mnemos::chips::<category>` |
| buses / address decoding | `src/topology/` | `mnemos::topology` |
| system assembly — per-console wiring, manifests | `src/manifests/<system>/` | `mnemos::manifests::<system>` |
| scheduler / deterministic execution | `src/runtime/` | `mnemos::runtime` |
| chip observation contract — the memory/debug-layer/trace surfaces a chip exposes about itself | `src/instrumentation/` (contract types in `chips/shared`) | `mnemos::instrumentation` |
| debugger + debugging tools — breakpoints, stepping, watchpoints, disassembly, trace export, artifact dumps, monitor | `src/debug/` | `mnemos::debug` |
| frontend-facing system interface | `src/frontend_sdk/` | `mnemos::frontend_sdk` |
| peripheral / controller devices | `src/peripheral_sdk/` | `mnemos::peripheral` |
| runnable programs + their per-system glue | `src/apps/<app>/` | `mnemos::apps::<app>` |

**The boundary that catches most mistakes:** `foundation` is the *std++* layer —
universal primitives with no domain identity. The moment a thing has a
recognizable *domain* (it is *string* manipulation, *text* codecs, *crypto*,
*compression*, *io*), it leaves `foundation` and gets its own named module.
"I'll just drop this helper in `foundation/util`" is the smell to avoid.

### Specific placements established by this ADR

- **String operations** → `src/common/` (a `string` / `string_utils` unit) — not
  scattered across callers, not in `foundation`.
- **Text encoding/decoding** → `src/text/`, split into `encoding.{hpp,cpp}` and
  `decoding.{hpp,cpp}`.
- **Hash and encryption** → `src/security/cryptography/`. This includes
  checksums (CRC) and digests (SHA): they are hashing.
- **Cross-cutting framework services** → `src/extensions/<name>/`, modeled on
  .NET's `Microsoft.Extensions.*`. The folder is an organizational grouping; the
  namespace is **flat** (`mnemos::logging`, not `mnemos::extensions::logging`) for
  ergonomic call sites — the one deliberate folder↔namespace divergence in the
  tree. **Logging** is the first such service: `src/extensions/logging/`
  (`mnemos::logging`) with a `console/` provider (`mnemos::logging::console`),
  shaped after `ILogger` / `ILoggerProvider` / `ILoggerFactory`.

### Procedure

**Before adding functionality:**
1. Name the capability in one phrase ("zip extraction", "Shift-JIS decode",
   "SHA-256").
2. Find its row in the catalog → that module is its home. If no row fits, it is
   a new category: add the module (below) and a catalog row in this ADR.
3. If the home module does not exist yet, create it; otherwise add the file
   there.
4. The consumer (player, manifest, chip) then *depends on* that module — it
   never absorbs the capability.

**Adding a new top-level module:**
- `src/<module>/` with flat sources + headers, a `tests/` subdir, and a
  `CMakeLists.txt` that declares the library, calls
  `mnemos_apply_common_target_options`, `mnemos_declare_tier(NAME … TIER n
  DEPENDS …)`, and registers a `mnemos_add_test`.
- Add `add_subdirectory(<module>)` to `src/CMakeLists.txt` in tier order.
- Choose the lowest tier the dependencies allow.

### Reconciliation with existing code

The pre-policy `foundation` hashing headers `crc32.hpp` / `sha256.hpp` have been
relocated to `src/security/cryptography/` (`mnemos::security::cryptography`), and
`foundation/log.hpp` has been removed in favour of `src/extensions/logging/`
(`mnemos::logging`) — it had no production users, so it was retired outright
rather than migrated. With those moves done, `foundation` is strictly the std++
primitive layer; new hashing/crypto and logging go to their named modules.

## Consequences

- A reviewer can answer "is this in the right place?" from the catalog, not from
  taste.
- Reusable capabilities stay reusable and low-tier; consumers depend on them
  instead of hiding them.
- The catalog is the living index: a new category is a one-row change here,
  keeping the policy authoritative as the tree grows.
- The pre-policy `foundation` debts are resolved, not just recorded: `crc32` /
  `sha256` now live in `security/cryptography` and logging in
  `extensions/logging`, leaving `foundation` as std++ primitives only.
