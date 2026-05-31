// Pins the file I/O primitives: write_file -> read_file round-trips a binary
// blob, and read_file of a missing path returns nullopt.

#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

using mnemos::io::read_file;
using mnemos::io::write_file;

TEST_CASE("io: write_file -> read_file round-trips a binary blob") {
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "mnemos_io_file_roundtrip.bin";
    const std::string path = tmp.string();
    const std::vector<std::uint8_t> payload{0x00, 0xFF, 0x7E, 0x81, 0xDE, 0xAD, 0xBE, 0xEF};

    REQUIRE(write_file(path, payload));
    auto loaded = read_file(path);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == payload);

    fs::remove(tmp);
}

TEST_CASE("io: read_file returns nullopt for a missing path") {
    CHECK(read_file("this/file/definitely/does/not/exist.bin") == std::nullopt);
}
