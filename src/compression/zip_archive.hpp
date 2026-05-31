#pragma once

// Read-only PKZIP container reader over an in-memory archive buffer. Lists
// central-directory entries and extracts STORED (method 0) and DEFLATE
// (method 8) entries -- the only methods commercial-ROM .zip files use.
// ZIP64, encryption, and other methods are intentionally unsupported.
//
// Clean-room against the PKWARE APPNOTE.TXT v6.3.10 specification.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::compression {

    enum class zip_method : std::uint8_t { stored, deflated, unsupported };

    struct zip_entry final {
        std::string name;
        zip_method method{zip_method::unsupported};
        std::uint32_t compressed_size{};
        std::uint32_t uncompressed_size{};
        std::uint32_t local_header_offset{};
    };

    // Parsed view over a .zip held in memory. The archive BORROWS the buffer
    // passed to open() -- it must outlive the zip_archive (extract() reads from
    // it). Returns nullopt for a non-zip / truncated / malformed archive.
    class zip_archive final {
      public:
        [[nodiscard]] static std::optional<zip_archive> open(std::span<const std::uint8_t> bytes);

        [[nodiscard]] const std::vector<zip_entry>& entries() const noexcept { return entries_; }

        // Extract one entry to a fresh buffer. nullopt on a malformed local
        // header, a decompression error, or an unsupported method.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>>
        extract(const zip_entry& entry) const;

      private:
        std::span<const std::uint8_t> bytes_;
        std::vector<zip_entry> entries_;
    };

} // namespace mnemos::compression
