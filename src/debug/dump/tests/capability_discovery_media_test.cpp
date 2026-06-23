#include "capability_discovery.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using mnemos::chips::frame_buffer_view;
    using mnemos::debug::capability_descriptor;
    using mnemos::debug::capability_manifest;
    using mnemos::debug::capability_state;
    using mnemos::debug::discover_dump_capabilities;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::media_capability_info;
    using mnemos::frontend_sdk::media_hash_algorithm;
    using mnemos::frontend_sdk::media_image_info;
    using mnemos::frontend_sdk::media_page_hash;
    using mnemos::frontend_sdk::media_residency;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;

    [[nodiscard]] const capability_descriptor* find_descriptor(const capability_manifest& manifest,
                                                               std::string_view id) {
        for (const auto& descriptor : manifest.capabilities) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool has_diagnostic(const capability_manifest& manifest, std::string_view code) {
        for (const auto& diag : manifest.diagnostics) {
            if (diag.code == code) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string_view payload_value(const capability_descriptor& descriptor,
                                                 std::string_view key) {
        for (const auto& field : descriptor.payload) {
            if (field.key == key) {
                return field.value;
            }
        }
        return {};
    }

    class media_system final : public player_system {
      public:
        explicit media_system(media_capability_info media) : media_(std::move(media)) {}

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] const media_capability_info& media_capabilities() const noexcept override {
            return media_;
        }
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        media_capability_info media_{};
        std::vector<spec_field> spec_{};
    };

} // namespace

TEST_CASE("media capability discovery reports resident paged and streamed metadata",
          "[capability_discovery][media]") {
    media_capability_info media{};
    media.media.push_back(media_image_info{.id = "cart",
                                           .label = "Cartridge",
                                           .residency = media_residency::resident,
                                           .byte_count = 0x80000U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .full_hash = "sha256-cart",
                                           .provider_id = "resident.cart",
                                           .revision = "world",
                                           .cache_hint = "resident"});
    media.media.push_back(media_image_info{
        .id = "disc",
        .label = "Disc image",
        .residency = media_residency::paged,
        .byte_count = 0x200000U,
        .page_size = 4096U,
        .hash_algorithm = media_hash_algorithm::sha1,
        .full_hash = "sha1-disc",
        .page_hashes = {media_page_hash{.offset = 0U, .byte_count = 4096U, .hash = "p0"},
                        media_page_hash{.offset = 4096U, .byte_count = 4096U, .hash = "p1"}},
        .provider_id = "disc.cache",
        .revision = "rev-a",
        .cache_hint = "read_ahead"});
    media.media.push_back(media_image_info{.id = "stream",
                                           .label = "ADPCM stream",
                                           .residency = media_residency::streamed,
                                           .byte_count = 0x100000U,
                                           .hash_algorithm = media_hash_algorithm::crc32,
                                           .full_hash = "89abcdef",
                                           .provider_id = "sample.stream",
                                           .revision = "rev-b",
                                           .cache_hint = "sequential"});

    media_system sys{std::move(media)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* cart = find_descriptor(manifest, "media.cart");
    REQUIRE(cart != nullptr);
    CHECK(cart->state == capability_state::available);
    CHECK(payload_value(*cart, "residency") == "resident");
    CHECK(payload_value(*cart, "hash_algorithm") == "sha256");
    CHECK(payload_value(*cart, "provider_id") == "resident.cart");

    const auto* disc = find_descriptor(manifest, "media.disc");
    REQUIRE(disc != nullptr);
    CHECK(disc->state == capability_state::available);
    CHECK(payload_value(*disc, "residency") == "paged");
    CHECK(payload_value(*disc, "page_size") == "4096");
    CHECK(payload_value(*disc, "page_hash_count") == "2");
    CHECK(payload_value(*disc, "page_hash.1.offset") == "4096");
    CHECK(payload_value(*disc, "page_hash.1.hash") == "p1");
    CHECK(payload_value(*disc, "cache_hint") == "read_ahead");

    const auto* stream = find_descriptor(manifest, "media.stream");
    REQUIRE(stream != nullptr);
    CHECK(stream->state == capability_state::available);
    CHECK(payload_value(*stream, "residency") == "streamed");
    CHECK(payload_value(*stream, "hash_algorithm") == "crc32");
}

TEST_CASE("media capability discovery diagnoses missing hashes provider outages and revisions",
          "[capability_discovery][media]") {
    media_capability_info media{};
    media.media.push_back(media_image_info{.id = "no_hash",
                                           .label = "Missing hash",
                                           .residency = media_residency::resident,
                                           .byte_count = 1024U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .provider_id = "resident.cart",
                                           .revision = "rev-ok"});
    media.media.push_back(media_image_info{.id = "offline",
                                           .label = "Remote disc",
                                           .residency = media_residency::paged,
                                           .byte_count = 4096U,
                                           .page_size = 2048U,
                                           .hash_algorithm = media_hash_algorithm::sha1,
                                           .full_hash = "sha1-offline",
                                           .provider_id = "remote.cache",
                                           .provider_available = false,
                                           .revision = "rev-ok",
                                           .cache_hint = "read_ahead"});
    media.media.push_back(media_image_info{.id = "bad_rev",
                                           .label = "Unsupported revision",
                                           .residency = media_residency::streamed,
                                           .byte_count = 8192U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .full_hash = "sha256-stream",
                                           .provider_id = "stream.cache",
                                           .revision = "rev-unsupported",
                                           .revision_supported = false});
    media.media.push_back(media_image_info{.id = "bad_page",
                                           .label = "Paged without size",
                                           .residency = media_residency::paged,
                                           .byte_count = 8192U,
                                           .hash_algorithm = media_hash_algorithm::sha1,
                                           .full_hash = "sha1-paged",
                                           .provider_id = "disc.cache",
                                           .revision = "rev-ok"});

    media_system sys{std::move(media)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* no_hash = find_descriptor(manifest, "media.no_hash");
    REQUIRE(no_hash != nullptr);
    CHECK(no_hash->state == capability_state::degraded);
    CHECK(has_diagnostic(manifest, "media.hash.missing"));

    const auto* offline = find_descriptor(manifest, "media.offline");
    REQUIRE(offline != nullptr);
    CHECK(offline->state == capability_state::unavailable);
    CHECK(payload_value(*offline, "provider_available") == "false");
    CHECK(has_diagnostic(manifest, "media.provider.unavailable"));

    const auto* bad_rev = find_descriptor(manifest, "media.bad_rev");
    REQUIRE(bad_rev != nullptr);
    CHECK(bad_rev->state == capability_state::degraded);
    CHECK(payload_value(*bad_rev, "revision_supported") == "false");
    CHECK(has_diagnostic(manifest, "media.revision.unsupported"));

    const auto* bad_page = find_descriptor(manifest, "media.bad_page");
    REQUIRE(bad_page != nullptr);
    CHECK(bad_page->state == capability_state::degraded);
    CHECK(has_diagnostic(manifest, "media.page_size.missing"));
}
