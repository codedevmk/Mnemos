#include <mnemos/chips/storage/c1541/disk_bind.hpp>

#include <mnemos/chips/storage/c1541/gcr.hpp>

#include <array>
#include <span>

namespace mnemos::chips::storage::c1541 {
    namespace {

        constexpr std::uint8_t header_sync = 0xFFU;
        constexpr std::uint8_t gap_byte = 0x55U;
        constexpr int header_sync_len = 5;
        constexpr int header_gap_len = 9;
        constexpr int data_sync_len = 5;
        constexpr int tail_gap_len = 8;

        // Append GCR-encode of `raw` (length must be a multiple of 4) to `out`.
        void append_gcr(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> raw) {
            for (std::size_t i = 0; i + 4U <= raw.size(); i += 4U) {
                const std::array<std::uint8_t, 4> group = {raw[i], raw[i + 1U], raw[i + 2U],
                                                           raw[i + 3U]};
                std::array<std::uint8_t, 5> code{};
                gcr_encode_4to5(group, code);
                out.insert(out.end(), code.begin(), code.end());
            }
        }

        void append_n(std::vector<std::uint8_t>& out, std::uint8_t value, int count) {
            out.insert(out.end(), static_cast<std::size_t>(count), value);
        }

    } // namespace

    std::uint8_t gcr_density_zone(std::uint8_t track) noexcept {
        if (track <= 17U) {
            return 3U;
        }
        if (track <= 24U) {
            return 2U;
        }
        if (track <= 30U) {
            return 1U;
        }
        return 0U;
    }

    std::vector<gcr_track> bind_gcr(const d64_image& disk) {
        std::vector<gcr_track> tracks;
        if (!disk.loaded()) {
            return tracks;
        }
        const auto id = disk.disk_id();

        for (std::uint8_t track = 1U; track <= disk.track_count(); ++track) {
            gcr_track gt;
            gt.density_zone = gcr_density_zone(track);
            const std::uint8_t sectors = d64_image::sectors_per_track(track);
            for (std::uint8_t sector = 0; sector < sectors; ++sector) {
                const auto data = disk.sector(track, sector);
                if (data.size() < d64_image::sector_size) {
                    continue;
                }

                // Header block: id, track, sector + checksum.
                const auto hchk = static_cast<std::uint8_t>(sector ^ track ^ id[0] ^ id[1]);
                const std::array<std::uint8_t, 8> header = {0x08U, hchk,  sector, track,
                                                            id[1], id[0], 0x00U,  0x00U};
                append_n(gt.bytes, header_sync, header_sync_len);
                append_gcr(gt.bytes, header);
                append_n(gt.bytes, gap_byte, header_gap_len);

                // Data block: $07 marker, 256 bytes, checksum, two padding bytes.
                std::array<std::uint8_t, 260> block{};
                block[0] = 0x07U;
                std::uint8_t dchk = 0U;
                for (std::size_t i = 0; i < d64_image::sector_size; ++i) {
                    block[1U + i] = data[i];
                    dchk = static_cast<std::uint8_t>(dchk ^ data[i]);
                }
                block[257] = dchk;
                append_n(gt.bytes, header_sync, data_sync_len);
                append_gcr(gt.bytes, block);
                append_n(gt.bytes, gap_byte, tail_gap_len);
            }
            tracks.push_back(std::move(gt));
        }
        return tracks;
    }

} // namespace mnemos::chips::storage::c1541
