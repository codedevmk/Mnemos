#include "rom_set.hpp"

#include "crc32.hpp"
#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using mnemos::manifests::common::load_rom_set;
    using mnemos::manifests::common::make_directory_rom_provider;
    using mnemos::manifests::common::make_zip_rom_provider;
    using mnemos::manifests::common::rom_file_provider;
    using mnemos::manifests::common::rom_set_decl;
    using mnemos::security::cryptography::crc32;

    // Provider over an in-memory name -> bytes map.
    [[nodiscard]] rom_file_provider
    map_provider(std::map<std::string, std::vector<std::uint8_t>, std::less<>> files) {
        return [files = std::move(files)](
                   std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
            const auto it = files.find(name);
            if (it == files.end()) {
                return std::nullopt;
            }
            return it->second;
        };
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    // Builds a minimal STORED-method zip over the given entries.
    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::map<std::string, std::vector<std::uint8_t>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t crc;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;

        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const std::uint32_t crc = crc32(data);
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U); // local file header signature
            put16(out, 20U);         // version needed
            put16(out, 0U);          // flags
            put16(out, 0U);          // method: stored
            put32(out, 0U);          // mod time/date
            put32(out, crc);
            put32(out, size); // compressed
            put32(out, size); // uncompressed
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U); // extra length
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, crc, size, local_offset});
        }

        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U); // central directory signature
            put16(out, 20U);         // version made by
            put16(out, 20U);         // version needed
            put16(out, 0U);          // flags
            put16(out, 0U);          // method: stored
            put32(out, 0U);          // mod time/date
            put32(out, c.crc);
            put32(out, c.size); // compressed
            put32(out, c.size); // uncompressed
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U); // extra length
            put16(out, 0U); // comment length
            put16(out, 0U); // disk number
            put16(out, 0U); // internal attributes
            put32(out, 0U); // external attributes
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;

        put32(out, 0x06054B50U); // end of central directory signature
        put16(out, 0U);          // disk number
        put16(out, 0U);          // central directory disk
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U); // comment length
        return out;
    }

} // namespace

TEST_CASE("rom_set interleaves an even/odd program pair and verifies CRCs", "[rom_set]") {
    const std::vector<std::uint8_t> low{0x10U, 0x12U, 0x14U, 0x16U};
    const std::vector<std::uint8_t> high{0x11U, 0x13U, 0x15U, 0x17U};

    rom_set_decl decl;
    decl.name = "interleave";
    decl.regions.push_back(
        {.name = "maincpu",
         .size = 8U,
         .fill = 0xFFU,
         .files = {
             {.name = "prog.lo", .offset = 0U, .stride = 2U, .size = 4U, .crc32 = crc32(low)},
             {.name = "prog.hi", .offset = 1U, .stride = 2U, .size = 4U, .crc32 = crc32(high)}}});

    const auto image = load_rom_set(decl, map_provider({{"prog.lo", low}, {"prog.hi", high}}));
    REQUIRE(image.ok());
    const auto* region = image.region("maincpu");
    REQUIRE(region != nullptr);
    const std::vector<std::uint8_t> expected{0x10U, 0x11U, 0x12U, 0x13U,
                                             0x14U, 0x15U, 0x16U, 0x17U};
    CHECK(*region == expected);
}

TEST_CASE("rom_set fills uncovered bytes and keeps loading past bad files", "[rom_set]") {
    rom_set_decl decl;
    decl.name = "issues";
    decl.regions.push_back(
        {.name = "gfx",
         .size = 8U,
         .fill = 0xAAU,
         .files = {
             {.name = "tiles.a", .offset = 0U, .stride = 1U, .size = 2U, .crc32 = std::nullopt},
             {.name = "absent.b", .offset = 2U, .stride = 1U, .size = 2U, .crc32 = std::nullopt},
             {.name = "wrong.c", .offset = 4U, .stride = 1U, .size = 2U, .crc32 = 0xDEADBEEFU},
         }});

    const auto image = load_rom_set(
        decl, map_provider({{"tiles.a", {0x01U, 0x02U}}, {"wrong.c", {0x03U, 0x04U}}}));
    CHECK_FALSE(image.ok());
    REQUIRE(image.issues.size() == 2U);
    CHECK(image.issues[0].file == "absent.b");
    CHECK(image.issues[1].file == "wrong.c");
    CHECK(image.issues[1].message.find("crc32 mismatch") != std::string::npos);

    // Present data placed (even the CRC-flagged file), gaps keep the fill.
    const auto* region = image.region("gfx");
    REQUIRE(region != nullptr);
    const std::vector<std::uint8_t> expected{0x01U, 0x02U, 0xAAU, 0xAAU,
                                             0x03U, 0x04U, 0xAAU, 0xAAU};
    CHECK(*region == expected);
}

TEST_CASE("rom_set reports a file overflowing its region", "[rom_set]") {
    rom_set_decl decl;
    decl.name = "overflow";
    decl.regions.push_back(
        {.name = "small",
         .size = 2U,
         .fill = 0x00U,
         .files = {
             {.name = "big.bin", .offset = 0U, .stride = 1U, .size = 0U, .crc32 = std::nullopt}}});

    const auto image =
        load_rom_set(decl, map_provider({{"big.bin", {0x01U, 0x02U, 0x03U, 0x04U}}}));
    CHECK_FALSE(image.ok());
    REQUIRE(image.issues.size() == 1U);
    CHECK(image.issues[0].message.find("overflows") != std::string::npos);
    const auto* region = image.region("small");
    REQUIRE(region != nullptr);
    CHECK((*region == std::vector<std::uint8_t>{0x01U, 0x02U})); // what fit, loaded
}

TEST_CASE("rom_set loads through a zip bundle provider", "[rom_set]") {
    const std::vector<std::uint8_t> low{0xA0U, 0xA2U};
    const std::vector<std::uint8_t> high{0xA1U, 0xA3U};
    auto provider = make_zip_rom_provider(make_stored_zip({{"p.lo", low}, {"p.hi", high}}));
    REQUIRE(provider.has_value());

    rom_set_decl decl;
    decl.name = "zipped";
    decl.regions.push_back(
        {.name = "maincpu",
         .size = 4U,
         .fill = 0xFFU,
         .files = {
             {.name = "p.lo", .offset = 0U, .stride = 2U, .size = 2U, .crc32 = crc32(low)},
             {.name = "p.hi", .offset = 1U, .stride = 2U, .size = 2U, .crc32 = crc32(high)},
         }});

    const auto image = load_rom_set(decl, *provider);
    REQUIRE(image.ok());
    const auto* region = image.region("maincpu");
    REQUIRE(region != nullptr);
    CHECK((*region == std::vector<std::uint8_t>{0xA0U, 0xA1U, 0xA2U, 0xA3U}));
}

TEST_CASE("rom_set rejects a malformed zip bundle", "[rom_set]") {
    CHECK_FALSE(make_zip_rom_provider({0x00U, 0x01U, 0x02U}).has_value());
}

TEST_CASE("rom_set loads through a directory provider", "[rom_set]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mnemos_rom_set_test";
    fs::create_directories(dir);
    const std::vector<std::uint8_t> data{0x5AU, 0x5BU, 0x5CU};
    REQUIRE(mnemos::io::write_file((dir / "chip.bin").string(), data));

    rom_set_decl decl;
    decl.name = "dir";
    decl.regions.push_back(
        {.name = "samples",
         .size = 4U,
         .fill = 0x00U,
         .files = {
             {.name = "chip.bin", .offset = 0U, .stride = 1U, .size = 3U, .crc32 = crc32(data)},
         }});

    const auto image = load_rom_set(decl, make_directory_rom_provider(dir.string()));
    fs::remove_all(dir);

    REQUIRE(image.ok());
    const auto* region = image.region("samples");
    REQUIRE(region != nullptr);
    CHECK((*region == std::vector<std::uint8_t>{0x5AU, 0x5BU, 0x5CU, 0x00U}));
}

TEST_CASE("rom_set flags a zero-size region declaration", "[rom_set]") {
    rom_set_decl decl;
    decl.name = "zero";
    decl.regions.push_back({.name = "empty", .size = 0U, .fill = 0U, .files = {}});
    const auto image = load_rom_set(decl, map_provider({}));
    CHECK_FALSE(image.ok());
    CHECK(image.region("empty") == nullptr);
}
