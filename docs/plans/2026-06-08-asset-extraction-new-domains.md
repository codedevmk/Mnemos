# Asset Extraction — New Domains

Status: Draft, awaiting review
Date: 2026-06-08

Extends the graphics asset-extraction pattern (`docs/plans/2026-06-08-asset-extraction-api.md`,
shipped in #60–#62) to three new domains. Each reuses the same architecture:
an **optional chip capability** does the system-specific decode; a **generic
consumer** walks `player_system` and serialises; new systems opt in by
implementing one capability and never touch the consumer.

This document is the design + sequencing for sign-off. It does not implement
anything. The three domains are independent; the recommended order is A → B → C.

---

## Domain A — Audio / sample extraction (recommended first)

The biggest net-new capability and the closest parallel to the graphics work.

### What's extractable (grounded in the current chips)

| Chip | Class | Extractable assets |
| --- | --- | --- |
| RF5C68 (Sega CD PCM) | sampler | 8 channels of PCM from the 64 KiB `waveram()`; per-channel start/loop/step from registers |
| YM2612 (Genesis FM) | FM synth + DAC | 6 FM voices as instrument patches (operator TL/AR/DR/SR/RR/mult/detune from the register file); the DAC PCM stream |
| SID 6581 (C64) | subtractive | 3 voices as patches (waveform, ADSR, freq, filter routing) |
| SN76489 (SMS/GG PSG) | PSG | 3 tone + 1 noise channel state (period, attenuation, noise mode) |

Two natural asset kinds emerge:

- **`sample`** — a PCM waveform: interleaved/mono `s16` (or `u8`) frames + a
  sample rate + an optional loop point. Backed by real sample memory
  (RF5C68 `waveram`) or a captured stream (YM2612 DAC).
- **`voice` / `patch`** — a synth program: a named bag of register-derived
  fields (already exposed as `register_descriptor`s via each chip's
  `register_snapshot()`). This is a structured dump, not audio.

### Contract — `src/chips/shared/audio_views.hpp` (namespace `mnemos::instrumentation`)

Mirrors `asset_views.hpp`. Neutral, decode-on-demand, borrowed-until-tick.

```cpp
enum class audio_asset_kind : std::uint8_t { sample, voice };

struct sample_view {                 // a PCM waveform
    std::string_view name;
    std::span<const std::int16_t> frames; // interleaved if channels > 1
    std::uint32_t sample_rate{};
    std::uint8_t channels{1};
    int loop_start{-1};              // -1 = no loop
    std::uint32_t source_addr{};
};

struct voice_field { std::string_view name; std::int64_t value; }; // register-derived
struct voice_view {                  // a synth program / patch
    std::string_view name;
    std::span<const voice_field> fields;
};

class audio_source {
  public:
    virtual ~audio_source() = default;
    [[nodiscard]] virtual std::span<const sample_view> samples() const = 0;
    [[nodiscard]] virtual std::span<const voice_view> voices() const = 0;
};
```

Add `audio_source* audio() { return nullptr; }` to `ichip_introspection`
(forward-declared in `introspection_views.hpp`, exactly like `asset_source`).

### Exporter — `src/debug/audio_export.{hpp,cpp}`

System-agnostic, modelled on `debug::export_assets`:
- each `sample_view` → `<base>.<chip>.sample.<name>.wav` (canonical RIFF/WAVE
  PCM; a tiny encoder in `src/audio/` or alongside the exporter — no new dep).
- all `voice_view`s + sample metadata → `<base>.audio.json`.
- Returns the count written; walks `sys.chips()` only.

### CLI — `apps/player --extract-audio <base> [--extract-frames N]`

Mirrors `--extract-assets`; lets sample memory fill before extraction.

### Open decisions

- **WAV location.** A `wav_audio` writer belongs beside the PNG encoders.
  There is no `src/audio` image-equivalent today; recommend a small
  `src/audio/wav.{hpp,cpp}` tier (or fold into `debug/`). Confirm placement.
- **DAC/stream capture.** RF5C68 `waveram` is a true sample store (easy).
  YM2612 DAC + the synth voices are *streams*, not stored samples — capturing
  them means recording over N frames, which is a different shape than the
  graphics "snapshot". Recommend: phase 1 = RF5C68 samples + register-derived
  voices for all four chips; phase 2 (optional) = streamed DAC capture.

### Increments

1. `audio_views.hpp` contract + `ichip_introspection::audio()` + unit tests.
2. RF5C68 `audio_source` (waveram → samples, registers → channels).
3. `wav` writer + `debug::export_audio` + exporter tests.
4. `--extract-audio` CLI + an SMS/Genesis end-to-end smoke.
5. Register-derived `voice` views for SN76489 / SID / YM2612.

---

## Domain B — Tilemap / scene reconstruction (medium)

A "scene" is the composed background plane — the nametable laid out with its
tiles, palettes, flips, and scroll applied — distinct from the raw pattern
sheet the current `tileset` asset exposes. Useful for level ripping.

### Key design tension: indexed vs RGB

The graphics `asset_source` is **indexed + single palette per image**. A full
scene mixes palettes per tile (Genesis: 4 palettes; SMS: 2), so it cannot be a
single-palette `indexed_image`. Two options:

- **(Recommended) Reuse the existing `debug_layer` surface** (RGB framebuffer),
  which already exists for exactly this — Genesis exposes a `plane_a` layer
  today. Generalise it: add `plane_b`, `window`, and a `scene` (full composite)
  layer per VDP; SMS/VIC gain their first `debug_layer`s. The exporter already
  writes `debug_layer`s as PPM in `dump_screenshot_artifacts`; extend the
  asset exporter to also emit them as PNG.
- (Alternative) Add a `scene` to the asset contract carrying *resolved RGB*
  rather than indices — but that duplicates `debug_layer`. Not recommended.

So domain B is mostly **per-chip `debug_layer` work + an exporter that emits
layers as PNG**, not a new contract. This keeps scenes (RGB, palette-resolved)
and primitives (indexed, re-tintable) cleanly separated.

### Per-chip scope

- **SMS VDP**: compose the 32×28 name table (scroll applied) → one `bg` layer.
- **Genesis VDP**: already has `plane_a`; add `plane_b`, `window`, and a full
  `scene` composite (reuse the scanline renderer's plane walk).
- **VIC-II**: text/bitmap screen is already the framebuffer; a `scene` layer
  adds little. Likely skip, or expose the 40×25 char matrix as a layer.

### Increments

1. Exporter: emit each chip `debug_layer` as `<base>.<chip>.layer.<name>.png`
   (the data is already there for Genesis `plane_a`).
2. SMS VDP `bg` `debug_layer`.
3. Genesis `plane_b` / `window` / `scene` `debug_layer`s.

---

## Domain C — Dev-frontend asset browser (heaviest; new app + gated dependency)

`src/apps/dev` is a stub (`README.md` only). A live browser is a **new
application**, not an extension of an existing one, and it pulls in the Eliot
UI Kit (see the dependency boundary below).

### Approach

The browser UI is built on the **Eliot UI Kit**. The dev app boots a
`player_system` (via `adapter_registry`, like the player), then:
- enumerates `chips()` → `introspection().assets()` / `audio()`,
- renders the asset list + a preview pane with Eliot UI Kit widgets (resolve
  indexed → RGB and present the same way the player surfaces framebuffers),
- steps frames on demand so assets update live.

### Dependency boundary (needs an ADR)

The Eliot UI Kit is an Eliot dependency. `AGENTS.md` states Mnemos "must not
take Eliot runtime, UI, allocator, or namespace dependencies unless a future
approved ADR introduces an integration boundary." So Domain C is **gated on
that ADR**: it must define the integration boundary (which Eliot UI Kit
surfaces the dev app may use, how they are pinned/vendored, and that the
dependency is confined to `apps/dev` — the headless core, chips, and other
frontends stay Eliot-free). Land the ADR before starting C.

### Increments (after the integration-boundary ADR)

1. `apps/dev` skeleton on the Eliot UI Kit: boot a `player_system`, window,
   frame stepping.
2. Asset list + selection (Eliot UI Kit widgets).
3. Preview pane (present the resolved asset).
4. Audio asset playback/inspection (depends on Domain A).

---

## Recommended sequencing

1. **A (audio)** — highest value, mirrors the proven graphics pattern, no new
   deps, no UI. Lands as ~5 reviewable increments like the graphics work.
2. **B (scenes)** — medium; mostly per-chip `debug_layer`s + a PNG path in the
   exporter. Reuses existing surfaces.
3. **C (browser)** — last; gated on the Eliot UI Kit integration-boundary ADR,
   and benefits from A + B already producing assets to display.

## Cross-cutting open decisions

- WAV encoder placement (`src/audio/` vs `debug/`).
- Whether streamed DAC/synth capture is in scope for phase 1 (recommend: no).
- Eliot UI Kit integration-boundary ADR for the dev frontend (Domain C):
  scope, pinning/vendoring, and confinement to `apps/dev`.
- Whether the audio exporter gets its own CLI flag or folds into a single
  `--extract-assets` that emits both graphics and audio.
