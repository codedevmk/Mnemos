// CHD v5 reader: synthetic unit tests (CRC-16/CCITT-FALSE, the 2-level map
// Huffman decoder, an in-memory "none"-codec CHD opened through disc_image) and
// env-gated real-corpus tests (MNEMOS_SEGACD_CHD) over an actual .chd disc -- a
// data-track parse + a cdfl (FLAC) audio-track byte-order check.

#include "chd_reader.hpp"
#include "disc_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

    using mnemos::disc::disc_format;
    using mnemos::disc::disc_image;
    using mnemos::disc::track_type;
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
    constexpr std::uint32_t kBlockUnitBytes = 512;
    constexpr std::uint32_t kBlockHunkBytes = 4096;

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

    std::vector<std::uint8_t> build_none_block_chd() {
        const std::uint64_t hunk_data_offset = kBlockHunkBytes; // raw-map index 1
        const std::uint64_t map_offset = hunk_data_offset + kBlockHunkBytes;
        const std::uint64_t total = map_offset + 4;

        std::vector<std::uint8_t> v(static_cast<std::size_t>(total), 0);
        std::memcpy(v.data(), "MComprHD", 8);
        put_be32(v, 8, 124);
        put_be32(v, 12, 5);
        put_be32(v, 16, 0);
        put_be32(v, 20, 0);
        put_be32(v, 24, 0);
        put_be32(v, 28, 0);
        put_be64(v, 32, kBlockHunkBytes);
        put_be64(v, 40, map_offset);
        put_be64(v, 48, 0);
        put_be32(v, 56, kBlockHunkBytes);
        put_be32(v, 60, kBlockUnitBytes);

        for (std::uint32_t i = 0; i < kBlockHunkBytes; ++i) {
            v[static_cast<std::size_t>(hunk_data_offset) + i] =
                static_cast<std::uint8_t>((i * 37U) ^ (i >> 3U));
        }
        put_be32(v, static_cast<std::size_t>(map_offset), 1);
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

TEST_CASE("chd probe exposes v5 header metadata without decoding media", "[disc][chd]") {
    const auto bytes = build_none_chd();
    const auto info = chd::probe(std::span<const std::uint8_t>{bytes});
    REQUIRE(info.has_value());
    CHECK(info->version == 5U);
    CHECK(info->header_bytes == 124U);
    CHECK(info->codecs[0] == 0U);
    CHECK(info->logical_bytes == kHunkBytes);
    CHECK(info->hunk_bytes == kHunkBytes);
    CHECK(info->unit_bytes == kFrameSize);
    CHECK(info->hunk_count == 1U);
    CHECK(info->has_cd_unit_layout);

    auto block_device = bytes;
    put_be32(block_device, 16, 0x6C7A6D61U); // "lzma"
    put_be64(block_device, 32, 40960000ULL);
    put_be32(block_device, 56, 4096U);
    put_be32(block_device, 60, 512U);
    const auto block_info = chd::probe(std::span<const std::uint8_t>{block_device});
    REQUIRE(block_info.has_value());
    CHECK(block_info->codecs[0] == 0x6C7A6D61U);
    CHECK(block_info->hunk_count == 10000U);
    CHECK_FALSE(block_info->has_cd_unit_layout);

    auto corrupt = bytes;
    corrupt[0] = 0U;
    CHECK_FALSE(chd::probe(std::span<const std::uint8_t>{corrupt}).has_value());
}

TEST_CASE("chd decodes a synthetic none-codec block device image", "[disc][chd]") {
    const auto bytes = build_none_block_chd();
    CHECK_FALSE(chd::decode(std::span<const std::uint8_t>{bytes}).has_value());

    const auto block = chd::decode_block_device(std::span<const std::uint8_t>{bytes});
    REQUIRE(block.has_value());
    CHECK(block->info.logical_bytes == kBlockHunkBytes);
    CHECK(block->info.unit_bytes == kBlockUnitBytes);
    REQUIRE(block->data.size() == kBlockHunkBytes);
    for (std::uint32_t i = 0; i < kBlockHunkBytes; ++i) {
        CHECK(block->data[i] == static_cast<std::uint8_t>((i * 37U) ^ (i >> 3U)));
    }
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

    std::vector<std::uint8_t> build_huff_block_chd() {
        constexpr std::uint32_t hunk_bytes = 256U;
        constexpr std::uint32_t unit_bytes = 1U;
        constexpr std::uint32_t header_bytes = 124U;

        bit_writer huff;
        // The CHD 8-bit Huffman payload first imports a compact tree-of-tree.
        // This stream makes every byte symbol use its natural 8-bit canonical code.
        huff.put(1, 3); // small.numbits[0] = 1
        huff.put(0, 3); // start = 1
        for (int index = 1; index < 9; ++index) {
            huff.put(0, 3);
        }
        huff.put(1, 3); // small.numbits[9] = 1
        huff.put(7, 3); // stop importing remaining small-tree lengths
        huff.put(1, 1); // symbol 9 => code length 8 for main-tree symbol 0
        huff.put(0, 1); // symbol 0 => repeat previous length
        huff.put(7, 3);
        huff.put(246, 8); // 9 + 246 = 255 repeated lengths
        for (std::uint32_t i = 0; i < hunk_bytes; ++i) {
            huff.put(i, 8);
        }
        huff.flush();

        bit_writer map;
        map.put(1, 4);  // escape
        map.put(1, 4);  // symbol 0 has one-bit code length
        map.put(1, 4);  // escape
        map.put(0, 4);  // repeat zero lengths
        map.put(12, 4); // nodes 1..15
        map.put(0, 1);  // hunk type 0, codec slot 0
        map.put(static_cast<std::uint32_t>(huff.bytes.size()), 16);
        map.put(0, 16); // per-hunk CRC field is stored in the raw map
        map.flush();

        std::vector<std::uint8_t> section(16, 0);
        const std::uint64_t firstoffs = header_bytes + section.size() + map.bytes.size();
        std::vector<std::uint8_t> rawmap(12, 0);
        rawmap[0] = 0; // codec slot 0
        rawmap[1] = static_cast<std::uint8_t>(huff.bytes.size() >> 16U);
        rawmap[2] = static_cast<std::uint8_t>(huff.bytes.size() >> 8U);
        rawmap[3] = static_cast<std::uint8_t>(huff.bytes.size());
        for (int i = 5; i >= 0; --i) {
            rawmap[4 + (5 - i)] = static_cast<std::uint8_t>(firstoffs >> (i * 8));
        }
        const std::uint16_t mapcrc = chd::crc16_ccitt(std::span<const std::uint8_t>{rawmap});

        put_be32(section, 0, static_cast<std::uint32_t>(map.bytes.size()));
        for (int i = 5; i >= 0; --i) {
            section[4 + (5 - i)] = static_cast<std::uint8_t>(firstoffs >> (i * 8));
        }
        put_be16(section, 10, mapcrc);
        section[12] = 16; // lengthbits
        section[13] = 16; // selfbits
        section[14] = 16; // parentbits
        section.insert(section.end(), map.bytes.begin(), map.bytes.end());

        std::vector<std::uint8_t> v;
        v.resize(header_bytes, 0);
        std::memcpy(v.data(), "MComprHD", 8);
        put_be32(v, 8, header_bytes);
        put_be32(v, 12, 5);
        put_be32(v, 16, 0x68756666U); // "huff"
        put_be64(v, 32, hunk_bytes);
        put_be64(v, 40, header_bytes);
        put_be64(v, 48, 0);
        put_be32(v, 56, hunk_bytes);
        put_be32(v, 60, unit_bytes);
        v.insert(v.end(), section.begin(), section.end());
        v.insert(v.end(), huff.bytes.begin(), huff.bytes.end());
        return v;
    }

} // namespace

TEST_CASE("chd decodes a synthetic huff-codec block device image", "[disc][chd]") {
    const auto bytes = build_huff_block_chd();
    const auto block = chd::decode_block_device(std::span<const std::uint8_t>{bytes});
    REQUIRE(block.has_value());
    CHECK(block->info.codecs[0] == 0x68756666U);
    CHECK(block->info.logical_bytes == 256U);
    CHECK(block->info.unit_bytes == 1U);
    REQUIRE(block->data.size() == 256U);
    for (std::uint32_t i = 0; i < 256U; ++i) {
        CHECK(block->data[i] == static_cast<std::uint8_t>(i));
    }
}

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

// Data-gated: decode the disc's first CD-DA (cdfl/FLAC) audio track and confirm
// the FLAC payload was decoded (open succeeds with cdfl active -- every frame's
// CRC-8/CRC-16 had to validate) and that the output byte order is right. CD audio
// is highly correlated sample to sample, so the correct little-endian reading is
// far "smoother" (small consecutive-sample deltas) than the byte-swapped reading,
// which looks like noise -- a definitive check that the frame CRCs cannot give.
TEST_CASE("chd cdfl audio track decodes to plausible little-endian CD-DA",
          "[disc][chd][data][audio]") {
#if defined(_MSC_VER)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    const char* chd_path = std::getenv("MNEMOS_SEGACD_CHD");
    if (chd_path == nullptr) {
        SUCCEED("MNEMOS_SEGACD_CHD not set -- skipping cdfl audio check");
        return;
    }
    const auto img = disc_image::open(std::string{chd_path});
    REQUIRE(img.has_value());

    const disc_image::track* audio = nullptr;
    for (const disc_image::track& t : img->tracks()) {
        if (t.type == track_type::audio) {
            audio = &t;
            break;
        }
    }
    if (audio == nullptr) {
        SUCCEED("no CD-DA track on this disc -- skipping cdfl audio check");
        return;
    }

    // Scan the audio track for a sector carrying real signal (a loud sample),
    // then compare consecutive-sample smoothness for the little-endian reading
    // (how the reader writes it) versus the byte-swapped reading.
    constexpr int samples_per_sector = 588; // 2352 / 4
    std::array<std::uint8_t, 2352> raw{};
    double le_delta = 0.0;
    double swapped_delta = 0.0;
    int loud_sectors = 0;
    // Aggregate over many signal-bearing sectors rather than judging a single one:
    // a lone atypical sector (noise burst, sound effect) can read smoother
    // byte-swapped, but real music is overwhelmingly smoother in the correct order
    // across the track.
    const std::uint32_t scan = std::min<std::uint32_t>(audio->sector_count, 20000U);
    for (std::uint32_t s = 0; s < scan && loud_sectors < 256; ++s) {
        REQUIRE(img->read_raw_sector(audio->start_lba + s, std::span<std::uint8_t, 2352>{raw}));
        std::int32_t peak = 0;
        for (int i = 0; i < samples_per_sector; ++i) {
            const auto le =
                static_cast<std::int16_t>(static_cast<std::uint16_t>(raw[i * 4]) |
                                          (static_cast<std::uint16_t>(raw[i * 4 + 1]) << 8U));
            peak = std::max(peak, std::abs(static_cast<int>(le)));
        }
        if (peak < 4000) {
            continue; // silence / very quiet -- not a useful smoothness sample
        }
        std::int16_t prev_le = 0;
        std::int16_t prev_swapped = 0;
        for (int i = 0; i < samples_per_sector; ++i) {
            const auto le =
                static_cast<std::int16_t>(static_cast<std::uint16_t>(raw[i * 4]) |
                                          (static_cast<std::uint16_t>(raw[i * 4 + 1]) << 8U));
            const auto swapped =
                static_cast<std::int16_t>((static_cast<std::uint16_t>(raw[i * 4]) << 8U) |
                                          static_cast<std::uint16_t>(raw[i * 4 + 1]));
            if (i > 0) {
                le_delta += std::abs(static_cast<int>(le) - static_cast<int>(prev_le));
                swapped_delta +=
                    std::abs(static_cast<int>(swapped) - static_cast<int>(prev_swapped));
            }
            prev_le = le;
            prev_swapped = swapped;
        }
        ++loud_sectors;
    }

    REQUIRE(loud_sectors >= 16);     // the cdfl track decoded to real, non-silent audio
    CHECK(le_delta < swapped_delta); // little-endian is the natural sample order
}
