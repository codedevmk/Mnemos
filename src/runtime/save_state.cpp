#include "save_state.hpp"

#include "crc32.hpp"
#include "state.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>

namespace mnemos::runtime {
    namespace {

        constexpr std::array<std::uint8_t, 4> magic = {'M', 'N', 'M', 'S'};
        constexpr int zstd_level = 3; // default level: fast, good enough for states + rewind

        std::span<const std::uint8_t> as_bytes(std::string_view s) noexcept {
            return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                                 s.size());
        }

        std::vector<std::uint8_t> zstd_compress(std::span<const std::uint8_t> src) {
            const std::size_t bound = ZSTD_compressBound(src.size());
            std::vector<std::uint8_t> dst(bound);
            const std::size_t n =
                ZSTD_compress(dst.data(), dst.size(), src.data(), src.size(), zstd_level);
            if (ZSTD_isError(n) != 0U) {
                return {};
            }
            dst.resize(n);
            return dst;
        }

        // Cap on the frame's self-declared content size. The CRC covers only the
        // compressed body, so a hostile file reaches this with an arbitrary
        // declared size; without a cap that is an attacker-chosen allocation.
        // Largest real state today is a few MB (Sega CD: ~1 MB of RAM banks plus
        // chip chunks); 256 MiB leaves generous headroom for future systems.
        constexpr unsigned long long max_state_content_size = 256ULL * 1024U * 1024U;

        std::optional<std::vector<std::uint8_t>>
        zstd_decompress(std::span<const std::uint8_t> src) {
            const unsigned long long size = ZSTD_getFrameContentSize(src.data(), src.size());
            if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN ||
                size > max_state_content_size) {
                return std::nullopt;
            }
            std::vector<std::uint8_t> dst(static_cast<std::size_t>(size));
            const std::size_t n = ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size());
            if (ZSTD_isError(n) != 0U || n != dst.size()) {
                return std::nullopt;
            }
            return dst;
        }

        void write_chunk(chips::state_writer& w, const std::string& id, std::uint32_t version,
                         std::span<const std::uint8_t> data) {
            w.blob(as_bytes(id));
            w.u32(version);
            w.u64(data.size());
            w.bytes(data);
        }

    } // namespace

    std::vector<std::uint8_t> write_save_state(const save_target& target) {
        // Body: one chunk per chip, then one per memory region.
        std::vector<std::uint8_t> body;
        chips::state_writer bw(body);
        for (const save_chip& sc : target.chips) {
            std::vector<std::uint8_t> chunk;
            chips::state_writer cw(chunk);
            if (sc.chip != nullptr) {
                sc.chip->save_state(cw);
            }
            write_chunk(bw, sc.id, 1U, chunk);
        }
        for (const save_memory& sm : target.memory) {
            write_chunk(bw, sm.id, 1U, std::span<const std::uint8_t>(sm.bytes));
        }
        for (const save_component& sc : target.components) {
            std::vector<std::uint8_t> chunk;
            chips::state_writer cw(chunk);
            if (sc.save) {
                sc.save(cw);
            }
            write_chunk(bw, sc.id, 1U, chunk);
        }

        const std::vector<std::uint8_t> compressed = zstd_compress(body);
        const auto chunk_count = static_cast<std::uint32_t>(
            target.chips.size() + target.memory.size() + target.components.size());

        std::vector<std::uint8_t> out;
        out.insert(out.end(), magic.begin(), magic.end());
        chips::state_writer hw(out);
        hw.u32(save_state_format_version);
        hw.blob(as_bytes(target.manifest_id));
        hw.u32(target.manifest_rev);
        hw.u64(target.master_cycle);
        hw.u32(chunk_count);
        out.insert(out.end(), compressed.begin(), compressed.end());

        // CRC32 over the header + compressed body, appended little-endian.
        const std::uint32_t crc = security::cryptography::crc32(std::span<const std::uint8_t>(out));
        chips::state_writer cw(out);
        cw.u32(crc);
        return out;
    }

    load_result read_save_state(std::span<const std::uint8_t> data, const save_target& target) {
        // magic (4) + at least the CRC (4).
        if (data.size() < magic.size() + 4U ||
            !std::equal(magic.begin(), magic.end(), data.begin())) {
            return {.status = load_status::bad_magic};
        }

        const std::span<const std::uint8_t> after_magic = data.subspan(magic.size());
        chips::state_reader hr(after_magic);
        const std::uint32_t version = hr.u32();
        const std::vector<std::uint8_t> id_bytes = hr.blob();
        const std::uint32_t rev = hr.u32();
        const std::uint64_t master_cycle = hr.u64();
        const std::uint32_t chunk_count = hr.u32();
        if (!hr.ok()) {
            return {.status = load_status::truncated};
        }
        if (version != save_state_format_version) {
            return {.status = load_status::unsupported_version};
        }
        static_cast<void>(rev); // recorded; manifest match is by id in v0.1

        const std::string id(id_bytes.begin(), id_bytes.end());
        if (id != target.manifest_id) {
            return {.status = load_status::manifest_mismatch};
        }

        // Header length = consumed so far; the body sits between it and the CRC.
        const std::size_t header_len = data.size() - hr.remaining();
        if (header_len + 4U > data.size()) {
            return {.status = load_status::truncated};
        }
        const std::size_t body_end = data.size() - 4U;
        const std::span<const std::uint8_t> compressed =
            data.subspan(header_len, body_end - header_len);

        // Verify the trailing CRC32 over everything before it.
        const std::uint32_t stored_crc = static_cast<std::uint32_t>(data[body_end]) |
                                         (static_cast<std::uint32_t>(data[body_end + 1U]) << 8U) |
                                         (static_cast<std::uint32_t>(data[body_end + 2U]) << 16U) |
                                         (static_cast<std::uint32_t>(data[body_end + 3U]) << 24U);
        if (security::cryptography::crc32(data.subspan(0, body_end)) != stored_crc) {
            return {.status = load_status::bad_crc};
        }

        const std::optional<std::vector<std::uint8_t>> body = zstd_decompress(compressed);
        if (!body) {
            return {.status = load_status::decompress_failed};
        }

        chips::state_reader br(*body);
        for (std::uint32_t i = 0; i < chunk_count; ++i) {
            const std::vector<std::uint8_t> chunk_id_bytes = br.blob();
            const std::uint32_t chunk_version = br.u32();
            const std::uint64_t chunk_size = br.u64();
            static_cast<void>(chunk_version);
            if (!br.ok() || chunk_size > br.remaining()) {
                return {.status = load_status::truncated};
            }
            std::vector<std::uint8_t> chunk(static_cast<std::size_t>(chunk_size));
            br.bytes(chunk);
            const std::string chunk_id(chunk_id_bytes.begin(), chunk_id_bytes.end());

            // Dispatch to the matching chip, memory region, or component; skip
            // unknown chunks (forward compatibility). Ids are unique across the
            // three sets, so the first match wins.
            bool handled = false;
            for (const save_chip& sc : target.chips) {
                if (sc.id == chunk_id && sc.chip != nullptr) {
                    chips::state_reader cr(chunk);
                    sc.chip->load_state(cr);
                    if (!cr.ok()) {
                        return {.status = load_status::chunk_rejected,
                                .master_cycle = master_cycle};
                    }
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                for (const save_memory& sm : target.memory) {
                    if (sm.id == chunk_id) {
                        const std::size_t n = std::min(sm.bytes.size(), chunk.size());
                        if (n > 0U) {
                            std::memcpy(sm.bytes.data(), chunk.data(), n);
                        }
                        handled = true;
                        break;
                    }
                }
            }
            if (!handled) {
                for (const save_component& sc : target.components) {
                    if (sc.id == chunk_id) {
                        if (sc.load) {
                            chips::state_reader cr(chunk);
                            sc.load(cr);
                            if (!cr.ok()) {
                                return {.status = load_status::chunk_rejected,
                                        .master_cycle = master_cycle};
                            }
                        }
                        handled = true;
                        break;
                    }
                }
            }
        }
        if (!br.ok()) {
            return {.status = load_status::truncated};
        }

        return {.status = load_status::ok, .master_cycle = master_cycle};
    }

} // namespace mnemos::runtime
