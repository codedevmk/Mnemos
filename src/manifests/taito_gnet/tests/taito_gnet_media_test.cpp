#include "taito_gnet_media.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    namespace gnet = mnemos::manifests::taito_gnet;

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    void put_be32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
        v[off] = static_cast<std::uint8_t>(x >> 24);
        v[off + 1] = static_cast<std::uint8_t>(x >> 16);
        v[off + 2] = static_cast<std::uint8_t>(x >> 8);
        v[off + 3] = static_cast<std::uint8_t>(x);
    }

    void put_be64(std::vector<std::uint8_t>& v, std::size_t off, std::uint64_t x) {
        for (int i = 7; i >= 0; --i) {
            v[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(x);
            x >>= 8;
        }
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;
        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, size);
            put32(out, size);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U);
            put16(out, 20U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, c.size);
            put32(out, c.size);
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32(out, 0x06054B50U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U);
        return out;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_gnet_block_chd_header() {
        std::vector<std::uint8_t> chd(124, 0);
        std::memcpy(chd.data(), "MComprHD", 8);
        put_be32(chd, 8, 124U);
        put_be32(chd, 12, 5U);
        put_be32(chd, 16, 0x6C7A6D61U); // "lzma"
        put_be32(chd, 20, 0x7A6C6962U); // "zlib"
        put_be32(chd, 24, 0x68756666U); // "huff"
        put_be32(chd, 28, 0x666C6163U); // "flac"
        put_be64(chd, 32, 40960000ULL);
        put_be64(chd, 40, 124U);
        put_be64(chd, 48, 0U);
        put_be32(chd, 56, 4096U);
        put_be32(chd, 60, 512U);
        return chd;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_gnet_none_block_chd() {
        constexpr std::uint32_t hunk_bytes = 4096U;
        constexpr std::uint32_t unit_bytes = 512U;
        const std::uint64_t hunk_data_offset = hunk_bytes;
        const std::uint64_t map_offset = hunk_data_offset + hunk_bytes;
        std::vector<std::uint8_t> chd(static_cast<std::size_t>(map_offset + 4U), 0);
        std::memcpy(chd.data(), "MComprHD", 8);
        put_be32(chd, 8, 124U);
        put_be32(chd, 12, 5U);
        put_be32(chd, 16, 0U);
        put_be32(chd, 20, 0U);
        put_be32(chd, 24, 0U);
        put_be32(chd, 28, 0U);
        put_be64(chd, 32, hunk_bytes);
        put_be64(chd, 40, map_offset);
        put_be64(chd, 48, 0U);
        put_be32(chd, 56, hunk_bytes);
        put_be32(chd, 60, unit_bytes);
        for (std::uint32_t i = 0; i < hunk_bytes; ++i) {
            chd[static_cast<std::size_t>(hunk_data_offset) + i] =
                static_cast<std::uint8_t>(0xA5U ^ i);
        }
        put_be32(chd, static_cast<std::size_t>(map_offset), 1U);
        return chd;
    }

    [[nodiscard]] const char* opt_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return (value != nullptr && *value != '\0') ? value : nullptr;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    read_file_bytes(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        const std::vector<char> raw{std::istreambuf_iterator<char>{in},
                                    std::istreambuf_iterator<char>{}};
        return std::vector<std::uint8_t>{raw.begin(), raw.end()};
    }

} // namespace

TEST_CASE("taito_gnet media inspector reports CHD block-device headers",
          "[taito_gnet][media]") {
    const auto package = make_stored_zip({
        {"readme.txt", {'G', '-', 'N', 'E', 'T'}},
        {"CHAOSHEAT.CHD", make_gnet_block_chd_header()},
    });

    const auto report = gnet::inspect_gnet_package(std::span<const std::uint8_t>{package});
    REQUIRE(report.has_value());
    REQUIRE(report->archive_entries.size() == 2U);
    REQUIRE(report->chd_entries.size() == 1U);
    CHECK(gnet::has_probeable_chd_media(*report));

    const auto& entry = report->chd_entries.front();
    CHECK(entry.name == "CHAOSHEAT.CHD");
    CHECK(entry.payload_extracted);
    REQUIRE(entry.chd.has_value());
    CHECK(entry.chd->version == 5U);
    CHECK(entry.chd->codecs[0] == 0x6C7A6D61U);
    CHECK(entry.chd->logical_bytes == 40960000ULL);
    CHECK(entry.chd->hunk_bytes == 4096U);
    CHECK(entry.chd->unit_bytes == 512U);
    CHECK_FALSE(entry.chd->has_cd_unit_layout);
}

TEST_CASE("taito_gnet media loader mounts synthetic CHD flash cards", "[taito_gnet][media]") {
    const auto package = make_stored_zip({
        {"readme.txt", {'G', '-', 'N', 'E', 'T'}},
        {"card.chd", make_gnet_none_block_chd()},
    });

    const auto cards = gnet::load_gnet_flash_cards(std::span<const std::uint8_t>{package});
    REQUIRE(cards.has_value());
    REQUIRE(cards->size() == 1U);
    CHECK(cards->front().name == "card.chd");
    CHECK(cards->front().media.info.logical_bytes == 4096U);
    CHECK(cards->front().media.info.unit_bytes == 512U);
    REQUIRE(cards->front().media.data.size() == 4096U);
    CHECK(cards->front().media.data[0] == 0xA5U);
    CHECK(cards->front().media.data[255] == static_cast<std::uint8_t>(0xA5U ^ 255U));
}

TEST_CASE("taito_gnet media inspector rejects non-zip packages", "[taito_gnet][media]") {
    const std::vector<std::uint8_t> not_zip = {'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D'};
    CHECK_FALSE(gnet::inspect_gnet_package(std::span<const std::uint8_t>{not_zip}).has_value());
}

TEST_CASE("taito_gnet media inspector recognizes a real local G-NET package",
          "[taito_gnet][media][data]") {
    const char* package_env = opt_env("MNEMOS_TAITO_GNET_PACKAGE");
    if (package_env == nullptr) {
        SKIP("set MNEMOS_TAITO_GNET_PACKAGE to a Taito G-NET package zip");
    }

    const auto bytes = read_file_bytes(package_env);
    if (!bytes) {
        SKIP("MNEMOS_TAITO_GNET_PACKAGE could not be read");
    }

    const auto report = gnet::inspect_gnet_package(std::span<const std::uint8_t>{*bytes});
    REQUIRE(report.has_value());
    REQUIRE_FALSE(report->chd_entries.empty());
    CHECK(gnet::has_probeable_chd_media(*report));

    for (const auto& entry : report->chd_entries) {
        INFO(entry.name);
        REQUIRE(entry.payload_extracted);
        REQUIRE(entry.chd.has_value());
        CHECK(entry.chd->version == 5U);
        CHECK(entry.chd->unit_bytes == 512U);
        CHECK(entry.chd->hunk_bytes >= entry.chd->unit_bytes);
        CHECK_FALSE(entry.chd->has_cd_unit_layout);
    }

    const auto cards = gnet::load_gnet_flash_cards(std::span<const std::uint8_t>{*bytes});
    REQUIRE(cards.has_value());
    REQUIRE(cards->size() == report->chd_entries.size());
    for (const auto& card : *cards) {
        INFO(card.name);
        CHECK(card.media.info.unit_bytes == 512U);
        CHECK(card.media.data.size() == card.media.info.logical_bytes);
        CHECK(std::any_of(card.media.data.begin(), card.media.data.end(),
                          [](std::uint8_t byte) { return byte != 0U; }));
    }
}
