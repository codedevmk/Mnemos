#pragma once

// The graphics ASSET-EXTRACTION contract: the decoded, individually-
// addressable graphics a chip can surface for ripping tools -- palettes,
// tile/pattern sheets, sprites, and fonts. Tier 2: declared here so tier-2
// chips (a VDP, a VIC-II) can decode their native pixel formats into these
// neutral types; consumed by tier 6+ (the asset exporter, future asset UI).
//
// This is the structured complement to introspection_views.hpp:
//   * memory_view  -> raw, undecoded bytes (VRAM, CRAM)
//   * debug_layer  -> a whole pre-composited RGB framebuffer (a plane view)
//   * asset_source -> decoded, per-asset graphics with palette structure
//
// Pixel data is INDEXED (palette indices) with the resolving palette attached,
// not pre-resolved RGB. Indexed pixels are re-tintable, smaller, and truer to
// the hardware; the exporter resolves to RGB when it writes PNGs. Decode is
// inherently chip-specific (SMS 4bpp planar vs Genesis vs VIC-II bitmap), so
// it lives on the chip; everything that consumes an asset_source is generic
// and never grows per-system code.
//
// Like every introspection capability, asset_source is OPTIONAL: a chip that
// does not expose graphics returns nullptr from
// `ichip_introspection::assets()`. The spans an asset_source hands out are
// owned by the chip and valid only until its next tick (the same lifetime
// rule as memory_view); a consumer that needs to retain them must copy.

#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::instrumentation {

    // What a `graphic_asset` is. Palettes are surfaced separately as
    // `palette_view`, so a graphic_asset is always one of the pixel kinds.
    enum class asset_kind : std::uint8_t {
        tileset, // a sheet of fixed-size background tiles / patterns
        sprite,  // one decoded hardware sprite
        font,    // a tile range a manifest marks as text glyphs
        bitmap,  // a full decoded bitmap-mode image (e.g. a C64 bitmap screen)
    };

    [[nodiscard]] constexpr std::string_view asset_kind_name(asset_kind kind) noexcept {
        switch (kind) {
        case asset_kind::tileset:
            return "tileset";
        case asset_kind::sprite:
            return "sprite";
        case asset_kind::font:
            return "font";
        case asset_kind::bitmap:
            return "bitmap";
        }
        return "unknown";
    }

    // A resolved color table. `colors` are 0x00RRGGBB (alpha unused), indexed
    // by the values in an `indexed_image`. `transparent_index` is the entry the
    // hardware treats as transparent (e.g. sprite color 0), or -1 when the
    // palette is fully opaque. Borrowed for the chip's lifetime; see the file
    // header's tick lifetime rule.
    struct palette_view final {
        std::string_view name;
        std::span<const std::uint32_t> colors;
        int transparent_index{-1};
    };

    // An indexed raster: `indices` are palette indices, row-major, exactly
    // width*height entries. `palette` selects which entry of the owning
    // asset_source's `palettes()` resolves this image to RGB.
    struct indexed_image final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::span<const std::uint8_t> indices;
        std::uint32_t palette{};

        // True when `indices` is sized consistently with width*height.
        [[nodiscard]] constexpr bool well_formed() const noexcept {
            return indices.size() == static_cast<std::size_t>(width) * height;
        }
    };

    // One decoded graphics asset: a tile sheet, a single sprite, or a font
    // sheet. For sheet kinds (`tileset`, `font`) `tile_w`/`tile_h` describe the
    // cell grid laid over the image; a single `sprite` leaves them 0 and uses
    // the image's own dimensions. `source_addr` is the chip-relative address
    // (e.g. a VRAM offset) the asset was decoded from, for cross-referencing
    // against a memory_view dump.
    struct graphic_asset final {
        asset_kind kind{};
        std::string_view name;
        indexed_image image;
        std::uint32_t tile_w{};
        std::uint32_t tile_h{};
        std::uint32_t source_addr{};
    };

    // The optional capability returned by `ichip_introspection::assets()`. A
    // chip decodes its current graphics state into palettes plus a flat list of
    // graphic assets. Both spans, and everything they point at, follow the
    // tick lifetime rule from the file header.
    class asset_source {
      public:
        asset_source() = default;
        asset_source(const asset_source&) = delete;
        asset_source& operator=(const asset_source&) = delete;
        virtual ~asset_source() = default;

        [[nodiscard]] virtual std::span<const palette_view> palettes() const = 0;
        [[nodiscard]] virtual std::span<const graphic_asset> graphics() const = 0;
    };

} // namespace mnemos::instrumentation
