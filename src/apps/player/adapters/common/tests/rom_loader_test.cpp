#include "rom_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::clean_rom_name;
    using mnemos::apps::player::adapters::load_rom;

    // A deflate .zip holding a single "payload.bin" entry (128-byte body), from
    // PowerShell Compress-Archive. Exercises load_rom's signature detection,
    // entry selection, and inner-name plumbing end to end.
    constexpr std::array<std::uint8_t, 157> kRomZip = {
        0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x1A, 0x76, 0xBF, 0x5C, 0xE9,
        0xF1, 0xBC, 0xE7, 0x25, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00,
        0x70, 0x61, 0x79, 0x6C, 0x6F, 0x61, 0x64, 0x2E, 0x62, 0x69, 0x6E, 0xF3, 0xCD, 0x4B, 0xCD,
        0xCD, 0x2F, 0x56, 0x70, 0x71, 0x75, 0xF3, 0x71, 0x0C, 0x71, 0x55, 0x28, 0xCA, 0x2F, 0xCD,
        0x4B, 0xD1, 0x2D, 0x29, 0xCA, 0x2C, 0x50, 0x28, 0x49, 0x2D, 0x2E, 0xD1, 0x53, 0xF0, 0xA5,
        0xB1, 0x3C, 0x00, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00,
        0x1A, 0x76, 0xBF, 0x5C, 0xE9, 0xF1, 0xBC, 0xE7, 0x25, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x70, 0x61, 0x79, 0x6C, 0x6F, 0x61, 0x64, 0x2E, 0x62, 0x69, 0x6E,
        0x50, 0x4B, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x39, 0x00, 0x00,
        0x00, 0x4E, 0x00, 0x00, 0x00, 0x00, 0x00};

    [[nodiscard]] std::filesystem::path write_temp(const std::string& tag,
                                                   std::span<const std::uint8_t> data) {
        const auto p = std::filesystem::temp_directory_path() / ("mnemos_rom_loader_" + tag);
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        return p;
    }
} // namespace

TEST_CASE("rom_loader: clean_rom_name strips directory and extension") {
    CHECK(clean_rom_name("game.bin") == "game");
    CHECK(clean_rom_name("game") == "game"); // no extension
    CHECK(clean_rom_name("C:/roms/Sonic.md") == "Sonic");
    CHECK(clean_rom_name("/home/u/Sonic.md") == "Sonic");
    CHECK(clean_rom_name("dir/.hidden") == ""); // dotfile with no body
}

TEST_CASE("rom_loader: clean_rom_name preserves the basename verbatim") {
    // Region tags, parentheses, spaces, etc. stay in the title -- the
    // helper does no semantic cleanup.
    CHECK(clean_rom_name("Streets of Rage 2 (USA, Europe).md") ==
          "Streets of Rage 2 (USA, Europe)");
    CHECK(clean_rom_name("/r/Blades of Vengence (EJU) [!].bin") == "Blades of Vengence (EJU) [!]");
}

TEST_CASE("rom_loader: load_rom transparently extracts a zipped ROM by inner name") {
    const auto zip = write_temp("rom.zip", kRomZip);
    const auto loaded = load_rom(zip.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->bytes.size() == 128U);
    // Inner entry name drives family/title detection, not the .zip path.
    CHECK(loaded->name == "payload.bin");
    const std::vector<std::uint8_t> head{loaded->bytes.begin(), loaded->bytes.begin() + 6};
    CHECK(head == std::vector<std::uint8_t>{'M', 'n', 'e', 'm', 'o', 's'});
    std::filesystem::remove(zip);
}

TEST_CASE("rom_loader: load_rom returns a bare ROM unchanged with its path as the name") {
    const std::array<std::uint8_t, 6> raw = {'S', 'E', 'G', 'A', 0x01, 0x02};
    const auto path = write_temp("raw.bin", raw);
    const auto loaded = load_rom(path.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->bytes == std::vector<std::uint8_t>(raw.begin(), raw.end()));
    CHECK(loaded->name == path.string());
    std::filesystem::remove(path);
}
