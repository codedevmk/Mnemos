# Manifest Path Migration — Make `build_system()` Production

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax. **DO NOT START** any phase until the corresponding "Pre-Phase Decisions" block at the top is resolved with the user.

**Status:** PLAN ONLY. Multi-day work. No code touched until the pre-phase decisions below are signed off.

**Goal:** Migrate every supported system (today: SMS, Genesis, C64; future: 32X, Sega CD, Saturn, Amiga, CPS1, CPS2) from hand-written `assemble_*()` C++ functions to declarative TOML manifests consumed by an extended `build_system()`. After completion, `manifests/<system>/<system>_system.cpp` is gone; adding a new system means writing a `.toml` file plus any genuinely-new chip implementations.

---

## Pre-Phase Decisions (block all coding until resolved)

These five design choices fundamentally shape the rest of the plan. Walk through them with the user before any code lands.

### D1. Callback registry pattern OK as the escape hatch for non-declarative wiring?

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

### D2. System-specific MMIO escape hatch: named handlers vs free-form code?

Genesis's $A10000-$A1001F controller block, $A11100 BUSREQ, $A11200 Z80 RESET, $A12000-$A12FFF Z80 bank window are all hardcoded MMIO handlers today. Two options to express them:

- **D2.a (recommended)** — manifest names a "system-specific MMIO type" (e.g., `genesis.controller_block`), the host registers that name with a factory that returns `read/write` handlers.
- **D2.b** — manifest declares each byte-level register inline. Verbose; doesn't work for stateful blocks.

**Recommended:** D2.a (same pattern as D1 callbacks, generalized to MMIO regions). **Decision needed:** confirm.

### D3. Co-existence policy — how long do both paths run side by side?

Migrating Genesis is a multi-day effort. During that time, the manifest path and `assemble_genesis()` will both exist. Two options:

- **D3.a (recommended)** — Both paths live in tree. The player adapter keeps using `assemble_genesis()` until the manifest path is byte-perfect against the parity harness, then atomically switches. Each system migrates independently (SMS first, then Genesis, then C64).
- **D3.b** — Cutover from day one; broken Genesis is acceptable during migration. **Faster but high-risk for a memorial project.**

**Recommended:** D3.a. **Decision needed:** confirm.

### D4. Migration order — SMS first as smoke test, or Genesis first to flush out everything?

- **D4.a (recommended)** — SMS first (smallest, 214 LOC). Validates the framework with minimum complexity. Then Genesis (most complex, 311 LOC) which stress-tests every feature. Then C64.
- **D4.b** — Genesis first. Forces every feature to be designed upfront; nothing falls between the cracks. Higher upfront cost; higher risk of a stuck migration.

**Recommended:** D4.a. **Decision needed:** confirm.

### D5. Parity verification budget — which ROMs gate each migration step?

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

## Phase A — Foundation Extensions

Purely additive. Existing `assemble_*()` functions continue to work throughout. Each sub-phase ends green; no system-level regressions possible.

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

### B.1 — SMS migration

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
