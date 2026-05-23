# Mnemos — Todos

**Version:** 0.1 (initial draft)
**Status:** Draft, awaiting review
**Companion documents:** `mnemos-architecture-tds-v0.1.md`, `mnemos-project-plan-v0.1.md`

---

## How to Use This File

- Tasks are grouped by milestone (M0 through M10).
- M0–M3 are itemized to actionable granularity.
- M4–M10 are itemized to scoping granularity; each will be expanded to actionable items at the start of its milestone.
- A task is "done" when its acceptance criterion is met and its work has landed in a green CI run.
- Tasks are intentionally not assigned. Assignment happens in your task tracker, not in this file.
- When a task is completed, change `[ ]` to `[x]` and add a short reference to the PR or commit.

---

## M0 — Workspace and Toolchain Bring-Up

### Repository setup
- [x] Initialize monorepo `mnemos/` with the directory layout from TDS §5.
- [x] Add `LICENSE` (Apache-2.0) at root.
- [x] Add `LICENSE-chips` (MIT) at root.
- [x] Add `THIRD_PARTY_NOTICES.md` (empty template).
- [x] Add `README.md` with one-paragraph vision blurb and pointer to docs.
- [x] Add `CONTRIBUTING.md` (placeholder; populated by M9).
- [x] Add `CODE_OF_CONDUCT.md`.
- [x] Add `SECURITY.md` (placeholder).

### Source control hygiene
- [x] Add `.gitignore` that rejects `build/`, `*.log`, `*.bin` (firmware), IDE caches, vcpkg/Conan artifacts.
- [x] Add `.gitattributes` (line endings, binary classifications).
- [x] Add `.editorconfig`.

### Formatting and linting
- [x] Add `.clang-format` (LLVM base + project overrides; column limit, brace style decided here).
- [x] Add `.clang-tidy` with curated check list (modernize-*, bugprone-*, performance-*, readability-* subset).
- [x] Add pre-commit hook script (`tools/git-hooks/pre-commit`) running clang-format dry-run.
- [x] Add `tools/install-hooks.sh` and `tools/install-hooks.ps1`.

### Build system
- [x] Author root `CMakeLists.txt` with project declaration, C++23 baseline, global warning flags.
- [x] Author `CMakePresets.json` covering all eight presets from TDS §6.3.
- [x] Author `cmake/modules/MnemosDeclareTier.cmake` exposing `mnemos_declare_tier()`.
- [x] Author `cmake/modules/MnemosCompilerFlags.cmake` with the per-platform flag bundle from TDS §6.4.
- [x] Author `cmake/modules/MnemosTesting.cmake` providing `mnemos_add_test()`.
- [x] Enforce out-of-source builds at root CMakeLists.
- [x] Add empty `extern/` with `README.md` documenting FetchContent usage convention.

### Foundation skeleton
- [x] Create `src/foundation/CMakeLists.txt` declaring `mnemos::foundation` as a header-only target with one no-op header.
- [x] Add one trivial unit test under `src/foundation/tests/` proving Catch2 wiring works.
- [x] FetchContent integration for Catch2 v3 in `cmake/modules/`.

### Continuous Integration
- [x] Choose CI provider (GitHub Actions is the default; deviation requires an ADR).
- [x] Add `.github/workflows/ci.yml` with matrix: {windows-latest, ubuntu-24.04} × {Debug, Release}.
- [x] CI runs configure + build + test for each preset.
- [x] CI fails on any compiler warning.
- [x] CI runs `clang-format --dry-run` and fails on diff.
- [x] Add CI status badge to `README.md`.

### Documentation seed
- [x] Create `docs/architecture/mnemos-architecture-tds-v0.1.md` (this TDS, committed verbatim).
- [x] Create `docs/architecture/mnemos-project-plan-v0.1.md` (this plan, committed verbatim).
- [x] Create `docs/architecture/mnemos-todos-v0.1.md` (this file, committed verbatim).
- [x] Create `docs/adr/0001-monorepo-layout.md` recording the layout decision.
- [x] Create `docs/adr/0002-cmake-and-toolchain.md` recording build system choices.
- [x] Create `docs/adr/0003-license-split.md` recording Apache+MIT rationale.

### Acceptance
- [x] A "trivial change" PR (touching one comment) passes the full CI matrix on both platforms with zero warnings. (fad647b: CI run 26275205022 green across all 7 jobs on windows-latest + ubuntu-24.04, incl. clang-format gate)

---

## M1 — Foundation Library and First Chip

### Foundation library
- [x] Implement `foundation::time` (steady_clock wrappers, frame-time helpers). (this commit: windows-msvc-debug configure/build/test green)
- [x] Implement `foundation::log` (leveled, structured, sinks). (d00f7e3 + de38104; CI run 26277600708 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::bits` (popcount, clz, byte order, bitfield helpers). (e0ca6d2 + 8ed9bd7; CI run 26278425815 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::span_ext`, `foundation::expected_ext` (light extensions to std). (1cc1c1d + c5d71b6; CI run 26279089811 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::fs` (filesystem facade with platform-safe path handling). (5ecbe01; CI run 26279447600 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::thread` (jthread wrappers, latches, signals — std-based). (f04befe; CI run 26279663407 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::allocator` (arena, pool — minimal v0.1 surface). (13cbec5 + ce90a3e; CI run 26280113028 green across clang-format and all 6 build/test jobs)
- [x] Implement `foundation::id` (compile-time string IDs / fnv1a hashing for chip IDs). (7ac85be; CI run 26280299754 green across clang-format and all 6 build/test jobs)
- [x] Unit tests for every module above with ≥ 75 % branch coverage. (1d20324; CI run 26280879101 green across clang-format, foundation coverage, and all 6 build/test jobs)

### Chip taxonomy and base interfaces
- [x] Create `src/chips/common/` library target `mnemos::chips::common`. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `chip_class` enum. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `chip_metadata`, `reset_kind`, `register_descriptor` types. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `i_chip` interface (TDS §8.2). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define specialized interfaces: `i_cpu`, `i_audio_synth`, `i_video`, `i_bus_controller`, `i_storage`, `i_mapper`, `i_peripheral`. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Define `i_chip_introspection` interface (forward-decl OK; full def in M4). (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Implement chip factory registry with static-init registration pattern. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)
- [x] Unit tests for taxonomy + registration. (e5e7fe3; CI run 26284448382 green across clang-format and all 6 build/test jobs)

### MOS 6510 implementation
- [x] Create `src/chips/cpu/m6510/` library target `mnemos::chips::cpu::m6510`. (72e4a6e; CI run 26320527793 green across clang-format and all 6 build/test jobs)
- [ ] Implement registers, flags, addressing modes. (registers + status flags done in 72e4a6e; addressing-mode enum defined, per-mode effective-address resolution lands with opcode execution)
- [ ] Implement all 151 documented opcodes with cycle counts.
- [ ] Implement all undocumented (illegal) opcodes the C64 software relies on.
- [ ] Implement decimal mode arithmetic.
- [ ] Implement IRQ, NMI, RES handling with correct cycle semantics.
- [x] Implement the 6510 I/O port at addresses $00/$01. (8bb1bda; CI run 26320828290 green across clang-format and all 6 build/test jobs)
- [ ] Implement save / load state.
- [ ] Implement introspection (register snapshot, PC, cycle counter, instruction event tap).
- [x] Register factory under `"mos.6510"`. (72e4a6e; CI run 26320527793 green across clang-format and all 6 build/test jobs)

### Test infrastructure for 6502 family
- [ ] Vendor or fetch Klaus 2M65 functional test ROM with provenance documented.
- [ ] Vendor or fetch decimal mode test.
- [ ] Vendor or fetch undocumented opcode test suite.
- [ ] Author `m6510_conformance_test` integrating all of the above; runs in CI.
- [ ] Author per-opcode microtests (cycle count, flag updates) for at least the trickiest 20 opcodes.

### Acceptance
- [ ] `m6510_conformance_test` passes in CI on all four matrix combinations (Win/Linux × Debug/Release).
- [ ] Zero warnings, zero sanitizer hits when run under ASan+UBSan on Linux Clang.
- [ ] ADR `docs/adr/0004-chip-contract.md` records the contract decisions made during this milestone.

---

## M2 — Chip Library Expansion (C64 Set)

### VIC-II 6569
- [ ] Create `src/chips/video/vic_ii_6569/` library target.
- [ ] Implement raster engine (PAL: 312 lines × 63 cycles).
- [ ] Implement all graphics modes (standard text, multicolor text, standard bitmap, multicolor bitmap, extended color text).
- [ ] Implement sprite engine (8 sprites, multicolor, expansion, sprite-sprite/sprite-data collisions).
- [ ] Implement border generation (open borders trick must work).
- [ ] Implement raster IRQ generation.
- [ ] Implement badline cycle stealing.
- [ ] Implement MMIO register set ($D000–$D3FF mirrored).
- [ ] Save / load state.
- [ ] Introspection: register snapshot, current raster line, current cycle, sprite states.

### SID 6581
- [ ] Create `src/chips/audio/sid_6581/` library target.
- [ ] Implement register interface ($D400–$D7FF mirrored).
- [ ] Implement three voices with ADSR.
- [ ] Implement waveform generators (triangle, sawtooth, pulse, noise, combined).
- [ ] Implement filter (resonance + cutoff; v0.1 acceptable as "audible and stable", quality refinement is its own task).
- [ ] Implement ring modulation and sync.
- [ ] Implement read-back of OSC3 and ENV3.
- [ ] Save / load state.
- [ ] Introspection: register snapshot, voice states, filter state.
- [ ] Reference: at least one published SID emulation note set documented in `src/chips/audio/sid_6581/NOTES.md`.

### CIA 6526
- [ ] Create `src/chips/bus_controller/cia_6526/` library target.
- [ ] Implement two parallel ports (A and B).
- [ ] Implement two timers (A and B) with all modes (one-shot, continuous, cascaded).
- [ ] Implement TOD clock.
- [ ] Implement serial shift register.
- [ ] Implement IRQ generation and mask.
- [ ] Save / load state.
- [ ] Unit tests covering timer mode transitions.

### C64 PLA (memory banking)
- [ ] Create `src/chips/mapper/c64_pla/` library target.
- [ ] Implement LORAM/HIRAM/CHAREN/GAME/EXROM decoding to the 14-state banking table.
- [ ] Wire to bus region overlay control.

### Acceptance
- [ ] Each chip's unit tests pass in CI.
- [ ] SID register interface validated against a reference register trace from at least one known SID tune.
- [ ] No regressions in M1's 6510 tests.

---

## M3 — Topology, Manifest Loader, Runtime, and First Boot

### Topology library
- [ ] Create `src/topology/` library target `mnemos::topology`.
- [ ] Implement `bus` with width, endianness, sorted region table.
- [ ] Implement region kinds: `ram`, `rom`, `mmio_chip`, `mapper`.
- [ ] Implement bus read/write fast path with cached resolution.
- [ ] Implement mapper hook plumbing.
- [ ] Implement overlay control (PLA-driven).
- [ ] Unit tests covering region resolution edge cases (boundary, mirror, overlay precedence).

### Manifest loader
- [ ] Create `src/manifests/common/` library target `mnemos::manifests::common`.
- [ ] FetchContent integration for `tomlplusplus`.
- [ ] Define `manifest_schema/1` types in C++.
- [ ] Implement TOML parser with strict validation (TDS §10.3).
- [ ] Implement manifest-to-component-graph builder (instantiate chips by ID via the factory registry, wire buses).
- [ ] Implement ROM file loader with SHA-256 verification.
- [ ] Surface validation errors with file/line/column.
- [ ] Unit tests covering each validation rule.

### C64 manifest
- [ ] Create `src/manifests/c64/` directory.
- [ ] Author `c64.pal.toml` per TDS §10.2.
- [ ] Document ROM acquisition and SHA-256s in `src/manifests/c64/ROMS.md`.
- [ ] Note: ROM files themselves are NOT committed; CI obtains them from a configured source.

### Runtime library
- [ ] Create `src/runtime/` library target `mnemos::runtime`.
- [ ] Implement master clock with divider table.
- [ ] Implement fixed-divider scheduler dispatching per-chip ticks.
- [ ] Implement frame-tagged input buffer.
- [ ] Implement frame boundary detection and signaling.
- [ ] Implement save state with header + per-chip chunks (TDS §15).
- [ ] FetchContent integration for `zstd`.
- [ ] Implement save state compression (zstd) and decompression.
- [ ] Implement CRC32 trailing checksum.
- [ ] Implement rewind ring (configurable depth, default 600 frames).
- [ ] Unit tests for scheduler dispatch, save/load roundtrip, rewind.

### Headless runtime CLI
- [ ] Create `tools/mnemos_runtime_cli/` executable target.
- [ ] CLI options: `--manifest`, `--rom-dir`, `--frames`, `--dump-hash`, `--save`, `--load`, `--input-log`.
- [ ] Outputs framebuffer hash (SHA-256 of RGBA bytes) per frame or at end.

### First golden test
- [ ] Author `tests/golden/c64_basic_boot_test.cpp`.
- [ ] Boot C64 manifest with no cartridge, run 600 frames (~12 s @ 50 Hz PAL).
- [ ] Hash framebuffer at frame 600.
- [ ] Commit the expected hash.
- [ ] CI runs this on every PR.

### Acceptance
- [ ] `mnemos_runtime_cli load c64.pal --frames 600 --dump-hash` produces an identical hash on Win+Linux, Debug+Release.
- [ ] Save → load → continue produces an identical hash trajectory from the load point onward.
- [ ] ADR `docs/adr/0005-scheduler-strategy.md` records the fixed-divider choice and its limitations.

---

## M4 — Instrumentation API and Developer Frontend MVP

(Scoping-level items; expand at milestone start.)

- [ ] Promote `i_chip_introspection` from forward decl to full interface.
- [ ] Implement `i_runtime_introspection`.
- [ ] Implement breakpoint engine (PC, memory R/W, conditional).
- [ ] Implement watch engine.
- [ ] Implement event subscription with filters.
- [ ] Author Cap'n Proto schemas under `src/instrumentation/wire/`.
- [ ] Build-time codegen for C++ wire bindings.
- [ ] Implement wire transport (named pipe on Windows, domain socket on Linux).
- [ ] Implement in-process wire server.
- [ ] Bring up Vulkan rendering primitives in `frontend_sdk` (minimum needed for dev frontend).
- [ ] Bring up custom UI toolkit primitives (windowing, panels, text).
- [ ] Build `apps::dev` with disassembly, memory view, registers, breakpoints, controls, framebuffer mirror.
- [ ] ADR for breakpoint expression language.

---

## M5 — Player Frontend MVP

(Scoping-level items; expand at milestone start.)

- [ ] Expand `frontend_sdk` UI toolkit (lists, theming, asset loader, input mapping).
- [ ] Build `apps::player` with library view, launch, framebuffer presentation, audio, input, save/load, pause/reset.
- [ ] Configurable library root directory.
- [ ] Basic per-game save-state slots.
- [ ] Multiplatform gamepad input (XInput on Windows, evdev / SDL_gamepad on Linux).
- [ ] Audio output (WASAPI on Windows, PipeWire/PulseAudio on Linux).

---

## M6 — Sega Master System

- [ ] Implement `chips::cpu::z80` (full undocumented behavior; passes ZEXALL).
- [ ] Implement `chips::audio::sn76489`.
- [ ] Implement `chips::video::sms_vdp`.
- [ ] Implement SMS mapper (Sega mapper, Codemasters mapper).
- [ ] Author `manifests/sms/` (NTSC + PAL).
- [ ] Golden frame tests for known commercial SMS ROMs.
- [ ] Confirm zero contract changes from M1–M3; if any, raise ADR.

---

## M7 — Scripting (Lua + Python)

- [ ] FetchContent integration for Lua 5.4.
- [ ] FetchContent integration for sol2.
- [ ] Build `instrumentation::scripting_lua` with sandboxed VM.
- [ ] Expose introspection API to Lua.
- [ ] Add Lua console panel to dev frontend.
- [ ] Build `mnemos-py` Python package over wire protocol.
- [ ] Publish reference asset-extraction Lua script.
- [ ] Publish reference asset-extraction Python script.
- [ ] Documentation: `docs/scripting/lua-api.md`, `docs/scripting/python-api.md`.

---

## M8 — Sega Genesis

- [ ] Implement `chips::cpu::m68000` (full, passes available test suites).
- [ ] Implement `chips::audio::ym2612`.
- [ ] Verify reuse of `chips::audio::sn76489` (Genesis PSG).
- [ ] Verify reuse of `chips::cpu::z80` (Genesis sound CPU).
- [ ] Implement `chips::video::vdp_315_5313` (Genesis VDP).
- [ ] Implement Genesis cartridge mappers (SSF2, EEPROM-backed, etc.).
- [ ] Author `manifests/genesis/` (NTSC + PAL).
- [ ] Golden frame tests for known commercial Genesis ROMs.
- [ ] Validate dual-CPU scheduling correctness.

---

## M9 — Multiplatform Parity Audit

- [ ] Full clean Linux build in a fresh container, no Windows host involvement.
- [ ] Linux performance baseline recorded.
- [ ] Linux input (evdev / SDL_gamepad) verified across at least 3 gamepad models.
- [ ] Linux audio path verified.
- [ ] Sanitizer builds (ASan, UBSan) green on full system suite.
- [ ] Populate `BUILDING.md` for both platforms.
- [ ] Populate `CONTRIBUTING.md`.

---

## M10 — Netplay Foundations

- [ ] Confirm M3 input subsystem meets rollback requirements; revise if not.
- [ ] Add cross-client state hash introspection capability.
- [ ] Implement rollback engine (input prediction, mispredict detect, resimulate).
- [ ] Implement lockstep fallback transport.
- [ ] Implement local lobby + direct-connect.
- [ ] Implement netplay-aware input mapping (player slot assignment).
- [ ] Smoke test: two SMS instances on same LAN, 2-player ROM, 10-minute session, no desync.
- [ ] ADR `docs/adr/00NN-netplay-architecture.md`.

---

## Cross-Cutting Backlog (Not Tied to a Single Milestone)

### Governance
- [ ] Implement `naming_validator` tool (M1 or later).
- [ ] Implement `tier_dependency_validator` tool (verify CMake graph matches TDS §4).
- [ ] Wire both validators into CI.
- [ ] Author governance ADRs as new patterns emerge.

### Documentation
- [ ] Per-chip `NOTES.md` template adopted from the first chip implementation onward.
- [ ] Per-system `NOTES.md` template adopted from C64 onward.
- [x] `docs/architecture/` index page with reading order.
- [x] `docs/adr/` index page with chronological list.

### Tooling
- [ ] ROM hashing utility (`tools/rom_hasher`) for manifest authoring.
- [ ] Manifest validator standalone CLI (`tools/manifest_validator`).
- [ ] Save-state inspector (`tools/state_inspect`) for debugging.

### Performance (post-M3)
- [ ] Microbenchmark harness in `tests/bench/`.
- [ ] Per-chip cycle-cost benchmark.
- [ ] System-level frames-per-second benchmark on reference hardware.

---

**End of Mnemos Todos v0.1.**
