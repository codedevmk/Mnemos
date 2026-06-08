// Verifies the graphics asset-extraction contract (tier 2): a chip can expose
// an `asset_source` of palettes + decoded graphic assets through
// `ichip_introspection::assets()`, and a generic consumer can read indexed
// pixels, resolve them against the attached palette, and inspect grid metadata
// WITHOUT downcasting to any concrete chip type. This is the abstraction the
// system-agnostic asset exporter depends on.

#include "asset_views.hpp"
#include "introspection_views.hpp"
#include "shared.hpp" // chip.hpp + ibus.hpp + chip_registry

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::instrumentation::asset_kind;
    using mnemos::instrumentation::asset_kind_name;
    using mnemos::instrumentation::asset_source;
    using mnemos::instrumentation::graphic_asset;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::indexed_image;
    using mnemos::instrumentation::palette_view;

    // A minimal asset_source: one 4-color palette (index 0 transparent), one
    // 2x2 tileset of 1x1 cells, and one 2x2 sprite. The backing arrays live in
    // the source so the borrowed spans stay valid for its lifetime.
    class fake_assets final : public asset_source {
      public:
        [[nodiscard]] std::span<const palette_view> palettes() const override { return pal_table_; }
        [[nodiscard]] std::span<const graphic_asset> graphics() const override {
            return asset_table_;
        }

      private:
        std::array<std::uint32_t, 4> colors_{0x000000U, 0xFF0000U, 0x00FF00U, 0x0000FFU};
        std::array<palette_view, 1> pal_table_{
            palette_view{.name = "main", .colors = colors_, .transparent_index = 0}};

        std::array<std::uint8_t, 4> tile_px_{0U, 1U, 2U, 3U};
        std::array<std::uint8_t, 4> sprite_px_{1U, 1U, 0U, 2U};
        std::array<graphic_asset, 2> asset_table_{
            graphic_asset{.kind = asset_kind::tileset,
                          .name = "patterns",
                          .image = {.width = 2U, .height = 2U, .indices = tile_px_, .palette = 0U},
                          .tile_w = 1U,
                          .tile_h = 1U,
                          .source_addr = 0x0000U},
            graphic_asset{
                .kind = asset_kind::sprite,
                .name = "sprite_00",
                .image = {.width = 2U, .height = 2U, .indices = sprite_px_, .palette = 0U},
                .tile_w = 0U,
                .tile_h = 0U,
                .source_addr = 0x3F00U}};
    };

    class gfx_introspection final : public ichip_introspection {
      public:
        [[nodiscard]] asset_source* assets() override { return &assets_; }

      private:
        fake_assets assets_;
    };

    class gfx_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "gfx",
                    .family = "test",
                    .klass = chip_class::video,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        gfx_introspection intro_;
    };

    class plain_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "plain",
                    .family = "test",
                    .klass = chip_class::peripheral,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        ichip_introspection intro_;
    };

} // namespace

TEST_CASE("asset_kind_name maps every kind", "[asset]") {
    CHECK(asset_kind_name(asset_kind::tileset) == "tileset");
    CHECK(asset_kind_name(asset_kind::sprite) == "sprite");
    CHECK(asset_kind_name(asset_kind::font) == "font");
    CHECK(asset_kind_name(asset_kind::bitmap) == "bitmap");
}

TEST_CASE("ichip_introspection exposes no asset_source by default", "[asset]") {
    plain_chip chip;
    CHECK(chip.introspection().assets() == nullptr);
}

TEST_CASE("asset_source surfaces palettes with a transparent index", "[asset]") {
    gfx_chip chip;
    auto* src = chip.introspection().assets();
    REQUIRE(src != nullptr);

    auto pals = src->palettes();
    REQUIRE(pals.size() == 1U);
    CHECK(pals[0].name == "main");
    CHECK(pals[0].transparent_index == 0);
    REQUIRE(pals[0].colors.size() == 4U);
    CHECK(pals[0].colors[1] == 0xFF0000U);
    CHECK(pals[0].colors[3] == 0x0000FFU);
}

TEST_CASE("graphic_asset indexed pixels resolve through the attached palette", "[asset]") {
    gfx_chip chip;
    auto* src = chip.introspection().assets();
    REQUIRE(src != nullptr);

    auto assets = src->graphics();
    REQUIRE(assets.size() == 2U);

    const graphic_asset& tiles = assets[0];
    CHECK(tiles.kind == asset_kind::tileset);
    CHECK(tiles.name == "patterns");
    CHECK(tiles.tile_w == 1U);
    CHECK(tiles.tile_h == 1U);
    REQUIRE(tiles.image.well_formed());

    // A generic consumer resolves indices -> RGB via the referenced palette.
    auto pals = src->palettes();
    REQUIRE(tiles.image.palette < pals.size());
    const palette_view& pal = pals[tiles.image.palette];
    REQUIRE(tiles.image.indices.size() == 4U);
    CHECK(pal.colors[tiles.image.indices[1]] == 0xFF0000U); // index 1 -> red
    CHECK(pal.colors[tiles.image.indices[3]] == 0x0000FFU); // index 3 -> blue
}

TEST_CASE("single sprite asset leaves the cell grid unset", "[asset]") {
    gfx_chip chip;
    auto assets = chip.introspection().assets()->graphics();
    REQUIRE(assets.size() == 2U);

    const graphic_asset& spr = assets[1];
    CHECK(spr.kind == asset_kind::sprite);
    CHECK(spr.name == "sprite_00");
    CHECK(spr.tile_w == 0U);
    CHECK(spr.tile_h == 0U);
    CHECK(spr.source_addr == 0x3F00U);
    REQUIRE(spr.image.well_formed());
    CHECK(spr.image.width == 2U);
    CHECK(spr.image.height == 2U);
}

TEST_CASE("indexed_image well_formed rejects a size mismatch", "[asset]") {
    std::array<std::uint8_t, 3> short_px{0U, 1U, 2U};
    const indexed_image bad{.width = 2U, .height = 2U, .indices = short_px, .palette = 0U};
    CHECK_FALSE(bad.well_formed());
}
