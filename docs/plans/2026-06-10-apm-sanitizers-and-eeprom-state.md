# APM Sanitizer Coverage + EEPROM Save-State Wiring

Status: Draft, awaiting review
Date: 2026-06-10

Plan for the infrastructure items from the 2026-06-10 code-review session
(session https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF): putting
the APM tracer's VEH/ABI/allocator code under sanitizers (it currently has
zero sanitizer execution anywhere), fixing the two remaining page_guard
races with deterministic regression tests, and giving the EEPROM devices a
serialization path so system savestates stop silently losing their FSMs
and write-enable latches. This document is design + sequencing for
sign-off; it implements nothing.

---

## Summary of recommendations

- **Item 1:** Do **both tracks, in this order**: (a) an MSVC `/fsanitize=address` preset + scoped CI job first — it executes the *actual shipped* Win32 VEH code (`apm/memory/page_guard.cpp`) rather than a parallel implementation; then (b) a POSIX `page_guard` backend so the existing `linux-clang-asan` job exercises the shared watch-table/pending-recovery logic on every PR. Land the two known `t_pending` races' fix (per-thread pending *list* + live-watch revalidation on single-step) in the same phase as the first CI job that can execute it, with deterministic regression tests for both races.
- **Item 2:** Choose **option (b)** — non-virtual `save_state(state_writer&)` / `load_state(state_reader&)` methods on `eeprom_93c46` / `eeprom_i2c`, plus a lightweight `save_device` slot in `runtime::save_target`. Option (c) is subsumed (the system registers one device entry that serializes its wrapper latches and delegates to the chip methods); option (a) is rejected (boilerplate-heavy, and `ichip`-ness alone wouldn't put the devices into any save target — targets are hand-assembled). Flag as proposed **ADR-0024** since it extends ADR-0008's container with a third chunk source.

---

## Part 1 — Sanitizer coverage for the APM tracer

### 1.1 Current state (grounded)

- `apm/` is added unconditionally from the top-level `CMakeLists.txt` (line 47), but `apm/CMakeLists.txt` gates `bindings/genesis` and `host` on `WIN32`. `apm/abi` (header-only C ABI) and `apm/memory` build everywhere.
- `apm/memory/page_guard.cpp` has a real implementation only under `#if defined(_WIN32) && (_M_X64 || _M_IX86)`; elsewhere it is a stub with `supported() == false` (lines 231–248). All three tests in `apm/memory/tests/page_guard_proof.cpp` begin with `if (!page_guard::supported()) { SUCCEED(...); return; }` — so under the existing `linux-clang-asan` job they *pass vacuously*. Only `tagged_allocator_test.cpp` and `bank_registry_test.cpp` get real Linux ASan coverage today.
- The CI matrix (`.github/workflows/ci.yml`): Windows runs only `windows-msvc-debug/release` (no sanitizer); the `sanitizers` job is Linux-only (`linux-clang-asan` preset, `CMakePresets.json` lines 117–131). Net effect: the VEH fault handler, the plugin host (`apm/host/plugin_host.cpp`, `LoadLibraryA`/`GetProcAddress`), and the DLL-boundary exception barriers in `apm/bindings/genesis/genesis_binding.cpp` — the hardening from commit `b4292e0` — have **zero sanitizer execution anywhere**, and on Windows CI run only under plain Debug/Release.
- `apm/bindings/genesis/CMakeLists.txt` builds the binding test (`mnemos_apm_genesis_binding_test`) by compiling `genesis_binding.cpp` *directly into the test executable* — no DLL, no `LoadLibrary`. Nothing in that test is Windows-specific; it is only excluded from Linux because the whole subdirectory is inside the `if(WIN32)` block in `apm/CMakeLists.txt`. The Windows-only part is the `SHARED` library target (linking non-PIC engine static libs into a DLL).

### 1.2 Track (a): MSVC ASan preset + CI job — recommended first step

**Preset design** (`CMakePresets.json`): add `windows-msvc-asan` inheriting `base`:

- `CMAKE_BUILD_TYPE: Debug`, but **override `CMAKE_CXX_FLAGS_DEBUG`** to `"/Zi /Ob0 /Od /fsanitize=address"`. This is the load-bearing detail: CMake's MSVC Debug default is `/Zi /Ob0 /Od /RTC1`, and `/RTC1` is a **hard compile error** combined with `/fsanitize=address`. None of the existing presets set flags explicitly (they rely on CMake defaults — verified in `CMakePresets.json`), so the override must be in the new preset, not in `cmake/modules/MnemosCompilerFlags.cmake`.
- `CMAKE_EXE_LINKER_FLAGS_INIT` / `CMAKE_SHARED_LINKER_FLAGS_INIT: "/INCREMENTAL:NO"` — incremental linking is incompatible with ASan (the linker otherwise emits a warning and silently disables it).
- `MNEMOS_BUILD_APPS: OFF` — skips the SDL3 FetchContent/build entirely (option at top-level `CMakeLists.txt` line 31), keeping the job's build time sane; nothing under `apm/` needs SDL.
- Matching `buildPresets`/`testPresets` entries.

**Constraints checked:**
- x64 is supported by MSVC ASan; the CI job already forces `VsDevCmd.bat -arch=x64` (`ci.yml` lines 121–125), which also puts `clang_rt.asan_dynamic-x86_64.dll` on `PATH` for the ctest step.
- MSVC has **no UBSan** — this preset is ASan-only; that is acceptable (UBSan coverage stays on the Linux job).
- `/W4 /WX` (`MnemosCompilerFlags.cmake`) stays; keeping `/Zi` avoids warning C5072 (ASan without debug info).
- All third-party code (Catch2 via `mnemos_fetch_catch2`, zstd via FetchContent in `src/runtime/CMakeLists.txt`) is built from source in-tree, so `FLAGS_DEBUG` instruments everything uniformly — no container-annotation mismatch between instrumented and uninstrumented objects.
- **Known risk to validate empirically in the PR:** ASan's own Windows exception machinery vs. the VEH in `page_guard.cpp`. Our handler is registered with `AddVectoredExceptionHandler(1U, ...)` *after* ASan initializes, so it should sit ahead of ASan's; the deliberately-faulting page_guard tests are the canary. If ASan intercepts the AV first, fall back to scoping `ASAN_OPTIONS` (e.g. `handle_segv=0`) onto the page_guard test via `set_tests_properties(... PROPERTIES ENVIRONMENT ...)` in `apm/memory/CMakeLists.txt`. The plan should not assert this works until the job runs.

**CI job design** (`.github/workflows/ci.yml`): a new `windows-msvc-asan` job alongside `sanitizers`, reusing the existing Windows `cmd` + `vswhere` + `VsDevCmd` steps verbatim. Test step starts **scoped**: `ctest --preset windows-msvc-asan -R "^mnemos_apm" --output-on-failure` — this matches all five apm tests including `mnemos_apm_genesis_binding_test` (which transitively executes `build_genesis_runtime`, i.e. a meaningful slice of the engine under ASan too). Widening to the full suite is a later, separately-evaluated step (build of the full headless tree is required anyway since the binding test links `mnemos::manifests::genesis` + `mnemos::runtime`).

**Bonus, cheap win:** restructure `apm/CMakeLists.txt` so the `WIN32` gate covers only the `SHARED` library target and `host/` (which are genuinely Win32: DLL linking, `LoadLibrary`), while `bindings/genesis`'s *test* target builds on all platforms. That immediately puts the `b4292e0` exception barriers under the existing `linux-clang-asan` job, before any Windows job exists. Requires only moving the `if(WIN32)` inside `apm/bindings/genesis/CMakeLists.txt` around the `add_library(... SHARED ...)` block (and dropping the `#pragma warning` portability issue if any — `genesis_binding.cpp` has none; the `#pragma warning(suppress: 4191)` is in `plugin_host.cpp`, which stays gated).

### 1.3 Track (b): POSIX page_guard backend — feasible, second

**Design** (x86-64 Linux only; `supported()` stays honest elsewhere, so the `arm64` CI job keeps skipping):
- `allocate()`: `mmap(PROT_READ|PROT_WRITE)`; `watch()`: `mprotect` to `PROT_READ` (write-watch) / `PROT_NONE` (read-watch).
- Fault path: `sigaction(SIGSEGV, SA_SIGINFO)`; `si_addr` gives the fault address; `uc_mcontext.gregs[REG_ERR]` bit 1 distinguishes write; `REG_RIP` gives host IP. Recovery is the direct port of the Windows scheme: `mprotect(PROT_READ|PROT_WRITE)`, set `gregs[REG_EFL] |= 0x100` (TF), return; a `SIGTRAP` handler re-protects and clears TF. Chain to the previously-installed handler (saved `struct sigaction`) for unmatched faults — the analogue of `EXCEPTION_CONTINUE_SEARCH`.
- **Refactor first**: split `page_guard.cpp` into a platform-neutral core (watch table, range/kind filtering, the per-thread pending list, handler-copy-outside-lock policy) plus thin OS backends (`page_guard_win32.cpp`, `page_guard_posix.cpp`) that supply protect/unprotect and context accessors. This is what makes "the Linux ASan job exercises the same logic" literally true rather than aspirational — and the race fix is written once.

**Honest feasibility caveats (why this is second, not first):**
1. **Async-signal-safety.** The Windows VEH runs as ordinary code; a POSIX SIGSEGV handler may not safely call anything that allocates. The guard invokes a user `std::function` (`guard_handler`) from the handler, and even the existing proof tests allocate inside it (`events.push_back`). Under ASan this usually works but is formally unsupported. Mitigations: document the constraint on `guard_handler` ("no allocation on POSIX"), pre-`reserve()` in the tests, and keep `mprotect`-in-handler (technically not on the async-signal-safe list, but reliable on Linux and the standard guard-page idiom).
2. **ASan owns SIGSEGV by default** (`handle_segv=1`, and `allow_user_segv_handler` historically defaults off on Linux), so our `sigaction` may be ignored or bypassed. Fix is per-test scoping, not job-wide: `set_tests_properties(mnemos_apm_memory_page_guard_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=handle_segv=0:handle_sigbus=0:allow_user_segv_handler=1")` in `apm/memory/CMakeLists.txt` (harmless on non-ASan presets). Losing ASan's segv *reporting* for that one binary is the accepted cost; ASan's heap/UB checks remain active.
3. TF-based single-step is x86-only on both OSes; that asymmetry already exists on Windows (the `_M_X64 || _M_IX86` gate added in `b4292e0`) and carries over.

### 1.4 The two known races and the fix that must land with the coverage

Both are in `apm/memory/page_guard.cpp` and are backend-generic in design:

1. **Cross-page fault overwrites the single `t_pending` slot** (line 53). Two *separate* watches on adjacent pages + one unaligned access straddling the boundary: the first AV unprotects page A and arms `t_pending`; the instruction re-runs and **faults again on page B before retiring** (so no single-step trap intervenes); the second AV overwrites `t_pending` — page A is never re-protected and its watch is silently dead. This is single-threaded and **deterministically testable**: `allocate(8192)`, `watch()` the last byte of page 0 and first byte of page 1 separately, then one `volatile std::uint16_t` store across the boundary; assert both watches still fire on subsequent single-page writes. **Fix:** replace `t_pending` with a small fixed-capacity per-thread array (no allocation — signal-safe on POSIX); the single-step/SIGTRAP handler drains *all* active entries.
2. **`unwatch()` racing an in-flight single-step.** The `EXCEPTION_SINGLE_STEP` branch (lines 77–87) re-protects from the stale `t_pending.protect` *without taking `g_mutex` or consulting `g_watches`*. If the watch was removed in the window (by another thread, or — deterministically — by the guard handler itself, which since `b4292e0` runs after the page is opened but before the access retires), the page is re-protected with **no watch entry left**: the next access faults, matches nothing, falls to `EXCEPTION_CONTINUE_SEARCH`, and the process dies. **Fix:** in the single-step branch, take `g_mutex`, look up the pending page in `g_watches`, re-protect using the *live* entry's protection only if a covering watch still exists, otherwise skip. **Deterministic test:** handler calls `guard.unwatch()` on its own watch; after recovery, write the same address again and assert no crash and `hits == 1`.

These fixes and tests land in the **same PR** as the first sanitizer job able to execute them (track (a)'s scoped Windows ASan job, plus they run on existing plain-Windows jobs immediately), satisfying "merged code must have been executed".

---

## Part 2 — EEPROM save-state wiring

### 2.1 Current state (grounded)

- `src/chips/storage/eeprom_93c46/eeprom_93c46.hpp` and `eeprom_i2c/eeprom_i2c.hpp` are plain classes (not `ichip`), exposing only `bytes()` (the backing store, for `.srm` persistence via e.g. `sms_runtime::battery_ram()`). Their FSMs are private and unreachable: 93C46's `stage_`, `cs_`, `clk_`, `data_out_`, **`write_enable_` (the long-lived EWEN latch)**, `cycles_`, `opcode_`, `buffer_`; I2C's `stage_`, `bit_count_`, `shift_in_/out_`, `addr_`, `block_high_`, `reading_`, `transmitting_`, `master_ack_`, `prev_scl_/sda_`, `sda_out_`.
- They are embedded **by value, outside the chip graph**: `sms_runtime::eeprom` + the system-level `eeprom_enabled` latch (`src/manifests/sms/sms_runtime.hpp` lines 43–45, wired by reference via `install_93c46_overlays` in `sms_system.cpp:147`); Genesis holds `cart_eeprom_runtime { optional<eeprom_i2c> device; bool scl; bool sda; }` (`src/manifests/genesis/genesis_eeprom.hpp` lines 34–39) — note the **latched line levels `scl`/`sda` are state too**, outside the device.
- `runtime::save_target` (`src/runtime/save_state.hpp`) has exactly two chunk sources: `save_chip` (requires `chips::ichip*`) and `save_memory` (raw borrowed bytes). Targets are hand-assembled — today only by `tools/mnemos_runtime_cli/cli.cpp` (C64, line 715); the CLI explicitly prints "SMS save/load state is not wired up yet" (line 471), and nothing builds a Genesis target. So the bug today is a *latent contract gap*: whoever wires SMS/Genesis states with the current `save_target` shape will necessarily drop the EEPROM FSM and EWEN latch.
- The same gap covers other non-chip cart state: `cart_sram_runtime::enabled/write_protect` ($A130F1 latches, `genesis_cart.hpp:46–54`) and `cart_banking_runtime::bank` (`genesis_banking.hpp:19–23`). The chosen mechanism should serve all of them; EEPROM goes first.

### 2.2 Decision: option (b), with (c) folded in

- **(a) full `ichip`** — rejected. ADR-0004 requires `metadata()`, `tick()`, `reset(reset_kind)`, introspection, no-arg construction + factory registration. The EEPROMs are combinational slaves "with no clock of their own" (their own header comments) — `tick` is meaningless; `eeprom_i2c` needs a size at construction; and crucially, being an `ichip` doesn't get a device into a `save_target` anyway, since targets are assembled by hand. High boilerplate, no automatic benefit.
- **(c) system-private chunk** — no such concept exists in the container; it would be reinvented per system, and the FSM layout knowledge would leak out of the chip directory (the chip owns its private fields; only the chip can serialize/validate them).
- **(b) chosen.** Two parts:
  1. **Chip methods**, same shape and naming as the `ichip` pair so a future promotion to `ichip` is mechanical: `void save_state(chips::state_writer&) const;` / `void load_state(chips::state_reader&);` on both EEPROMs. They serialize the *entire* device including `store_` (a save state restores the full machine; `.srm` persistence stays orthogonal via `bytes()`).
  2. **Container slot** in `src/runtime/save_state.hpp`:
     ```
     struct save_device final {
         std::string id;
         std::function<void(chips::state_writer&)> save;
         std::function<void(chips::state_reader&)> load;
     };
     ```
     added to `save_target::devices`; `write_save_state` emits one chunk per device; `read_save_state`'s dispatch loop checks `devices` between `chips` and `memory`. Unknown-chunk skipping and the existing `truncated`/`ok()` semantics are untouched.
  - The system's registration lambda is where (c)'s concern lives: SMS registers one `"cart_eeprom"` device that writes `eeprom_enabled` + delegates to `eeprom.save_state(w)`; Genesis registers `"cart_eeprom"` writing `scl`, `sda` + delegating to `device->save_state(w)` (presence-gated on `device.has_value()`).
  - Add `make_save_target(...)` helpers next to each runtime (`sms_runtime`, `genesis_runtime`) so enumeration lives with the system that knows its devices — the structural fix for "silently lose": future state wiring calls the helper instead of hand-listing chunks.

### 2.3 ADR-0021 validation rules applied on load

Per `docs/adr/proposed/0021-save-state-semantic-validation.md` (reader = bounds defence, chip = semantics defence, `state_reader::fail()` as rejection channel; pre-release layouts may extend without version gates if save/load change symmetrically in one commit):

- 93C46: `stage_` decoded `u8` must be `<= stage::read_word` else `reader.fail()`; `cycles_` clamped/failed against the max bits of the current stage (opcode: 8, data: 16); `opcode_`/`buffer_` are full-range valid; bools via `boolean()`.
- I2C: `stage_ <= stage::read_data` else fail; `bit_count_` outside `[0,8]` fails; `addr_ &= addr_mask_`; `block_high_` masked to `block_bits_`; **store blob length must equal `store_.size()`** (capacity comes from `cart_eeprom_config`, not the state file) else fail — the REU-style "wrong-model blob" case ADR-0021 §Context calls out.
- The wrapper lambdas validate nothing extra (latches are bools) but must keep save/load symmetric.

### 2.4 Proposed ADR

Draft `docs/adr/proposed/0024-save-state-stateful-devices.md` (next free number after 0023): *"Non-chip stateful devices in the save-state container"*. Decision shape: records the (a)/(b)/(c) trade-off above, extends ADR-0008's "one chunk per chip + per memory region" to "+ per registered stateful device", names the `save_state/load_state` signature convention as the promotion path to `ichip`, and lists the known follow-on consumers (Genesis `cart_sram_runtime` latches, `cart_banking_runtime::bank`, SMS mapper latches if any). Follows the front-matter format of `0021`/`0023` so `adr_lint.py` (G9) passes.

---

## Phased sequencing (each phase = one reviewable PR)

| Phase | Content | What it proves / exit criterion |
|---|---|---|
| **1** | `page_guard` race fixes: per-thread pending *array* + locked live-watch revalidation in the single-step path; two new deterministic tests in `page_guard_proof.cpp` (cross-page double-fault; handler-self-`unwatch`). Windows-only code paths, but tests run on existing `windows-msvc-debug/release` jobs immediately. | Existing Windows CI executes the fixed VEH; both races have red-before/green-after regression tests. |
| **2** | `windows-msvc-asan` preset (flags override dropping `/RTC1`, `/INCREMENTAL:NO`, `MNEMOS_BUILD_APPS=OFF`) + CI job running `ctest -R "^mnemos_apm"`. | The riskiest tree code — VEH fault handler (with Phase 1 fixes), tagged allocator, bank registry, C-ABI binding incl. `b4292e0` exception barriers — *executes under ASan* for the first time. Validates the ASan-vs-VEH ordering question empirically. |
| **3** | Un-gate `mnemos_apm_genesis_binding_test` on Linux (move the `WIN32` gate inside `apm/bindings/genesis/CMakeLists.txt` to cover only the `SHARED` target; `host/` stays gated). | The exception-barrier code runs under the existing `linux-clang-asan` (ASan+UBSan) job on every PR, no new CI cost. |
| **4** | POSIX `page_guard`: refactor into platform-neutral core + win32/posix backends; `mmap`/`mprotect`/SIGSEGV/SIGTRAP-TF implementation; per-test `ASAN_OPTIONS` scoping; `reserve()` + documented no-allocation constraint on `guard_handler`; `supported()==true` only on x86-64 Linux. | All five page_guard tests (3 existing + 2 from Phase 1) go from vacuous-skip to real under `linux-clang-asan`; the shared core (including the race fixes) is sanitizer-tested on every PR, not just the Windows job. |
| **5** | EEPROM `save_state`/`load_state` methods + ADR-0021 validation + unit round-trip tests (incl. mid-transaction: clock in EWEN + half a WRITE on the 93C46, half a control byte on the I2C; save; load into fresh device; finish the transaction; assert the write lands / is gated correctly). Lives entirely in `src/chips/storage/`. | The FSMs and the EWEN latch are serializable and semantically validated, proven at chip level. |
| **6** | `save_device` slot in `src/runtime/save_state.{hpp,cpp}` + container tests in `src/runtime/tests/save_state_test.cpp` (round-trip, unknown-device-chunk skip, `fail()` propagation through a device load). | Container supports the third chunk source without disturbing ADR-0008 framing, CRC, or skip semantics. |
| **7** | System wiring: `make_save_target()` helpers for `sms_runtime` (registers `"cart_eeprom"` = `eeprom_enabled` + device, gated on `eeprom_93c46_active`) and `genesis_runtime` (`"cart_eeprom"` = `scl`/`sda` + device); system-level integration test proving an in-flight EEPROM transaction survives a state round-trip on the real bus path (pattern: `src/manifests/genesis/tests/genesis_eeprom_test.cpp`). | End-to-end: the originally-reported silent loss is impossible for any future caller that uses the helper. |
| **8** | `docs/adr/proposed/0024-save-state-stateful-devices.md` (could also ride with Phase 6). | Decision is recorded in the governance pipeline (G9 lint clean). |

Phases 1–4 and 5–8 are independent streams; within each, order is load-bearing (1 before 2 so the ASan job lands green on fixed code; 5 before 6 before 7).

## Open questions needing human sign-off

1. **Windows ASan scope/cost:** is the scoped `-R "^mnemos_apm"` job acceptable long-term, or should it widen to the full headless suite once timing on `windows-latest` is measured? (Full-suite Windows ASan is a separate, larger commitment.)
2. **Is the POSIX page_guard wanted as a product capability or purely as test infrastructure?** The tracer host (`apm/host/`) remains Windows-only either way (`LoadLibrary`); Phase 4 is justified as CI coverage, but someone should confirm a Linux tracer isn't on the roadmap (which would change the backend's ambition level).
3. **Per-test `ASAN_OPTIONS=handle_segv=0` scoping** (Phase 4) disables ASan's segv reporting for that one test binary — acceptable?
4. **Device chunk granularity/naming:** one `"cart_eeprom"` chunk per system vs. separate `"cart"` umbrella covering sram/banking latches later; and whether the EEPROM `store_` belongs inside the device chunk (proposed) vs. as a parallel `save_memory` entry — interacts with how `.srm` and savestates should reconcile on load.
5. **ADR numbering and whether Phase 7 should also wire the CLI** (`tools/mnemos_runtime_cli/cli.cpp` currently rejects SMS save/load) — proposed as a follow-up, not part of this work.

## Non-goals

- No Linux tracer host (no `dlopen` port of `apm/host/plugin_host.cpp`).
- No ARM64 page_guard (Windows or Linux); `supported()` stays honestly false there (per the `b4292e0` rationale).
- No TSan job (the race fixes are validated by deterministic single-threaded reproductions, not a new sanitizer dimension).
- No promotion of EEPROMs (or datasette/c1541) to full `ichip`; no factory registration changes.
- No save-state format version bump (ADR-0021 rule 3: pre-release symmetric extension) and no `.srm` file-format changes.
- No change to rewind (`src/runtime/rewind.hpp`) or player adapters beyond what `make_save_target` helpers require.

### Critical Files for Implementation

- /home/user/Mnemos/apm/memory/page_guard.cpp
- /home/user/Mnemos/CMakePresets.json
- /home/user/Mnemos/.github/workflows/ci.yml
- /home/user/Mnemos/src/runtime/save_state.hpp (and .cpp)
- /home/user/Mnemos/src/chips/storage/eeprom_93c46/eeprom_93c46.hpp (and eeprom_i2c counterpart)
