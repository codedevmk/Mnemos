#include "zip_archive.hpp"

#include "inflate.hpp"

#include <cstring>

namespace mnemos::compression {

    namespace {

        constexpr std::uint32_t kEocdSig = 0x06054b50U;    // "PK\5\6"
        constexpr std::uint32_t kCentralSig = 0x02014b50U; // "PK\1\2"
        constexpr std::uint32_t kLocalSig = 0x04034b50U;   // "PK\3\4"
        constexpr std::size_t kEocdFixed = 22U;
        constexpr std::size_t kEocdSearchMax = 65557U; // 22 + max 65535 comment
        constexpr std::size_t kCentralFixed = 46U;
        constexpr std::size_t kLocalFixed = 30U;

        // ZIP integers are always little-endian. Callers bounds-check first.
        [[nodiscard]] std::uint16_t rd_u16(std::span<const std::uint8_t> b,
                                           std::size_t o) noexcept {
            return static_cast<std::uint16_t>(b[o] | (static_cast<std::uint16_t>(b[o + 1]) << 8U));
        }
        [[nodiscard]] std::uint32_t rd_u32(std::span<const std::uint8_t> b,
                                           std::size_t o) noexcept {
            return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8U) |
                   (static_cast<std::uint32_t>(b[o + 2]) << 16U) |
                   (static_cast<std::uint32_t>(b[o + 3]) << 24U);
        }

        // Locate the End-Of-Central-Directory record by scanning the tail
        // backwards, so a trailing comment that contains the magic can't fool
        // us. nullopt if not present.
        [[nodiscard]] std::optional<std::size_t>
        find_eocd(std::span<const std::uint8_t> b) noexcept {
            if (b.size() < kEocdFixed) {
                return std::nullopt;
            }
            const std::size_t window = b.size() < kEocdSearchMax ? b.size() : kEocdSearchMax;
            const std::size_t start = b.size() - window;
            for (std::size_t i = b.size() - kEocdFixed + 1U; i-- > start;) {
                if (rd_u32(b, i) == kEocdSig) {
                    return i;
                }
            }
            return std::nullopt;
        }

    } // namespace

    std::optional<zip_archive> zip_archive::open(std::span<const std::uint8_t> bytes) {
        const auto eocd = find_eocd(bytes);
        if (!eocd) {
            return std::nullopt;
        }
        const std::size_t e = *eocd;
        const std::uint16_t total = rd_u16(bytes, e + 10U);
        const std::uint32_t cd_size = rd_u32(bytes, e + 12U);
        const std::uint32_t cd_offset = rd_u32(bytes, e + 16U);
        if (static_cast<std::uint64_t>(cd_offset) + cd_size > bytes.size()) {
            return std::nullopt;
        }

        zip_archive z;
        z.bytes_ = bytes;
        z.entries_.reserve(total);
        std::size_t o = cd_offset;
        const std::size_t cd_end = static_cast<std::size_t>(cd_offset) + cd_size;
        for (std::uint16_t n = 0; n < total; ++n) {
            if (o + kCentralFixed > cd_end || rd_u32(bytes, o) != kCentralSig) {
                return std::nullopt;
            }
            const std::uint16_t method = rd_u16(bytes, o + 10U);
            const std::uint16_t name_len = rd_u16(bytes, o + 28U);
            const std::uint16_t extra_len = rd_u16(bytes, o + 30U);
            const std::uint16_t comment_len = rd_u16(bytes, o + 32U);
            const std::size_t var = static_cast<std::size_t>(name_len) + extra_len + comment_len;
            if (o + kCentralFixed + var > cd_end) {
                return std::nullopt;
            }
            zip_entry entry;
            entry.name.assign(reinterpret_cast<const char*>(bytes.data() + o + kCentralFixed),
                              name_len);
            entry.method = method == 0U   ? zip_method::stored
                           : method == 8U ? zip_method::deflated
                                          : zip_method::unsupported;
            entry.compressed_size = rd_u32(bytes, o + 20U);
            entry.uncompressed_size = rd_u32(bytes, o + 24U);
            entry.local_header_offset = rd_u32(bytes, o + 42U);
            z.entries_.push_back(std::move(entry));
            o += kCentralFixed + var;
        }
        return z;
    }

    std::optional<std::vector<std::uint8_t>> zip_archive::extract(const zip_entry& entry) const {
        if (entry.method == zip_method::unsupported) {
            return std::nullopt;
        }
        const std::size_t lho = entry.local_header_offset;
        if (lho + kLocalFixed > bytes_.size() || rd_u32(bytes_, lho) != kLocalSig) {
            return std::nullopt;
        }
        const std::uint16_t name_len = rd_u16(bytes_, lho + 26U);
        const std::uint16_t extra_len = rd_u16(bytes_, lho + 28U);
        const std::size_t data = lho + kLocalFixed + name_len + extra_len;
        if (static_cast<std::uint64_t>(data) + entry.compressed_size > bytes_.size()) {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> comp = bytes_.subspan(data, entry.compressed_size);

        // uncompressed_size comes straight from an untrusted central directory:
        // cap it before allocating, or a ~100-byte archive can demand 4 GiB per
        // entry. 256 MiB covers any plausible ROM/media payload, and a deflate
        // stream cannot legitimately expand beyond ~1032x its compressed size.
        constexpr std::uint64_t max_entry_size = 256ULL * 1024U * 1024U;
        constexpr std::uint64_t max_expansion = 1032U;
        if (entry.uncompressed_size > max_entry_size ||
            (entry.method == zip_method::deflated &&
             entry.uncompressed_size >
                 (static_cast<std::uint64_t>(entry.compressed_size) + 64U) * max_expansion)) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> out(entry.uncompressed_size);
        if (entry.method == zip_method::stored) {
            if (entry.compressed_size != entry.uncompressed_size) {
                return std::nullopt;
            }
            std::memcpy(out.data(), comp.data(), out.size());
            return out;
        }

        const auto written = inflate_raw(comp, out);
        if (!written || *written != entry.uncompressed_size) {
            return std::nullopt;
        }
        return out;
    }

} // namespace mnemos::compression
