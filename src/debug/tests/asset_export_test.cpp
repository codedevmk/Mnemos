// Verifies export_assets walks player_system through the asset_source contract
// only -- resolving indexed graphics to RGB PNGs and emitting a JSON manifest
// -- with no knowledge of which system produced the assets.

#include "asset_export.hpp"
#include "asset_views.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;
    using mnemos::instrumentation::asset_kind;
    using mnemos::instrumentation::asset_source;
    using mnemos::instrumentation::debug_layer;
    using mnemos::instrumentation::graphic_asset;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::palette_view;

    class fake_assets final : public asset_source {
      public:
        [[nodiscard]] std::span<const palette_view> palettes() const override { return pal_table_; }
        [[nodiscard]] std::span<const graphic_asset> graphics() const override {
            return asset_table_;
        }

      private:
        std::array<std::uint32_t, 2> colors_{0xFF0000U, 0x0000FFU};
        std::array<palette_view, 1> pal_table_{
            palette_view{.name = "main", .colors = colors_, .transparent_index = 0}};
        std::array<std::uint8_t, 2> tile_px_{0U, 1U};
        std::array<graphic_asset, 1> asset_table_{
            graphic_asset{.kind = asset_kind::tileset,
                          .name = "patterns",
                          .image = {.width = 2U, .height = 1U, .indices = tile_px_, .palette = 0U},
                          .tile_w = 1U,
                          .tile_h = 1U,
                          .source_addr = 0U}};
    };

    class gfx_intro final : public ichip_introspection {
      public:
        [[nodiscard]] asset_source* assets() override { return &assets_; }

      private:
        fake_assets assets_;
    };

    class gfx_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "vdp-1", // sanitizes to "vdp_1"
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
        gfx_intro intro_;
    };

    // A composed RGB scene (a 2x2 "plane_a" framebuffer).
    class fake_layer final : public debug_layer {
      public:
        [[nodiscard]] std::string_view name() const noexcept override { return "plane_a"; }
        [[nodiscard]] frame_buffer_view view() const override {
            return {.pixels = px_.data(), .width = 2U, .height = 2U, .stride = 0U};
        }

      private:
        std::array<std::uint32_t, 4> px_{0x010203U, 0x040506U, 0x070809U, 0x0A0B0CU};
    };

    // A chip that exposes only a debug_layer (no asset_source) -- export must
    // still include it and write its layer PNG.
    class layer_intro final : public ichip_introspection {
      public:
        [[nodiscard]] std::span<debug_layer* const> debug_layers() override { return table_; }

      private:
        fake_layer layer_;
        std::array<debug_layer*, 1> table_{&layer_};
    };

    class layer_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "vdp-2", // sanitizes to "vdp_2"
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
        layer_intro intro_;
    };

    // A chip that exposes no asset_source -- export must skip it entirely.
    class plain_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "cpu",
                    .family = "test",
                    .klass = chip_class::cpu,
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

    class gfx_system final : public player_system {
      public:
        gfx_system() {
            chip_list_[0] = &plain_;
            chip_list_[1] = &gfx_;
            chip_list_[2] = &layer_;
        }
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override { return chip_list_; }

      private:
        plain_chip plain_;
        gfx_chip gfx_;
        layer_chip layer_;
        std::array<ichip*, 3> chip_list_{};
        std::vector<spec_field> spec_{};
    };

    [[nodiscard]] std::filesystem::path make_scratch_dir(const std::string& tag) {
        const auto base =
            std::filesystem::temp_directory_path() / ("mnemos_asset_export_test_" + tag);
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base);
        return base;
    }

    [[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::vector<std::uint8_t> out;
        if (!in) {
            return out;
        }
        in.seekg(0, std::ios::end);
        out.resize(static_cast<std::size_t>(in.tellg()));
        in.seekg(0, std::ios::beg);
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return out;
    }

    [[nodiscard]] bool is_png(const std::vector<std::uint8_t>& b) {
        return b.size() >= 8U && b[0] == 0x89U && b[1] == 0x50U && b[2] == 0x4EU && b[3] == 0x47U;
    }

    [[nodiscard]] std::string read_text(const std::filesystem::path& p) {
        const auto bytes = read_file(p);
        return std::string(bytes.begin(), bytes.end());
    }

} // namespace

TEST_CASE("export_assets writes a PNG per palette + asset and a JSON manifest", "[asset_export]") {
    const auto scratch = make_scratch_dir("basic");
    const auto base = (scratch / "out").string();

    gfx_system sys;
    // gfx chip: palette swatch + tileset resolved + tileset indexed (3 PNGs;
    // the .pal is not counted). layer chip: one debug_layer PNG. Total 4.
    CHECK(mnemos::debug::export_assets(sys, base) == 4U);

    // Files land under the chip's sanitized id ("vdp-1" -> "vdp_1").
    const auto pal_png = scratch / "out.vdp_1.pal.main.png";
    const auto tile_png = scratch / "out.vdp_1.tileset.patterns.png";
    const auto tile_idx = scratch / "out.vdp_1.tileset.patterns.idx.png";
    const auto pal_jasc = scratch / "out.vdp_1.pal.main.pal";
    const auto layer_png = scratch / "out.vdp_2.layer.plane_a.png";
    REQUIRE(std::filesystem::exists(pal_png));
    REQUIRE(std::filesystem::exists(tile_png));
    REQUIRE(std::filesystem::exists(tile_idx));
    REQUIRE(std::filesystem::exists(pal_jasc));
    REQUIRE(std::filesystem::exists(layer_png)); // layer-only chip still exported
    CHECK(is_png(read_file(pal_png)));
    CHECK(is_png(read_file(tile_png)));
    CHECK(is_png(read_file(tile_idx)));
    CHECK(is_png(read_file(layer_png)));
    // The .pal is JASC-PAL text: header line, format version, entry count (2).
    CHECK(read_text(pal_jasc).starts_with("JASC-PAL\n0100\n2\n"));

    // The asset-free CPU chip produces no files.
    for (const auto& entry : std::filesystem::directory_iterator(scratch)) {
        CHECK(entry.path().filename().string().find("cpu") == std::string::npos);
    }
}

TEST_CASE("export_assets manifest describes palettes and assets", "[asset_export]") {
    const auto scratch = make_scratch_dir("manifest");
    const auto base = (scratch / "out").string();

    gfx_system sys;
    (void)mnemos::debug::export_assets(sys, base);

    const auto manifest = scratch / "out.assets.json";
    REQUIRE(std::filesystem::exists(manifest));
    const std::string json = read_text(manifest);

    CHECK(json.find("\"id\": \"vdp_1\"") != std::string::npos);
    CHECK(json.find("\"part_number\": \"vdp-1\"") != std::string::npos);
    CHECK(json.find("\"name\": \"main\"") != std::string::npos);
    CHECK(json.find("\"transparent_index\": 0") != std::string::npos);
    CHECK(json.find("\"kind\": \"tileset\"") != std::string::npos);
    CHECK(json.find("\"name\": \"patterns\"") != std::string::npos);
    CHECK(json.find("\"width\": 2") != std::string::npos);
    CHECK(json.find("\"file\": \"out.vdp_1.tileset.patterns.png\"") != std::string::npos);
    CHECK(json.find("\"indexed_file\": \"out.vdp_1.tileset.patterns.idx.png\"") !=
          std::string::npos);
    CHECK(json.find("\"pal_file\": \"out.vdp_1.pal.main.pal\"") != std::string::npos);

    // The layer-only chip is listed with its debug_layer scene.
    CHECK(json.find("\"id\": \"vdp_2\"") != std::string::npos);
    CHECK(json.find("\"name\": \"plane_a\"") != std::string::npos);
    CHECK(json.find("\"file\": \"out.vdp_2.layer.plane_a.png\"") != std::string::npos);
}

TEST_CASE("export_assets writes an empty manifest for a system with no graphics",
          "[asset_export]") {
    class empty_system final : public player_system {
      public:
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        std::vector<spec_field> spec_{};
    };

    const auto scratch = make_scratch_dir("empty");
    const auto base = (scratch / "out").string();

    empty_system sys;
    CHECK(mnemos::debug::export_assets(sys, base) == 0U);

    const auto manifest = scratch / "out.assets.json";
    REQUIRE(std::filesystem::exists(manifest));
    const std::string json = read_text(manifest);
    CHECK(json.find("\"chips\": []") != std::string::npos);
}
