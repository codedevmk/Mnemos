#include "rom_set_toml.hpp"

#include "rom_set.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
    using mnemos::manifests::common::load_rom_set;
    using mnemos::manifests::common::parse_rom_set_decl;
    using mnemos::manifests::common::rom_file_provider;
} // namespace

TEST_CASE("rom_set_toml parses a full declaration with defaults", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "demo"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog.lo"
offset = 0x0
stride = 2
size = 0x10000
crc32 = 0x1234ABCD

[[region.file]]
name = "prog.hi"
offset = 0x1
stride = 2
crc32 = "0xDEADBEEF"

[[region]]
name = "tiles_a"
size = 0x20000
fill = 0x00
)");
    REQUIRE(result.ok());
    const auto& decl = *result.value;
    CHECK(decl.name == "demo");
    CHECK(decl.board == "irem_m72");
    REQUIRE(decl.regions.size() == 2U);
    const auto& main = decl.regions[0];
    CHECK(main.name == "maincpu");
    CHECK(main.size == 0x100000U);
    CHECK(main.fill == 0xFFU); // default
    REQUIRE(main.files.size() == 2U);
    CHECK(main.files[0].stride == 2U);
    CHECK(main.files[0].size == 0x10000U);
    REQUIRE(main.files[0].crc32.has_value());
    CHECK(*main.files[0].crc32 == 0x1234ABCDU);
    CHECK(main.files[1].offset == 1U);
    CHECK(main.files[1].size == 0U); // default: any
    REQUIRE(main.files[1].crc32.has_value());
    CHECK(*main.files[1].crc32 == 0xDEADBEEFU);
    CHECK(decl.regions[1].fill == 0x00U);
    CHECK(decl.regions[1].files.empty());
    CHECK_FALSE(decl.cps_b_profile.has_value()); // optional key, absent here
    CHECK_FALSE(decl.parent.has_value());        // optional key, absent here
    CHECK(decl.dips.empty());                    // cabinet DIP metadata is explicit
    CHECK(decl.hle.empty());                     // explicit substitutions only
}

TEST_CASE("rom_set_toml parses the optional parent set name", "[rom_set_toml]") {
    SECTION("present") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"sf2hf\"\nboard = \"capcom_cps1\"\n"
            "parent = \"sf2ce\"\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        REQUIRE(result.value->parent.has_value());
        CHECK(*result.value->parent == "sf2ce");
    }
    SECTION("absent leaves it unset") {
        const auto result = parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n"
                                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK_FALSE(result.value->parent.has_value());
    }
    SECTION("empty string is rejected") {
        const auto result =
            parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nparent = \"\"\n"
                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("non-string is rejected") {
        const auto result =
            parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nparent = 7\n"
                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("path separators / traversal are rejected at the trust boundary") {
        for (const char* bad : {"../sf2ce", "..\\sf2ce", "/etc/passwd", "a/b", "c:foo"}) {
            const auto result = parse_rom_set_decl(
                std::string("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nparent = \"") +
                bad + "\"\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
            CHECK_FALSE(result.ok());
        }
    }
}

TEST_CASE("rom_set_toml parses explicit HLE declarations", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "The i8751 dump is unavailable; this profile declares the substitution."

[[region]]
name = "maincpu"
size = 0x100
)");
    REQUIRE(result.ok());
    REQUIRE(result.value->hle.size() == 1U);
    CHECK(result.value->hle[0].chip == "mcu");
    CHECK(result.value->hle[0].profile == "irem_m72.dbreedm72_no_dump_mcu");
    CHECK_FALSE(result.value->hle[0].rationale.empty());
}

TEST_CASE("rom_set_toml rejects bad HLE declarations", "[rom_set_toml]") {
    SECTION("missing rationale") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[hle]]\nchip = \"mcu\"\n"
            "profile = \"p\"\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("empty profile") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[hle]]\nchip = \"mcu\"\n"
            "profile = \"\"\nrationale = \"documented\"\n[[region]]\nname = \"maincpu\"\n"
            "size = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("unknown key is rejected") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[hle]]\nchip = \"mcu\"\n"
            "profile = \"p\"\nrationale = \"documented\"\nextra = true\n[[region]]\n"
            "name = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml parses DIP switch metadata", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "rtype"
board = "irem_m72"

[[dip]]
bank = "SW1"
name = "Lives"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "2"
value = 0x0002

[[dip.option]]
label = "3"
value = 0x0003

[[region]]
name = "maincpu"
size = 0x100
)");
    REQUIRE(result.ok());
    REQUIRE(result.value->dips.size() == 1U);
    const auto& dip = result.value->dips[0];
    CHECK(dip.bank == "SW1");
    CHECK(dip.name == "Lives");
    CHECK(dip.mask == 0x0003U);
    CHECK(dip.default_value == 0x0003U);
    REQUIRE(dip.options.size() == 2U);
    CHECK(dip.options[0].label == "2");
    CHECK(dip.options[0].value == 0x0002U);
    CHECK(dip.options[1].label == "3");
    CHECK(dip.options[1].value == 0x0003U);
}

TEST_CASE("rom_set_toml parses conditional DIP switch metadata", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "rtype"
board = "irem_m72"

[[dip]]
bank = "SW1"
name = "Coinage"
mask = 0x00f0
default = 0x00f0
condition_mask = 0x0400
condition_value = 0x0400

[[dip.option]]
label = "1 Coin / 1 Credit"
value = 0x00f0

[[region]]
name = "maincpu"
size = 0x100
)");
    REQUIRE(result.ok());
    REQUIRE(result.value->dips.size() == 1U);
    const auto& condition = result.value->dips[0].condition;
    REQUIRE(condition.has_value());
    CHECK(condition->mask == 0x0400U);
    CHECK(condition->value == 0x0400U);
}

TEST_CASE("rom_set_toml rejects bad DIP switch metadata", "[rom_set_toml]") {
    SECTION("zero mask") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Lives\"\nmask = 0\ndefault = 0\n[[dip.option]]\nlabel = \"3\"\n"
            "value = 0\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("missing options") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Lives\"\nmask = 3\ndefault = 3\n[[region]]\nname = \"maincpu\"\n"
            "size = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("default outside mask") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Lives\"\nmask = 3\ndefault = 4\n[[dip.option]]\nlabel = \"3\"\n"
            "value = 3\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("option outside mask") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Lives\"\nmask = 3\ndefault = 3\n[[dip.option]]\nlabel = \"bad\"\n"
            "value = 4\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("unknown key") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Lives\"\nmask = 3\ndefault = 3\nextra = true\n[[dip.option]]\n"
            "label = \"3\"\nvalue = 3\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("partial condition is rejected") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Coinage\"\nmask = 0xf0\ndefault = 0xf0\ncondition_mask = 0x400\n"
            "[[dip.option]]\nlabel = \"1 Coin / 1 Credit\"\nvalue = 0xf0\n[[region]]\n"
            "name = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("condition value outside mask is rejected") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[dip]]\nbank = \"SW1\"\n"
            "name = \"Coinage\"\nmask = 0xf0\ndefault = 0xf0\ncondition_mask = 0x400\n"
            "condition_value = 0x800\n[[dip.option]]\nlabel = \"1 Coin / 1 Credit\"\n"
            "value = 0xf0\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml parses the optional cps_b_profile id", "[rom_set_toml]") {
    SECTION("present") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nboard = \"capcom_cps1\"\n"
            "cps_b_profile = 24\n[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        REQUIRE(result.value->cps_b_profile.has_value());
        CHECK(*result.value->cps_b_profile == 24U);
    }
    SECTION("absent leaves it unset") {
        const auto result = parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n"
                                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK_FALSE(result.value->cps_b_profile.has_value());
    }
}

TEST_CASE("rom_set_toml parses the optional orientation", "[rom_set_toml]") {
    using mnemos::manifests::common::screen_orientation;
    SECTION("vertical") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\norientation = \"vertical\"\n"
            "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK(result.value->orientation == screen_orientation::vertical);
    }
    SECTION("absent defaults to horizontal") {
        const auto result = parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n"
                                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK(result.value->orientation == screen_orientation::horizontal);
    }
    SECTION("invalid value is rejected") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\norientation = \"sideways\"\n"
            "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml parses the optional sprite_order", "[rom_set_toml]") {
    using mnemos::manifests::common::sprite_draw_order;
    SECTION("descending") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nsprite_order = \"descending\"\n"
            "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK(result.value->sprite_order == sprite_draw_order::descending);
    }
    SECTION("absent defaults to ascending") {
        const auto result = parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n"
                                               "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        REQUIRE(result.ok());
        CHECK(result.value->sprite_order == sprite_draw_order::ascending);
    }
    SECTION("invalid value is rejected") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\nsprite_order = \"sideways\"\n"
            "[[region]]\nname = \"maincpu\"\nsize = 0x100\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml parses the CPS1 layout keys", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "x"

[[region]]
name = "gfx"
size = 0x100

[[region.file]]
name = "g0"
offset = 0
stride = 8
unit = 2

[[region.file]]
name = "prog"
offset = 0x80
stride = 2
unit = 2
swap = true

[[region.file]]
name = "snd"
offset = 0x40
source_offset = 0x20
length = 0x10
)");
    REQUIRE(result.ok());
    const auto& files = result.value->regions[0].files;
    REQUIRE(files.size() == 3U);
    CHECK(files[0].unit == 2U);
    CHECK(files[0].stride == 8U);
    CHECK_FALSE(files[0].swap);
    CHECK(files[1].unit == 2U);
    CHECK(files[1].swap);
    CHECK(files[2].source_offset == 0x20U);
    CHECK(files[2].length == 0x10U);
    CHECK(files[2].unit == 1U); // default
}

TEST_CASE("rom_set_toml rejects bad CPS1 layout keys", "[rom_set_toml]") {
    SECTION("unit zero") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[region]]\nname = "
            "\"a\"\nsize = 8\n[[region.file]]\nname = \"f\"\nunit = 0\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("swap not a boolean") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[region]]\nname = "
            "\"a\"\nsize = 8\n[[region.file]]\nname = \"f\"\nswap = 1\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml rejects bad declarations with located diagnostics", "[rom_set_toml]") {
    SECTION("missing schema and regions") {
        const auto result = parse_rom_set_decl("[set]\nname = \"x\"\n");
        CHECK_FALSE(result.ok());
        REQUIRE(result.errors.size() >= 2U);
    }
    SECTION("wrong schema") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/9\"\nname = \"x\"\n[[region]]\nname = "
            "\"a\"\nsize = 1\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("zero-size region") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[region]]\nname = "
            "\"a\"\nsize = 0\n");
        CHECK_FALSE(result.ok());
    }
    SECTION("unknown key carries a source position") {
        const auto result = parse_rom_set_decl("[set]\nschema = \"mnemos-romset/1\"\nname = "
                                               "\"x\"\nbogus = 1\n[[region]]\nname = "
                                               "\"a\"\nsize = 1\n",
                                               "demo.toml");
        CHECK_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1U);
        CHECK(result.errors[0].source == "demo.toml");
        CHECK(result.errors[0].line == 4U);
    }
    SECTION("malformed TOML reports the parse error") {
        const auto result = parse_rom_set_decl("[set\n");
        CHECK_FALSE(result.ok());
        REQUIRE(result.errors.size() == 1U);
    }
    SECTION("crc32 out of range") {
        const auto result = parse_rom_set_decl(
            "[set]\nschema = \"mnemos-romset/1\"\nname = \"x\"\n[[region]]\nname = "
            "\"a\"\nsize = 8\n[[region.file]]\nname = \"f\"\ncrc32 = 0x1FFFFFFFF\n");
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("rom_set_toml declarations drive the loader end to end", "[rom_set_toml]") {
    const auto result = parse_rom_set_decl(R"(
[set]
schema = "mnemos-romset/1"
name = "interleaved"

[[region]]
name = "maincpu"
size = 8

[[region.file]]
name = "lo.bin"
offset = 0
stride = 2

[[region.file]]
name = "hi.bin"
offset = 1
stride = 2
)");
    REQUIRE(result.ok());

    std::map<std::string, std::vector<std::uint8_t>, std::less<>> files{
        {"lo.bin", {0x10U, 0x12U, 0x14U, 0x16U}},
        {"hi.bin", {0x11U, 0x13U, 0x15U, 0x17U}},
    };
    const rom_file_provider provider =
        [&files](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
        const auto it = files.find(name);
        if (it == files.end()) {
            return std::nullopt;
        }
        return it->second;
    };

    const auto image = load_rom_set(*result.value, provider);
    REQUIRE(image.ok());
    const auto* region = image.region("maincpu");
    REQUIRE(region != nullptr);
    const std::vector<std::uint8_t> expected{0x10U, 0x11U, 0x12U, 0x13U,
                                             0x14U, 0x15U, 0x16U, 0x17U};
    CHECK(*region == expected);
}
