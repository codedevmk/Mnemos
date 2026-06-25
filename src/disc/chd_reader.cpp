// CHD v5 compressed-disc reader; clean-room from the public CHD format.
//
// See chd_reader.hpp for the contract and codec scope. The v5 compressed map
// uses a two-level canonical-Huffman scheme (a small "Huffman of Huffman"
// length tree decodes the code lengths of the 16-symbol compression-type tree),
// big-endian on disk, MSB-first bit order.

#include "chd_reader.hpp"

#include "circ_ecc.hpp"
#include "flac_decoder.hpp"
#include "inflate.hpp"
#include "lzma1.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::disc::chd {
    namespace {

        // ---- CD geometry ----
        constexpr std::uint32_t kSectorData = 2352;                  // raw sector bytes
        constexpr std::uint32_t kSubcode = 96;                       // subcode bytes (dropped)
        constexpr std::uint32_t kFrameSize = kSectorData + kSubcode; // 2448, a "unit"
        constexpr std::uint32_t kTrackPadding = 4; // tracks padded to 4-frame runs

        constexpr std::array<std::uint8_t, 12> kSyncHeader = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

        // ---- v5 header layout (124 bytes, big-endian) ----
        constexpr std::size_t kHeaderSize = 124;
        constexpr std::uint32_t kCodecNone = 0;
        // FourCC codecs we accept, read as a big-endian 32-bit word.
        constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
            return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) << 24) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 16) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 8) |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(d));
        }
        constexpr std::uint32_t kCodecCdzl = fourcc('c', 'd', 'z', 'l');
        constexpr std::uint32_t kCodecCdlz = fourcc('c', 'd', 'l', 'z');
        constexpr std::uint32_t kCodecCdfl = fourcc('c', 'd', 'f', 'l'); // CD-DA audio (FLAC)
        constexpr std::uint32_t kCodecLzma = fourcc('l', 'z', 'm', 'a');
        constexpr std::uint32_t kCodecZlib = fourcc('z', 'l', 'i', 'b');
        constexpr std::uint32_t kCodecHuff = fourcc('h', 'u', 'f', 'f');
        constexpr std::uint32_t kCodecFlac = fourcc('f', 'l', 'a', 'c');

        // ---- v5 map entry compression types ----
        constexpr std::uint8_t kCompType0 = 0; // codec slot 0..3
        constexpr std::uint8_t kCompNone = 4;
        constexpr std::uint8_t kCompSelf = 5;
        constexpr std::uint8_t kCompParent = 6;
        constexpr std::uint8_t kCompRleSmall = 7;
        constexpr std::uint8_t kCompRleLarge = 8;
        constexpr std::uint8_t kCompSelf0 = 9;
        constexpr std::uint8_t kCompSelf1 = 10;
        constexpr std::uint8_t kCompParentSelf = 11;
        constexpr std::uint8_t kCompParent0 = 12;
        constexpr std::uint8_t kCompParent1 = 13;

        // ---- big-endian fetch helpers ----
        std::uint16_t be16(const std::uint8_t* p) {
            return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
        }
        std::uint32_t be24(const std::uint8_t* p) {
            return (static_cast<std::uint32_t>(p[0]) << 16) |
                   (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
        }
        std::uint32_t be32(const std::uint8_t* p) {
            return (static_cast<std::uint32_t>(p[0]) << 24) |
                   (static_cast<std::uint32_t>(p[1]) << 16) |
                   (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
        }
        std::uint64_t be48(const std::uint8_t* p) {
            std::uint64_t v = 0;
            for (int i = 0; i < 6; ++i) {
                v = (v << 8) | p[i];
            }
            return v;
        }
        std::uint64_t be64(const std::uint8_t* p) {
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i) {
                v = (v << 8) | p[i];
            }
            return v;
        }
        void put24(std::uint8_t* p, std::uint32_t v) {
            p[0] = static_cast<std::uint8_t>(v >> 16);
            p[1] = static_cast<std::uint8_t>(v >> 8);
            p[2] = static_cast<std::uint8_t>(v);
        }
        void put48(std::uint8_t* p, std::uint64_t v) {
            for (int i = 5; i >= 0; --i) {
                p[i] = static_cast<std::uint8_t>(v);
                v >>= 8;
            }
        }
        void put16(std::uint8_t* p, std::uint16_t v) {
            p[0] = static_cast<std::uint8_t>(v >> 8);
            p[1] = static_cast<std::uint8_t>(v);
        }

        // ---- MSB-first bit reader over the compressed map stream ----
        struct bit_reader final {
            std::span<const std::uint8_t> src;
            std::size_t doffset{};
            std::uint32_t buffer{};
            int bits{};
            bool overflowed() const {
                return (doffset - static_cast<std::size_t>(bits) / 8U) > src.size();
            }
            std::uint32_t peek(int numbits) {
                if (numbits == 0) {
                    return 0;
                }
                if (numbits > bits) {
                    while (bits <= 24) {
                        if (doffset < src.size()) {
                            buffer |= static_cast<std::uint32_t>(src[doffset]) << (24 - bits);
                        }
                        ++doffset;
                        bits += 8;
                    }
                }
                return buffer >> (32 - numbits);
            }
            void remove(int numbits) {
                buffer <<= numbits;
                bits -= numbits;
            }
            std::uint32_t read(int numbits) {
                const std::uint32_t v = peek(numbits);
                remove(numbits);
                return v;
            }
        };

        // ---- canonical Huffman decoder (codes assigned descending by length,
        // per the CHD map convention) backed by a flat maxbits lookup table ----
        struct huffman_decoder final {
            std::uint32_t numcodes{};
            int maxbits{};
            std::vector<std::uint8_t> numbits; // code length per symbol
            std::vector<std::uint32_t> code;   // canonical code per symbol
            std::vector<std::uint32_t> lookup; // (symbol << 5) | length

            void init(std::uint32_t codes, int maxb) {
                numcodes = codes;
                maxbits = maxb;
                numbits.assign(codes, 0);
                code.assign(codes, 0);
                lookup.assign(static_cast<std::size_t>(1) << maxb, 0);
            }

            // Assign canonical codes from the per-symbol code lengths. Returns
            // false if the lengths do not form a valid (full) prefix code.
            bool assign_canonical_codes() {
                std::array<std::uint32_t, 33> bithisto{};
                for (std::uint32_t i = 0; i < numcodes; ++i) {
                    if (numbits[i] > maxbits) {
                        return false;
                    }
                    if (numbits[i] <= 32) {
                        ++bithisto[numbits[i]];
                    }
                }
                std::uint32_t curstart = 0;
                for (int codelen = 32; codelen > 0; --codelen) {
                    const std::uint32_t nextstart = (curstart + bithisto[codelen]) >> 1;
                    if (codelen != 1 && nextstart * 2 != (curstart + bithisto[codelen])) {
                        return false;
                    }
                    bithisto[codelen] = curstart;
                    curstart = nextstart;
                }
                for (std::uint32_t i = 0; i < numcodes; ++i) {
                    if (numbits[i] > 0) {
                        code[i] = bithisto[numbits[i]]++;
                    }
                }
                return true;
            }

            bool build_lookup_table() {
                const std::size_t end = lookup.size();
                for (std::uint32_t c = 0; c < numcodes; ++c) {
                    if (numbits[c] == 0) {
                        continue;
                    }
                    const int shift = maxbits - numbits[c];
                    const std::size_t first = static_cast<std::size_t>(code[c]) << shift;
                    const std::size_t last = (static_cast<std::size_t>(code[c] + 1) << shift) - 1;
                    if (first >= end || last >= end || last < first) {
                        return false;
                    }
                    const std::uint32_t value = (c << 5) | (numbits[c] & 0x1F);
                    for (std::size_t d = first; d <= last; ++d) {
                        lookup[d] = value;
                    }
                }
                return true;
            }

            std::uint32_t decode_one(bit_reader& br) const {
                const std::uint32_t bitsval = br.peek(maxbits);
                const std::uint32_t entry = lookup[bitsval];
                br.remove(static_cast<int>(entry & 0x1F));
                return entry >> 5;
            }
        };

        // Import the RLE-encoded tree that the v5 map uses for the compression-
        // type alphabet's lengths (numbits = 5 for maxbits >= 16, etc.).
        bool import_tree_rle(huffman_decoder& dec, bit_reader& br) {
            int numbits = (dec.maxbits >= 16) ? 5 : (dec.maxbits >= 8) ? 4 : 3;
            std::uint32_t curnode = 0;
            while (curnode < dec.numcodes) {
                const int nodebits = static_cast<int>(br.read(numbits));
                if (nodebits != 1) {
                    dec.numbits[curnode++] = static_cast<std::uint8_t>(nodebits);
                } else {
                    const int next = static_cast<int>(br.read(numbits));
                    if (next == 1) {
                        dec.numbits[curnode++] = 1;
                    } else {
                        int repcount = static_cast<int>(br.read(numbits)) + 3;
                        if (static_cast<std::uint32_t>(repcount) + curnode > dec.numcodes) {
                            return false;
                        }
                        while (repcount-- > 0) {
                            dec.numbits[curnode++] = static_cast<std::uint8_t>(next);
                        }
                    }
                }
            }
            if (curnode != dec.numcodes) {
                return false;
            }
            return dec.assign_canonical_codes() && dec.build_lookup_table() && !br.overflowed();
        }

        bool import_tree_huffman(huffman_decoder& dec, bit_reader& br) {
            huffman_decoder small;
            small.init(24, 6);
            small.numbits[0] = static_cast<std::uint8_t>(br.read(3));
            const int start = static_cast<int>(br.read(3)) + 1;
            int count = 0;
            for (int index = 1; index < 24; ++index) {
                if (index < start || count == 7) {
                    small.numbits[static_cast<std::size_t>(index)] = 0;
                } else {
                    count = static_cast<int>(br.read(3));
                    small.numbits[static_cast<std::size_t>(index)] =
                        static_cast<std::uint8_t>(count == 7 ? 0 : count);
                }
            }
            if (!small.assign_canonical_codes() || !small.build_lookup_table()) {
                return false;
            }

            std::uint32_t temp = dec.numcodes - 9U;
            std::uint8_t rlefullbits = 0;
            while (temp != 0U) {
                temp >>= 1U;
                ++rlefullbits;
            }

            int last = 0;
            std::uint32_t curcode = 0;
            while (curcode < dec.numcodes) {
                const int value = static_cast<int>(small.decode_one(br));
                if (value != 0) {
                    dec.numbits[curcode++] = static_cast<std::uint8_t>(value - 1);
                    last = value - 1;
                } else {
                    int repeat = static_cast<int>(br.read(3)) + 2;
                    if (repeat == 9) {
                        repeat += static_cast<int>(br.read(rlefullbits));
                    }
                    while (repeat-- > 0 && curcode < dec.numcodes) {
                        dec.numbits[curcode++] = static_cast<std::uint8_t>(last);
                    }
                }
            }

            return curcode == dec.numcodes && dec.assign_canonical_codes() &&
                   dec.build_lookup_table() && !br.overflowed();
        }

        // ---- codec scratch sizes ----
        std::uint32_t frames_per_hunk(std::uint32_t hunk_bytes) { return hunk_bytes / kFrameSize; }

        std::uint32_t adler32(std::span<const std::uint8_t> data) noexcept {
            constexpr std::uint32_t mod = 65521U;
            std::uint32_t a = 1U;
            std::uint32_t b = 0U;
            for (std::uint8_t byte : data) {
                a += byte;
                if (a >= mod) {
                    a -= mod;
                }
                b += a;
                b %= mod;
            }
            return (b << 16U) | a;
        }

        bool decode_zlib_hunk(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) {
            if (in.size() >= 6U) {
                const std::uint8_t cmf = in[0];
                const std::uint8_t flg = in[1];
                const bool valid_header =
                    (cmf & 0x0FU) == 8U && ((cmf >> 4U) <= 7U) &&
                    (((static_cast<std::uint32_t>(cmf) << 8U) | flg) % 31U) == 0U &&
                    (flg & 0x20U) == 0U;
                if (valid_header) {
                    std::size_t consumed = 0;
                    const auto written =
                        compression::inflate_raw(in.subspan(2), out, consumed);
                    const std::size_t checksum_offset = 2U + consumed;
                    if (written && *written == out.size() && checksum_offset + 4U <= in.size()) {
                        const std::uint32_t stored = be32(in.data() + checksum_offset);
                        if (stored == adler32(out)) {
                            return true;
                        }
                    }
                }
            }

            const auto written = compression::inflate_raw(in, out);
            return written && *written == out.size();
        }

        bool decode_flac_block_hunk(std::span<const std::uint8_t> in,
                                    std::span<std::uint8_t> out) {
            if ((out.size() % 4U) != 0U) {
                return false;
            }
            std::span<const std::uint8_t> flac = in;
            if (in.size() >= 3U && in[1] == 0xFFU && (in[2] & 0xFEU) == 0xF8U) {
                // CHD block-FLAC stores a byte-order marker before the bare FLAC frame.
                flac = in.subspan(1);
            }
            const std::uint32_t sample_pairs = static_cast<std::uint32_t>(out.size() / 4U);
            std::vector<std::int16_t> samples(static_cast<std::size_t>(sample_pairs) * 2U);
            const auto consumed = flac_decode_interleaved(flac, sample_pairs, samples);
            if (!consumed) {
                return false;
            }
            for (std::uint32_t i = 0; i < sample_pairs; ++i) {
                const std::uint16_t l = static_cast<std::uint16_t>(samples[i * 2U]);
                const std::uint16_t r = static_cast<std::uint16_t>(samples[i * 2U + 1U]);
                std::uint8_t* p = out.data() + static_cast<std::size_t>(i) * 4U;
                p[0] = static_cast<std::uint8_t>(l & 0xFFU);
                p[1] = static_cast<std::uint8_t>(l >> 8U);
                p[2] = static_cast<std::uint8_t>(r & 0xFFU);
                p[3] = static_cast<std::uint8_t>(r >> 8U);
            }
            return true;
        }

        bool decode_huff8_hunk(std::span<const std::uint8_t> in,
                               std::span<std::uint8_t> out) {
            bit_reader br{.src = in};
            huffman_decoder dec;
            dec.init(256, 16);
            if (!import_tree_huffman(dec, br)) {
                return false;
            }
            for (std::uint8_t& byte : out) {
                const std::uint32_t symbol = dec.decode_one(br);
                if (symbol > 0xFFU) {
                    return false;
                }
                byte = static_cast<std::uint8_t>(symbol);
            }
            return !br.overflowed();
        }

        // Decompress a single cdzl/cdlz CD hunk into a flat frames*2352 sector
        // buffer (subcode dropped). `is_lzma` selects the base-lane codec.
        bool decode_cd_hunk(std::span<const std::uint8_t> in, std::uint32_t hunk_bytes,
                            bool is_lzma, std::span<std::uint8_t> out) {
            const std::uint32_t frames = frames_per_hunk(hunk_bytes);
            const std::uint32_t complen_bytes = (hunk_bytes < 65536) ? 2U : 3U;
            const std::uint32_t ecc_bytes = (frames + 7U) / 8U;
            const std::uint32_t header_bytes = ecc_bytes + complen_bytes;
            if (in.size() < static_cast<std::size_t>(ecc_bytes) + 2U) {
                return false;
            }
            std::uint32_t complen_base =
                (static_cast<std::uint32_t>(in[ecc_bytes]) << 8) | in[ecc_bytes + 1U];
            if (complen_bytes > 2U) {
                if (in.size() < static_cast<std::size_t>(ecc_bytes) + 3U) {
                    return false;
                }
                complen_base = (complen_base << 8) | in[ecc_bytes + 2U];
            }
            if (in.size() < static_cast<std::size_t>(header_bytes) + complen_base) {
                return false;
            }

            const std::size_t base_size = static_cast<std::size_t>(frames) * kSectorData;
            std::vector<std::uint8_t> base(base_size);
            const std::span<const std::uint8_t> base_in = in.subspan(header_bytes, complen_base);
            if (is_lzma) {
                const auto consumed = compression::lzma1_decode(3, 0, 2, base_in, base);
                if (!consumed) {
                    return false;
                }
            } else {
                const auto written = compression::inflate_raw(base_in, base);
                if (!written || *written != base_size) {
                    return false;
                }
            }

            // Reassemble: copy each frame's 2352 sector bytes; subcode dropped.
            for (std::uint32_t f = 0; f < frames; ++f) {
                std::uint8_t* sector = out.data() + static_cast<std::size_t>(f) * kSectorData;
                std::memcpy(sector, base.data() + static_cast<std::size_t>(f) * kSectorData,
                            kSectorData);
                // ECC-regen bitmap is LSB-first within each byte (bit f%8 of
                // byte f/8). When set, the encoder stripped sync + EDC/ECC.
                if ((in[f / 8U] & (1U << (f % 8U))) != 0) {
                    std::memcpy(sector, kSyncHeader.data(), kSyncHeader.size());
                    circ_ecc_regen_sector(
                        std::span<std::uint8_t, kSectorData>{sector, kSectorData});
                }
            }
            return true;
        }

        // Decode a single cdfl (CD-FLAC) hunk into the flat frames*2352 sector
        // buffer. Unlike cdzl/cdlz this codec has no ECC bitmap / complen header:
        // the hunk is a bare FLAC frame stream (audio) followed by the DEFLATE
        // subcode, which we drop. FLAC yields logical sample values; we serialise
        // each as little-endian (low byte first) below -- the same .bin order the
        // cdzl/cdlz path already holds in its inflated/LZMA'd sector bytes and the
        // order the CD-DA consumer reads, so no byte-swap is applied on any codec.
        bool decode_cdfl_hunk(std::span<const std::uint8_t> in, std::uint32_t hunk_bytes,
                              std::span<std::uint8_t> out) {
            const std::uint32_t frames = frames_per_hunk(hunk_bytes);
            const std::uint32_t sample_pairs = frames * (kSectorData / 4U); // 588 per sector
            std::vector<std::int16_t> samples(static_cast<std::size_t>(sample_pairs) * 2U);
            const auto consumed = flac_decode_interleaved(in, sample_pairs, samples);
            if (!consumed) {
                return false; // malformed stream or a frame CRC mismatch
            }
            for (std::uint32_t i = 0; i < sample_pairs; ++i) {
                const std::uint16_t l = static_cast<std::uint16_t>(samples[i * 2U]);
                const std::uint16_t r = static_cast<std::uint16_t>(samples[i * 2U + 1U]);
                std::uint8_t* p = out.data() + static_cast<std::size_t>(i) * 4U;
                p[0] = static_cast<std::uint8_t>(l & 0xFFU);
                p[1] = static_cast<std::uint8_t>(l >> 8U);
                p[2] = static_cast<std::uint8_t>(r & 0xFFU);
                p[3] = static_cast<std::uint8_t>(r >> 8U);
            }
            return true;
        }

        // ---- CD-track metadata (CHTR / CHT2 ASCII payloads) ----
        struct meta_track {
            int number{};
            int frames{};
            int pregap{};
            int postgap{};
            bool pregap_in_file{}; // pgtype 'V' => pause bytes are stored
            std::uint32_t sector_size{};
            bool is_audio{};
        };

        // Minimal scanf-style parse of "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d
        // [PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d]". Returns false if the core
        // four fields are missing.
        bool parse_track_meta(std::string_view s, meta_track& t) {
            auto field_after = [&](std::string_view key, std::size_t& out_end) -> std::string_view {
                const std::size_t k = s.find(key);
                if (k == std::string_view::npos) {
                    return {};
                }
                std::size_t b = k + key.size();
                std::size_t e = b;
                while (e < s.size() && s[e] != ' ' && s[e] != '\0' && s[e] != '\n' &&
                       s[e] != '\r') {
                    ++e;
                }
                out_end = e;
                return s.substr(b, e - b);
            };
            auto to_int = [](std::string_view v, int& out) {
                if (v.empty()) {
                    return false;
                }
                int val = 0;
                for (char c : v) {
                    if (c < '0' || c > '9') {
                        return false;
                    }
                    val = val * 10 + (c - '0');
                }
                out = val;
                return true;
            };
            std::size_t e = 0;
            const std::string_view num = field_after("TRACK:", e);
            const std::string_view type = field_after("TYPE:", e);
            const std::string_view frames = field_after("FRAMES:", e);
            if (!to_int(num, t.number) || type.empty() || !to_int(frames, t.frames)) {
                return false;
            }
            if (type == "MODE1") {
                t.sector_size = 2048;
                t.is_audio = false;
            } else if (type == "MODE1_RAW" || type == "MODE2_RAW") {
                t.sector_size = 2352;
                t.is_audio = false;
            } else if (type == "MODE2") {
                t.sector_size = 2336;
                t.is_audio = false;
            } else if (type == "AUDIO") {
                t.sector_size = 2352;
                t.is_audio = true;
            } else {
                return false; // unsupported track form
            }
            int v = 0;
            if (to_int(field_after("PREGAP:", e), v)) {
                t.pregap = v;
            }
            if (to_int(field_after("POSTGAP:", e), v)) {
                t.postgap = v;
            }
            const std::string_view pgtype = field_after("PGTYPE:", e);
            t.pregap_in_file = !pgtype.empty() && pgtype.front() == 'V';
            return true;
        }

    } // namespace

    std::uint16_t crc16_ccitt(std::span<const std::uint8_t> data) noexcept {
        // CRC-16/CCITT-FALSE, table generated from poly 0x1021 (no reflection).
        static const std::array<std::uint16_t, 256> table = [] {
            std::array<std::uint16_t, 256> t{};
            for (std::uint32_t i = 0; i < 256; ++i) {
                std::uint16_t crc = static_cast<std::uint16_t>(i << 8);
                for (int b = 0; b < 8; ++b) {
                    crc = (crc & 0x8000U) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                                          : static_cast<std::uint16_t>(crc << 1);
                }
                t[i] = crc;
            }
            return t;
        }();
        std::uint16_t crc = 0xFFFF;
        for (std::uint8_t byte : data) {
            crc = static_cast<std::uint16_t>((crc << 8) ^ table[(crc >> 8) ^ byte]);
        }
        return crc;
    }

    std::optional<std::vector<std::uint8_t>>
    decode_compressed_map(std::span<const std::uint8_t> map_section, std::uint32_t hunk_count,
                          std::uint32_t hunk_bytes, std::uint32_t unit_bytes) {
        if (map_section.size() < 16) {
            return std::nullopt;
        }
        const std::uint32_t mapbytes = be32(&map_section[0]);
        const std::uint64_t firstoffs = be48(&map_section[4]);
        const std::uint16_t mapcrc = be16(&map_section[10]);
        const std::uint8_t lengthbits = map_section[12];
        const std::uint8_t selfbits = map_section[13];
        const std::uint8_t parentbits = map_section[14];
        if (lengthbits > 32 || selfbits > 32 || parentbits > 32) {
            return std::nullopt;
        }
        if (static_cast<std::uint64_t>(16) + mapbytes > map_section.size()) {
            return std::nullopt;
        }

        bit_reader br{.src = map_section.subspan(16, mapbytes)};

        huffman_decoder dec;
        dec.init(16, 8);
        if (!import_tree_rle(dec, br)) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> rawmap(static_cast<std::size_t>(hunk_count) * 12U, 0);

        // Pass 1: per-hunk compression type (RLE over the type alphabet).
        std::uint8_t lastcomp = 0;
        int repcount = 0;
        for (std::uint32_t h = 0; h < hunk_count; ++h) {
            std::uint8_t* entry = rawmap.data() + static_cast<std::size_t>(h) * 12U;
            if (repcount > 0) {
                entry[0] = lastcomp;
                --repcount;
            } else {
                if (br.overflowed()) {
                    return std::nullopt;
                }
                const std::uint8_t val = static_cast<std::uint8_t>(dec.decode_one(br));
                if (val == kCompRleSmall) {
                    entry[0] = lastcomp;
                    repcount = 2 + static_cast<int>(dec.decode_one(br));
                } else if (val == kCompRleLarge) {
                    entry[0] = lastcomp;
                    repcount = 2 + 16 + (static_cast<int>(dec.decode_one(br)) << 4);
                    repcount += static_cast<int>(dec.decode_one(br));
                } else {
                    entry[0] = lastcomp = val;
                }
            }
        }

        // Pass 2: per-hunk length / offset / crc.
        std::uint64_t curoffset = firstoffs;
        std::uint32_t last_self = 0;
        std::uint64_t last_parent = 0;
        for (std::uint32_t h = 0; h < hunk_count; ++h) {
            std::uint8_t* entry = rawmap.data() + static_cast<std::size_t>(h) * 12U;
            std::uint64_t offset = curoffset;
            std::uint32_t length = 0;
            std::uint16_t crc = 0;
            switch (entry[0]) {
            case kCompType0:
            case kCompType0 + 1:
            case kCompType0 + 2:
            case kCompType0 + 3:
                length = br.read(lengthbits);
                curoffset += length;
                crc = static_cast<std::uint16_t>(br.read(16));
                break;
            case kCompNone:
                length = hunk_bytes;
                curoffset += length;
                crc = static_cast<std::uint16_t>(br.read(16));
                break;
            case kCompSelf:
                offset = br.read(selfbits);
                last_self = static_cast<std::uint32_t>(offset);
                break;
            case kCompParent:
                offset = br.read(parentbits);
                last_parent = offset;
                break;
            case kCompSelf1:
                ++last_self;
                [[fallthrough]];
            case kCompSelf0:
                entry[0] = kCompSelf;
                offset = last_self;
                break;
            case kCompParentSelf:
                entry[0] = kCompParent;
                last_parent = offset =
                    (static_cast<std::uint64_t>(h) * hunk_bytes) / (unit_bytes ? unit_bytes : 1);
                break;
            case kCompParent1:
                last_parent += hunk_bytes / (unit_bytes ? unit_bytes : 1);
                [[fallthrough]];
            case kCompParent0:
                entry[0] = kCompParent;
                offset = last_parent;
                break;
            default:
                return std::nullopt;
            }
            put24(entry + 1, length);
            put48(entry + 4, offset);
            put16(entry + 10, crc);
        }

        if (crc16_ccitt(rawmap) != mapcrc) {
            return std::nullopt;
        }
        return rawmap;
    }

    namespace {

        std::optional<std::vector<std::uint8_t>>
        decode_hunk_map(std::span<const std::uint8_t> file, const chd_file_info& info,
                        bool compressed) {
            const std::uint32_t mapentrybytes = compressed ? 12U : 4U;
            if (compressed) {
                if (info.map_offset >= file.size()) {
                    return std::nullopt;
                }
                return decode_compressed_map(
                    file.subspan(info.map_offset), static_cast<std::uint32_t>(info.hunk_count),
                    info.hunk_bytes, info.unit_bytes);
            }

            if (info.hunk_count >
                (std::numeric_limits<std::uint64_t>::max() / mapentrybytes)) {
                return std::nullopt;
            }
            const std::uint64_t rawsize = info.hunk_count * mapentrybytes;
            if (info.map_offset + rawsize > file.size() || info.map_offset + rawsize < info.map_offset) {
                return std::nullopt;
            }
            return std::vector<std::uint8_t>{
                file.begin() + static_cast<std::ptrdiff_t>(info.map_offset),
                file.begin() + static_cast<std::ptrdiff_t>(info.map_offset + rawsize)};
        }

    } // namespace

    std::optional<chd_file_info> probe(std::span<const std::uint8_t> file) {
        if (file.size() < kHeaderSize) {
            return std::nullopt;
        }
        if (std::memcmp(file.data(), "MComprHD", 8) != 0) {
            return std::nullopt;
        }
        const std::uint32_t length = be32(&file[8]);
        const std::uint32_t version = be32(&file[12]);
        if (version != 5 || length != kHeaderSize) {
            return std::nullopt;
        }

        chd_file_info info{};
        info.version = version;
        info.header_bytes = length;
        for (int i = 0; i < 4; ++i) {
            info.codecs[static_cast<std::size_t>(i)] = be32(&file[16 + 4 * i]);
        }
        info.logical_bytes = be64(&file[32]);
        info.map_offset = be64(&file[40]);
        info.meta_offset = be64(&file[48]);
        info.hunk_bytes = be32(&file[56]);
        info.unit_bytes = be32(&file[60]);
        if (info.hunk_bytes == 0 || info.unit_bytes == 0) {
            return std::nullopt;
        }
        info.hunk_count = (info.logical_bytes + info.hunk_bytes - 1) / info.hunk_bytes;
        if (info.hunk_count == 0 || info.hunk_count > 0x00FFFFFFULL) {
            return std::nullopt; // bound the map size
        }
        info.has_cd_unit_layout =
            info.unit_bytes == kFrameSize && (info.hunk_bytes % kFrameSize) == 0;
        return info;
    }

    std::optional<chd_image_data> decode(std::span<const std::uint8_t> file) {
        const auto info = probe(file);
        if (!info || !info->has_cd_unit_layout) {
            return std::nullopt;
        }

        const std::array<std::uint32_t, 4>& codec = info->codecs;
        const std::uint64_t metaoffset = info->meta_offset;
        const std::uint32_t hunkbytes = info->hunk_bytes;
        const std::uint64_t hunkcount = info->hunk_count;

        const bool compressed = codec[0] != kCodecNone;

        // The primary codec (slot 0) must be one we can decode. cdzl/cdlz carry
        // data tracks; cdfl carries CD-DA audio (a FLAC-primary, all-audio disc is
        // valid). Any hunk on a still-unsupported slot is zero-filled rather than
        // failing the whole image.
        if (codec[0] != kCodecNone && codec[0] != kCodecCdzl && codec[0] != kCodecCdlz &&
            codec[0] != kCodecCdfl) {
            return std::nullopt;
        }

        // Decode the map: compressed (12-byte entries) or raw (4-byte indices).
        auto decoded_map = decode_hunk_map(file, *info, compressed);
        if (!decoded_map) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> rawmap = std::move(*decoded_map);
        const std::uint32_t mapentrybytes = compressed ? 12U : 4U;

        // Decompress every hunk into one flat frame-ordered buffer of 2352-byte
        // sectors (subcode dropped). hunk i lives at i * sectors_per_hunk.
        const std::uint32_t frames = frames_per_hunk(hunkbytes);
        const std::size_t flat_size = static_cast<std::size_t>(hunkcount) * frames * kSectorData;
        std::vector<std::uint8_t> flat(flat_size, 0);

        for (std::uint64_t h = 0; h < hunkcount; ++h) {
            std::uint8_t* dst = flat.data() + static_cast<std::size_t>(h) * frames * kSectorData;
            const std::span<std::uint8_t> dst_span{dst,
                                                   static_cast<std::size_t>(frames) * kSectorData};
            const std::uint8_t* entry = rawmap.data() + static_cast<std::size_t>(h) * mapentrybytes;

            if (!compressed) {
                const std::uint64_t blockoffs = static_cast<std::uint64_t>(be32(entry)) * hunkbytes;
                if (blockoffs == 0) {
                    continue; // zero-fill (no parent support)
                }
                if (blockoffs + hunkbytes > file.size()) {
                    return std::nullopt;
                }
                // Raw hunk holds interleaved 2448-byte units; extract sectors.
                for (std::uint32_t f = 0; f < frames; ++f) {
                    std::memcpy(dst + static_cast<std::size_t>(f) * kSectorData,
                                file.data() + blockoffs + static_cast<std::size_t>(f) * kFrameSize,
                                kSectorData);
                }
                continue;
            }

            const std::uint8_t type = entry[0];
            const std::uint32_t blocklen = be24(entry + 1);
            const std::uint64_t blockoffs = be48(entry + 4);
            switch (type) {
            case kCompType0:
            case kCompType0 + 1:
            case kCompType0 + 2:
            case kCompType0 + 3: {
                const std::uint32_t this_codec = codec[type];
                if (this_codec != kCodecCdzl && this_codec != kCodecCdlz &&
                    this_codec != kCodecCdfl) {
                    // Still-unsupported codec for this slot: leave the hunk
                    // zero-filled rather than failing the whole image.
                    break;
                }
                if (blockoffs + blocklen > file.size()) {
                    return std::nullopt;
                }
                const std::span<const std::uint8_t> comp = file.subspan(blockoffs, blocklen);
                const bool ok =
                    this_codec == kCodecCdfl
                        ? decode_cdfl_hunk(comp, hunkbytes, dst_span)
                        : decode_cd_hunk(comp, hunkbytes, this_codec == kCodecCdlz, dst_span);
                if (!ok) {
                    return std::nullopt;
                }
                break;
            }
            case kCompNone: {
                if (blockoffs + hunkbytes > file.size()) {
                    return std::nullopt;
                }
                for (std::uint32_t f = 0; f < frames; ++f) {
                    std::memcpy(dst + static_cast<std::size_t>(f) * kSectorData,
                                file.data() + blockoffs + static_cast<std::size_t>(f) * kFrameSize,
                                kSectorData);
                }
                break;
            }
            case kCompSelf: {
                if (blockoffs >= hunkcount) {
                    return std::nullopt;
                }
                std::memcpy(
                    dst, flat.data() + static_cast<std::size_t>(blockoffs) * frames * kSectorData,
                    static_cast<std::size_t>(frames) * kSectorData);
                break;
            }
            default:
                return std::nullopt; // PARENT unsupported
            }
        }

        // ---- track table from CD-track metadata (CHTR / CHT2) ----
        chd_image_data img;
        std::uint32_t toc_end = 0;      // running absolute LBA
        std::uint32_t file_sectors = 0; // running padded frame cursor in the flat image

        std::uint64_t meta = metaoffset;
        // Enumerate the CD-track metadata in file order. CHT2/CHTR records for
        // track N appear in order; both are accepted (CHT2 carries pregap info).
        int parsed = 0;
        while (meta != 0 && meta + 16 <= file.size()) {
            const std::uint8_t* mh = file.data() + meta;
            const std::uint32_t tag = be32(mh);
            const std::uint32_t lenflags = be32(mh + 4);
            const std::uint32_t mlen = lenflags & 0x00FFFFFFU;
            const std::uint64_t next = be64(mh + 8);
            constexpr std::uint32_t kChtr = fourcc('C', 'H', 'T', 'R');
            constexpr std::uint32_t kCht2 = fourcc('C', 'H', 'T', '2');
            if ((tag == kChtr || tag == kCht2) && meta + 16 + mlen <= file.size()) {
                const char* payload = reinterpret_cast<const char*>(mh + 16);
                std::string_view sv(payload, mlen);
                meta_track mt;
                if (parse_track_meta(sv, mt) && mt.number == parsed + 1) {
                    // Only the first track may be a data track; later tracks
                    // must be audio (matches the CD-track convention used here).
                    chd_track t;
                    t.number = static_cast<std::uint32_t>(mt.number);
                    t.is_audio = mt.is_audio;
                    t.sector_size = kSectorData; // flat image always raw 2352
                    int pregap = mt.pregap_in_file ? mt.pregap : 0;
                    if (parsed == 0) {
                        t.start_lba = 0;
                    } else {
                        t.start_lba = toc_end + static_cast<std::uint32_t>(pregap);
                    }
                    const std::uint32_t end_lba = t.start_lba +
                                                  static_cast<std::uint32_t>(mt.frames) -
                                                  static_cast<std::uint32_t>(pregap);
                    t.sector_count = end_lba - t.start_lba;
                    // Flat-image byte offset of INDEX 1: this track's block starts at
                    // the cumulative padded cursor `file_sectors`, and any in-file
                    // pregap pause sectors precede INDEX 1. read_raw_sector adds
                    // (lba - start_lba), so the offset must NOT subtract start_lba --
                    // doing so only cancelled for the start_lba==0 data track and put
                    // every audio track's window on top of the data track.
                    t.data_offset =
                        (static_cast<std::uint64_t>(file_sectors) + pregap) * kSectorData;
                    toc_end = end_lba + static_cast<std::uint32_t>(mt.postgap);
                    file_sectors += ((static_cast<std::uint32_t>(mt.frames) + kTrackPadding - 1) /
                                     kTrackPadding) *
                                    kTrackPadding;
                    img.tracks.push_back(t);
                    ++parsed;
                }
            }
            meta = next;
        }

        if (img.tracks.empty()) {
            return std::nullopt;
        }

        img.data = std::move(flat);
        img.total_sectors = toc_end;
        return img;
    }

    std::optional<chd_block_image_data>
    decode_block_device(std::span<const std::uint8_t> file, std::uint64_t max_logical_bytes) {
        const auto info = probe(file);
        if (!info || info->has_cd_unit_layout || info->logical_bytes > max_logical_bytes) {
            return std::nullopt;
        }
        if (info->hunk_bytes < info->unit_bytes || (info->hunk_bytes % info->unit_bytes) != 0U) {
            return std::nullopt;
        }
        if (info->hunk_count >
            (std::numeric_limits<std::uint64_t>::max() / info->hunk_bytes)) {
            return std::nullopt;
        }
        const std::uint64_t padded_size = info->hunk_count * info->hunk_bytes;
        if (padded_size > max_logical_bytes || padded_size > static_cast<std::uint64_t>(
                                                   std::numeric_limits<std::size_t>::max())) {
            return std::nullopt;
        }

        const bool compressed = info->codecs[0] != kCodecNone;
        auto decoded_map = decode_hunk_map(file, *info, compressed);
        if (!decoded_map) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t>& rawmap = *decoded_map;
        const std::uint32_t mapentrybytes = compressed ? 12U : 4U;

        std::vector<std::uint8_t> padded(static_cast<std::size_t>(padded_size), 0);
        enum class hunk_state : std::uint8_t { pending, decoding, done };
        std::vector<hunk_state> state(static_cast<std::size_t>(info->hunk_count),
                                      hunk_state::pending);

        auto decode_one = [&](auto&& self, std::uint64_t h) -> bool {
            if (h >= info->hunk_count) {
                return false;
            }
            hunk_state& s = state[static_cast<std::size_t>(h)];
            if (s == hunk_state::done) {
                return true;
            }
            if (s == hunk_state::decoding) {
                return false;
            }
            s = hunk_state::decoding;

            std::uint8_t* dst = padded.data() + static_cast<std::size_t>(h) * info->hunk_bytes;
            const std::span<std::uint8_t> dst_span{dst, info->hunk_bytes};
            const std::uint8_t* entry =
                rawmap.data() + static_cast<std::size_t>(h) * mapentrybytes;

            if (!compressed) {
                const std::uint64_t blockoffs =
                    static_cast<std::uint64_t>(be32(entry)) * info->hunk_bytes;
                if (blockoffs != 0U) {
                    if (blockoffs + info->hunk_bytes > file.size()) {
                        return false;
                    }
                    std::memcpy(dst, file.data() + blockoffs, info->hunk_bytes);
                }
                s = hunk_state::done;
                return true;
            }

            const std::uint8_t type = entry[0];
            const std::uint32_t blocklen = be24(entry + 1);
            const std::uint64_t blockoffs = be48(entry + 4);
            switch (type) {
            case kCompType0:
            case kCompType0 + 1:
            case kCompType0 + 2:
            case kCompType0 + 3: {
                const std::uint32_t this_codec = info->codecs[type];
                if (blockoffs + blocklen > file.size()) {
                    return false;
                }
                const std::span<const std::uint8_t> comp = file.subspan(blockoffs, blocklen);
                bool ok = false;
                if (this_codec == kCodecLzma) {
                    ok = compression::lzma1_decode(3, 0, 2, comp, dst_span).has_value();
                } else if (this_codec == kCodecZlib) {
                    ok = decode_zlib_hunk(comp, dst_span);
                } else if (this_codec == kCodecHuff) {
                    ok = decode_huff8_hunk(comp, dst_span);
                } else if (this_codec == kCodecFlac) {
                    ok = decode_flac_block_hunk(comp, dst_span);
                }
                if (!ok) {
                    return false;
                }
                break;
            }
            case kCompNone:
                if (blockoffs + info->hunk_bytes > file.size()) {
                    return false;
                }
                std::memcpy(dst, file.data() + blockoffs, info->hunk_bytes);
                break;
            case kCompSelf:
                if (!self(self, blockoffs)) {
                    return false;
                }
                std::memcpy(dst, padded.data() + static_cast<std::size_t>(blockoffs) *
                                             info->hunk_bytes,
                            info->hunk_bytes);
                break;
            default:
                return false; // parent CHDs and unsupported RLE forms are not mountable here.
            }

            s = hunk_state::done;
            return true;
        };

        for (std::uint64_t h = 0; h < info->hunk_count; ++h) {
            if (!decode_one(decode_one, h)) {
                return std::nullopt;
            }
        }

        padded.resize(static_cast<std::size_t>(info->logical_bytes));
        return chd_block_image_data{.info = *info, .data = std::move(padded)};
    }

} // namespace mnemos::disc::chd
