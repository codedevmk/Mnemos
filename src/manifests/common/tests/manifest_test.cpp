#include <mnemos/manifests/manifest.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    using namespace mnemos::manifests;

    [[nodiscard]] bool any_error_contains(const load_result& r, std::string_view needle) {
        for (const auto& d : r.errors) {
            if (d.message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
} // namespace

TEST_CASE("parse_manifest accepts a valid C64 manifest") {
    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "commodore.c64.pal"
display_name = "Commodore 64 (PAL)"
family = "commodore"
revision = 1

[clock]
master_hz = 17734472
master_to_cpu_divider = 18
master_to_video_divider = 4

[[chip]]
id = "cpu"
type = "mos.6510"
attached_bus = "main"

[[chip]]
id = "video"
type = "mos.6569"
attached_bus = "main"
mmio_range = "0xD000-0xD3FF"

[[bus]]
id = "main"
address_bits = 16
endianness = "little"

[[bus.region]]
name = "ram"
range = "0x0000-0xFFFF"
backing = "ram"
size = 65536
)toml";

    const auto r = parse_manifest(text, "c64.toml");
    REQUIRE(r.ok());
    CHECK(r.value->schema == "mnemos-manifest/1");
    CHECK(r.value->id == "commodore.c64.pal");
    CHECK(r.value->family == "commodore");
    CHECK(r.value->revision == 1U);
    CHECK(r.value->clock.master_hz == 17734472U);
    CHECK(r.value->clock.master_to_cpu_divider == 18U);

    REQUIRE(r.value->chips.size() == 2U);
    CHECK(r.value->chips[0].type == "mos.6510");
    REQUIRE(r.value->chips[1].mmio_range.has_value());
    CHECK(r.value->chips[1].mmio_range->start == 0xD000U);
    CHECK(r.value->chips[1].mmio_range->end == 0xD3FFU);

    REQUIRE(r.value->buses.size() == 1U);
    CHECK(r.value->buses[0].address_bits == 16U);
    CHECK(r.value->buses[0].endian == endianness::little);
    REQUIRE(r.value->buses[0].regions.size() == 1U);
    CHECK(r.value->buses[0].regions[0].backing == region_backing::ram);
    CHECK(r.value->buses[0].regions[0].range.end == 0xFFFFU);
}

TEST_CASE("parse_manifest rejects the wrong schema") {
    const auto r = parse_manifest("[manifest]\nschema = \"nope\"\nid = \"x\"\n"
                                  "[clock]\nmaster_hz = 1\n[[bus]]\nid = \"m\"\n");
    CHECK_FALSE(r.ok());
    CHECK(any_error_contains(r, "schema"));
}

TEST_CASE("parse_manifest requires id, clock, and a bus") {
    const auto r = parse_manifest("[manifest]\nschema = \"mnemos-manifest/1\"\n");
    CHECK_FALSE(r.ok());
    CHECK(any_error_contains(r, "manifest.id"));
    CHECK(any_error_contains(r, "[clock]"));
    CHECK(any_error_contains(r, "[[bus]]"));
}

TEST_CASE("parse_manifest reports malformed TOML with a source position") {
    const auto r = parse_manifest("[manifest\nbroken", "bad.toml");
    CHECK_FALSE(r.ok());
    REQUIRE_FALSE(r.errors.empty());
    CHECK(r.errors[0].source == "bad.toml");
    CHECK(r.errors[0].line >= 1U);
}

TEST_CASE("parse_manifest validates region ranges and rom requirements") {
    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "x"
[clock]
master_hz = 1
[[bus]]
id = "main"
[[bus.region]]
name = "basic"
range = "not-a-range"
backing = "rom"
)toml";

    const auto r = parse_manifest(text);
    CHECK_FALSE(r.ok());
    CHECK(any_error_contains(r, "range is invalid"));
    CHECK(any_error_contains(r, "requires file and sha256"));
}
