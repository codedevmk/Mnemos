# Multi-System Architectural Decoupling Plan

> **Status (2026-05-27):** Items 2 → 1 → 3 → 4A all landed and verified in a single session. Commits `573b361` → `509c92a` → `ce5c575` → `ad130af`. 48/48 ctest entries pass on `windows-msvc-debug`. BoV at f=120 still 0/215040 vs the reference after each commit. See per-item Outcome blocks below; the high-level deltas from the original plan are summarised in the "Deltas From Plan" section at the bottom.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple the emulation core from the player frontend so adding a system (target roster: SMS, Genesis, 32X, Sega CD, Saturn, C64, Amiga 500, CPS1, CPS2) requires no edits to `apps/player/main.cpp`. Foundation only — does not include the chip/manifest work each new system itself needs.

**Architecture:** Four layered foundation changes, executed in dependency order:

1. **Item 2 — Introspection escape hatch.** Promote `ichip::introspection()` from marker class to capability container. Each chip advertises memory views, register views, optional trace target, optional debug layers. CPUs get the trace target (m68000's `m68000_diagnostics` from session 2026-05-27 maps cleanly). VDPs get memory views + debug layers (Genesis VDP: VRAM/CRAM/VSRAM/regs + plane A layer).
2. **Item 1 — `player_system` debug enumerator.** Add `chips()` + `chip(id)` to the SDK. Build system-agnostic `apps/player/debug_dump.cpp` that walks the chip list and calls introspection methods. Rewrite the Genesis-specific `--screenshot` block in `main.cpp` to use only the SDK; eliminate the two `dynamic_cast<genesis_adapter*>` sites.
3. **Item 3 — Adapter registry.** `frontend_sdk/adapter_registry.hpp` with static-init self-registration in each adapter source. CMake change: adapter targets become OBJECT libraries so static-init survives the linker. main.cpp's switch becomes a registry lookup.
4. **Item 4A — Scheduler factory injection.** `scheduler_factory` interface; each adapter accepts an optional factory (default = current `runtime::scheduler`). Enables tool/test substitution without changing existing behavior.

Item 4B (slice-based multi-clock scheduler) is OUT OF SCOPE for this plan; it lands when 32X / Saturn / Sega CD enter active development.

**Tech Stack:** C++23, CMake 3.28+/Ninja, Catch2 v3 (`mnemos_add_test`), MSVC `/W4 /WX` + GCC/Clang `-Wall -Wextra -Wpedantic -Werror`.

**Conventions to follow:**
- Flat module layout: headers beside sources, basename-only includes, globally-unique names ([[mnemos-flat-module-layout]]).
- 4-space indentation inside namespaces; `#pragma once`; `[[nodiscard]]` on pure accessors; non-copyable interface pattern.
- Each item ends green: build, run `ctest`, run `clang-format --dry-run --Werror`, commit. Confirm result with Marius before moving to next item (CLAUDE.md "ONE change at a time").
- Windows build prelude for every `cmake`/`ctest` invocation:
  ```powershell
  cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" >NUL && <command>"
  ```
- Tests-first per item where structural: write the catch2 case that exercises the new surface, watch it fail, implement, watch it pass.

---

## Reference Material

- Session 2026-05-27 (this plan's origin): identified the 4 decoupling debts via `apps/player/main.cpp:234,283` Genesis casts, the empty `ichip_introspection` marker, the switch-based adapter selection, and the in-adapter scheduler construction.
- [[mnemos-flat-module-layout]] — module layout invariants.
- [[genesis-four-title-parity]] — current parity baseline; verification target for "no behavior regression" checks.

---

## Item 2 — Introspection Escape Hatch

### File Structure

- `src/instrumentation/api/introspection_views.hpp` — **create**: define `memory_view`, `register_view`, `trace_target`, `debug_layer` capability interfaces.
- `src/instrumentation/api/CMakeLists.txt` — **modify**: install the new header.
- `src/chips/shared/chip.hpp` — **modify**: extend `ichip_introspection` with the four capability accessors (default-empty so existing chips compile unchanged).
- `src/chips/cpu/m68000/m68000.hpp` + `.cpp` — **modify**: introspection now exposes `m68000_diagnostics` as `trace_target` and `register_snapshot` as `register_view`. The `introspection_surface` inner class grows accordingly.
- `src/chips/video/genesis_vdp/genesis_vdp.hpp` + `.cpp` — **modify**: introspection exposes `vram`/`cram`/`vsram`/`regs` as memory views, plane A as a debug layer.
- `src/instrumentation/api/tests/introspection_views_test.cpp` — **create**: catch2 case exercising a mock chip implementing each capability.

### Steps

- [x] Read `src/chips/shared/chip.hpp` and `src/instrumentation/api/` to confirm tier-6 location and existing namespace conventions.
- [x] Define `memory_view` interface (name + byte span; word-endian hint deferred -- not needed yet) in `src/chips/shared/introspection_views.hpp` (tier 2, NOT tier 6 -- see Deltas).
- [x] Define `register_view` interface (returns `span<const register_descriptor>`).
- [x] Define `trace_target` interface (install/uninstall callback; install with empty callback clears).
- [x] Define `debug_layer` interface (name + `frame_buffer_view`).
- [x] Promote `ichip_introspection` (previously empty marker at `chip.hpp:7-15`) to capability container with four default-empty accessors. Existing chips compile unchanged.
- [x] Write `introspection_views_test.cpp` (in `src/chips/shared/tests/`) with a minimal mock chip exposing each capability; 5 cases / 29 assertions.
- [x] Retrofit `m68000`: introspection now exposes `m68000_diagnostics` as `trace_target` (the install bridges PC-only into `pc+cycles` trace_event) and `register_snapshot` as `register_view`.
- [x] Retrofit `genesis_vdp`: introspection exposes `vram_`, `cram_`, `vsram_`, `reg_` as `memory_view`s; plane A as a `debug_layer` (renderer ported from `apps/player/main.cpp`'s inline helper, now using the chip's own `cram_to_rgb` LUT).
- [x] Run `ctest --preset windows-msvc-debug`; 47/47 pre-existing + the new introspection test pass.
- [x] Commit: scope-prefix `instrumentation:` per recent commit style.
- [x] Confirm with Marius before moving to Item 1.

### Outcome

- Commit: **`573b361`** — `instrumentation: promote ichip_introspection to capability container`
- New files: `src/chips/shared/introspection_views.hpp`, `src/chips/shared/tests/introspection_views_test.cpp`
- 47/47 tests pass; +5 new test cases (29 assertions) for the four capability sub-interfaces.

---

## Item 1 — `player_system` Debug Enumerator

### File Structure

- `src/frontend_sdk/player_system.hpp` — **modify**: add `chips()` + `chip(id)` virtuals with default empty impl.
- `src/frontend_sdk/CMakeLists.txt` — no change expected.
- `src/apps/player/adapters/genesis/genesis_adapter.hpp` + `.cpp` — **modify**: implement `chips()` returning the chip list in scheduler order.
- `src/apps/player/adapters/sms/sms_adapter.hpp` + `.cpp` — **modify**: same.
- `src/apps/player/adapters/c64/c64_adapter.hpp` + `.cpp` — **modify**: same.
- `src/apps/player/debug_dump.hpp` + `.cpp` — **create**: `dump_screenshot_artifacts(player_system, base_path)` and `install_screenshot_trace(player_system, csv_path)`. System-agnostic, uses only the SDK + introspection.
- `src/apps/player/main.cpp` — **modify**: replace the Genesis-specific `--screenshot` block at lines 225-310 with calls into `debug_dump`. Delete the two `dynamic_cast<genesis_adapter*>` sites.
- `src/apps/player/tests/debug_dump_test.cpp` — **create**: exercise debug_dump against a mock `player_system` whose chips expose introspection.

### Steps

- [x] Add `chips()` + `chip(id)` virtuals to `player_system`, default empty span / nullptr.
- [x] Implement `chips()` in genesis_adapter, sms_adapter — returning the system's chips in scheduler order. **(C64 adapter doesn't exist yet; deferred.)**
- [x] Write `debug_dump.hpp` with `dump_screenshot_artifacts()` (free function) + `trace_csv_session` (RAII handle). Both system-agnostic via the introspection surface.
- [x] Write `debug_dump_test.cpp` covering framebuffer-PPM, per-chip sidecars, trace install/uninstall round-trip, and the no-traceable-chip path. 3 cases / 13 assertions.
- [x] Rewrite `apps/player/main.cpp` `--screenshot` block: replaced the Genesis cast + 4 inline helpers (`dump_framebuffer_ppm`, `dump_plane_a_ppm`, `cram_entry_to_rgb`, `scroll_size_cells`) with a single call into `debug_dump`. Both `dynamic_cast<genesis_adapter*>` sites removed. Net main.cpp diff: -190 lines.
- [x] Regression: BoV at f=120 byte-perfect 0/215040 vs the reference.
- [x] Run ctest, commit (`apps/player: route --screenshot through player_system introspection`).
- [x] Confirm with Marius.

### Outcome

- Commit: **`509c92a`** — `apps/player: route --screenshot through player_system introspection`
- New files: `src/apps/player/debug_dump.{hpp,cpp}`, `src/apps/player/tests/debug_dump_test.cpp`
- 48/48 tests pass; +3 new test cases (13 assertions). BoV f=120 byte-perfect.
- **Known regression to flag for future work:** the `r,z,i` (m68000 cycle-sources) columns in the trace CSV are gone; the `[vdp]` / `[sched]` diagnostic log lines in `--screenshot` stderr output are gone. The underlying data is still queryable (`m68000_diagnostics::last_cycle_sources()`, VDP introspection memory_views). Re-add via a chip-specific `trace_extra` sub-interface (proposed in the "Out-of-Scope" section below) when next needed for cycle parity work.

---

## Item 3 — Adapter Registry

### File Structure

- `src/frontend_sdk/adapter_registry.hpp` + `.cpp` — **create**: singleton `adapter_registry` with `register_family(name, factory)` and `create(name, rom, options)`.
- `src/frontend_sdk/CMakeLists.txt` — **modify**: include the new sources.
- `src/apps/player/adapters/genesis/genesis_adapter.cpp` — **modify**: append anonymous-namespace self-registration block.
- `src/apps/player/adapters/sms/sms_adapter.cpp` — **modify**: same.
- `src/apps/player/adapters/c64/c64_adapter.cpp` — **modify**: same.
- `src/apps/player/adapters/genesis/CMakeLists.txt`, `sms/CMakeLists.txt`, `c64/CMakeLists.txt` — **modify**: change `add_library(... STATIC)` to `OBJECT` library so static-init objects survive linking. Update parent target_link_libraries accordingly.
- `src/apps/player/CMakeLists.txt` — **modify**: link the adapter OBJECT libraries directly.
- `src/apps/player/main.cpp` — **modify**: replace adapter-selection switch with `adapter_registry::instance().create(family, rom, opts)`.
- `src/frontend_sdk/tests/adapter_registry_test.cpp` — **create**: register two mock adapters, look them up by family, verify factory invocation.

### Steps

- [x] Define `adapter_registry` interface: `register_family`, `create`, `families()`. Singleton accessor.
- [x] Write `adapter_registry_test.cpp`; 4 cases / 11 assertions covering register, create, unknown-family, re-registration, and sorted `families()` listing.
- [x] Implement the registry. `std::unordered_map<std::string, factory>` keyed by family name; mutex for thread-safe registration. `frontend_sdk` converted from INTERFACE to STATIC lib to host the singleton.
- [x] **(Deviation from plan)** Did NOT convert adapter targets to OBJECT libraries — used `force_link()` pattern instead. More portable, explicit, avoids OBJECT-lib transitive-link quirks. See Deltas section.
- [x] Add self-registration block at the bottom of each adapter's primary `.cpp` (`genesis` + `sms`).
- [x] Rewrite main.cpp's adapter dispatch as a registry lookup. Adapter selection now: `detect_family(rom)` → `registry.create(family_id_string, options)`. `force_link()` calls keep adapter TUs alive.
- [x] Regression: BoV at f=120 byte-perfect 0/215040; all 48 tests pass.
- [x] Commit (`frontend_sdk: self-registering adapter registry`).
- [x] Confirm with Marius.

### Outcome

- Commit: **`ce5c575`** — `frontend_sdk: self-registering adapter registry`
- New files: `src/frontend_sdk/adapter_registry.{hpp,cpp}`, `src/frontend_sdk/tests/adapter_registry_test.cpp`
- 48/48 tests pass; +4 new test cases (11 assertions). BoV f=120 byte-perfect.
- **Adding a new system from now on:** drop a new adapter directory + add one `force_link()` call in `main.cpp` + register the family string inside the new adapter. main.cpp no longer names a concrete adapter type.

---

## Item 4A — Scheduler Factory Injection

### File Structure

- `src/frontend_sdk/scheduler_factory.hpp` + `.cpp` — **create**: `scheduler_factory` interface + `default_scheduler_factory` that wraps current `runtime::scheduler`.
- `src/apps/player/adapters/genesis/genesis_adapter.hpp` + `.cpp` — **modify**: accept optional `scheduler_factory*` ctor param; null → default.
- `src/apps/player/adapters/sms/sms_adapter.hpp` + `.cpp` — **modify**: same.
- `src/apps/player/adapters/c64/c64_adapter.hpp` + `.cpp` — **modify**: same.
- `src/frontend_sdk/tests/scheduler_factory_test.cpp` — **create**: substitute a mock factory; verify its `create()` is called with the expected chip vector.

### Steps

- [x] Define `scheduler_factory` interface (`create(chips, frame_source) -> runtime::scheduler` by VALUE, not unique_ptr -- see Deltas) + `default_scheduler_factory` impl.
- [x] Write `scheduler_factory_test.cpp` exercising the default path + a recording-mock substitution. 2 cases / 5 assertions.
- [x] Update each adapter ctor to accept `scheduler_factory*` (default null) and route construction through a `make_scheduler` helper.
- [x] Extend `adapter_options` with an optional `scheduler_factory*` field so the registry forwards it through.
- [x] BoV at f=120 byte-perfect 0/215040; all 48 ctest entries pass (frontend_sdk_test now 7 cases / 37 assertions across all SDK functionality).
- [x] Commit (`frontend_sdk: inject scheduler via factory interface`).
- [x] Confirm with Marius.

### Outcome

- Commit: **`ad130af`** — `frontend_sdk: inject scheduler via factory interface`
- New files: `src/frontend_sdk/scheduler_factory.{hpp,cpp}`, `src/frontend_sdk/tests/scheduler_factory_test.cpp`
- Default path unchanged; substitution available for tooling. Item 4B (slice-based multi-clock scheduler) drops in as a second factory implementation when 32X / Saturn / Sega CD demand it.

---

## Deltas From Plan

These are the places the implementation diverged from the plan as written. Captured so future plans calibrate against reality.

1. **Item 2 — interfaces are tier 2, not tier 6.** Plan said `src/instrumentation/api/introspection_views.hpp`. Reality: capability interfaces have to be in `src/chips/shared/introspection_views.hpp` because tier-2 chips need to *inherit* from them when implementing capabilities, and a tier-2 chip can't depend on a tier-6 header. Same reason `ichip_introspection` was originally declared in `chip.hpp` despite living in the `mnemos::instrumentation` namespace.
2. **Item 1 — no C64 adapter retrofitted.** Plan listed `c64_adapter.{hpp,cpp}` as needing the `chips()` impl. Reality: the C64 adapter doesn't exist yet in `src/apps/player/adapters/`. When it lands, it'll need the standard `chips()` override.
3. **Item 1 — `r,z,i` cycle-source CSV columns dropped.** The previous Genesis-specific trace CSV had `frame,inst,pc,cycles,r,z,i` where the last three were m68000-specific cycle-source counters. The generic `trace_event` carries only `pc + cycles`. Re-add via a `trace_extra` sub-interface (see Out-of-Scope) when a future parity investigation needs the breakdown.
4. **Item 1 — Genesis `[vdp]/[sched]` diagnostic log lines dropped from `--screenshot`.** The previous main.cpp printed Genesis-specific VDP register state + scheduler vs CPU cycle delta to stderr after each screenshot. Removed with the cast. Re-add via a generic chip introspection log helper if/when needed.
5. **Item 3 — `force_link()` pattern, not OBJECT libraries.** Plan said convert adapter targets to OBJECT libs so static-init survives the linker. Reality: implemented `force_link()` no-ops in each adapter that main.cpp calls once. More portable across MSVC/GCC/Clang, explicit at the call site, no CMake gymnastics. Trade-off: main.cpp grows one line per system supported (currently 2 lines). Not OBJECT-lib transparency, but worth it for portability.
6. **Item 3 — Registry shares one TU with `frontend_sdk` instead of being its own lib.** Plan said `adapter_registry.{hpp,cpp}` as new files (correct) but the implementation lives inside the now-STATIC `mnemos_frontend_sdk` target alongside `scheduler_factory.cpp`. The plan implicitly assumed `frontend_sdk` was already a STATIC lib; it was actually INTERFACE-only, so promoting it to STATIC was an extra plan step.
7. **Item 4A — `scheduler_factory::create` returns `runtime::scheduler` by value, not `unique_ptr`.** Plan suggested `unique_ptr<runtime::scheduler>`. Reality: `runtime::scheduler` is movable, and the adapters store the scheduler as a value member, so by-value avoids an unnecessary heap allocation per system construction. No semantic difference for substitution.

---

## Out-of-Scope (future)

- **Item 4B — slice-based multi-clock scheduler.** Needed for 32X / Saturn / Sega CD; gate on actual target-system progress. Drops in as a second `scheduler_factory` impl.
- **Family detection registry.** Currently family detection is a small switch in `apps/player/adapters/common/`; promoting it to a registry is similarly low-value until a system collides with existing magic-number heuristics.
- **CD-ROM / floppy / arcade-ROM bundle abstractions.** When Sega CD or Saturn or Amiga or CPS enter scope.
- **`trace_extra` capability sub-interface.** A chip can override `ichip_introspection::trace_extra()` to surface per-instruction columns beyond the generic `pc + cycles`. The m68000 would expose `r,z,i` (refresh fired / Z80 bus accesses / IRQ entered) so the generic `--screenshot` trace_csv_session re-emits the columns that today's Genesis-only path dropped. Recommended when the next cycle-parity investigation needs the breakdown.
- **Generic chip introspection log helper.** Re-add the `[vdp]/[sched]` style diagnostic stderr output via a system-agnostic "dump register_view + memory_view summary" helper that any system can call from `--screenshot`.

---

## Verification Across Plan

- Every existing test passes at every step. Pre-plan baseline: 47/47 pass, 7 skipped (data-gated / opt-in conformance).
- Every commit individually green-builds and green-tests on `windows-msvc-debug`.
- Genesis pixel-perfect set ([[genesis-four-title-parity]]: BoV, Sonic 1+2, SoR, AB, TF3) must remain 0/215040 at f=120 — re-run after item 1 (where main.cpp's screenshot path changes) and after item 4A (where scheduler construction shifts).
