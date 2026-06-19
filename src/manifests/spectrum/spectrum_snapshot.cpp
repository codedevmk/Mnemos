#include "spectrum_snapshot.hpp"

#include <algorithm>
#include <cstring>

namespace mnemos::manifests::spectrum {

    namespace {
        // .z80 RLE: an ED ED pair escapes a (count, value) run; every other byte is
        // literal. Decodes into `out` until it is full or the input is exhausted.
        void rle_decompress(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) {
            std::size_t i = 0;
            std::size_t o = 0;
            while (i < in.size() && o < out.size()) {
                if (i + 1 < in.size() && in[i] == 0xEDU && in[i + 1] == 0xEDU) {
                    if (i + 3 >= in.size()) {
                        break;
                    }
                    const std::uint8_t count = in[i + 2];
                    const std::uint8_t value = in[i + 3];
                    i += 4;
                    for (std::uint8_t c = 0; c < count && o < out.size(); ++c) {
                        out[o++] = value;
                    }
                } else {
                    out[o++] = in[i++];
                }
            }
        }

        [[nodiscard]] std::uint16_t rd16(std::span<const std::uint8_t> d, std::size_t off) {
            return static_cast<std::uint16_t>(d[off] | (d[off + 1] << 8U));
        }

        // Split a 48 KiB linear image ($4000-$FFFF) into the three banks the 48K
        // machine maps: $4000 -> bank 5, $8000 -> bank 2, $C000 -> bank 0.
        void store_48k_linear(spectrum_snapshot& snap, std::span<const std::uint8_t> mem) {
            const std::array<int, 3> banks = {5, 2, 0};
            for (std::size_t i = 0; i < banks.size(); ++i) {
                const std::size_t off = i * 0x4000U;
                if (off >= mem.size()) {
                    break;
                }
                const std::size_t n = std::min<std::size_t>(0x4000U, mem.size() - off);
                std::memcpy(snap.bank[static_cast<std::size_t>(banks[i])].data(), mem.data() + off,
                            n);
            }
        }
    } // namespace

    std::optional<spectrum_snapshot> load_z80_snapshot(std::span<const std::uint8_t> d) {
        if (d.size() < 30) {
            return std::nullopt;
        }
        spectrum_snapshot snap;
        chips::cpu::z80::registers& r = snap.regs;

        r.af = static_cast<std::uint16_t>((d[0] << 8U) | d[1]); // A, F
        r.bc = rd16(d, 2);
        r.hl = rd16(d, 4);
        std::uint16_t pc = rd16(d, 6);
        r.sp = rd16(d, 8);
        r.i = d[10];
        std::uint8_t flags1 = d[12];
        if (flags1 == 0xFFU) {
            flags1 = 1U; // historical quirk: 255 means 1
        }
        r.r = static_cast<std::uint8_t>((d[11] & 0x7FU) | ((flags1 & 1U) << 7U));
        snap.border = static_cast<std::uint8_t>((flags1 >> 1U) & 0x07U);
        const bool compressed_v1 = (flags1 & 0x20U) != 0U;
        r.de = rd16(d, 13);
        r.bc2 = rd16(d, 15);
        r.de2 = rd16(d, 17);
        r.hl2 = rd16(d, 19);
        r.af2 = static_cast<std::uint16_t>((d[21] << 8U) | d[22]); // A', F'
        r.iy = rd16(d, 23);
        r.ix = rd16(d, 25);
        r.iff1 = d[27] != 0U;
        r.iff2 = d[28] != 0U;
        r.im = d[29] & 0x03U;

        if (pc != 0U) {
            // Version 1: a single 48 KiB block at offset 30 ($4000-$FFFF).
            r.pc = pc;
            const std::span<const std::uint8_t> mem = d.subspan(30);
            if (compressed_v1) {
                std::array<std::uint8_t, 0xC000> linear{};
                rle_decompress(mem, linear);
                store_48k_linear(snap, linear);
            } else {
                if (mem.size() < 0xC000U) {
                    return std::nullopt;
                }
                store_48k_linear(snap, mem.first(0xC000U));
            }
            return snap;
        }

        // Version 2/3: an extended header, then page-numbered memory blocks.
        if (d.size() < 35) {
            return std::nullopt;
        }
        const std::uint16_t ext_len = rd16(d, 30);
        r.pc = rd16(d, 32);
        const std::uint8_t hw_mode = d[34];
        const bool is_v2 = ext_len == 23U;
        // 48K: v2 hardware 0/1, v3 hardware 0. 128K: v2 hardware 3/4, v3 hardware
        // 4/5/6. Anything else (SamRam, +3, Pentagon...) is unsupported.
        const bool is_48k = is_v2 ? (hw_mode <= 1U) : (hw_mode == 0U);
        const bool is_128k = is_v2 ? (hw_mode == 3U || hw_mode == 4U)
                                   : (hw_mode == 4U || hw_mode == 5U || hw_mode == 6U);
        if (!is_48k && !is_128k) {
            return std::nullopt;
        }
        snap.is_128k = is_128k;
        if (is_128k) {
            snap.port_7ffd = d[35]; // last value written to $7FFD
        }

        std::size_t i = 32U + ext_len;
        while (i + 3U <= d.size()) {
            const std::uint16_t blen = rd16(d, i);
            const std::uint8_t page = d[i + 2];
            i += 3U;
            // Page -> RAM bank. 48K: 8->5 ($4000), 4->2 ($8000), 5->0 ($C000).
            // 128K: page n -> bank n-3 (pages 3..10 = banks 0..7). Other pages
            // (ROM images) are skipped.
            int target = -1;
            if (is_128k) {
                if (page >= 3U && page <= 10U) {
                    target = page - 3;
                }
            } else if (page == 8U) {
                target = 5;
            } else if (page == 4U) {
                target = 2;
            } else if (page == 5U) {
                target = 0;
            }

            const std::size_t consumed = (blen == 0xFFFFU) ? 0x4000U : blen;
            if (i + consumed > d.size()) {
                return std::nullopt;
            }
            if (target >= 0) {
                auto& dst = snap.bank[static_cast<std::size_t>(target)];
                if (blen == 0xFFFFU) {
                    std::memcpy(dst.data(), d.data() + i, 0x4000U);
                } else {
                    rle_decompress(d.subspan(i, blen), dst);
                }
            }
            i += consumed;
        }
        return snap;
    }

    std::optional<spectrum_snapshot> load_sna_snapshot(std::span<const std::uint8_t> d) {
        constexpr std::size_t k_sna_48k = 27U + 0xC000U; // 49179
        if (d.size() < k_sna_48k) {
            return std::nullopt;
        }
        spectrum_snapshot snap;
        chips::cpu::z80::registers& r = snap.regs;

        r.i = d[0];
        r.hl2 = rd16(d, 1);
        r.de2 = rd16(d, 3);
        r.bc2 = rd16(d, 5);
        r.af2 = rd16(d, 7);
        r.hl = rd16(d, 9);
        r.de = rd16(d, 11);
        r.bc = rd16(d, 13);
        r.iy = rd16(d, 15);
        r.ix = rd16(d, 17);
        r.iff2 = (d[19] & 0x04U) != 0U;
        r.iff1 = r.iff2;
        r.r = d[20];
        r.af = rd16(d, 21);
        std::uint16_t sp = rd16(d, 23);
        r.im = d[25] & 0x03U;
        snap.border = static_cast<std::uint8_t>(d[26] & 0x07U);
        store_48k_linear(snap, d.subspan(27, 0xC000U));

        // PC is on the stack; a RETN pops it (SP += 2). For 48K the stack is RAM,
        // so resolve it from the linear image we just stored (bank 0/2/5).
        if (sp >= 0x4000U && sp <= 0xFFFEU) {
            const std::uint8_t lo = d[27 + (sp - 0x4000U)];
            const std::uint8_t hi = d[27 + (sp - 0x4000U) + 1];
            r.pc = static_cast<std::uint16_t>(lo | (hi << 8U));
            sp = static_cast<std::uint16_t>(sp + 2U);
        }
        r.sp = sp;
        return snap;
    }

    std::optional<spectrum_snapshot> load_snapshot(std::span<const std::uint8_t> d) {
        if (d.size() == 27U + 0xC000U) {
            return load_sna_snapshot(d);
        }
        return load_z80_snapshot(d);
    }

} // namespace mnemos::manifests::spectrum
