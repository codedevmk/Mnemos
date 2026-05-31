#include "rom_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
    using mnemos::apps::player::adapters::clean_rom_name;
    using mnemos::apps::player::adapters::read_file;
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

TEST_CASE("rom_loader: read_file returns nullopt for a missing path") {
    CHECK(read_file("this/file/definitely/does/not/exist.bin") == std::nullopt);
}

TEST_CASE("rom_loader: read_file round-trips a binary blob") {
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "mnemos_rom_loader_roundtrip.bin";
    const std::string path = tmp.string();
    const std::vector<std::uint8_t> payload{0x00, 0xFF, 0x7E, 0x81, 0xDE, 0xAD, 0xBE, 0xEF};
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
    }
    auto loaded = read_file(path);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == payload);
    std::remove(path.c_str());
}

TEST_CASE("rom_loader: read_file returns an empty vector for a zero-byte file") {
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "mnemos_rom_loader_empty.bin";
    const std::string path = tmp.string();
    {
        std::ofstream out(path, std::ios::binary);
    }
    auto loaded = read_file(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->empty());
    std::remove(path.c_str());
}
