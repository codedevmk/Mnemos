# Multi-System Architectural Decoupling Plan

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

- [ ] Read `src/chips/shared/chip.hpp` and `src/instrumentation/api/` to confirm tier-6 location and existing namespace conventions.
- [ ] Define `memory_view` interface (name + byte span + optional word-endian hint) in `instrumentation/api/introspection_views.hpp`.
- [ ] Define `register_view` interface (returns `span<const register_descriptor>`).
- [ ] Define `trace_target` interface (install/uninstall callback).
- [ ] Define `debug_layer` interface (name + `frame_buffer_view`).
- [ ] Promote `ichip_introspection` (currently empty marker at `chip.hpp:7-15`) to capability container with four default-empty accessors. Existing chips compile unchanged.
- [ ] Write `introspection_views_test.cpp` with a minimal mock chip exposing each capability; assert the accessors return the expected values. Tests-first: this should fail at link time before retrofits.
- [ ] Retrofit `m68000`: introspection now exposes `m68000_diagnostics` as `trace_target` (the install/uninstall maps to existing `set_trace_callback`) and `register_snapshot` as `register_view`.
- [ ] Retrofit `genesis_vdp`: introspection exposes `vram_`, `cram_`, `vsram_`, `reg_` as `memory_view`s; plane A framebuffer as a `debug_layer`. Plane A computation already exists for the screenshot path — wire the existing helper.
- [ ] Run `ctest --preset windows-msvc-debug`; ensure all 47 pre-existing tests + the new introspection test pass.
- [ ] Run `clang-format --dry-run --Werror` on touched files.
- [ ] Commit: scope-prefix `instrumentation:` per recent commit style.
- [ ] Confirm with Marius before moving to Item 1.

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

- [ ] Add `chips()` + `chip(id)` virtuals to `player_system`, default empty span / nullptr.
- [ ] Implement `chips()` in genesis_adapter, sms_adapter, c64_adapter — returning the system's chips in scheduler order.
- [ ] Write `debug_dump.hpp` with the two free helpers. Implement them using only `player_system` + the introspection surface (memory_view, debug_layer, trace_target).
- [ ] Write `debug_dump_test.cpp` covering: framebuffer-PPM path, per-chip memory dump for >=2 memory views, debug layer PPM, trace install/uninstall.
- [ ] Rewrite `apps/player/main.cpp` `--screenshot` block: replace the Genesis cast + custom dumps with a single call into `debug_dump`. Remove both `dynamic_cast<genesis_adapter*>` sites.
- [ ] Regression: re-run the parity harness from session 2026-05-27 against a pixel-perfect title (BoV at f=120) — output must still be 0/215040.
- [ ] Run ctest, clang-format, commit (`apps/player: route --screenshot through player_system introspection`).
- [ ] Confirm with Marius.

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

- [ ] Define `adapter_registry` interface: `register_family`, `create`, `families()`. Singleton accessor.
- [ ] Write `adapter_registry_test.cpp` first; assert registration + lookup + duplicate-family error path.
- [ ] Implement the registry. Use `std::unordered_map<std::string, factory>` keyed by family name. Mutex for thread-safe static-init registration.
- [ ] Convert each adapter library target to OBJECT in CMakeLists. Verify the build still links and behaves correctly.
- [ ] Add self-registration block at the bottom of each adapter's primary `.cpp` file.
- [ ] Rewrite main.cpp's adapter dispatch as a registry lookup. Adapter selection now: `detect_family(rom)` → `registry.create(family, rom, opts)`.
- [ ] Regression: run all 47 tests + the player harness on at least one title per family (Genesis, SMS, C64) — must still match prior behavior.
- [ ] Commit (`frontend_sdk: self-registering adapter registry`).
- [ ] Confirm with Marius.

---

## Item 4A — Scheduler Factory Injection

### File Structure

- `src/frontend_sdk/scheduler_factory.hpp` + `.cpp` — **create**: `scheduler_factory` interface + `default_scheduler_factory` that wraps current `runtime::scheduler`.
- `src/apps/player/adapters/genesis/genesis_adapter.hpp` + `.cpp` — **modify**: accept optional `scheduler_factory*` ctor param; null → default.
- `src/apps/player/adapters/sms/sms_adapter.hpp` + `.cpp` — **modify**: same.
- `src/apps/player/adapters/c64/c64_adapter.hpp` + `.cpp` — **modify**: same.
- `src/frontend_sdk/tests/scheduler_factory_test.cpp` — **create**: substitute a mock factory; verify its `create()` is called with the expected chip vector.

### Steps

- [ ] Define `scheduler_factory` interface (`create(chips, frame_source) -> unique_ptr<runtime::scheduler>`) + `default_scheduler_factory` impl.
- [ ] Write `scheduler_factory_test.cpp` exercising the substitution path.
- [ ] Update each adapter ctor to accept `scheduler_factory*` (default null) and route construction through it.
- [ ] Run all 47 tests; behavior must be unchanged when factory is null (default path).
- [ ] Commit (`frontend_sdk: inject scheduler via factory interface`).
- [ ] Confirm with Marius.

---

## Out-of-Scope (future)

- **Item 4B — slice-based multi-clock scheduler.** Needed for 32X / Saturn / Sega CD; gate on actual target-system progress.
- **Family detection registry.** Currently family detection is a small switch in `apps/player/adapters/common/`; promoting it to a registry is similarly low-value until a system collides with existing magic-number heuristics.
- **CD-ROM / floppy / arcade-ROM bundle abstractions.** When Sega CD or Saturn or Amiga or CPS enter scope.

---

## Verification Across Plan

- Every existing test passes at every step. Pre-plan baseline: 47/47 pass, 7 skipped (data-gated / opt-in conformance).
- Every commit individually green-builds and green-tests on `windows-msvc-debug`.
- Genesis pixel-perfect set ([[genesis-four-title-parity]]: BoV, Sonic 1+2, SoR, AB, TF3) must remain 0/215040 at f=120 — re-run after item 1 (where main.cpp's screenshot path changes) and after item 4A (where scheduler construction shifts).
