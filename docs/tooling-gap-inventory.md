# Mnemos developer / debug / game-dev tooling gap inventory & tracking checklist (2026-06-11)

Companion to [`progress-analysis.md`](progress-analysis.md) (hardware parity) and
[`parity-gap-inventory.md`](parity-gap-inventory.md) (hardware checklist). Those two
cover **silicon**; this one covers the **tooling layer** — debuggers, disassemblers,
viewers, save-states, scripting, frontends, asset/audio analysis. It exists because the
hardware audit explicitly excluded tooling, which hid an entire axis.

> **The headline inverts.** On *hardware* Mnemos is the deeper, more accurate emulator
> (net-ahead on 5/6 shared systems). On *tooling* **Emu is well ahead** — it leads 8 of
> 14 tooling categories, ties 5, and Mnemos leads 0. Mnemos's strongest dev-facing
> pieces (GUI debugger, disassemblers, scripting, save-state wiring) are missing or
> README-only stubs. As a *development platform* (not just a game runner), this is
> arguably the larger backlog.

> **Porting mandate (ADR-0006 §1 · proposed ADR-0024):** every item ported from Emu is a
> **re-architecture to Mnemos-or-better standards** (tiering, contracts, STD-001/002
> naming + error model, reusable canonical home, determinism + instrumentation) —
> **never a transcription of Emu C**. Items here name *what* to build (behaviour); the
> *how* follows the mandate.

## How to use

- Tick `[x]` when merged; reference the item ID (`T#` / `N#`) in the PR title.
- `[~]` for in-progress.
- Two ID series: **T#** = Emu has it, Mnemos lacks/partial (true parity gap). **N#** =
  neither has it fully — the modern next-gen dev-emulator bar (forward-looking).
- Some items cross-reference hardware checklist IDs (e.g. T4 ⇄ G7, T7 ⇄ D1) — same
  underlying work, listed in both because it has both a hardware and a tooling face.

## Legend

- **State:** MISSING · PARTIAL (present but incomplete) · STUB (README/skeleton only)
- **Sev:** HIGH · MED · LOW (impact on Mnemos as a dev-capable next-gen emulator)
- **Effort:** S · M · L · XL
- **Type:** `vs Emu` (Emu implements it) · `next-gen` (neither does; modern bar)

## Progress

- **Tooling parity items (T, vs Emu): 0 / 11** — 5 HIGH · 5 MED · 1 LOW
- **Next-gen items (N): 0 / 9** — 1 HIGH · 5 MED · 3 LOW

---

## At-a-glance: Emu vs Mnemos tooling (14 categories)

| Category | Emu | Mnemos | Verdict |
|---|---|---|---|
| Interactive debugger | C64 full (BP/watch/raster-BP/callstack/step-over-out) + Genesis/SMS CLI + Studio watch | engine only (BP/watch/step/event bus), **unwired, no callstack** | Emu ahead |
| Disassemblers | M68k, SH-2, 6510 (+symbols), Z80 | **none** (capstone external-only) | Emu ahead |
| Memory tools | Studio live hex + devkit poke | view + dump; no search/edit/compare | Emu ahead |
| Tracing / logging | devkit event-bus + Saturn 38-file tracer | inst-trace CSV + PSG→VGM + **APM sidecar** | Even |
| Video debug viewers | Studio **live** plane/sprite viewer | rich **offline** PNG export | Emu ahead (live) |
| Audio tooling | Studio channels + **FFT/THD A/B** + 5-fmt WAV + C64 SID player/scope/MIDI | WAV export + PSG VGM | Emu ahead |
| Save-state / rewind / TAS | C64/SMS/Genesis save; **rewind + TAS movie** (C64) | generic container solid, C64/Genesis lower-level targets plus **CPS2 player save/load**; rewind unwired | Emu ahead |
| Asset tools | asset_codec + dumps + **C64 inject** | best-structured **export** (PNG+JSON), **no import** | Even |
| Scripting / automation | devkit C-API + Python-over-CLI harness | **README stubs only** | Emu ahead |
| Frontends / UX | CLI + **Studio: ImGui GUI, DX11/12/Vulkan, 11 widgets** | SDL player + CLI; **no GUI**; apps/dev vapor | Emu ahead |
| Conformance / differential | TomHarte/Lorenz + ref-capture + 96-file unit + Python sweep + Saturn cross-emu ledger | m6510/m68000/z80 corpora + golden + **GPGX A/B** + oracle registry | Even |
| Cheats / patches | none | none | Both lack |
| Archive / containers | ZIP + **CHD** + LHA/LNX | ZIP + cue/bin/iso; **no CHD** | Emu ahead |
| Profiling | none dedicated | bus microbench + APM cycle metering | Even |

---

## Tooling parity gaps — Emu has, Mnemos lacks (T-series) — 0 / 11

#### Debugger & disassembly
- [ ] **T2** CPU disassemblers (M68k, SH-2, Z80, 6510) — none in-tree; capstone is external-CLI-only, not in the build · MISSING · HIGH · M/CPU · vs Emu
- [ ] **T3** Wire the debugger engine to an interactive frontend (monitor/REPL or GUI) — `src/debug/debugger` engine is solid but nothing drives it · PARTIAL · HIGH · M · vs Emu
- [ ] **T10** Debugger depth — callstack tracking (JSR/RTS/RTI), raster/scanline breakpoints, symbol/label-file import (VICE/ACME/ca65) · PARTIAL · MED · M · vs Emu
- [ ] **T11** Memory search (scan-for-value) + edit/poke · MISSING · LOW · S · vs Emu

#### GUI / frontend
- [ ] **T1** GUI dev-suite — Emu's `frontends/studio` is a ~17k-LOC ImGui app (DX11/DX12/Vulkan) with 11 dockable widgets (CPU snapshot, disasm, memory lens, watch debugger, event timeline, video layers, audio channels). Mnemos has **no GUI** (`apps/dev` is a README stub). Brings live tile/sprite/palette/layer viewers with it · MISSING · HIGH · XL · vs Emu

#### Save-state / rewind / movie
- [ ] **T4** Whole-system save-state aggregators — generic container + 79 per-chip serializers exist; Genesis has a deterministic runtime target, CPS2 now exposes a frontend save/load path (`--save-state`, `--load-state`, F5/F9), but SMS, Sega CD, 32X, M72, CPS1, and broader rewind/TAS player wiring remain · PARTIAL · HIGH · M · vs Emu · ⇄ hardware G7
- [ ] **T5** Wire rewind ring + deterministic input-movie/TAS into the player — both exist (`runtime/rewind`, `runtime/input`) but are C64-CLI-only and unwired to `mnemos_player` · PARTIAL · MED · M · vs Emu

#### Audio tooling
- [ ] **T6** Audio A/B differential analysis — FFT spectrum, THD, level/noise-floor, reference-clip compare (Emu's `devkit/analysis/audio` + Studio). Mnemos only exports WAV/VGM · MISSING · MED · M · vs Emu

#### Asset pipeline
- [ ] **T8** Asset injection / import (round-trip) — Mnemos `asset_export` is one-way; add re-injection (Emu C64 does Koala/charset/sprite bidirectional) · MISSING · MED · M · vs Emu

#### Disc / container formats
- [ ] **T7** CHD compressed disc support (v5 codec stack) — `.chd` currently rejected · MISSING · HIGH · L · vs Emu · ⇄ hardware D1

#### Per-system dev suites
- [ ] **T9** C64 developer suite (~4,400 LOC in Emu) — 6510 disassembler + 777-LOC debugger + symbols, SID player/scope/MIDI, sprite IO+mux, charset/image IO, LHA/LNX archive loaders · MISSING · MED · HIGH · vs Emu

---

## Next-gen game-dev tooling — neither has fully (N-series) — 0 / 9

The modern bar for a development-focused emulator (Mesen / BizHawk / mGBA / no$-class).
These are forward-looking targets, not Emu-parity gaps.

#### Scripting & automation
- [ ] **N1** Working embedded scripting (Lua and/or Python) with a memory + event + frame API — Mnemos's `scripting_lua` / `scripting_python_ipc` / `wire` are **README-only stubs** today · STUB · HIGH · L · next-gen

#### Debugging depth (modern)
- [ ] **N4** Source-level / symbol debugging (symbol map, labelled disasm, source line mapping) · MISSING · MED · M · next-gen
- [ ] **N6** Conditional / data breakpoints + filtered trace-logging exposed in a UI (the debugger engine already supports predicate conditions; no UI surfaces them) · PARTIAL · MED · M · next-gen

#### Game-dev / romhacking
- [ ] **N2** Cheat / patch engine — Game Genie / Action Replay codes + IPS / BPS patch loading · MISSING · MED · M · next-gen
- [ ] **N3** Hitbox / collision / event-overlay visualization · MISSING · MED · M · next-gen

#### Determinism / TAS
- [ ] **N5** First-class TAS movie format with re-recording + savestate-in-movie (bk2/fm2-class), beyond C64's basic record/replay · MISSING · MED · M · next-gen

#### Performance & workflow
- [ ] **N7** Performance profiler — per-chip frame-time breakdown / sampling / flamegraph (only a bus microbench + APM cycle metering exist) · PARTIAL · LOW · M · next-gen
- [ ] **N8** Hot-reload of ROM / assets without restart · MISSING · LOW · M · next-gen
- [ ] **N9** Netplay (low priority for a dev-focused tool) · MISSING · LOW · L · next-gen

---

## Where Mnemos already leads (do not regress)

Mnemos's tooling **substrate** is better than Emu's even though its built-on tools are
thinner — the deficit is tools-on-top, not foundation:

- **System-agnostic introspection contract** (`src/chips/shared/introspection_views.hpp`):
  `memory_view` / `register_view` / `trace_target` / `reg_write_trace` / `debug_layer` /
  `asset_source` / `audio_source` — generic tools observe any chip with zero downcasting.
  Build new tooling on this, not per-system hooks.
- **APM external-tracer sidecar** (`apm/`) — observes the unmodified engine from another
  process via page-guard watchpoints. Emu has no equivalent.
- **Asset export structure** — indexed-PNG + JSON manifests (`asset_export.cpp`) beat
  Emu's ad-hoc dumps; the import side (T8) is what's missing.
- **GPGX libretro A/B differential rig** + clean oracle/governance registry.
- **Clean-room ZIP/DEFLATE/LZMA** stack wired into ROM loading.

---

## Suggested sequence (dev-platform leverage)

1. **T2 disassemblers + T3 wire the debugger** — unlocks every interactive-debug workflow; prerequisite for a usable debugger UX. Reuse the `introspection_views` contract.
2. **T4 save-state wiring** (⇄ G7) + **T5 rewind/movie in the player** — high daily-use value; the primitives already exist, this is wiring.
3. **N1 embedded scripting** — the biggest next-gen multiplier (test automation, romhacking, conditional debugging); replaces the stub dirs.
4. **T7 CHD** (⇄ D1) — shared with the hardware backlog.
5. **T6 audio A/B** + **T8 asset import** — round out the analysis/asset pipeline.
6. **T1 Studio GUI** — highest payoff for dev UX but XL effort; do it once the headless tooling (T2/T3/T4) gives it something to render. Brings live viewers + N3/N6 surfaces with it.
7. **T9 C64 suite, N2 cheats, N4 source-level, N5 TAS, N7–N9** — as the platform matures.
