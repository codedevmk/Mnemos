#pragma once

// CHD v5 compressed-media reader; clean-room from the public CHD format.
//
// Decodes a v5 CHD (header + compressed/uncompressed hunk map + CD track
// metadata) into a flat, fully decompressed image of raw 2352-byte CD sectors
// plus a synthesised track table. Data-track codecs only for now: the
// uncompressed "none" codec, "cdzl" (DEFLATE sector lane), and "cdlz" (LZMA
// sector lane). FLAC ("cdfl") audio is rejected -- a disc whose primary codec
// is cdfl cannot be opened here yet. Block-device CHDs decode into their raw
// logical byte image for flash-card/HDD media using the same v5 hunk map.
//
// The decoder reuses the in-tree DEFLATE (compression::inflate_raw), LZMA1
// (compression::lzma1_decode) and Mode-1 CIRC ECC regeneration
// (disc::circ_ecc_regen_sector). The 96-byte CD subcode lane is intentionally
// dropped (downstream CD consumers never read it).

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::disc::chd {

    // CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no input/output
    // reflection, no final XOR. Used to validate the decoded v5 map.
    [[nodiscard]] std::uint16_t crc16_ccitt(std::span<const std::uint8_t> data) noexcept;

    // One synthesised CD track parsed from the CHD CD-track metadata.
    struct chd_track {
        std::uint32_t number{};       // 1-based track number
        std::uint32_t start_lba{};    // absolute LBA where the track begins
        std::uint32_t sector_count{}; // playable frames (pregap excluded)
        std::uint64_t data_offset{};  // byte offset of the track in the flat image
        std::uint32_t sector_size{};  // 2352 (raw) / 2048 (cooked) / 2336
        bool is_audio{};
    };

    // A fully decoded CHD CD image: one contiguous 2352-bytes-per-sector buffer
    // (subcode stripped) and the track table that indexes into it.
    struct chd_image_data {
        std::vector<std::uint8_t> data; // flat raw sectors, frame-ordered
        std::vector<chd_track> tracks;
        std::uint32_t total_sectors{};
    };

    // Header-level information for any valid v5 CHD. This probe intentionally
    // does not decode the hunk map or require CD geometry; block-device CHDs
    // used by arcade flash/HDD media can be identified here even when decode()
    // cannot mount them yet.
    struct chd_file_info {
        std::uint32_t version{};
        std::uint32_t header_bytes{};
        std::array<std::uint32_t, 4> codecs{};
        std::uint64_t logical_bytes{};
        std::uint64_t map_offset{};
        std::uint64_t meta_offset{};
        std::uint32_t hunk_bytes{};
        std::uint32_t unit_bytes{};
        std::uint64_t hunk_count{};
        bool has_cd_unit_layout{};
    };

    struct chd_block_image_data {
        chd_file_info info;
        std::vector<std::uint8_t> data; // flat logical block-device bytes
    };

    // Parse and validate the CHD v5 fixed header without decoding media.
    // Returns nullopt for non-CHD input, unsupported CHD versions, or malformed
    // dimensions that would overflow downstream map sizing.
    [[nodiscard]] std::optional<chd_file_info> probe(std::span<const std::uint8_t> file);

    // Decode an entire CHD file image already held in memory. Returns nullopt
    // for: a non-CHD / non-v5 file, an unsupported codec (e.g. cdfl primary),
    // a map-CRC mismatch, or any malformed/oversized field.
    [[nodiscard]] std::optional<chd_image_data> decode(std::span<const std::uint8_t> file);

    // Decode a non-CD v5 CHD into a flat logical block-device image. Intended
    // for arcade flash cards and HDDs; callers must provide a memory cap because
    // later Taito arcade-PC images can be multi-GB CHDs.
    [[nodiscard]] std::optional<chd_block_image_data>
    decode_block_device(std::span<const std::uint8_t> file,
                        std::uint64_t max_logical_bytes = 512ULL * 1024ULL * 1024ULL);

    // ---- Pieces exposed for unit testing ----

    // Decode a v5 compressed hunk map (the 2-level "Huffman of Huffman" path).
    // `map_section` is the bytes from the file's map offset onward; `hunk_count`
    // hunks are produced. On success returns the packed 12-byte-per-hunk raw map
    // (type, u24 length, u48 offset, u16 crc) with its CRC already verified.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    decode_compressed_map(std::span<const std::uint8_t> map_section, std::uint32_t hunk_count,
                          std::uint32_t hunk_bytes, std::uint32_t unit_bytes);

} // namespace mnemos::disc::chd
