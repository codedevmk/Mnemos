#include "prg_disk.hpp"

#include "d64_image.hpp"

#include <algorithm>
#include <cstddef>

namespace mnemos::chips::storage::c1541 {

    namespace {

        constexpr std::uint8_t pad = 0xA0U;
        constexpr std::uint8_t dir_track = d64_image::directory_track;    // 18
        constexpr std::uint16_t block_data = d64_image::sector_size - 2U; // 254 payload bytes

        // Byte offset of (track, sector) in a 35-track image, mirroring
        // d64_image::sector_offset (which needs a loaded image; this is the same
        // zoned arithmetic so we can author the buffer before it exists).
        [[nodiscard]] std::size_t offset_of(std::uint8_t track, std::uint8_t sector) noexcept {
            std::size_t blocks = 0;
            for (std::uint8_t t = 1U; t < track; ++t) {
                blocks += d64_image::sectors_per_track(t);
            }
            blocks += sector;
            return blocks * d64_image::sector_size;
        }

    } // namespace

    std::vector<std::uint8_t> make_prg_disk(std::span<const std::uint8_t> prg,
                                            std::span<const std::uint8_t> name) {
        if (prg.empty()) {
            return {};
        }

        // Lay the file out as a chain of data blocks across every sector except
        // track 18 (BAM + directory), filling track 1 outward. 35 tracks hold
        // 683 sectors; 664 are available once track 18's 19 are reserved.
        struct ts final {
            std::uint8_t track;
            std::uint8_t sector;
        };
        std::vector<ts> blocks;
        const std::size_t needed = (prg.size() + block_data - 1U) / block_data;
        for (std::uint8_t t = 1U; t <= 35U && blocks.size() < needed; ++t) {
            if (t == dir_track) {
                continue;
            }
            const std::uint8_t spt = d64_image::sectors_per_track(t);
            for (std::uint8_t s = 0U; s < spt && blocks.size() < needed; ++s) {
                blocks.push_back({.track = t, .sector = s});
            }
        }
        if (blocks.size() < needed) {
            return {}; // program does not fit on a single-sided disk
        }

        std::vector<std::uint8_t> img(d64_image::size_35_tracks, 0x00U);

        // Write the file's block chain: each block links to the next; the final
        // one carries (0, used+1) so read_chain knows how many bytes it holds.
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const std::size_t off = offset_of(blocks[i].track, blocks[i].sector);
            const std::size_t src = i * block_data;
            const std::size_t chunk = std::min<std::size_t>(block_data, prg.size() - src);
            const bool last = i + 1U == blocks.size();
            if (last) {
                img[off + 0U] = 0x00U;
                img[off + 1U] = static_cast<std::uint8_t>(chunk + 1U);
            } else {
                img[off + 0U] = blocks[i + 1U].track;
                img[off + 1U] = blocks[i + 1U].sector;
            }
            for (std::size_t b = 0; b < chunk; ++b) {
                img[off + 2U + b] = prg[src + b];
            }
        }

        // BAM (18/0): link to the directory, DOS version 'A', a cosmetic disk
        // header, and an all-free block map (the KERNAL ignores it on LOAD).
        {
            const std::size_t off = offset_of(dir_track, 0U);
            img[off + 0x00U] = dir_track;
            img[off + 0x01U] = 0x01U; // first directory sector
            img[off + 0x02U] = 0x41U; // 'A'
            for (std::uint8_t t = 1U; t <= 35U; ++t) {
                const std::size_t e = off + 0x04U + static_cast<std::size_t>(t - 1U) * 4U;
                img[e + 0U] = d64_image::sectors_per_track(t);
                img[e + 1U] = 0xFFU;
                img[e + 2U] = 0xFFU;
                img[e + 3U] = 0x1FU;
            }
            for (std::size_t i = 0; i < 16U; ++i) {
                img[off + 0x90U + i] = pad; // disk name
            }
            img[off + 0xA0U] = pad;
            img[off + 0xA1U] = pad;
            img[off + 0xA2U] = '0'; // disk id
            img[off + 0xA3U] = '0';
            img[off + 0xA4U] = pad;
            img[off + 0xA5U] = '2'; // DOS type "2A"
            img[off + 0xA6U] = 'A';
        }

        // Directory (18/1): no further dir sectors, one closed-PRG entry.
        {
            const std::size_t off = offset_of(dir_track, 1U);
            img[off + 0x00U] = 0x00U; // no next directory sector
            img[off + 0x01U] = 0xFFU;
            img[off + 0x02U] = 0x82U; // closed PRG
            img[off + 0x03U] = blocks.front().track;
            img[off + 0x04U] = blocks.front().sector;
            for (std::size_t i = 0; i < 16U; ++i) {
                img[off + 0x05U + i] = i < name.size() ? name[i] : pad;
            }
            const auto count = static_cast<std::uint16_t>(blocks.size());
            img[off + 0x1EU] = static_cast<std::uint8_t>(count & 0xFFU);
            img[off + 0x1FU] = static_cast<std::uint8_t>((count >> 8U) & 0xFFU);
        }

        return img;
    }

} // namespace mnemos::chips::storage::c1541
