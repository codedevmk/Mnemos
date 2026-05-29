# Manifest Path Migration — Make `build_system()` Production

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax. **DO NOT START** any phase until the corresponding "Pre-Phase Decisions" block at the top is resolved with the user.

> **Status (2026-05-29):** **Phase A COMPLETE. Phase B.1 COMPLETE — SMS adapter fully cut over to the manifest path.** The player's `sms_adapter` now builds via `build_sms_runtime()` (embedded manifest + `build_system` + SMS host callbacks); `assemble_sms` is retained UNCHANGED as the independent byte-for-byte parity oracle (the planned "thin wrapper" was rejected because it would make the parity tests tautological). Added: the Codemasters-mapper manifest variant (`sms.ntsc/pal.codemasters.toml`) + parity test; manifests embedded as compiled string resources via `configure_file` (single source of truth, `manifest_toml(region, codemasters)`), DRY-ing the previously hand-copied test manifests; the orphaned `sms_manifest_parity_test` wired into the data-gated runner. Verified: full suite 52 ctest entries, 0 failures; SHA-pinned `sms_boot_test` golden hash unchanged; Sega+Codemasters+runtime parity all byte-identical to `assemble_sms`. Commits `1745305`, `dc57c9f`, `e7b8d91`, `a6f0c30`, `560bad7`. **Next: B.2 (Genesis).**

> **Status (2026-05-27 end-of-session):** **Phase A COMPLETE. Phase B.1 FUNCTIONALLY COMPLETE** (SMS manifest path produces a runnable system, validated end-to-end). All eleven sub-phases (A.1–A.5 + B.1.1–B.1.5) landed in a single session. `build_system()` expresses every production-system mechanism `assemble_*()` relies on, and the SMS smoke test exercises every mechanism through one end-to-end pipeline (parse → build → configure → callbacks → mirror RAM → mapper overlay → mmio_block factory → CPU step). Pre-phase decisions D1–D5 resolved. 49/49 ctest entries pass; BoV f=120 still byte-perfect 0/215040 vs the reference after every commit.

> **Phase A commits:**
> - A.1 `c3c8484` — per-chip `[chip.config]` TOML
> - A.2 — callback registry plumbing
> - A.3 — declarative chip gating (`[[gate]]` + system-agnostic `gated_chip`)
> - A.4 `5a4048c` — generic mapper overlays (`[[bus.region]] backing="mapper"`)
> - A.5 `80164e7` — system-specific MMIO escape (`[[mmio_block]]`)

> **Phase B.1 commits:**
> - B.1.1 `8bf3a2d` — z80 port_in/port_out callbacks
> - B.1.2 `0b41ba6` — sms_mapper + codemasters_mapper read_overlay/write_overlay
> - B.1.2b `5a6c10c` — builder RAM-region mirroring via `size < range`
> - B.1.3 `495d688` — SMS NTSC/PAL TOML manifests + sms_vdp::configure
> - B.1.4 — sms_callbacks host helper (`sms_callbacks_state` + `make_sms_host_tables`)
> - B.1.5 `b26c30f` — end-to-end manifest-path smoke test (10/10 assertions pass on first run)

> **What's left for full B.1 ship:** ~~adapter cutover~~ **DONE (2026-05-29).** Rather than restructuring `sms_system` into a thin wrapper (which would nullify the parity oracle), a parallel `sms_runtime` + `build_sms_runtime()` was added in `manifests/sms` and the adapter holds that instead. `assemble_sms`/`sms_system` are untouched and remain the parity oracle. Real work beyond "plumbing" surfaced during implementation: the Codemasters mapper needed its own manifest (auto-detected carts would otherwise regress), and runtime manifest delivery needed solving (embedded as a compiled resource).

**Goal:** Migrate every supported system (today: SMS, Genesis, C64; future: 32X, Sega CD, Saturn, Amiga, CPS1, CPS2) from hand-written `assemble_*()` C++ functions to declarative TOML manifests consumed by an extended `build_system()`. After completion, `manifests/<system>/<system>_system.cpp` is gone; adding a new system means writing a `.toml` file plus any genuinely-new chip implementations.

---

## Rationale — manifest vs adapter, honest trade-offs

This migration is a bet. It's defensible, but not free. Both paths now exist in tree; this section captures the comparison so future maintainers can re-evaluate as the roster evolves.

### Where the manifest path wins

- **System as data, not code.** `sms.ntsc.toml` (90 lines) answers "what chips, where, with what wiring" in 30 seconds of reading. `assemble_sms()` (214 lines of imperative C++) takes 5+ minutes for the same question.
- **Uniform schema across systems.** Adapters invent per-system initialization patterns (Genesis sets `set_irq_ack_callback` / `set_tas_callback` / `set_z80_bus_latency_enabled` via direct method calls; C64 wires its PLA differently; Saturn would be different again). Manifests fix the *vocabulary*: every system uses the same 5–6 constructs (`[chip.config]`, `[[bus]]`, `[[bus.region]]`, `[[gate]]`, `[[mmio_block]]`, callback IDs). A reader who learns one manifest reads all of them.
- **Tooling becomes possible.** Once systems are TOML, validators, diagram generators, schema documentation, machine-readable catalogues, and fuzzers can operate without a C++ link. None of these exist today; the cost of building them once data is structured is low.
- **Variants without code duplication.** PAL vs NTSC SMS is one config bit (`[chip.config] pal = true`). Adapters typically branch in code or add a parallel assembler per regional variant.
- **Scale break-even.** For 1–3 systems the adapter path is simpler (fewer moving parts, direct field access). For 9+ systems the shared builder amortises across them and the per-system cost collapses to one TOML + one small callbacks helper. The target roster (SMS, Genesis, 32X, Sega CD, Saturn, C64, Amiga, CPS1, CPS2) puts this squarely past the break-even.

### Where the manifest path costs

- **More lines, not fewer, per system.** B.1.4's `sms_callbacks.cpp` (200 LOC of closures) + `sms.ntsc.toml` (90 LOC) + each chip's `configure()` consumer is *more* code than `assemble_sms` (214 LOC). The win isn't density; it's that the structure is *uniform across systems*.
- **Indirection complicates debugging.** Stack traces traverse `build_system → chip.configure → find_callback<Sig> → variant access → captured `state*` → state.cpu->set_irq_line()`. The adapter path's call chain is `assemble_sms → bus.map_mmio(...) → lambda → cpu.set_irq_line()`. Three more layers in every fired callback. The TOML itself never appears in a stack trace.
- **Chicken-and-egg with chip pointers.** Closures need chip pointers; chips are constructed *by* `build_system`. Today's workaround is `sms_callbacks_state` with pointers populated after the build returns. Adapters dodge this because chips are value-owned and addressable from the host directly.
- **Schema churn fan-out.** A new chip config knob touches three sites: the TOML, the parser, the chip's `configure()`. Adapters touch one site (the call). For stable systems gaining a new tweak, adapter wins; for new systems with many tweaks, manifest wins.
- **Callbacks are `std::function` on hot paths.** Adapter lambdas can be captured by reference into the bus; manifest callbacks go through type-erased variant lookup. The bus_bench measured `std::function` at ~0.2 ns/call — negligible — but it's measurable and the adapter path doesn't have it.

### Realised vs future benefits

| Benefit | Today (post-B.1) | Roster fully populated |
|---|---|---|
| Reading "what's a Genesis" from TOML | partial (SMS) | yes |
| Uniform schema across systems | partial | yes |
| Adding a new system | n/a | TOML + callbacks ~1 day |
| Variants via config | yes | yes |
| External tooling | nothing built | possible |
| Hot-swap systems | no | could be added |
| Mod support | no | could be added |

### Honest verdict

If Mnemos were ever going to be a 3-system project forever (SMS + Genesis + C64), the manifest path's indirection cost would exceed its benefit and `build_system()` would be deletable dead code. Adapter path keeps it simple.

The migration is justified *because the roster is ambitious*. Nine systems including multi-CPU machines means the alternative is 9 hand-written assemblers totalling ~2500 LOC of subtly-different wiring patterns. The manifest path collapses that into 9 TOMLs + 9 small helpers on top of one shared builder. The real payoff is **cross-system uniformity**, not line count or runtime mechanism.

A new contributor 6 months from now sees `genesis_callbacks.cpp`, `sms_callbacks.cpp`, `c64_callbacks.cpp`, `amiga_callbacks.cpp` — same shape, same vocabulary. Today's parallel-but-different `assemble_*()` files don't have that property.

That's the bet.

---

## Pre-Phase Decisions (block all coding until resolved)

These five design choices fundamentally shape the rest of the plan. Walk through them with the user before any code lands.

### D1. Callback registry pattern OK as the escape hatch for non-declarative wiring? ✅ RESOLVED — recommended pattern adopted in A.2.

The 70% of system assembly that today's manifest schema can't express (inter-chip IRQ wiring, VDP→CPU DMA-read callbacks, Genesis TAS suppression, Z80 BUSREQ arbitration, etc.) needs a structured escape hatch. Proposal: manifests name callbacks by string ID; the host (player binary, runtime CLI, tests) supplies a `{name → function}` map alongside the ROM provider.

```toml
[[chip]]
id = "cpu"
type = "motorola.68000"
[chip.config]
tas_suppress = "genesis.tas_suppress"     # named callback
irq_ack = "genesis.vdp_irq_ack"
```

```cpp
build_system(manifest, roms, callbacks{
    {"genesis.tas_suppress", [&](u32 addr) { /* no-op */ }},
    {"genesis.vdp_irq_ack",  [&sys](int level) { sys.vdp.acknowledge_irq(level); }},
});
```

**Alternative:** entirely declarative, where every callback is expressed in TOML (impossible for non-trivial logic like Genesis's TAS write-suppression — requires actual code).

**Recommended:** callback registry. **Decision needed:** confirm.

### D2. System-specific MMIO escape hatch: named handlers vs free-form code? ✅ RESOLVED — named factory pattern adopted in A.5.

Genesis's $A10000-$A1001F controller block, $A11100 BUSREQ, $A11200 Z80 RESET, $A12000-$A12FFF Z80 bank window are all hardcoded MMIO handlers today. Two options to express them:

- **D2.a (recommended)** — manifest names a "system-specific MMIO type" (e.g., `genesis.controller_block`), the host registers that name with a factory that returns `read/write` handlers.
- **D2.b** — manifest declares each byte-level register inline. Verbose; doesn't work for stateful blocks.

**Recommended:** D2.a (same pattern as D1 callbacks, generalized to MMIO regions). **Decision needed:** confirm.

### D3. Co-existence policy — how long do both paths run side by side? ✅ RESOLVED — both paths in tree; atomic per-system cutover when parity proven. Phase A established the additive foundation; B.1 begins with both paths live.

Migrating Genesis is a multi-day effort. During that time, the manifest path and `assemble_genesis()` will both exist. Two options:

- **D3.a (recommended)** — Both paths live in tree. The player adapter keeps using `assemble_genesis()` until the manifest path is byte-perfect against the parity harness, then atomically switches. Each system migrates independently (SMS first, then Genesis, then C64).
- **D3.b** — Cutover from day one; broken Genesis is acceptable during migration. **Faster but high-risk for a memorial project.**

**Recommended:** D3.a. **Decision needed:** confirm.

### D4. Migration order — SMS first as smoke test, or Genesis first to flush out everything? ✅ RESOLVED — SMS → Genesis → C64.

- **D4.a (recommended)** — SMS first (smallest, 214 LOC). Validates the framework with minimum complexity. Then Genesis (most complex, 311 LOC) which stress-tests every feature. Then C64.
- **D4.b** — Genesis first. Forces every feature to be designed upfront; nothing falls between the cracks. Higher upfront cost; higher risk of a stuck migration.

**Recommended:** D4.a. **Decision needed:** confirm.

### D5. Parity verification budget — which ROMs gate each migration step? ✅ RESOLVED — load-bearing gate is BoV byte-perfect 0/215040 at f=120 after each commit; all 6 Genesis pixel-perfect titles validated at f=120/240/360/480 before B.2 ships; C64 BASIC boot test after B.3.

Per [[genesis-four-title-parity]], the load-bearing check is BoV/Sonic 1+2/SoR/AB/TF3 at f=120 byte-perfect 0/215040 vs the reference. For each migration step (SMS → Genesis → C64):

- **All 6 Genesis pixel-perfect titles** at f=120/240/360/480 must remain 0/215040 after Genesis migration.
- **C64 BASIC boot** (data-gated) must produce its idle state after C64 migration.
- **SMS** has no current pixel-perfect comparator (BIOSes only); use a smaller harness — boot + frame-count agreement.

**Decision needed:** which titles are non-negotiable gates, and at what frames.

---

## Realistic Scope

**12-20 days of focused work**, distributed across the phases below. Each phase has a "ship a commit, verify parity" cadence — no phase commits without green tests and (where applicable) BoV byte-perfect.

| Phase | Scope | Effort | Risk |
|---|---|---|---|
| A.1 (P5)  | Per-chip TOML config | ~2-3 days | Low |
| A.2       | Callback registry (D1) | ~2-3 days | Medium |
| A.3 (P4)  | Declarative chip gating | ~2-3 days | Medium |
| A.4 (P7)  | Generic mapper overlays | ~1-2 days | Low |
| A.5       | System-specific MMIO escape (D2) | ~2-3 days | Medium |
| B.1       | SMS migration (smoke test) | ~1-2 days | Low (if A is solid) |
| B.2       | Genesis migration | ~3-4 days | **HIGH** — parity-critical |
| B.3       | C64 migration | ~2-3 days | Medium |
| C         | Delete assemble_*() functions | ~0.5 day | Low (clean removal) |

---

## Phase A — Foundation Extensions ✅ COMPLETE (2026-05-27)

Purely additive. Existing `assemble_*()` functions continue to work throughout. All five sub-phases landed and verified in one session. Total: ~1500 LOC added across `chips/shared` (config + callbacks + predicates + mmio_factory + imapper extensions), `manifests/common` (schema additions + parser + builder pass per mechanism + gated_chip lifted from genesis), and tests (88 new assertions across 12 new catch2 cases).

### A.1 — Per-chip TOML config (P5)

Adds a `[chip.config]` table to each chip declaration. The builder calls `ichip::configure(config_table)` after construction, before `reset()`. Each chip's `configure()` parses its own keys; unknown keys produce diagnostics but don't fail the build.

**Files**
- `src/manifests/common/manifest.hpp` — extend `chip_decl` with `std::unordered_map<std::string, config_value>` config; define `config_value = std::variant<bool, std::int64_t, double, std::string>`.
- `src/manifests/common/manifest.cpp` — TOML parser fills the config map.
- `src/chips/shared/chip.hpp` — add `virtual void configure(const config_table&) {}` to `ichip` (default no-op).
- `src/chips/cpu/m68000/m68000.cpp` — implement `configure()` reading `z80_bus_latency` (bool).
- `src/chips/video/genesis_vdp/genesis_vdp.cpp` — `configure()` reading `pal` (bool).
- `src/manifests/common/builder.cpp` — call `chip->configure(decl.config)` after construction.
- `src/manifests/common/tests/manifest_test.cpp`, `builder_test.cpp` — exercise round-trip.

### A.2 — Callback registry (D1)

Manifest names callbacks by string ID; host supplies the map. Adds `callback_provider` parameter to `build_system()`.

**Files**
- `src/manifests/common/builder.hpp` — extend signature: `build_system(manifest, roms, callbacks)`. `callbacks` is a `std::unordered_map<std::string, callback_value>` where `callback_value` is a variant of common callback signatures (per-chip).
- `src/manifests/common/manifest.hpp` — `chip_decl.config` already supports strings (from A.1); a value that looks like a callback ID is recognized by the chip's `configure()`.
- `src/chips/cpu/m68000/m68000.cpp` — `configure()` reads `tas_suppress_callback`, `irq_ack_callback` from the supplied provider.
- `src/chips/video/genesis_vdp/genesis_vdp.cpp` — same for `dma_read_callback`, `irq_callback`, `delayed_irq_callback`, `vblank_callback`.

### A.3 — Declarative chip gating (P4)

Adds top-level `[[gate]]` entries to manifests, processed by builder via `gated_chip` wrappers (already exist in `genesis_system.hpp` — lift into `manifests/common/`).

**Files**
- `src/manifests/common/manifest.hpp` — `gate_decl { chip_id, predicate_id }`.
- `src/manifests/common/gated_chip.hpp` — lifted from `genesis_system.hpp:26-76` to be system-agnostic.
- `src/manifests/common/builder.cpp` — when building a chip declared in a `[[gate]]` block, wrap it with `gated_chip` keyed by the named predicate from the callback provider.
- `src/manifests/genesis/genesis_system.hpp` — delete `gated_chip` + `predicate_gated_chip` (now in common).

### A.4 — Generic mapper overlays (P7)

Mapper chip exposes its banking via the introspection surface; builder wires the overlays.

**Files**
- `src/manifests/common/manifest.hpp` — `region_decl.mapper_id` (already exists as `overlay` field, unused).
- `src/chips/shared/chip.hpp` — `imapper` gains `read_overlay(addr) -> u8`, `write_overlay(addr, val)`, `is_active(addr, write) -> bool`.
- `src/manifests/common/builder.cpp` — when a region has `mapper_id`, route reads/writes through the mapper.
- `src/chips/mapper/codemasters_mapper/codemasters_mapper.cpp` — fill in the new mapper methods.

### A.5 — System-specific MMIO escape (D2)

`mmio_block_decl` names a registered MMIO factory; host provides factories.

**Files**
- `src/manifests/common/manifest.hpp` — `[[mmio_block]] name = "genesis.controller_block", range = { start, end }, attached_bus = "main"`.
- `src/manifests/common/builder.hpp` — `mmio_block_provider` parameter (function returning `read/write` handlers given name + range).
- `src/manifests/common/builder.cpp` — wire each mmio_block onto its bus via the provider.

---

## Phase B — System Migrations

Each migration is a single PR. The PR keeps `assemble_*()` AND adds the manifest path; the player adapter is updated to use the manifest path; parity verified; only after that does the PR get merged. The hand-assembly `assemble_*()` is NOT deleted until Phase C.

### B.1 — SMS migration ✅ COMPLETE (2026-05-29)

The build_system path produces a runnable SMS system (as of `b26c30f`), and the player adapter is now fully cut over to it via `build_sms_runtime()` (commit `560bad7`). `assemble_sms` is retained unchanged as the parity oracle. Codemasters support, embedded-manifest delivery, and the data-gated parity gate all landed alongside.

**Files**
- `src/manifests/sms/sms.ntsc.toml`, `sms.pal.toml` — full system descriptions (chips, buses, regions, callbacks, gates, mmio_blocks).
- `src/manifests/sms/sms_callbacks.{hpp,cpp}` — registers SMS-specific callbacks + mmio_blocks with the build_system providers.
- `src/manifests/sms/sms_system.hpp` — KEEP `sms_system` struct as the runtime ownership glue (system_graph's `chips`/`buses`/`memory` vectors hold raw pointers; `sms_system` owns the strongly-typed handles the adapter needs).
- `src/manifests/sms/sms_system.cpp` — `assemble_sms()` is now a thin wrapper that calls `build_system()` with the SMS callbacks; ~30 lines instead of 214.
- `src/apps/player/adapters/sms/sms_adapter.cpp` — no change (uses `assemble_sms()`).
- **Acceptance:** SMS BIOS still boots; the trace CSV is non-empty (verified via z80 trace from earlier today).

### B.2 — Genesis migration

Highest-risk step. Must remain byte-perfect 0/215040 at f=120/240/360/480 for **all 6 pixel-perfect titles**.

**Files**
- `src/manifests/genesis/genesis.ntsc.toml`, `genesis.pal.toml` — chips, buses, regions including the $A10000-$A1001F controller block, $A11100 BUSREQ, $A11200 Z80 RESET, $A12000-$A12FFF Z80 bank window, VDP DMA stall logic.
- `src/manifests/genesis/genesis_callbacks.{hpp,cpp}` — TAS suppression, IRQ ack, DMA read, VBlank callback, Z80 banking, controller-port routing.
- `src/manifests/genesis/genesis_system.hpp` — keep struct; remove `gated_chip` / `predicate_gated_chip` (already deleted in A.3).
- `src/manifests/genesis/genesis_system.cpp` — `assemble_genesis()` is now ~50 lines (was 311).
- **Acceptance:** BoV/Sonic 1+2/SoR/AB/TF3 at f=120/240/360/480 all 0/215040.

### B.3 — C64 migration

Similar shape; primary risk is the PLA banking decode which is unique to C64.

---

## Phase C — Cleanup

After ALL THREE systems migrate and parity holds for 7+ days:

- [ ] Delete the `manifests/<system>/<system>_system.cpp` assembly bodies. Keep only the `<system>_system` struct + the thin wrapper that calls `build_system()`.
- [ ] Or fully delete and have adapter use `build_system()` directly with system-specific callback bundles.
- [ ] Decision: keep the struct for type safety (adapter holds `genesis_system*` not just `system_graph`) or push fully through `system_graph`.

---

## Verification

- Every commit individually green-builds and green-tests on `windows-msvc-debug`.
- BoV f=120 byte-perfect remains the load-bearing regression check throughout Phase A and B.1.
- All 6 Genesis pixel-perfect titles validated after B.2 lands.
- C64 BASIC boot test (data-gated, currently skipped) un-skipped and validated after B.3.
- ~1 week of soak time on master before Phase C deletes anything.

---

## Open Questions Beyond the Pre-Phase Decisions

- **Manifest schema versioning.** `schema_id = "mnemos-manifest/1"` exists today. Bumping to /2 vs additive-only fields?
- **Diagnostic counter access.** Genesis VDP exposes `vint_fired_count` / `vint_drain_count` for debug. Through the manifest path, these come via introspection — confirm the player's existing diagnostic dumps still work after migration.
- **Save state compatibility.** Existing save states use the chip-ID convention from `chip_decl.id`. The manifest path needs to preserve those IDs; verify save/load round-trips after each migration.
