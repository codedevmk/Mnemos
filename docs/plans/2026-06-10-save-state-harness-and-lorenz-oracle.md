# Save-State Fuzz/Divergence Harness + Lorenz CIA Oracle

Status: Draft, awaiting review
Date: 2026-06-10

Plan for two test-infrastructure items from the 2026-06-10 code-review
session (session https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF):
a harness that mechanically guards the save->load->continue determinism
contract (ADR-0021) for every registered chip and assembled system, and
wiring the Wolfgang Lorenz suite as the cycle-exact oracle for the CIA 6526
timer fixes. This document is design + sequencing for sign-off; it
implements nothing.

---

## Scope

Two work items that share one theme — mechanically enforcing the save-state contract:

1. **A save-state fuzz/divergence harness** that guards the save → load → continue determinism contract for every registered chip and every assembled system, catching both failure classes found in review: omitted live-timing fields (fixed in commit 4379816 for `m68000` / Genesis VDP) and trusted decoded values (fixed via semantic clamps + `state_reader::fail()`, `docs/adr/proposed/0021-save-state-semantic-validation.md`).
2. **The Wolfgang Lorenz suite wired as a CIA 6526 oracle**, validating the latch+1 timer period and the force-load/start-delay pipeline (commit a8f3037, `src/chips/bus_controller/cia_6526/cia_6526.cpp:283-298`) cycle-exactly, following the repo's data-gated conformance pattern (ADR-0006).

## Ground truth (what the code provides today)

- **Save-state container** (`src/runtime/save_state.{hpp,cpp}`): `write_save_state(save_target)` produces magic + header + zstd body of per-chip/per-memory chunks + trailing CRC32; `read_save_state` validates magic/version/manifest-id/CRC, decompresses (256 MiB cap), and dispatches chunks by id. `load_result::ok()` is the only failure channel; per ADR-0021, chips poison the reader via `state_reader::fail()` (`src/chips/shared/state.hpp:55`).
- **Important gap**: `read_save_state` calls `sc.chip->load_state(cr)` per chunk (`save_state.cpp:171-178`) but **never checks `cr.ok()` afterward** — a chip that calls `fail()` is silently half-loaded and the container still returns `load_status::ok`. The harness will hit this immediately; fixing it is a prerequisite phase.
- **Chip enumeration**: 34 chips self-register via `register_factory` into `chip_registry` (`src/chips/shared/chip_registry.cpp:112` exposes `registered_factories()`); registration lives in each chip's main `.cpp`, pulled in when manifest libraries are linked (the builder instantiates via `chips::create_chip`, `src/manifests/common/builder.cpp:167`).
- **System assembly**: each system has a hand-wired `assemble_*` (e.g. `src/manifests/c64/c64_system.hpp`, zero-fill ROMs explicitly supported) and a manifest-path `build_*_runtime` (`c64_runtime.hpp`, `genesis_runtime.hpp`). `save_target` wiring exists **only** in `tools/mnemos_runtime_cli/cli.cpp:714-736`, and only for the C64. Genesis adds gated CPU wrappers (`src/manifests/common/gated_chip.hpp`) and non-chip glue state (`genesis_runtime.hpp`: `sram`, `eeprom`, `banking`, `genesis_callbacks_state`) that no `save_target` covers yet.
- **Scheduler** (`src/runtime/scheduler.hpp`): per-chip divider accumulators are **not** part of any save state. The C64 is uniform-lockstep (all dividers 1) so a fresh scheduler resumes phase-correct; the Genesis schedule uses dividers 7/15 (`genesis_runtime.hpp:71-77`), so restoring into a fresh scheduler loses sub-divider phase — an expected first finding of the harness.
- **Data-gating pattern**: env-var + Catch2 `SKIP` + `SKIP_RETURN_CODE 4` (`tests/golden/CMakeLists.txt`, `src/chips/cpu/m6510/tests/m6510_conformance_test.cpp:161`, `src/chips/cpu/z80/tests/z80_singlestep_test.cpp:248`); oracle suites are registered in `tests/oracles/registry.yaml` and ratcheted by `tools/governance/oracle_runner.py` in the `linux-gcc-release` CI job (`.github/workflows/ci.yml:145-149`). The `linux-clang-asan` job runs the full ctest suite, so any always-on test is automatically sanitizer-covered.
- **Introspection**: `m6510` exposes `trace_target` (per-instruction PC) and a `register_view` naming `A`/`PC` (`src/chips/cpu/m6510/m6510.hpp:218`, `m6510.cpp:1457-1463`) — sufficient for KERNAL-call observation without patching execution.
- **Lorenz status**: the CIA NOTES already name Lorenz as the oracle and defer suite validation to system integration (`src/chips/bus_controller/cia_6526/NOTES.md:37-39`); `tests/oracles/registry.yaml` has no `mos.6526` or C64-machine entry yet.

---

# Item 1 — Save-state fuzz/divergence harness

## Architecture

Three layers, ordered cheapest-to-richest, all deterministic (seeded splitmix64 PRNG in a test-support header — no PRNG exists in `src/foundation`, and `std::uniform_int_distribution` is not cross-platform-stable, so the helper uses raw engine output only):

**Layer A — per-chip, registry-driven (always-on).** For every `chip_factory_descriptor` in `registered_factories()`:
- *Round-trip identity*: create → `configure({},{})` → `reset(power_on)` → optional stimulus (below) → `save_state` → load into a fresh instance → `save_state` again → byte-equal chunks, and `reader.ok()` true with `remaining() == 0` (catches both under-read and over-read asymmetries — the 4379816 class at chip granularity).
- *Robustness fuzz*: from the baseline chunk, generate K seeded mutants per strategy — single/multi byte flips, truncations (including empty), extensions, full-random buffers, all-0x00/all-0xFF — and `load_state` each into a fresh instance. The only assertions: no crash/OOB (ASan/UBSan job is the real referee) and, when the reader survives with `ok()`, the resulting state must itself re-serialize and reload cleanly (accepted ⇒ self-consistent). Mutants are *not* required to fail `ok()` — many mutations are semantically valid alternate states.
- *Stimulus for non-trivial baselines*: reset-state round-trips miss live fields that are zero at reset. For chips implementing `immio` (8 chips today: both VDP-class video chips, VIC-II, SID, CIA, VIA, REU, c64_cartridge — `dynamic_cast<chips::immio*>`), interleave seeded `mmio_write`s with `tick`s before saving. CPUs (`icpu`) get a trivial seeded-RAM `ibus` attached but are **not ticked** in phase 1 (executing random opcodes is out of scope); other chip classes are ticked without stimulus.

**Layer B — container-level fuzz (always-on).** Exercise `read_save_state` itself: header truncations, version/manifest-id corruption, zstd frames with hostile declared sizes, chunk-count vs body mismatches, and — the valuable class — *CRC-valid hostile bodies*: mutate the decompressed body (chunk ids, sizes, payloads), recompress, and recompute the trailing CRC using `security::cryptography::crc32`, so mutants reach the chunk parser and chip `load_state` paths instead of dying at `bad_crc` (`save_state.cpp:147`). Targets: the stub-chip target from `src/runtime/tests/save_state_test.cpp` plus a real assembled C64.

**Layer C — whole-system divergence (the contract test).** Per system scenario:
1. Assemble machine A; run `W` warm-up frames via `runtime::scheduler::run_frames` (frame-aligned save points only, matching how rewind and the CLI use the format).
2. `blob = write_save_state(target_A)`.
3. Run A for `M` more frames, recording a trace: per frame, the SHA-256 of each chip's serialized chunk (serialize via a bare `state_writer` — no container needed), each memory region, and the framebuffer hash.
4. Assemble fresh machine B; `read_save_state(blob, target_B)` must be `ok()`; build a fresh scheduler.
5. Run B for `M` frames recording the same trace; compare byte-exact. On mismatch, report the first divergent frame **and the divergent chip id** — per-chunk hashing buys chip-level attribution for free, which is what makes the harness actionable.

Scenario stimulus: the always-on C64 scenario uses zero-fill ROMs *except* a tiny hand-authored 6502 program placed in a synthetic "KERNAL" image (our own bytes — no copyright issue; `assemble_c64` documents zero-fill support) that programs CIA timers (continuous, one-shot, cascade, force-load mid-run), VIC raster IRQs, and SID writes, then loops. This makes the divergence check sensitive to exactly the omitted-live-timing-field class. Data-gated variants re-run the same protocol on real ROMs (`MNEMOS_C64_ROM_DIR`) for organic coverage.

## File / target layout

```
tests/CMakeLists.txt                          # + add_subdirectory(integration)
tests/integration/
  CMakeLists.txt                              # mnemos_add_test targets (MnemosTesting.cmake)
  support/
    state_fuzz.hpp                            # splitmix64, mutation ops, CRC-fixup container rebuild
    system_probe.hpp                          # probe = { scheduled chips, runtime::save_target, ivideo* }
    c64_probe.hpp / .cpp                      # wraps assemble_c64; save_target mirrors cli.cpp:714
    sms_probe.hpp / .cpp                      # phase S4
    genesis_probe.hpp / .cpp                  # phase S5
  chip_state_roundtrip_test.cpp               # Layer A identity   (always-on)
  chip_state_fuzz_test.cpp                    # Layer A fuzz       (always-on)
  container_fuzz_test.cpp                     # Layer B            (always-on)
  c64_state_divergence_test.cpp               # Layer C            (always-on + data-gated section)
  sms_state_divergence_test.cpp               # Layer C            (always-on, synthetic Z80 cart)
  genesis_state_divergence_test.cpp           # Layer C            (phase S5)
```

Probes live in test support, not `src/manifests`, because the manifests tier cannot name `runtime::save_target` (the tiering note in `genesis_runtime.hpp:33-35` makes this explicit). Tests link the system manifest libraries (`mnemos::manifests::c64` etc.) to populate the chip registry; the registry-driven tests assert a floor (`registered_factories().size() >= 30`) so a silent linker drop of registration TUs fails loudly rather than shrinking coverage.

Env knobs (all optional, defaults sized for CI): `MNEMOS_STATE_FUZZ_SEED`, `MNEMOS_STATE_FUZZ_ROUNDS` (soak multiplier for local/nightly runs), `MNEMOS_C64_ROM_DIR` (existing) for the data-gated divergence section.

## Phasing (each phase is one PR)

- **S0 — loader honours chip rejection.** In `read_save_state`, check `cr.ok()` after each `sc.chip->load_state(cr)` (and `cr.remaining()==0`) and surface a new `load_status::chunk_rejected`. Without this, ADR-0021's `fail()` channel is dead code at the container level and every fuzz assertion downstream is meaningless. Includes unit tests in `src/runtime/tests/save_state_test.cpp`. *Proves: the rejection channel actually rejects.*
- **S1 — scaffolding + Layer A.** `tests/integration` CMake wiring, `state_fuzz.hpp`, round-trip identity + fuzz tests over all registered chips (reset-state baselines, `immio` stimulus). *Proves: no chip OOBs on hostile chunks; save/load symmetry for every registered chip; sanitizer job now exercises every `load_state` with adversarial input on every PR.*
- **S2 — Layer B container fuzz.** CRC-fixup mutation helper + hostile-container corpus against stub and real-C64 targets. *Proves: the container parser and chunk dispatch are robust independent of chip behaviour.*
- **S3 — C64 divergence.** `c64_probe`, synthetic timer-stress program, always-on divergence test with per-chip attribution; data-gated real-ROM section. *Proves: the full save → load → continue contract for the richest currently-wired system; mechanically guards the 4379816 and a8f3037 fixes.*
- **S4 — SMS/GG divergence.** `sms_probe` + synthetic Z80 cart scenario. *Proves: the protocol generalizes; covers `sms_vdp`, `sn76489`, mappers.*
- **S5 — Genesis divergence.** Requires two design resolutions first (open questions 1–2 below): scheduler divider-phase capture and glue-state chunks (`sram`/`eeprom`/`banking`/callback latches). Expect this phase to *find* gaps, then fix them in the same or follow-up PRs. *Proves: divider-based systems and gated CPUs resume deterministically.*
- **S6 — oracle registration + soak knobs.** Add the always-on suites to `tests/oracles/registry.yaml` (data_gated: false) so the G5 ratchet pins them; document soak usage. *Proves: the harness itself can't silently vanish from CI.*

## Runtime budget

Always-on additions target < 60 s total in the debug/ASan presets: Layer A ≈ 34 chips × ~64 mutants × (load only, no ticking) — seconds; Layer B — sub-second; Layer C C64 ≈ 90 PAL frames × 2 machines ≈ 3.5 M chip-ticks/chip — a few seconds even under ASan. Comfortably inside the ≤ 5 min PR budget; soak depth is opt-in via env.

---

# Item 2 — Lorenz suite as the CIA 6526 oracle

## Decision: full C64 machine, not a CIA-only vector format

The Lorenz tests are C64 programs whose value is precisely that they measure timer behaviour *through* cycle-exact 6510 bus reads (the latch+1 period and the LOAD → first-underflow-at-`C_w+N+3` pipeline are visible only as read-back counter values at exact CPU-cycle offsets). Hand-translating them to a CIA-only vector format would re-introduce the interpretation step the oracle exists to remove. The full machine exists (`src/manifests/c64`), boots under golden test today, and `cia_6526` already serializes the pipeline fields (`cia_6526.cpp:687-688`). This mirrors how ZEXALL oracles the Z80 through a full SMS (`tests/golden/sms_zexall_test.cpp`).

## Harness mechanics (`tests/golden/c64_lorenz_test.cpp`)

1. **Gating**: `SKIP` unless both `MNEMOS_C64_ROM_DIR` (existing convention) and a new `MNEMOS_LORENZ_DIR` (directory of the suite's `.prg` files) are set; `SKIP_RETURN_CODE 4` in `tests/golden/CMakeLists.txt`. Optional `MNEMOS_LORENZ_ONLY` (name filter) and `MNEMOS_LORENZ_MAX_FRAMES` (per-test cap), mirroring `MNEMOS_M68000_ONLY` / `MNEMOS_SMS_ZEXALL_MAX_FRAMES` precedents.
2. **Boot**: real ROMs via `assemble_c64`, scheduler order as in `tests/golden/c64_basic_boot_test.cpp:133-136`; run until the BASIC `READY.` prompt appears in screen RAM (`$0400` scrape, PETSCII), bounded by a frame cap.
3. **Injection (per test PRG)**: copy the PRG payload to its 2-byte load address (normally `$0801`) directly into `sys->ram`, fix the BASIC program-end pointers, and place `RUN<CR>` in the KERNAL keyboard buffer (`$0277` + count at `$C6` — documented C64 PRG locations, no input-matrix timing dependence).
4. **Observation (no execution patching)**: install the m6510 per-instruction trace callback (`m6510.hpp:218`) and watch PCs:
   - `PC == $FFD2` (CHROUT jump-table entry): read `A` from the register view (`m6510.cpp:1457`) and accumulate the PETSCII output stream.
   - `PC == $FFD5` (KERNAL LOAD entry): Lorenz tests chain by LOADing the next test — reaching LOAD with no `error` in the accumulated output marks the current PRG **passed**; the harness stops there and proceeds to the next file itself (one fresh machine per PRG keeps tests independent and failures attributable).
   - Failure: output containing the suite's error report, or the per-test frame cap expiring. Border colour (`VIC $D020` via `mmio_read`) is logged as a secondary diagnostic.
5. **Test selection**: default list = the CIA-relevant programs (`cia1ta`, `cia1tb`, `cia2ta`, `cia2tb`, `cia1tab`, `cia1pb6`/`pb7`, `cia2pb6`/`pb7`, `imr`, `icr`, `oneshot`, `flipos` — final names confirmed against the obtained suite), each its own Catch2 `SECTION` for granular reporting. A full-suite mode (env flag) is a stretch goal that simultaneously oracles the 6510 (the constitution already lists Lorenz for the 6510, `constitution/MNE-CTX-PLAN-001.md:223`).
6. **Provenance discipline**: trap addresses and chaining behaviour are derived from the C64 Programmer's Reference (KERNAL jump table) and the Lorenz suite's own documentation/disassembly — **not** from VICE source, which is GPL and barred by ADR-0006 rule 2. Record this in the test header and update `src/chips/bus_controller/cia_6526/NOTES.md:37` to point at the harness.

## Phasing

- **L1 — harness skeleton.** Boot-to-READY helper, PRG injection, keyboard-buffer RUN, CHROUT capture, LOAD-chain detection; validated against one simple suite program end-to-end. *Proves: the runner mechanics, independent of CIA correctness.*
- **L2 — CIA subset + oracle registration.** Enable the CIA test list; add `mos.6526` (suite `ORC-CIA-LORENZ`, `data_gated: true`, `data_env: MNEMOS_LORENZ_DIR`) and a `machines.c64` golden entry to `tests/oracles/registry.yaml`; update CIA NOTES. *Proves: latch+1, force-load `C_w+N+3`, start-delay gating, and the NMOS ICR edge behaviour, cycle-exactly — the validation a8f3037 was missing.*
- **L3 (optional) — full-suite mode.** Env-gated all-programs run for 6510/VIC reuse; registered as an additional oracle when stable.

CI impact: zero on hosted runners (skips, like every data-gated suite); the oracle ratchet (`ci.yml:145`) records pass high-water on data-equipped runs.

---

## Open questions needing human sign-off

1. **Scheduler divider phase**: the save container records `master_cycle` but not per-chip divider accumulators (`scheduler.hpp:50`); Genesis (dividers 7/15) cannot resume phase-exactly into a fresh scheduler. Extend the format (permitted pre-release per ADR-0021 rule 3 — symmetric same-commit change), or constrain saves to divider-aligned/frame-aligned points and document it? This blocks phase S5 only.
2. **Genesis glue state**: `cart_sram_runtime`, `cart_eeprom_runtime`, `cart_banking_runtime`, and gate-predicate latches in `genesis_callbacks_state` need chunk representation (extra `save_memory` regions vs a pseudo-chip chunk). Design needed before S5.
3. **Long-term home for `save_target` wiring**: the test probes will initially mirror `cli.cpp:714-736` (drift risk between CLI and tests). Should a tier-appropriate adapter own this so CLI, player, rewind, and tests share one builder?
4. **New `load_status::chunk_rejected` (phase S0)**: confirm extending the enum + the CLI's error print (`cli.cpp:745`) is acceptable as the surfacing mechanism, vs folding rejection into `truncated`.
5. **Lorenz chaining convention**: confirm `$FFD5`-entry trapping against the actual suite disassembly once the data is in hand; fall back to per-frame screen-RAM scraping as the pass/fail detector if any program bypasses the jump table.
6. **Budget placement**: always-on divergence scenarios in the PR suite (recommended, they're seconds) vs nightly-only; and whether the fuzz default round count should differ between the ASan job and the plain debug jobs.

## Non-goals

- **No backward-compatibility testing of old save files** — ADR-0021 explicitly defers format stability until it becomes an interchange surface; the harness tests *current-build* round-trips only.
- **No libFuzzer/OSS-Fuzz/coverage-guided fuzzing** — seeded deterministic mutation under the existing `linux-clang-asan` ctest job only; a fuzzer entry point is a possible future addition, not this work.
- **No fixing of divergences the harness finds** — each finding (e.g. the Genesis scheduler-phase gap) is its own follow-up PR; the harness PRs land with known-failing scenarios disabled-and-tracked rather than blocked.
- **No random-opcode CPU execution fuzzing** — CPUs are fuzzed at the `load_state` surface only; instruction-level correctness stays with the existing SST/ZEXALL/Lorenz oracles.
- **No rewind/delta-encoding work** — `rewind_ring` benefits transitively from the divergence guarantee but is not modified.
- **No Lorenz-based ratification of the 6510 or VIC-II in this work** — the CIA is the target; broader reuse is the optional L3.
- **No committed test data** — ROMs and the Lorenz corpus remain env-var-gated per ADR-0006.

### Critical Files for Implementation

- /home/user/Mnemos/src/runtime/save_state.cpp — loader must start honouring `state_reader::fail()` (phase S0); container fuzz target
- /home/user/Mnemos/src/chips/shared/chip_registry.hpp — `registered_factories()` drives the generic per-chip harness
- /home/user/Mnemos/tools/mnemos_runtime_cli/cli.cpp — the only existing `save_target` wiring (lines 714–736), template for the system probes
- /home/user/Mnemos/tests/golden/CMakeLists.txt — data-gating/`SKIP_RETURN_CODE 4` pattern and home of the Lorenz test target
- /home/user/Mnemos/src/manifests/c64/c64_system.hpp — `assemble_c64` (zero-fill ROM support) used by both the divergence probe and the Lorenz runner
