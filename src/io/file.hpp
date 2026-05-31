#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::io {

    // Read the file at `path` into a byte vector. Returns nullopt if the
    // file can't be opened. Zero-byte files are returned as an empty vector,
    // not nullopt -- callers should treat empty as a load failure if they
    // need non-empty contents.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(const std::string& path);

    // Write `bytes` to the file at `path`, truncating any existing file.
    // Returns false if the file can't be opened or the stream goes bad mid-write.
    [[nodiscard]] bool write_file(const std::string& path, std::span<const std::uint8_t> bytes);

} // namespace mnemos::io
