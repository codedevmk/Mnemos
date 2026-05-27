# Deduplication + Interface Cleanup Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Pay down concrete debt found in the post-multi-system-decoupling audit (session 2026-05-27): real duplications, half-finished interfaces, single-source-of-truth violations, missing tooling-affordance gaps. Eight numbered items, executed in highest-impact / lowest-friction order. Items 5–8 are sequenced last because they require scoping conversations, not just implementation.

**Tech Stack:** C++23, CMake 3.28+/Ninja, Catch2 v3 (`mnemos_add_test`), MSVC `/W4 /WX` + GCC/Clang `-Wall -Wextra -Wpedantic -Werror`.

**Conventions:**
- Flat module layout; basename includes; globally-unique names ([[mnemos-flat-module-layout]]).
- Each item ends green: build + ctest + clang-format clean, commit, confirm with Marius before moving on.
- Windows build prelude every time:
  ```powershell
  cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" >NUL && <command>"
  ```
- BoV byte-perfect parity (0/215040 vs the reference at f=120) is the load-bearing regression check after any chip / scheduler / adapter touch.

---

## Recommended Execution Sequence

| Order | Item | Impact | Effort | Why this slot |
|---|---|---|---|---|
| 1 | **#4** z80 + m6510 trace_target | **High** | 4-6h × 2 | Unlocks `--screenshot` trace for SMS + C64. Closes a real tooling gap that left us blind during today's Sonic 1 regression. |
| 2 | **#1** Audio resampler dedupe | Low | 1-2h | Real duplication; trivial extraction; reader-visible cleanup. |
| 3 | **#2** `make_scheduler` helper dedupe | Low | 15 min | Just-introduced; fix before it propagates to a third adapter. |
| 4 | **#6** `chip_class` single source of truth | Low | 1-2h | Removes a quiet bug-magnet (three places to keep in sync per chip). |
| 5 | **#5** Common `cpu_diagnostics` facade | Medium | 4-6h | Direct follow-up to #4 — promotes the per-CPU pattern into a shared interface. Skip if #4 stays narrow. |
| 6 | **#3** `build_system` decision | High | varies (commit OR migrate) | **Decision item, not pure implementation.** Either delete the vestigial path (~30 min) or commit to the multi-day migration. Brief Marius before touching. |
| 7 | **#7** Empty marker interfaces | Medium | 4-6h | Either drop or fill `iaudio_synth`/`ibus_controller`/`istorage`/`imapper`/`iperipheral`. Decision required before implementation. |
| 8 | **#8** `Result<T,E>` introduction | Medium | 4-6h | Foundational. Best done after #3 since the migration would benefit from it. |

Items 1–4 are mechanical / low-risk. Items 5–8 each need a small design decision; flagged as such in their sections.

---

## Item #4 — CPU Trace Standardization (z80 + m6510)

**Goal:** z80 and m6510 expose the generic `instrumentation::trace_target` capability so the SDK's `trace_csv_session` works for any system, not just Genesis.

**Today** (`apps/player/debug_dump.cpp:138-155`): `trace_csv_session` walks `sys.chips()` and installs against the first chip whose `introspection().trace()` is non-null. Genesis's m68000 advertises one (from earlier session). z80 and m6510 don't — so SMS and C64 silently get inactive trace sessions.

### Sub-item 4a — z80 trace (instruction-stepped, mirrors m68000 pattern)

**Files**
- `src/chips/cpu/z80/z80.hpp` — **modify**: add private `trace_callback_` member; expose nothing public (the trace hook is installed only via introspection).
- `src/chips/cpu/z80/z80.cpp` — **modify**: fire callback at the start of `step_instruction()` (before opcode fetch). Add an `introspection_surface` inner class mirroring m68000's pattern: it owns a `trace_impl : trace_target` and a `registers_impl : register_view`.
- `src/chips/cpu/z80/z80.cpp` — **modify**: `introspection()` returns the surface.
- `src/chips/cpu/z80/tests/z80_test.cpp` — **add**: catch2 case that installs a trace via the introspection path and asserts pc + cycles fire as expected.

**Steps**
- [ ] Read z80.cpp's `step_instruction()` to find the trace-firing point.
- [ ] Add private `trace_callback_` (signature: `std::function<void(std::uint32_t)>` — PC at instruction start) + bridge it into `trace_target::install` via the surface.
- [ ] Wire the introspection_surface (mirror m68000's structure at `m68000.hpp:141-179`).
- [ ] Add z80 register_view (whichever architectural registers z80 exposes via its existing `register_snapshot` if any; if absent, defer that part).
- [ ] Add a catch2 test that fires a synthetic event and verifies the callback receives correct pc + cycles.
- [ ] `--screenshot` on an SMS title and verify the `.68k_trace.csv` (rename? to `.z80_trace.csv` — see Open Question) now contains rows.
- [ ] BoV f=120 byte-perfect regression check.
- [ ] Commit (`z80: expose trace_target via introspection`). Confirm.

**Open question:** the CSV filename in `trace_csv_session` is hardcoded by the caller in `main.cpp` to `<path>.68k_trace.csv`. For SMS/C64 the same suffix is misleading. Decision: either rename to a generic `.cpu_trace.csv` OR let the chip self-declare a filename hint via the trace_target. Recommend `.cpu_trace.csv` for now (caller-side change, low risk).

### Sub-item 4b — m6510 trace (cycle-stepped, harder)

**Files**
- `src/chips/cpu/m6510/m6510.hpp` + `.cpp` — **modify**: m6510 ticks per-cycle (not per-instruction). Trace must fire when a new instruction's opcode-fetch cycle starts, NOT every cycle.
- `src/chips/cpu/m6510/m6510.cpp` — **modify**: introspection_surface + trace_impl/register_impl mirrors.
- `src/chips/cpu/m6510/tests/m6510_test.cpp` — **add**: catch2 case for the trace path.

**Steps**
- [ ] Read m6510.cpp's `step_one_cycle()` to find the new-instruction detection point (probably a `phase == fetch` or `cycle_in_instruction == 0` check).
- [ ] Add trace_callback_ + introspection_surface as for z80.
- [ ] Fire callback ONLY at instruction-fetch cycles, not every cycle.
- [ ] Test that the callback fires once per executed instruction, not once per cycle.
- [ ] C64 BASIC boot test (data-gated, currently skipped without ROMs) — if running locally with ROMs, verify trace produces sensible PC sequence.
- [ ] Commit (`m6510: expose trace_target via introspection`). Confirm.

**Acceptance for #4 overall**
- `--screenshot` against any Genesis / SMS / C64 ROM produces a non-empty trace CSV (file name `<path>.cpu_trace.csv`).
- 48/48 ctest entries continue passing.
- BoV f=120 still 0/215040.

---

## Item #1 — Audio Resampler Dedupe

**Goal:** Extract `clip_i16`, `scale_q12`, `sample_channel_linear`, `sample_channel_box`, `sample_box`, and the fixed Q12 constants into `apps/player/adapters/common/audio_resampler.{hpp,cpp}`.

**Today** — Two near-identical copies:
- `src/apps/player/adapters/genesis/genesis_adapter.cpp:34-46`, `48-70`, `72-105`
- `src/apps/player/adapters/sms/sms_adapter.cpp:28-46`, `48-88`

The Genesis variants take a `stride + channel` (stereo); SMS uses single-channel (`sample_box`). Both invocation styles can share the same templated or stride-aware implementation.

### Files
- `src/apps/player/adapters/common/audio_resampler.hpp` — **create**: declare `clip_i16`, `scale_q12`, `sample_channel_linear`, `sample_channel_box`, plus the `kMixerGainShift` / `kMixerGainOne` / `kOutputRate` constants.
- `src/apps/player/adapters/common/audio_resampler.cpp` — **create**: implementations.
- `src/apps/player/adapters/common/CMakeLists.txt` — **modify**: add the new source.
- `src/apps/player/adapters/common/tests/audio_resampler_test.cpp` — **create**: at least 5 cases (clip overflow/underflow, scale rounding, linear interp endpoint behaviour, box-average degenerate cases).
- `src/apps/player/adapters/genesis/genesis_adapter.cpp` — **modify**: remove the local copies, include the common header.
- `src/apps/player/adapters/sms/sms_adapter.cpp` — **modify**: same.

### Steps
- [ ] Read both adapters' resampler helpers; confirm semantics are byte-identical and only the call shape differs.
- [ ] Write `audio_resampler_test.cpp` first against an empty common header; assert it fails to link.
- [ ] Define the common header + impl; tests pass.
- [ ] Switch genesis_adapter to include the common version; build + run mnemos_player on BoV f=120; confirm 0/215040 (audio path doesn't affect framebuffer parity, so this is just a regression sanity check).
- [ ] Switch sms_adapter similarly; SMS player_system tests still pass.
- [ ] Run `clang-format --dry-run --Werror`; commit (`adapters/common: dedupe audio resampler helpers`).

**Acceptance**
- Zero copies of `clip_i16` / `scale_q12` / `sample_box` outside `adapters/common/`.
- BoV f=120 byte-perfect.
- 48+ ctest entries pass.

---

## Item #2 — `make_scheduler` Helper Dedupe

**Goal:** Move the `make_scheduler(factory*, chips, frame_source)` helper from both adapter anonymous namespaces into `frontend_sdk/scheduler_factory.hpp` so a third adapter doesn't reintroduce the same copy.

**Today** — Added in Item 4A today; identical 8-line helper at:
- `src/apps/player/adapters/genesis/genesis_adapter.cpp:9-17` (post-Item-4A)
- `src/apps/player/adapters/sms/sms_adapter.cpp:9-17` (post-Item-4A)

### Files
- `src/frontend_sdk/scheduler_factory.hpp` — **modify**: add a free `make_scheduler(scheduler_factory*, std::vector<runtime::scheduled_chip>, chips::ivideo*)` function. Inline in the header (small).
- `src/apps/player/adapters/genesis/genesis_adapter.cpp` — **modify**: delete the local helper, call the SDK one.
- `src/apps/player/adapters/sms/sms_adapter.cpp` — **modify**: same.

### Steps
- [ ] Add the free function to scheduler_factory.hpp.
- [ ] Delete the anonymous-namespace `make_scheduler` from both adapters.
- [ ] Build, ctest, BoV regression.
- [ ] Commit (`frontend_sdk: hoist make_scheduler helper out of adapters`).

**Acceptance**
- `grep -r 'runtime::scheduler make_scheduler' src/apps/player/adapters` returns nothing.
- 48/48 ctest entries pass.

---

## Item #6 — `chip_class` Single Source of Truth

**Goal:** Eliminate the three-way redundancy in chip classification: (a) `chip_class` enum in `chip.hpp:19`, (b) `static constexpr chip_class static_class` on each tier-2 interface (`icpu`, `ivideo`, ...), (c) `metadata().klass` filled by every concrete chip. Pick one canonical source.

**Today** — A chip's class lives in three places: the interface inherited (`icpu`), the static_class constant (`icpu::static_class`), and the metadata returned (`m68000::metadata().klass = chip_class::cpu`).

### Decision

Recommended approach: **`metadata().klass` is the only source of truth**. Drop the `static_class` constants. Reasoning: metadata is already required, polymorphic, and queryable through the existing `ichip*` pointer. The static_class constants aren't consumed anywhere meaningful today (verify before deleting).

Verify with `grep -r 'static_class' src/` — if no consumers, delete. If consumers exist, this becomes a larger refactor.

### Files (preliminary, scope depends on grep)
- `src/chips/shared/chip.hpp` — **modify**: drop the `static_class` constants from `icpu`, `ivideo`, `iaudio_synth`, `ibus_controller`, `istorage`, `imapper`, `iperipheral`.
- Any consumer of `static_class` — **modify**: switch to `metadata().klass`.

### Steps
- [ ] grep for `static_class` consumers; list them.
- [ ] If empty: drop the constants in chip.hpp + commit.
- [ ] If non-empty: rewrite consumers to use `metadata().klass`, then drop.
- [ ] Build + ctest + BoV.
- [ ] Commit (`chips/shared: chip_class lives only in metadata().klass`).

---

## Item #5 — Common `cpu_diagnostics` Facade [Decision required]

**Goal:** After #4 lands trace_target on z80 and m6510, the per-CPU diagnostic pattern is repeated three times (each CPU has its own `_diagnostics` class). Decide whether to (a) keep per-CPU diagnostics, (b) introduce a base `cpu_diagnostics` interface that all CPUs implement, or (c) push everything through the generic `trace_target` + introspection surface and drop per-CPU diagnostics entirely.

**Decision factors**
- Per-CPU diagnostics carry chip-specific data (m68000's `cycle_sources` struct isn't meaningful for m6510). A shared base interface would lose that.
- Pushing through introspection only handles the lowest common denominator (pc + cycles).
- Hybrid: trace_target for common columns, optional `trace_extra` capability for chip-specific data (the deferred work flagged in the multi-system architecture plan).

### Recommended scope when implemented
Follow the hybrid path: trace_target stays the public CPU-trace contract (already standardized via #4). The chip's `_diagnostics` class becomes private implementation detail. The `trace_extra` Out-of-Scope item from the multi-system plan covers the chip-specific data path.

**Defer until** Marius signals chip-specific tracing is needed (e.g., next parity investigation that benefits from cycle_sources columns).

---

## Item #3 — `build_system()` Decision [Decision required]

**Goal:** Decide the fate of `src/manifests/common/builder.cpp`'s `build_system()` function and the `system_graph` data structure. Today they're vestigial — used only by their own unit tests. Every real system goes through hand-written `assemble_genesis()` / `assemble_sms()` / `assemble_c64()` (200-300 lines each).

### Option A — Delete it ✋ (~30 min)
- Remove `src/manifests/common/builder.{hpp,cpp}` + tests.
- Document in [[mnemos-port-emu-cores]] memory that hand-written assembly is the production path.
- Accept that adding 32X / Saturn / Sega CD means writing ~300 lines of C++ each.

### Option B — Migrate everything to it (~multi-day to multi-week)
- Items P4 (declarative gating) + P5 (per-chip config) + P7 (mapper overlays) from the original architectural sketch.
- Becomes the production path; `assemble_*()` deleted.
- Enables 32X / Saturn / Sega CD via manifests instead of bespoke code.

**Recommendation:** Before touching anything, have a 15-minute conversation about whether the multi-system roster (Saturn, Amiga, CPS, etc.) makes this worth the investment. If the roster is real → Option B is foundational. If it's aspirational → Option A removes ~600 LOC of confusion.

**Steps after the decision**
- Option A: see the 30-minute deletion plan in this section's decision block.
- Option B: this becomes its own plan doc (P4 + P5 + P7 combined). Beyond this plan's scope.

---

## Item #7 — Empty Marker Interfaces [Decision required]

**Goal:** Decide whether to drop or fill the five empty subclass interfaces in `src/chips/shared/chip.hpp`: `iaudio_synth`, `ibus_controller`, `istorage`, `imapper`, `iperipheral`. Today each holds only `static constexpr chip_class static_class = chip_class::*` and no actual methods.

### Decision factors

- **Drop:** if `chip_class` enum in `metadata()` is sufficient (see #6). The markers add type-level noise without contract.
- **Fill:** if there's real shared contract — e.g., `iaudio_synth::drain_samples(span<int16>) -> size_t` would unify YM2612 / SN76489 / SID / PSG (the four audio chips currently each have their own `pending_samples`/`drain_samples` shape).

### Recommended scope when implemented
**Fill `iaudio_synth` with the common audio-drain contract.** It's the only marker with 4+ concrete implementations doing similar work. Other markers (`ibus_controller`, `istorage`, `imapper`) have one or two implementations and the contract isn't crystallized; drop them in favour of `metadata().klass`.

**Defer until** Item #1 (audio resampler dedupe) lands and we have concrete experience with the audio path's interface needs.

---

## Item #8 — `std::expected<T, E>` Introduction [Decision required]

**Goal:** Unify error handling across fallible operations. Today the project has three styles:
- `adapter_registry::create` returns `nullptr` on failure with no error info.
- `parse_manifest` returns a `load_result` with optional value + diagnostic vector.
- `build_system` returns a `build_result` with optional value + diagnostic vector.

### Recommended approach
Introduce `mnemos::foundation::result<T>` (or use `std::expected<T, mnemos_error>` if C++23 support is universal across compilers in CI). Retrofit:
- `adapter_registry::create` returns `result<unique_ptr<player_system>>` instead of `unique_ptr` (or nullptr).
- `parse_manifest` returns `result<manifest>`.
- `build_system` similarly.

### Files (preliminary)
- `src/foundation/result.hpp` — **create**.
- Each fallible-returning function — **modify**.

**Defer until** #3 decision lands. If Option B (manifest migration), #8 supports it. If Option A (delete build_system), the cost-benefit shifts.

---

## Verification Across Plan

- Every commit individually green-builds and green-tests on `windows-msvc-debug`.
- BoV f=120 byte-perfect after every chip / scheduler / adapter touch.
- 48 ctest entries baseline; new tests added per item; never lose any.
