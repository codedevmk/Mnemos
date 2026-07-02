#include "rom_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <span>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::clean_rom_name;
    using mnemos::apps::player::adapters::load_rom;
    using mnemos::apps::player::adapters::load_rom_entries_by_extension;
    using mnemos::apps::player::adapters::load_rom_verbatim;

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

    void put_tar_octal(std::array<std::uint8_t, 512>& header, std::size_t offset,
                       std::size_t width, std::uint64_t value) {
        std::string digits;
        do {
            digits.push_back(static_cast<char>('0' + (value & 7U)));
            value >>= 3U;
        } while (value != 0U);
        std::reverse(digits.begin(), digits.end());

        std::fill(header.begin() + static_cast<std::ptrdiff_t>(offset),
                  header.begin() + static_cast<std::ptrdiff_t>(offset + width),
                  static_cast<std::uint8_t>(0U));
        const std::size_t digit_capacity = width - 1U;
        const std::size_t start = offset + digit_capacity - digits.size();
        std::copy(digits.begin(), digits.end(),
                  header.begin() + static_cast<std::ptrdiff_t>(start));
    }

    void append_tar_entry(std::vector<std::uint8_t>& tar, std::string_view name,
                          std::span<const std::uint8_t> data) {
        std::array<std::uint8_t, 512> header{};
        REQUIRE(name.size() < 100U);
        std::copy(name.begin(), name.end(), header.begin());
        put_tar_octal(header, 100U, 8U, 0644U);
        put_tar_octal(header, 108U, 8U, 0U);
        put_tar_octal(header, 116U, 8U, 0U);
        put_tar_octal(header, 124U, 12U, data.size());
        put_tar_octal(header, 136U, 12U, 0U);
        std::fill(header.begin() + 148U, header.begin() + 156U,
                  static_cast<std::uint8_t>(' '));
        header[156U] = static_cast<std::uint8_t>('0');
        constexpr std::string_view magic = "ustar";
        std::copy(magic.begin(), magic.end(), header.begin() + 257U);

        std::uint64_t checksum = 0U;
        for (std::uint8_t byte : header) {
            checksum += byte;
        }
        put_tar_octal(header, 148U, 8U, checksum);

        tar.insert(tar.end(), header.begin(), header.end());
        tar.insert(tar.end(), data.begin(), data.end());
        const std::size_t padding = (512U - (data.size() % 512U)) % 512U;
        tar.insert(tar.end(), padding, static_cast<std::uint8_t>(0U));
    }

    [[nodiscard]] std::vector<std::uint8_t> make_tar(
        const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> tar;
        for (const auto& [name, data] : entries) {
            append_tar_entry(tar, name, data);
        }
        tar.insert(tar.end(), 1024U, static_cast<std::uint8_t>(0U));
        return tar;
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

TEST_CASE("rom_loader: load_rom_entries_by_extension extracts ordered ADFs from tar archives") {
    std::vector<std::uint8_t> disk1(901120U, 0x11U);
    std::vector<std::uint8_t> disk2(901120U, 0x22U);
    disk1[0] = 0x44U;
    disk1[1] = 0x89U;
    disk2[0] = 0x44U;
    disk2[1] = 0x89U;

    const auto tar = make_tar({
        {"__MACOSX/._Game Disk 1 of 2.adf", {0x00U}},
        {"Game Disk 2 of 2.adf", disk2},
        {"readme.txt", {'i', 'g', 'n', 'o', 'r', 'e'}},
        {"Game Disk 1 of 2.adf", disk1},
    });
    const auto path = write_temp("amiga_adf_bundle.tar", tar);
    constexpr std::array<std::string_view, 3> extensions = {".adf", ".adz", ".gz"};

    const auto loaded = load_rom_entries_by_extension(path.string(), extensions);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 2U);
    CHECK((*loaded)[0].name == "Game Disk 1 of 2.adf");
    CHECK((*loaded)[0].bytes == disk1);
    CHECK((*loaded)[1].name == "Game Disk 2 of 2.adf");
    CHECK((*loaded)[1].bytes == disk2);

    std::filesystem::remove(path);
}

TEST_CASE("rom_loader: load_rom_verbatim accepts directory-backed ROM sets") {
    const auto dir = std::filesystem::temp_directory_path() / "mnemos_rom_loader_arcade_set";
    REQUIRE((std::filesystem::create_directories(dir) || std::filesystem::exists(dir)));

    const auto loaded = load_rom_verbatim(dir.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->bytes.empty());
    CHECK(loaded->name == dir.string());
    CHECK(loaded->directory_source);

    std::filesystem::remove_all(dir);
}
