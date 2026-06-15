// CHD v5 reader: synthetic unit tests (CRC-16/CCITT-FALSE, the 2-level map
// Huffman decoder, an in-memory "none"-codec CHD opened through disc_image) and
// an env-gated real-corpus test (MNEMOS_SEGACD_CHD) over an actual .chd disc.

#include "chd_reader.hpp"
#include "disc_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

    using mnemos::disc::disc_format;
    using mnemos::disc::disc_image;
    namespace chd = mnemos::disc::chd;

    void put_be16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
        v[off] = static_cast<std::uint8_t>(x >> 8);
        v[off + 1] = static_cast<std::uint8_t>(x);
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

    constexpr std::uint32_t kSectorData = 2352;
    constexpr std::uint32_t kFrameSize = 2448; // 2352 + 96 subcode
    constexpr std::uint32_t kFramesPerHunk = 8;
    constexpr std::uint32_t kHunkBytes = kFramesPerHunk * kFrameSize; // 19584

    // Deterministic sector content for frame `f`: a valid Mode-1 sync header,
    // then user bytes derived from f. ECC left zeroed (the reader copies the
    // hunk verbatim for the "none" codec, so what we write is what we read).
    std::array<std::uint8_t, kSectorData> make_sector(std::uint32_t f) {
        std::array<std::uint8_t, kSectorData> s{};
        s[0] = 0x00;
        for (int i = 1; i <= 10; ++i) {
            s[static_cast<std::size_t>(i)] = 0xFF;
        }
        s[11] = 0x00;
        s[15] = 0x01; // Mode 1
        for (std::size_t i = 16; i < kSectorData; ++i) {
            s[i] = static_cast<std::uint8_t>(f * 31U + i);
        }
        return s;
    }

    // Build a minimal valid v5 "none"-codec CHD with one hunk of 8 raw frames
    // and a single MODE1_RAW CHT2 track.
    std::vector<std::uint8_t> build_none_chd() {
        // The uncompressed v5 map stores a hunk *index*; the reader resolves the
        // file offset as index * hunkbytes, so the hunk data must sit on a
        // hunkbytes-aligned boundary. We place it at offset hunkbytes (index 1).
        // Layout: [124 header][pad ..to hunkbytes][hunk: 19584][4-byte map][meta]
        const std::uint64_t hunk_data_offset = kHunkBytes; // index 1
        const std::uint64_t map_offset = hunk_data_offset + kHunkBytes;
        const std::uint64_t meta_offset = map_offset + 4;

        std::string meta = "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:8 PREGAP:0 PGTYPE:MODE1 "
                           "PGSUB:RW POSTGAP:0";
        const std::uint32_t meta_len = static_cast<std::uint32_t>(meta.size()) + 1; // include NUL
        const std::uint64_t total = meta_offset + 16 + meta_len;

        std::vector<std::uint8_t> v(static_cast<std::size_t>(total), 0);

        // Header.
        std::memcpy(v.data(), "MComprHD", 8);
        put_be32(v, 8, 124); // header length
        put_be32(v, 12, 5);  // version
        put_be32(v, 16, 0);  // compressor[0] = none
        put_be32(v, 20, 0);
        put_be32(v, 24, 0);
        put_be32(v, 28, 0);
        put_be64(v, 32, kHunkBytes);  // logical bytes (1 hunk)
        put_be64(v, 40, map_offset);  // map offset
        put_be64(v, 48, meta_offset); // meta offset
        put_be32(v, 56, kHunkBytes);  // hunk bytes
        put_be32(v, 60, kFrameSize);  // unit bytes (2448)

        // Hunk data: 8 interleaved 2448-byte units (sector + zero subcode).
        for (std::uint32_t f = 0; f < kFramesPerHunk; ++f) {
            const auto sec = make_sector(f);
            std::memcpy(v.data() + hunk_data_offset + static_cast<std::size_t>(f) * kFrameSize,
                        sec.data(), kSectorData);
        }

        // Uncompressed map: one 4-byte big-endian hunk index. blockoffs =
        // index * hunkbytes; index 1 places the hunk at offset hunkbytes.
        put_be32(v, static_cast<std::size_t>(map_offset), 1);

        // Metadata entry: [tag 'CHT2'][len(+flags)][next=0][payload].
        const std::size_t m = static_cast<std::size_t>(meta_offset);
        v[m] = 'C';
        v[m + 1] = 'H';
        v[m + 2] = 'T';
        v[m + 3] = '2';
        put_be32(v, m + 4, meta_len); // flags (high byte) = 0
        put_be64(v, m + 8, 0);        // next = 0 (end of list)
        std::memcpy(v.data() + m + 16, meta.data(), meta.size());

        return v;
    }

} // namespace

TEST_CASE("chd crc16/ccitt-false matches known vectors", "[disc][chd]") {
    // The canonical CRC-16/CCITT-FALSE check value for "123456789" is 0x29B1.
    const std::array<std::uint8_t, 9> check = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(chd::crc16_ccitt(std::span<const std::uint8_t>{check}) == 0x29B1);
    // Empty input returns the init value.
    REQUIRE(chd::crc16_ccitt(std::span<const std::uint8_t>{}) == 0xFFFF);
    // Single 0x00 byte: 0xFFFF -> table[0xFF] folded.
    const std::array<std::uint8_t, 1> z = {0x00};
    REQUIRE(chd::crc16_ccitt(std::span<const std::uint8_t>{z}) == 0xE1F0);
}

namespace {

    // MSB-first bit writer matching the CHD map bit order (bit 7 of byte 0 first).
    struct bit_writer {
        std::vector<std::uint8_t> bytes;
        std::uint32_t acc{};
        int nbits{};
        void put(std::uint32_t value, int width) {
            for (int i = width - 1; i >= 0; --i) {
                acc = (acc << 1) | ((value >> i) & 1U);
                if (++nbits == 8) {
                    bytes.push_back(static_cast<std::uint8_t>(acc));
                    acc = 0;
                    nbits = 0;
                }
            }
        }
        void flush() {
            if (nbits > 0) {
                acc <<= (8 - nbits);
                bytes.push_back(static_cast<std::uint8_t>(acc));
                acc = 0;
                nbits = 0;
            }
        }
    };

} // namespace

TEST_CASE("chd 2-level map huffman decodes a hand-built compressed map", "[disc][chd]") {
    // Hand-build the COMPRESSED MAP bitstream for a 3-hunk map where every hunk
    // is COMPRESSION_NONE (type 4). The type-tree is RLE-encoded with 4-bit
    // length fields (maxbits=8 alphabet of 16). We assign symbol 4 a code length
    // of 1 (canonical code "0") and all other symbols length 0:
    //   nodes 0-3 : length 0           -> 4x "0000"
    //   node 4    : literal length 1   -> escape "0001" then "0001"
    //   nodes 5-15: run of 11 zeros    -> escape "0001", value "0000", rep "1000"
    // Then the per-hunk type stream is 3x the 1-bit code for symbol 4 ("0"), and
    // each NONE hunk contributes a 16-bit CRC field (we use 0).
    constexpr std::uint32_t kHunks = 3;
    bit_writer bw;
    // Type tree (RLE).
    for (int i = 0; i < 4; ++i) {
        bw.put(0, 4); // nodes 0-3 length 0
    }
    bw.put(1, 4); // escape
    bw.put(1, 4); // -> literal length 1 (node 4)
    bw.put(1, 4); // escape
    bw.put(0, 4); // value 0
    bw.put(8, 4); // repcount 8 -> run of 11 (covers nodes 5-15)
    // Type stream: 3 hunks, each symbol 4 = single "0" bit.
    for (std::uint32_t h = 0; h < kHunks; ++h) {
        bw.put(0, 1);
    }
    // Pass 2: each NONE hunk reads a 16-bit CRC (0).
    for (std::uint32_t h = 0; h < kHunks; ++h) {
        bw.put(0, 16);
    }
    bw.flush();

    // Compute the expected decoded raw map to derive the header CRC: 3 entries,
    // each [type=4][len=hunkbytes(u24)][offset(u48)][crc=0(u16)] with offsets
    // running from firstoffs and advancing by hunkbytes.
    const std::uint64_t firstoffs = 1000;
    std::vector<std::uint8_t> expected(kHunks * 12U, 0);
    std::uint64_t off = firstoffs;
    for (std::uint32_t h = 0; h < kHunks; ++h) {
        std::uint8_t* e = expected.data() + h * 12U;
        e[0] = 4; // COMPRESSION_NONE
        e[1] = static_cast<std::uint8_t>(kHunkBytes >> 16);
        e[2] = static_cast<std::uint8_t>(kHunkBytes >> 8);
        e[3] = static_cast<std::uint8_t>(kHunkBytes);
        for (int i = 5; i >= 0; --i) {
            e[4 + (5 - i)] = static_cast<std::uint8_t>(off >> (i * 8));
        }
        off += kHunkBytes;
    }
    const std::uint16_t mapcrc = chd::crc16_ccitt(std::span<const std::uint8_t>{expected});

    // Assemble the 16-byte map header + compressed payload.
    std::vector<std::uint8_t> section(16, 0);
    put_be32(section, 0, static_cast<std::uint32_t>(bw.bytes.size())); // mapbytes
    for (int i = 5; i >= 0; --i) {
        section[4 + (5 - i)] = static_cast<std::uint8_t>(firstoffs >> (i * 8)); // firstoffs u48
    }
    put_be16(section, 10, mapcrc);
    section[12] = 24; // lengthbits (unused by NONE entries)
    section[13] = 24; // selfbits
    section[14] = 24; // parentbits
    section[15] = 0;
    section.insert(section.end(), bw.bytes.begin(), bw.bytes.end());

    auto decoded = chd::decode_compressed_map(std::span<const std::uint8_t>{section}, kHunks,
                                              kHunkBytes, kFrameSize);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == expected.size());
    REQUIRE(std::equal(decoded->begin(), decoded->end(), expected.begin()));

    // The decoder must reject a corrupted map (CRC mismatch).
    std::vector<std::uint8_t> bad = section;
    bad[10] ^= 0xFF; // flip the stored map CRC
    REQUIRE_FALSE(chd::decode_compressed_map(std::span<const std::uint8_t>{bad}, kHunks, kHunkBytes,
                                             kFrameSize)
                      .has_value());
}

TEST_CASE("chd opens a synthetic none-codec image through disc_image", "[disc][chd]") {
    const auto bytes = build_none_chd();

    // Low-level decode.
    auto decoded = chd::decode(std::span<const std::uint8_t>{bytes});
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->tracks.size() == 1);
    REQUIRE(decoded->total_sectors == 8);

    // Through the public disc_image surface.
    auto img = disc_image::open_chd(bytes);
    REQUIRE(img.has_value());
    REQUIRE(img->format() == disc_format::chd);
    REQUIRE(img->total_sectors() == 8);
    REQUIRE(img->track_count() == 1);

    for (std::uint32_t lba = 0; lba < 8; ++lba) {
        std::array<std::uint8_t, 2352> raw{};
        REQUIRE(img->read_raw_sector(lba, std::span<std::uint8_t, 2352>{raw}));
        const auto golden = make_sector(lba);
        REQUIRE(std::equal(raw.begin(), raw.end(), golden.begin()));
        // Sync pattern at the head of every Mode-1 raw sector.
        REQUIRE(raw[0] == 0x00);
        REQUIRE(raw[1] == 0xFF);
        REQUIRE(raw[11] == 0x00);
    }
    REQUIRE(img->find_track(8) == nullptr); // out of range
}

// Data-gated: point MNEMOS_SEGACD_CHD at a real Sega CD .chd (cdzl/cdlz, no
// FLAC) to exercise the compressed-map + codec path end to end. Skips cleanly
// when unset so the suite stays green without the corpus.
TEST_CASE("chd opens a real corpus image", "[disc][chd][data]") {
#if defined(_MSC_VER)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    const char* chd_path = std::getenv("MNEMOS_SEGACD_CHD");
    if (chd_path == nullptr) {
        SUCCEED("MNEMOS_SEGACD_CHD not set -- skipping real-CHD parse");
        return;
    }

    auto img = disc_image::open(std::string{chd_path});
    REQUIRE(img.has_value());
    REQUIRE(img->format() == disc_format::chd);
    REQUIRE(img->track_count() >= 1);
    REQUIRE(img->total_sectors() > 0);

    // The first track is the data track; its first sector must be a Mode-1 raw
    // sector beginning with the 12-byte sync pattern.
    const auto tracks = img->tracks();
    const std::uint32_t lba = tracks.front().start_lba;
    std::array<std::uint8_t, 2352> raw{};
    REQUIRE(img->read_raw_sector(lba, std::span<std::uint8_t, 2352>{raw}));
    static const std::array<std::uint8_t, 12> sync = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    REQUIRE(std::equal(sync.begin(), sync.end(), raw.begin()));
}
