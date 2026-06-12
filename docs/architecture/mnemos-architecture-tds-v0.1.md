> **L3 design note** (2026-06-10): normative content lifted to `constitution/`
> per `constitution/MIGRATION.md` (MNE-CTX-PLAN-001 P0, ADR-0013). On any
> divergence, `CONSTITUTION.md` and the constitution modules take precedence.

# Mnemos — Architecture Technical Design Specification

**Version:** 0.1 (initial draft)
**Status:** Draft, awaiting review
**Owner:** Marius
**Audience:** Engine implementers, frontend implementers, contributing AI agents
**Scope:** Architectural contract for the Mnemos multi-system emulation + developer tooling suite.

---

## 1. Document Control

This document is the canonical architectural reference for Mnemos v0.1. It defines tier boundaries, contracts, repository layout, build topology, toolchain baseline, and the first concrete system (C64). Implementation-level interfaces (full headers, schemas) are sketched, not exhaustively specified — v0.2 promotes the sketches to authoritative definitions once v0.1 is reviewed.

Hard rule: no implementation work begins on a tier before its contract section in this document is reviewed and approved.

### 1.1 Conventions

- "MUST", "SHOULD", "MAY" carry RFC 2119 weight.
- Code examples are illustrative C++23 unless explicitly tagged otherwise.
- Identifiers in `monospace` are concrete names that will appear verbatim in source.
- "Tier N" refers to the layered architecture in §4; lower N = lower in the stack.

---

## 2. Purpose and Thesis

### 2.1 What Mnemos Is

A multi-system emulator framework and developer toolkit that pairs preservation-grade accuracy with a first-class observability surface. Two clients sit on top of one deterministic, headless runtime:

- **Player frontend** — a polished, modern launcher and play experience.
- **Developer frontend** — a unified debug/introspection workbench that attaches to any running system.

### 2.2 The Observability Thesis

Mnemos's differentiating posture is **observability-first**. Every emulated component exposes its state, its bus activity, and its time evolution through a uniform instrumentation API. Both frontends are clients of that API; external tooling (Python, agents, CI) is a client of the same API over a stable wire protocol. This is the architectural property that justifies the cost of cycle-accurate emulation and that enables every other capability — dev workbench, replay, netplay, asset extraction, automated testing.

### 2.3 What Mnemos Is Not

- Not a fork or derivative of any existing emulator. No GPL emulator source is lifted into Mnemos.
- Not a media center. The player frontend is a focused retro experience.
- Not a single-system project. Composability across systems is a non-negotiable design constraint.
- Not coupled to Eliot Engine. Future Eliot plugin path is preserved through clean API boundaries; no shared core.

---

## 3. Architectural Principles

These are non-negotiable. Every design decision below is justified by reference to one or more of these.

1. **Library-first, not driver-first.** Chips are reusable libraries. Systems are compositions of chips plus topology. No system owns its own copy of a 68000.
2. **Headless deterministic core.** The runtime runs without any UI, with identical output for identical input. Determinism is the substrate for testing, replay, and netplay.
3. **Cycle-accurate by default, opt-in HLE.** The chip library is built to a strict clock contract. HLE substitutions are declared in system manifests, never hidden.
4. **Observability is a first-class contract, not an afterthought.** Every chip exposes its state and events through the same instrumentation surface.
5. **Strict layered architecture.** Each tier may depend only on tiers below it. Lateral dependencies require explicit justification.
6. **Composition over inheritance.** Polymorphism only where it earns its keep; otherwise concrete types with value semantics.
7. **Modern C++23, SOLID, DRY.** Zero warnings under strict flags. No platform leakage above the foundation tier.
8. **Single API, multiple surfaces.** Frontends and external tools speak the same instrumentation API; only the transport differs.

---

## 4. Layered Architecture

Mnemos is organized in eight tiers. Each tier exposes a stable public contract; internals are private. Dependency direction is strictly downward.

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

Notes:

- The chip library (tier 2) MUST NOT know which system it lives in. It depends only on foundation.
- The runtime (tier 5) MUST NOT depend on instrumentation; instrumentation observes the runtime via injection.
- Frontends (tier 8) MUST NOT bypass the instrumentation API to reach the runtime directly.
- The scripting subsystems (Lua embedding, Python IPC) live at tier 6 alongside instrumentation, since they consume the same surface.

---

## 5. Monorepo Directory Layout

Single repository. Directory names are functional; tier hierarchy is enforced by CMake target dependency rules (§6), not by naming.

```
mnemos/
├── .github/                       # CI workflows (Actions or self-hosted)
├── cmake/                         # Modules, presets, toolchain files
│   ├── presets/
│   ├── modules/
│   └── toolchains/
├── docs/                          # Architecture, ADRs, schemas
│   ├── architecture/              # This TDS and successors
│   ├── adr/                       # Architecture decision records
│   └── schemas/                   # Manifest schemas, save-state spec, wire protocol
├── extern/                        # FetchContent notes and approved third-party policy
├── src/                           # Product source code
│   ├── foundation/                # Tier 1 (each module is flat: see note below)
│   │   ├── *.hpp                  # public headers at the module root
│   │   └── tests/
│   ├── chips/                     # Tier 2
│   │   ├── shared/                # Class taxonomy, base interfaces (ibus.hpp), registration
│   │   ├── cpu/
│   │   │   ├── m6510/
│   │   │   ├── m6502/
│   │   │   ├── z80/
│   │   │   ├── m68000/
│   │   │   ├── sh2/
│   │   │   └── ...
│   │   ├── audio/
│   │   │   ├── sid_6581/
│   │   │   ├── sn76489/
│   │   │   ├── ym2612/
│   │   │   └── ...
│   │   ├── video/
│   │   │   ├── vic_ii_6569/
│   │   │   ├── vdp_315_5313/
│   │   │   └── ...
│   │   ├── bus_controller/
│   │   │   ├── cia_6526/
│   │   │   └── ...
│   │   ├── storage/
│   │   └── mapper/
│   ├── topology/                  # Tier 3
│   ├── manifests/                 # Tier 4
│   │   ├── common/                # Shared manifest loader, validator
│   │   ├── c64/
│   │   ├── sms/
│   │   └── ...
│   ├── runtime/                   # Tier 5
│   ├── instrumentation/           # Tier 6
│   │   ├── api/
│   │   ├── wire/                  # Cap'n Proto schemas + generated code
│   │   ├── scripting_lua/
│   │   └── scripting_python_ipc/
│   ├── frontend_sdk/              # Tier 7
│   └── apps/                      # Tier 8
│       ├── player/
│       └── dev/
├── tests/                         # Cross-tier integration tests, golden tests
│   ├── integration/
│   └── golden/                    # Frame-perfect regression suite
├── tools/                         # Build scripts, code generators, ROM hashers
├── build/                         # Build outputs (gitignored)
├── .gitignore
├── .clang-format
├── .clang-tidy
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE                        # Apache-2.0 (core)
├── LICENSE-chips                  # MIT (chip library)
└── README.md
```

### 5.1 Module Layout Convention

Each module is self-contained and **flat**: its public headers and implementation
sources live directly at the module root (no nested `include/mnemos/...` tree), with
unit tests under `tests/`. CMake exposes the module root as the target's public
include directory, so headers are included by **basename in quotes** —
`#include "ibus.hpp"`, `#include "z80.hpp"` — both within a module and across modules
(the dependency graph propagates each linked module's root onto the include path).
Because there is no path qualifier, **every header filename MUST be globally unique**
across the repository (this is why the bus *interface* is `ibus.hpp` while the
topology *implementation* is `bus.hpp`).

### 5.1.1 Hygiene Rules (Day-Zero, Enforced by CI)

- All build artifacts MUST go under `build/`. The root directory MUST stay clean.
- All test logs MUST go under `build/logs/` or the active preset's build tree.
- `.gitignore` MUST reject `build/`, `*.log`, `*.bin` (firmware dumps), IDE caches, and stray CMake build trees.
- Pre-commit hook MUST run `clang-format --dry-run` and fail on diff.
- CI MUST fail on any compiler warning.

---

## 6. Build System Topology

### 6.1 CMake Layout

- CMake 3.28+ minimum (modern target features, presets v6).
- Out-of-source builds enforced (`if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR) FATAL_ERROR`).
- Root `CMakeLists.txt` declares the project, language baseline, global flags, and adds `src/`; `src/CMakeLists.txt` adds each tier in dependency order.
- Each tier's `CMakeLists.txt` defines exactly one library target (with possible per-chip sub-targets under tier 2).
- All public targets exported as `mnemos::<tier>` (e.g. `mnemos::foundation`, `mnemos::runtime`).
- Per-chip targets exported as `mnemos::chips::<category>::<part>` (e.g. `mnemos::chips::cpu::m6510`).

### 6.2 Dependency Enforcement

Each tier's CMakeLists MAY `target_link_libraries` against tiers strictly below it. A custom CMake function `mnemos_declare_tier(NAME tier_n DEPENDS ...)` validates this at configure time and fails the build on violation.

### 6.3 Presets

`CMakePresets.json` defines:

- **Configure presets:** `windows-msvc-debug`, `windows-msvc-release`, `windows-msvc-relwithdebinfo`, `linux-gcc-debug`, `linux-gcc-release`, `linux-clang-debug`, `linux-clang-release`.
- **Build presets:** matching each configure preset.
- **Test presets:** matching each configure preset.
- All presets use **Ninja** as the generator (single-config or multi-config as appropriate). VS solution generation is a developer convenience, not the CI path.

### 6.4 Compiler Flags

Baseline (every target):

- C++23 strict (`-std=c++23` / `/std:c++23`).
- All warnings as errors (`-Werror -Wall -Wextra -Wpedantic` / `/W4 /WX`).
- No implicit conversions in chip code (`-Wconversion` where feasible).
- Position-independent code on Linux.
- Sanitizers in dedicated build configs (`linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`).

### 6.5 Dependencies

Managed via CMake `FetchContent` with pinned commit SHAs. No system package dependencies for core libs.

| Dependency | Purpose | Tier consumer |
|------------|---------|----------------|
| Vulkan SDK | Rendering | frontend_sdk |
| glslang / shaderc | Shader compilation | frontend_sdk (build-time) |
| Cap'n Proto | Wire protocol schemas + RPC | instrumentation |
| Lua 5.4 | Embedded scripting | instrumentation/scripting_lua |
| sol2 | C++ ↔ Lua binding | instrumentation/scripting_lua |
| tomlplusplus | TOML parsing | manifests |
| Catch2 v3 | Testing framework | all tiers (tests/) |
| zstd | Save-state compression | runtime |

LuaJIT is not adopted in v0.1; stock Lua 5.4 is the baseline. LuaJIT may be added behind a CMake option in v0.2 if scripting-side performance becomes a measurable bottleneck.

---

## 7. Toolchain and Language Baseline

### 7.1 Language

- C++23 throughout. No C++26 features in v0.1 (compiler support remains uneven on Linux). Promote later, deliberately.
- Modules MAY be used in v0.2 once CMake support is broadly stable; v0.1 uses headers + PCH.
- No exceptions in the chip library or runtime hot paths. Errors propagate via `std::expected` (C++23). Frontends MAY use exceptions.
- No RTTI in the chip library or runtime. Frontends MAY use it.
- `constexpr`/`consteval` aggressively where it improves correctness or compile-time validation.

### 7.2 Development Environment

- **Primary IDE:** Visual Studio 2026 Community on Windows 11 (CMake-mode, not solution-driven).
- **Linux toolchain:** GCC 14+ or Clang 18+, Ninja, CMake 3.28+.
- **Cross-platform formatting:** `.clang-format` (LLVM base, project overrides).
- **Cross-platform linting:** `.clang-tidy` with curated checks.
- **Pre-commit:** clang-format, clang-tidy fast checks.

### 7.3 Target Platforms (v0.1)

- Windows 11 x64.
- Linux x64 (Ubuntu 24.04 LTS as reference distro).
- macOS, ARM64, and console targets are out of scope for v0.1.

### 7.4 License

- Apache 2.0 for foundation, topology, manifests, runtime, instrumentation, frontend_sdk, apps.
- MIT for the chip library (`src/chips/`).
- Any plugin that links GPL code MUST live under a clearly demarcated subdirectory and ship under GPL accordingly; no GPL code may enter Apache- or MIT-licensed tiers.
- `THIRD_PARTY_NOTICES.md` enumerates every dependency, its license, and its version.

---

## 8. Chip Library Contract

### 8.1 Class Taxonomy

Every chip declares its class at registration. The taxonomy is closed (extension requires an ADR):

```cpp
namespace mnemos::chips {

enum class chip_class : std::uint8_t {
    cpu,
    audio_synth,
    video,
    bus_controller,
    storage,
    mapper,
    peripheral,
};

} // namespace mnemos::chips
```

Tooling — manifest validator, dev frontend, scheduler — reasons about chips categorically through this taxonomy.

### 8.2 Base Interface

All chips implement `ichip`. Specialized interfaces (`icpu`, `iaudio_synth`, `ivideo`, etc.) inherit from `ichip` and add class-specific contracts.

```cpp
namespace mnemos::chips {

struct chip_metadata {
    std::string_view manufacturer;   // "MOS Technology"
    std::string_view part_number;    // "6510"
    std::string_view family;         // "6502"
    chip_class       klass;
    std::uint32_t    revision;       // schema-revision of this chip's contract
};

enum class reset_kind : std::uint8_t { power_on, hard, soft };

class ichip {
public:
    virtual ~ichip() = default;

    virtual chip_metadata metadata() const noexcept = 0;
    virtual void          tick(std::uint64_t cycles) = 0;
    virtual void          reset(reset_kind) = 0;

    virtual void save_state(state_writer&) const = 0;
    virtual void load_state(state_reader&) = 0;

    virtual instrumentation::ichip_introspection& introspection() noexcept = 0;
};

} // namespace mnemos::chips
```

### 8.3 Clock Contract

- A chip's `tick(cycles)` advances its internal state by exactly `cycles` of its own clock domain.
- The scheduler (tier 5) is responsible for translating master clock ticks into per-chip clock ticks via the divider declared in the system manifest.
- A chip MUST NOT call `tick` on another chip directly. All inter-chip interactions go through the bus or through explicit event posts handled by the scheduler.

### 8.4 Bus Interface

Chips attach to buses; they do not own them. The bus is provided by tier 3 (topology).

```cpp
class ibus {
public:
    virtual ~ibus() = default;
    virtual std::uint8_t  read8 (std::uint32_t addr) = 0;
    virtual void          write8(std::uint32_t addr, std::uint8_t value) = 0;
    virtual std::uint16_t read16(std::uint32_t addr) { /* default = 2 × read8 */ }
    virtual void          write16(std::uint32_t addr, std::uint16_t v) { /* ... */ }
    // 32-bit accessors as needed by 32-bit CPUs.
};
```

CPUs hold an `ibus*` injected at attach time. Width specializations (16/32) are provided so the SH-2 and 68000 don't byte-walk every fetch.

### 8.5 Introspection Surface

Every chip exposes:

- Named register snapshot (`std::span<const register_descriptor>`).
- Memory regions it owns (for memory viewers).
- Event taps (instruction executed, IRQ raised, DMA started, mode changed, etc.) — chip-class-specific.
- Cycle counter.

The introspection surface is read-mostly; mutations from a debugger go through explicit `control` methods, never by direct memory writes from the frontend.

### 8.6 Registration

Each chip implementation publishes a `chip_factory` registered at static-init time via a constructor of a translation-unit-local object. The factory is keyed by canonical chip ID (`"mos.6510"`, `"yamaha.ym2612"`).

```cpp
namespace {
const auto _register = mnemos::chips::register_factory(
    "mos.6510",
    [] { return std::make_unique<m6510>(); }
);
}
```

The manifest layer (tier 4) instantiates chips by ID; it never includes chip headers directly.

---

## 9. Bus and Topology Primitives

### 9.1 Address Space

A bus is a typed address space with:

- A width (bits).
- An endianness.
- An ordered list of regions, each mapped to a backing (RAM, ROM, MMIO chip, mapper).

Resolution is O(log N) via a sorted region table; hot-path lookups for chips with fixed views may cache the resolved backing pointer.

### 9.2 Mapper Infrastructure

Mappers are first-class chips of class `mapper`. Cartridge mappers (C64 cartridges, NES MMCs, Genesis SSF2, etc.) implement the same `ichip` interface and present a bus-shaped view to the parent bus.

### 9.3 MMIO Mediation

MMIO regions are owned by chips. A region declaration in the manifest binds an address range to a chip's MMIO read/write handlers. The bus routes accesses without per-call virtual dispatch where the region is contiguous and fixed.

---

## 10. System Manifest Schema

### 10.1 Format

TOML. Schema version declared in the file. Strict validation at load time. Errors surface with file/line/column.

### 10.2 Sketch (C64 PAL)

```toml
[manifest]
schema         = "mnemos-manifest/1"
id             = "commodore.c64.pal"
display_name   = "Commodore 64 (PAL)"
family         = "commodore"
revision       = 1

[clock]
master_hz                = 17734472
master_to_cpu_divider    = 18
master_to_video_divider  = 4

# --- Chips -------------------------------------------------------------------

[[chip]]
id            = "cpu"
type          = "mos.6510"
attached_bus  = "main"

[[chip]]
id            = "video"
type          = "mos.vic_ii.6569"
attached_bus  = "main"
mmio_range    = "0xD000-0xD3FF"

[[chip]]
id            = "audio"
type          = "mos.sid.6581"
attached_bus  = "main"
mmio_range    = "0xD400-0xD7FF"

[[chip]]
id            = "cia1"
type          = "mos.cia.6526"
attached_bus  = "main"
mmio_range    = "0xDC00-0xDCFF"

[[chip]]
id            = "cia2"
type          = "mos.cia.6526"
attached_bus  = "main"
mmio_range    = "0xDD00-0xDDFF"

# --- Bus topology ------------------------------------------------------------

[[bus]]
id            = "main"
address_bits  = 16
endianness    = "little"

[[bus.region]]
name      = "ram"
range     = "0x0000-0xFFFF"
backing   = "ram"
size      = 65536

[[bus.region]]
name      = "basic_rom"
range     = "0xA000-0xBFFF"
backing   = "rom"
file      = "<basic-rom-image>"
sha256    = "..."
overlay   = true        # PLA-controlled

[[bus.region]]
name      = "kernal_rom"
range     = "0xE000-0xFFFF"
backing   = "rom"
file      = "<kernal-rom-image>"
sha256    = "..."
overlay   = true

[[bus.region]]
name      = "char_rom"
range     = "0xD000-0xDFFF"
backing   = "rom"
file      = "<character-rom-image>"
sha256    = "..."
overlay   = true

# --- HLE declarations (none for C64; included as schema example) -------------

# [[hle]]
# chip      = "audio"
# rationale = "..."
```

### 10.3 Validation Rules

- Every `[[chip]]` MUST reference a chip ID registered in the chip library.
- Every `attached_bus` MUST reference a declared `[[bus]]`.
- MMIO ranges MUST be disjoint within a bus.
- ROM file SHA-256s MUST validate at load time.
- HLE substitutions MUST appear as explicit `[[hle]]` entries with a `rationale` field.

---

## 11. Runtime Contract

### 11.1 Responsibilities

The runtime (tier 5) owns:

- Master clock progression.
- Per-chip cycle dispatch (`tick(cycles)` calls).
- Input routing (frame-tagged input buffer).
- Save state and rewind ring.
- Determinism guarantees.
- Frame-boundary signaling.

The runtime does NOT own:

- Rendering or audio output (those live in the frontend SDK / apps).
- Any UI state.
- Direct introspection hooks (those live in tier 6).

### 11.2 Scheduling Strategy

v0.1 uses a **fixed-divider master clock scheduler**: a master tick advances all chips by their declared dividers. This is simple, deterministic, and correct for the v0.1 systems (C64, SMS, later Genesis). v0.2 MAY introduce slice-based scheduling for systems with dynamic clock ratios (Saturn, 32X).

### 11.3 Determinism Guarantees

Given:

- Identical manifest revision,
- Identical ROM SHA-256s,
- Identical save-state load point (or power-on),
- Identical input sequence (frame-tagged),

the runtime MUST produce identical output for: framebuffer contents at every frame, audio sample sequence, and save-state byte stream.

Determinism is validated in CI via golden frame-hash tests per system per known ROM.

### 11.4 Save State

Save states are taken at frame boundaries only in v0.1. Mid-frame save MAY be added in v0.2 if needed.

Save state contents:

- Header: magic, version, manifest ID, manifest revision, master cycle.
- Per-chip chunk: chip ID, chunk version, opaque chunk bytes.
- Per-bus chunk: ram regions, mapper state.

Chunks are framed with size prefixes; unknown chunks are skipped (forward-compat). Save states are zstd-compressed by default.

### 11.5 Rewind

A circular ring of save states (configurable depth, default 600 frames = 10 s at 60 Hz) is maintained as a background task. Rewind seeks the most recent state at or before the requested frame, then re-executes forward with the recorded input buffer.

Rewind cost is bounded by ring size and per-frame state delta size. v0.1 stores full states; v0.2 MAY add delta encoding.

---

## 12. Instrumentation API and Wire Protocol

### 12.1 In-Process API

Every chip exposes `ichip_introspection`. The runtime exposes `iruntime_introspection`. Both surfaces are pull-based for state queries and push-based (event subscription) for time-evolution events.

```cpp
namespace mnemos::instrumentation {

class iruntime_introspection {
public:
    virtual ~iruntime_introspection() = default;

    virtual std::uint64_t master_cycle() const noexcept = 0;
    virtual std::uint64_t frame_index() const noexcept = 0;

    virtual subscription_handle subscribe(event_filter, event_sink&) = 0;
    virtual void                unsubscribe(subscription_handle) noexcept = 0;

    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void step_frame() = 0;
    virtual void step_master_cycles(std::uint64_t) = 0;

    virtual std::expected<void, breakpoint_error>
        add_breakpoint(breakpoint_spec) = 0;
};

} // namespace mnemos::instrumentation
```

### 12.2 Wire Protocol

External clients (Python tooling, the developer frontend if run out-of-process, CI agents) speak the same surface via Cap'n Proto over a stable transport:

- Default transport: local domain socket on Linux, named pipe on Windows.
- TCP transport for remote sessions (deferred to v0.2).
- All messages versioned by schema revision; backward compatibility maintained within a major version.
- Schemas live in `src/instrumentation/wire/*.capnp` and generate C++ + Python bindings at build time.

### 12.3 Schema Versioning

Wire protocol uses semver. A breaking change requires a major bump and a deprecation window of one minor version. Generated client bindings include version-negotiation handshake.

---

## 13. Scripting Integration

### 13.1 Lua (In-Process)

- Lua 5.4 embedded via sol2.
- One Lua VM per attached session in the dev frontend.
- Lua scripts have access to the full in-process instrumentation API plus convenience helpers (breakpoint sugar, watch DSL, framebuffer accessors).
- Lua execution is sandboxed: no `os.execute`, no `io.popen`, restricted `require` (whitelist of trusted modules).
- Performance budget: Lua callbacks invoked from chip event taps MUST NOT exceed ~10 % of frame budget on reference hardware; the dev frontend warns above this.

### 13.2 Python (Out-of-Process)

- Python clients consume the Cap'n Proto wire protocol.
- A `mnemos-py` package (versioned with the wire schema) provides idiomatic bindings.
- Python is not embedded in any Mnemos process.
- Use cases: asset extraction, batch testing, research notebooks, agent orchestration, CI.

### 13.3 Why Not One Language

Lua wins in-process because of embedding footprint, sandboxability, per-frame performance, and genre familiarity. Python wins out-of-process because of its ecosystem (numpy, PIL, pandas) and researcher familiarity. Embedding Python would multiply the runtime footprint without commensurate benefit; exposing the wire protocol to Python is nearly free.

---

## 14. Frontend SDK Contract

The frontend SDK (tier 7) provides:

- A custom retained-mode UI toolkit (philosophy informed by MeGui/UIKit; codebase is independent).
- Vulkan-based renderer abstraction.
- Theme system.
- Asset loading (images, fonts, audio).
- Common widgets used by both player and dev apps.
- Input device abstraction (keyboard, mouse, gamepads — XInput on Win, evdev/SDL_gamepad on Linux).

The SDK is the only tier above the instrumentation API allowed to do platform graphics or audio.

Detailed SDK design is deferred to a separate v0.2 TDS (Mnemos Frontend SDK).

---

## 15. Save-State Format

```
+-------------------------------------+
| Header (uncompressed)               |
|   magic       : "MNMS" (4 bytes)    |
|   version     : u32 (semver-packed) |
|   manifest_id : varint-prefixed str |
|   manifest_rev: u32                 |
|   master_cycle: u64                 |
|   chunk_count : u32                 |
+-------------------------------------+
| Compressed payload (zstd)           |
|   for each chunk:                   |
|     chunk_id     : varint str       |
|     chunk_version: u32              |
|     chunk_size   : u64              |
|     chunk_data   : bytes            |
+-------------------------------------+
| Trailing CRC32 (uncompressed)       |
+-------------------------------------+
```

- `version` is the format version, not the manifest version.
- Unknown chunks at load time are skipped, not errored — enables forward compatibility within a major version.
- A breaking save-state format change requires major version bump and an explicit migration tool.

---

## 16. Determinism and Replay Guarantees

### 16.1 Determinism Sources

The runtime's determinism rests on:

1. Chip implementations being pure functions of (state, input). No undefined behavior, no uninitialized memory, no platform-dependent floating-point modes.
2. The scheduler being deterministic given the manifest and master clock.
3. Input being frame-tagged and replayed verbatim.

### 16.2 Replay Format

A replay is: (save state at frame F) + (input log from frame F onward) + (manifest reference + ROM hashes). Replays are portable across platforms and Mnemos versions within a major version.

### 16.3 Validation

CI runs golden-frame regression tests per known ROM: boot the system, run N frames with recorded inputs, hash the framebuffer at chosen checkpoints, compare against committed hashes. A divergence fails the build.

---

## 17. Netplay Architecture (Sketch)

Detailed netplay design is deferred to a separate v0.2 TDS. v0.1 commits to the following architectural prerequisites:

- Frame-tagged input subsystem (delivered with M3 — see project plan).
- State save/restore at frame boundaries (delivered with runtime).
- Stable hash of cross-client state (introduced as an introspection capability).

These three properties together enable both rollback netplay and lockstep fallback without further runtime changes. The netplay layer itself (matchmaking, NAT traversal, input prediction, transport) is built on top of the instrumentation API.

---

## 18. Testing Strategy

Three test tiers, all required to be green in CI:

1. **Unit tests** — per-chip, per-tier. Catch2 v3. Cover instruction-set conformance, register behavior, edge cases.
2. **Integration tests** — system-level. Boot a manifest, run N frames, assert state invariants.
3. **Golden frame tests** — per known ROM. Boot, run recorded inputs, hash framebuffer at checkpoints, compare against committed hashes.

A CPU implementation MUST pass its public conformance test suite (see `THIRD-PARTY-REFERENCES.md` for the corpora used) before its containing system is considered viable.

Sanitizer builds (ASan, UBSan, TSan) run nightly.

---

## 19. C64 Reference Component Graph

The C64 is the first system. Its component graph validates the chip library, topology, manifest, and runtime contracts.

```
                  +--------------+
                  |   manifest   |
                  |   c64.pal    |
                  +------+-------+
                         |
            +------------+------------+
            |            |            |
       +----v----+  +----v----+  +----v----+
       |  cpu    |  | video   |  | audio   |
       | m6510   |  | vic_ii  |  | sid     |
       |         |  | 6569    |  | 6581    |
       +----+----+  +----+----+  +----+----+
            |            |            |
            +------+-----+-----+------+
                   |           |
              +----v----+ +----v----+
              |  cia1   | |  cia2   |
              |  6526   | |  6526   |
              +----+----+ +----+----+
                   |           |
                   +-----+-----+
                         |
                    +----v----+
                    |  bus    |
                    |  main   |
                    +----+----+
                         |
       +-------+---------+---------+--------+
       |       |         |         |        |
   +---v--+ +--v---+ +---v---+ +---v---+ +--v---+
   | RAM  | |BASIC | |KERNAL | | CHAR  | | PLA  |
   |64 KiB| | ROM  | |  ROM  | |  ROM  | |bank  |
   +------+ +------+ +-------+ +-------+ +------+
```

Chips reused later:

- `mos.6510` — directly reusable in C128 (with 8502 variant).
- `mos.cia.6526` — reusable in C128, C16, drive controllers.
- `mos.sid.6581` and `mos.sid.8580` — reusable across the entire Commodore line.

Chips that establish patterns:

- `mos.vic_ii.6569` validates the video chip interface that VDP chips will later implement.
- `mos.6510` validates the CPU interface and bus interaction patterns.
- `mos.cia.6526` validates the bus-controller class and timer/IRQ semantics.

---

## 20. Open Questions Deferred to v0.2

These are explicit non-decisions for v0.1. Listing them prevents scope creep and documents what needs review next.

1. **Frontend SDK detailed design** — UI toolkit API, theming primitives, asset loader API, renderer abstraction surface.
2. **Player frontend UX specification** — library model, scraping pipeline, per-game profiles, couch/desk mode, session journal, achievements.
3. **Developer frontend UX specification** — panel taxonomy, dock layout, frame timeline UI, scripting console.
4. **Netplay protocol specification** — rollback algorithm, transport, matchmaking, NAT, lobby.
5. **Slice-based scheduler** — for systems with dynamic clock ratios (Saturn, 32X).
6. **Mid-frame save state** — if needed for advanced rewind / TAS work.
7. **LuaJIT opt-in** — if Lua scripting performance becomes a measurable bottleneck.
8. **C++ modules adoption** — when CMake support is broadly stable.
9. **macOS, ARM64, console targets** — beyond v0.1 platform scope.
10. **Asset pipeline conventions** — ROM library management, naming, metadata sources.

---

**End of Mnemos Architecture TDS v0.1.**
