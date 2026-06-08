# Asset Extraction API

Status: Draft, awaiting review
Date: 2026-06-08

## Goal

A simple, system-agnostic API for extracting graphics assets — palettes, tile/
pattern sheets, sprites, and fonts — from any current or future Mnemos
emulated system, without per-system code in the consumer or any frontend.

A new system "gets asset extraction for free" by implementing one optional
capability on its video chip; the extractor, exporter, CLI, and tests never
change.

## Why a new capability (not just the existing surfaces)

The introspection contract (`src/chips/shared/introspection_views.hpp`,
`mnemos::instrumentation`) already lets a chip expose:

- `memory_view` — raw bytes (VRAM, CRAM) with no decode.
- `debug_layer` — a *whole* pre-composited RGB framebuffer (e.g. Genesis
  "plane A"), with no per-asset structure.

Neither gives decoded, individually-addressable assets. Pixel decode (SMS 4bpp
planar, Genesis 4bpp planar tiles, VIC-II bitmap/multicolor) and palette
resolution (`palette_rgb`, today private to each VDP) are inherently
chip-specific, so the decode step must live on the chip. Everything above it
can be generic.

"Fonts" are not hardware on these systems: they are tile/pattern ranges drawn
as text. Font extraction is therefore a *named tile-range* asset, optionally
described by a manifest hint; no new decode path beyond tiles is required.

## Architecture

```
chip (tier 2)            -> implements asset_source : decode native gfx
instrumentation (tier 6) -> asset contract types (palette/tileset/sprite/font)
debug (consumer)         -> asset_export : walk chips, write files
graphics/images          -> PNG/PPM encoding (reused as-is)
apps/player (tier 8)     -> CLI flag --extract-assets
```

Dependency direction is preserved: the contract sits in the existing
`instrumentation` namespace beside `debug_layer`; the exporter lives in
`src/debug/` next to `debug_dump`; output goes through `graphics/images`.

### 1. Contract — `src/chips/shared/asset_views.hpp` (namespace `mnemos::instrumentation`)

Neutral, decoded asset types. Pixels are produced *indexed* with an attached
RGB palette so consumers can re-tint, plus a convenience resolved-RGB path for
direct PNG export.

```cpp
// Palettes are their own type (palette_view); a graphic_asset is a pixel kind.
enum class asset_kind : std::uint8_t { tileset, sprite, font };

// 0x00RRGGBB entries; index 0 may be transparent (see transparent_index).
struct palette_view {
    std::string_view name;
    std::span<const std::uint32_t> colors;
    int transparent_index{-1};   // -1 = opaque palette
};

// An indexed raster. `indices` are palette indices, row-major, width*height.
// `palette` indexes into the asset_source's palettes() list.
struct indexed_image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::span<const std::uint8_t> indices;
    std::uint32_t palette{};     // which palette_view resolves this image
};

// A decoded graphics asset: one tile sheet, one sprite, or one font sheet.
// `tile_w/tile_h` describe the cell grid for sheets (sprite uses its own size).
struct graphic_asset {
    asset_kind kind{};
    std::string_view name;       // "patterns", "sprite_03", "font", ...
    indexed_image image;
    std::uint32_t tile_w{};      // 0 = not a grid (single sprite)
    std::uint32_t tile_h{};
    std::uint32_t source_addr{}; // VRAM offset the asset was decoded from
};

// Optional capability returned by ichip_introspection::assets().
class asset_source {
  public:
    virtual ~asset_source() = default;
    [[nodiscard]] virtual std::span<const palette_view> palettes() const = 0;
    // Decode current chip state into graphic assets. Buffers are owned by the
    // source and valid until the next tick (same lifetime rule as memory_view).
    [[nodiscard]] virtual std::span<const graphic_asset> graphics() const = 0;
};
```

Add one accessor to `ichip_introspection` (default `nullptr`, like `trace()`):

```cpp
[[nodiscard]] virtual asset_source* assets() { return nullptr; }
```

Forward-declared as `class asset_source;` in `introspection_views.hpp` (the
`mnemos::instrumentation` block), so `ichip_introspection` stays light; chips
that implement it include `asset_views.hpp` directly.

### 2. Reference implementation — SMS VDP first

SMS VDP (`src/chips/video/sms_vdp/`) is the smallest complete case (16 KiB
VRAM, 32-entry CRAM, 8x8 4bpp planar patterns, 64-sprite SAT). Implement an
`asset_source` on its `introspection_surface`:

- **palettes()**: two 16-color palettes resolved from CRAM via the existing
  (now reused) `palette_rgb`; index 0 transparent for the sprite palette.
- **tileset**: all 512 background patterns (16 KiB VRAM / 32 bytes per tile)
  decoded to an indexed sheet laid out
  on a fixed grid (e.g. 16 tiles wide).
- **sprites**: walk the SAT, decode each sprite (8x8 / 8x16) as its own
  `graphic_asset`, palette = sprite palette.
- **font**: a `font` asset over the tile range the manifest marks as glyphs
  (see §4); falls back to "no font asset" when unhinted.

Genesis VDP and VIC-II follow the same shape in later increments (Genesis: 4bpp
planar, plane/sprite tiles, 4×16 CRAM palettes; VIC-II: bitmap/multicolor +
char ROM). No consumer changes when they land.

### 3. Exporter — `src/debug/asset_export.{hpp,cpp}`

System-agnostic, modeled exactly on `dump_screenshot_artifacts`:

```cpp
// Walk sys.chips(); for each chip exposing introspection().assets():
//   palettes -> <base>.<chip>.pal.<name>.png   (swatch strip) + .json
//   assets   -> <base>.<chip>.<kind>.<name>.png (resolved RGB)
// Plus <base>.assets.json manifest: every asset's kind, size, grid,
// source_addr, palette ref. Returns count written.
std::size_t export_assets(const frontend_sdk::player_system& sys,
                          const std::string& base_path);
```

Resolved-RGB conversion (indexed + palette -> packed `0x00RRGGBB`) is a small
shared helper here; PNG encoding reuses `graphics::images::png_image`. JSON
sidecar uses the foundation JSON writer if present, else a tiny local emitter.

### 4. Manifest font hint (optional, additive)

Make font extraction declarative via the existing per-chip `[chip.config]`
scalar bag (the manifest layer only supports flat scalar keys there, not nested
tables), read through the VDP's `configure(cfg, callbacks)` path -- no new
plumbing:

```toml
[chip.config]
font_first_tile = 4   # first glyph's VRAM tile index
font_count      = 96  # glyph tile count; 0 (default) = no font asset
```

Absent / zero count = no font asset; never an error. The hint is clamped to the
tile space. Shipped SMS manifests leave it unset (the glyph range is per-game);
the knob is documented in `src/manifests/sms/README.md` and exercised by the
VDP unit tests via `configure()`.

### 5. CLI surface — `apps/player`

Add `--extract-assets <base_path>` (and `--extract-frames N` to advance before
extracting), reusing the existing `--screenshot` arg/boot scaffolding. Output
lands under `scratch/` per AGENTS.md. The flag calls `debug::export_assets`;
the player gains no system-specific code.

## Testing

- **Contract** (`instrumentation` tests): indexed→RGB resolution, transparent
  index, grid layout math.
- **SMS VDP** (`chips/video/sms_vdp/tests`): seed known VRAM/CRAM, assert
  decoded palette RGB, tile pixels, sprite count/size, font range.
- **Exporter** (`debug/tests`): a fake `player_system` with a stub
  `asset_source` → assert filenames, PNG headers, and JSON manifest fields,
  with zero SMS/Genesis knowledge (proves system-agnosticism).
- **Golden**: small PNG/JSON goldens under `tests/golden/` for SMS.

## Increments (each independently reviewable + CI-green)

1. Contract header + `ichip_introspection::assets()` + contract unit tests.
2. SMS VDP `asset_source` (palettes + tileset + sprites) + chip tests.
3. `debug::export_assets` + exporter tests + indexed→RGB/JSON helpers.
4. `apps/player --extract-assets` CLI + integration test + SMS goldens.
5. Manifest `[video.font]` hint + SMS font asset.
6. (Later) Genesis VDP and VIC-II `asset_source` implementations.

## Out of scope / non-goals

- Tilemap/nametable reconstruction and full-scene rendering (`debug_layer`
  already covers whole-plane views).
- Writing assets back into a running system.
- Audio/sample extraction (different contract; future work).

## Decisions (locked 2026-06-08)

- **Pixel format:** indexed + palette in the contract (re-tintable, smaller,
  truer to hardware); the exporter resolves to RGB.
- **Output:** resolved-RGB PNG per asset/palette + a JSON manifest sidecar.
- **Scope:** increment 1 (the contract) was landed first for sign-off; the
  remaining increments (2–6: SMS/Genesis/VIC-II decoders, the exporter, the
  `--extract-assets` CLI, and the SMS font hint) then followed as separate
  commits and are all included in this change set.
