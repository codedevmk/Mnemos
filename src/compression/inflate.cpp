#include "inflate.hpp"

#include <array>
#include <cstring>

namespace mnemos::compression {

    namespace {

        constexpr unsigned kMaxBits = 15U; // longest Huffman code (RFC 1951 §3.2.7)

        // LSB-first bit reader over the compressed buffer (RFC 1951 §3.1.1).
        struct bit_reader final {
            std::span<const std::uint8_t> src;
            std::size_t pos{};
            std::uint32_t bits{};
            unsigned nbits{};
            bool err{};
        };

        // Ensure >= n bits are buffered (n <= 25 so 32 bits never overflow).
        void br_refill(bit_reader& br, unsigned n) noexcept {
            while (br.nbits < n) {
                if (br.pos >= br.src.size()) {
                    br.err = true;
                    return;
                }
                br.bits |= static_cast<std::uint32_t>(br.src[br.pos++]) << br.nbits;
                br.nbits += 8U;
            }
        }

        std::uint32_t br_read(bit_reader& br, unsigned n) noexcept {
            br_refill(br, n);
            const std::uint32_t v = br.bits & ((1U << n) - 1U);
            br.bits >>= n;
            br.nbits -= n;
            return v;
        }

        void br_align(bit_reader& br) noexcept {
            const unsigned drop = br.nbits & 7U;
            br.bits >>= drop;
            br.nbits -= drop;
        }

        // Canonical Huffman decode table: counts[L] = #codes of length L,
        // symbols[] ordered by (length, symbol). Alphabet caps at 288.
        struct huffman_tab final {
            std::array<std::uint16_t, kMaxBits + 1U> counts{};
            std::array<std::uint16_t, 288> symbols{};
        };

        // Build a table from per-symbol code lengths. false on malformed input.
        [[nodiscard]] bool huffman_build(huffman_tab& tab, const std::uint8_t* lengths,
                                         std::size_t n) noexcept {
            tab.counts.fill(0);
            for (std::size_t i = 0; i < n; ++i) {
                if (lengths[i] > kMaxBits) {
                    return false;
                }
                ++tab.counts[lengths[i]];
            }
            tab.counts[0] = 0; // length 0 = unused symbol

            // Kraft-McMillan: reject an over- or under-subscribed code set.
            unsigned left = 1;
            for (unsigned len = 1; len <= kMaxBits; ++len) {
                left <<= 1U;
                if (tab.counts[len] > left) {
                    return false;
                }
                left -= tab.counts[len];
            }

            std::array<std::uint16_t, kMaxBits + 2U> offsets{};
            for (unsigned len = 1; len < kMaxBits; ++len) {
                offsets[len + 1U] = static_cast<std::uint16_t>(offsets[len] + tab.counts[len]);
            }
            tab.symbols.fill(0);
            for (std::size_t i = 0; i < n; ++i) {
                if (lengths[i] != 0) {
                    tab.symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
                }
            }
            return true;
        }

        // Decode one symbol. -1 on error. Accumulates code bit-by-bit and
        // resolves it against the per-length count buckets.
        [[nodiscard]] int huffman_decode(bit_reader& br, const huffman_tab& tab) noexcept {
            unsigned code = 0;
            unsigned first = 0;
            unsigned index = 0;
            for (unsigned len = 1; len <= kMaxBits; ++len) {
                br_refill(br, 1);
                if (br.err) {
                    return -1;
                }
                code = (code << 1U) | (br.bits & 1U);
                br.bits >>= 1U;
                br.nbits -= 1U;
                const unsigned cnt = tab.counts[len];
                if (code - first < cnt) {
                    return tab.symbols[index + (code - first)];
                }
                index += cnt;
                first = (first + cnt) << 1U;
            }
            return -1;
        }

        // Fixed Huffman code lengths (RFC 1951 §3.2.6).
        constexpr std::array<std::uint8_t, 288> make_fixed_litlen() noexcept {
            std::array<std::uint8_t, 288> a{};
            for (int i = 0; i < 144; ++i) {
                a[static_cast<std::size_t>(i)] = 8;
            }
            for (int i = 144; i < 256; ++i) {
                a[static_cast<std::size_t>(i)] = 9;
            }
            for (int i = 256; i < 280; ++i) {
                a[static_cast<std::size_t>(i)] = 7;
            }
            for (int i = 280; i < 288; ++i) {
                a[static_cast<std::size_t>(i)] = 8;
            }
            return a;
        }
        constexpr std::array<std::uint8_t, 288> kFixedLitLen = make_fixed_litlen();
        constexpr std::array<std::uint8_t, 30> kFixedDist = [] {
            std::array<std::uint8_t, 30> a{};
            a.fill(5);
            return a;
        }();

        // Length / distance base + extra-bit tables (RFC 1951 §3.2.5).
        constexpr std::array<std::uint16_t, 29> kLengthBase = {
            3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        constexpr std::array<std::uint8_t, 29> kLengthExtra = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        constexpr std::array<std::uint16_t, 30> kDistanceBase = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        constexpr std::array<std::uint8_t, 30> kDistanceExtra = {
            0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        // Code-length alphabet permutation (RFC 1951 §3.2.7).
        constexpr std::array<std::uint8_t, 19> kCodeLengthOrder = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

        // Resolve a length/distance symbol and copy the back-reference. The
        // copy is byte-by-byte so length > distance (RLE-style overlap, e.g.
        // dist=1) expands correctly. false on error.
        [[nodiscard]] bool copy_backref(bit_reader& br, const huffman_tab& dist_tab,
                                        unsigned length_sym, std::span<std::uint8_t> dst,
                                        std::size_t& out_pos) noexcept {
            if (length_sym > 285) {
                return false;
            }
            const unsigned li = length_sym - 257U;
            if (li >= kLengthBase.size()) {
                return false;
            }
            std::uint32_t len = kLengthBase[li];
            if (kLengthExtra[li] != 0) {
                len += br_read(br, kLengthExtra[li]);
            }

            const int dist_sym = huffman_decode(br, dist_tab);
            if (dist_sym < 0 || dist_sym >= static_cast<int>(kDistanceBase.size())) {
                return false;
            }
            std::uint32_t dist = kDistanceBase[static_cast<std::size_t>(dist_sym)];
            if (kDistanceExtra[static_cast<std::size_t>(dist_sym)] != 0) {
                dist += br_read(br, kDistanceExtra[static_cast<std::size_t>(dist_sym)]);
            }
            if (br.err || dist == 0 || dist > out_pos || out_pos + len > dst.size()) {
                return false;
            }
            for (std::uint32_t i = 0; i < len; ++i) {
                dst[out_pos] = dst[out_pos - dist];
                ++out_pos;
            }
            return true;
        }

        // Decode the dynamic literal/length + distance tables (RFC 1951 §3.2.7).
        [[nodiscard]] bool decode_dynamic(bit_reader& br, huffman_tab& lt,
                                          huffman_tab& dt) noexcept {
            const unsigned hlit = br_read(br, 5) + 257U;
            const unsigned hdist = br_read(br, 5) + 1U;
            const unsigned hclen = br_read(br, 4) + 4U;
            if (br.err || hlit > 286 || hdist > 30) {
                return false;
            }

            std::array<std::uint8_t, 19> cl_lengths{};
            for (unsigned i = 0; i < hclen; ++i) {
                cl_lengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(br_read(br, 3));
            }
            if (br.err) {
                return false;
            }
            huffman_tab cl_tab;
            if (!huffman_build(cl_tab, cl_lengths.data(), cl_lengths.size())) {
                return false;
            }

            // Run-length-decode the hlit + hdist code lengths.
            std::array<std::uint8_t, 286 + 30> all_lengths{};
            const unsigned total = hlit + hdist;
            unsigned i = 0;
            while (i < total) {
                const int sym = huffman_decode(br, cl_tab);
                if (sym < 0) {
                    return false;
                }
                if (sym < 16) {
                    all_lengths[i++] = static_cast<std::uint8_t>(sym);
                } else if (sym == 16) {
                    if (i == 0) {
                        return false;
                    }
                    const std::uint8_t prev = all_lengths[i - 1U];
                    const unsigned rep = br_read(br, 2) + 3U;
                    if (br.err || i + rep > total) {
                        return false;
                    }
                    for (unsigned k = 0; k < rep; ++k) {
                        all_lengths[i++] = prev;
                    }
                } else if (sym == 17) {
                    const unsigned rep = br_read(br, 3) + 3U;
                    if (br.err || i + rep > total) {
                        return false;
                    }
                    i += rep;
                } else if (sym == 18) {
                    const unsigned rep = br_read(br, 7) + 11U;
                    if (br.err || i + rep > total) {
                        return false;
                    }
                    i += rep;
                } else {
                    return false;
                }
            }
            return huffman_build(lt, all_lengths.data(), hlit) &&
                   huffman_build(dt, all_lengths.data() + hlit, hdist);
        }

        // Decode one DEFLATE block. false on error; sets done on BFINAL.
        [[nodiscard]] bool inflate_block(bit_reader& br, std::span<std::uint8_t> dst,
                                         std::size_t& out_pos, bool& done) noexcept {
            const unsigned bfinal = br_read(br, 1);
            const unsigned btype = br_read(br, 2);
            if (br.err) {
                return false;
            }
            done = bfinal != 0;

            if (btype == 0) { // stored: byte-align then copy LEN raw bytes
                br_align(br);
                if (br.pos + 4U > br.src.size()) {
                    return false;
                }
                const unsigned len =
                    br.src[br.pos] | (static_cast<unsigned>(br.src[br.pos + 1]) << 8U);
                const unsigned nlen =
                    br.src[br.pos + 2] | (static_cast<unsigned>(br.src[br.pos + 3]) << 8U);
                br.pos += 4U;
                if ((len ^ 0xFFFFU) != nlen || br.pos + len > br.src.size() ||
                    out_pos + len > dst.size()) {
                    return false;
                }
                std::memcpy(dst.data() + out_pos, br.src.data() + br.pos, len);
                br.pos += len;
                out_pos += len;
                br.bits = 0;
                br.nbits = 0;
                return true;
            }

            huffman_tab lit_tab;
            huffman_tab dist_tab;
            if (btype == 1) {
                if (!huffman_build(lit_tab, kFixedLitLen.data(), kFixedLitLen.size()) ||
                    !huffman_build(dist_tab, kFixedDist.data(), kFixedDist.size())) {
                    return false;
                }
            } else if (btype == 2) {
                if (!decode_dynamic(br, lit_tab, dist_tab)) {
                    return false;
                }
            } else {
                return false; // reserved BTYPE 3
            }

            for (;;) {
                const int sym = huffman_decode(br, lit_tab);
                if (sym < 0) {
                    return false;
                }
                if (sym < 256) {
                    if (out_pos >= dst.size()) {
                        return false;
                    }
                    dst[out_pos++] = static_cast<std::uint8_t>(sym);
                } else if (sym == 256) {
                    return true; // end of block
                } else if (!copy_backref(br, dist_tab, static_cast<unsigned>(sym), dst, out_pos)) {
                    return false;
                }
            }
        }

    } // namespace

    std::optional<std::size_t> inflate_raw(std::span<const std::uint8_t> src,
                                           std::span<std::uint8_t> dst) noexcept {
        bit_reader br{.src = src};
        std::size_t out_pos = 0;
        bool done = false;
        while (!done) {
            if (!inflate_block(br, dst, out_pos, done)) {
                return std::nullopt;
            }
        }
        return out_pos;
    }

} // namespace mnemos::compression
