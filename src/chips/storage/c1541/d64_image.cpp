#include "d64_image.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace mnemos::chips::storage::c1541 {
    namespace {

        constexpr std::uint8_t pad = 0xA0U;

        // Replace 0xA0 padding (and 0x00) with PETSCII spaces for display.
        std::uint8_t display_char(std::uint8_t c) noexcept {
            return (c == pad || c == 0x00U) ? 0x20U : c;
        }

        bool name_matches(std::span<const std::uint8_t> name,
                          std::span<const std::uint8_t> pattern) {
            std::size_t i = 0;
            for (const std::uint8_t p : pattern) {
                if (p == 0x2AU) { // '*' matches the rest
                    return true;
                }
                if (p == 0x00U || p == pad) { // pattern ended
                    break;
                }
                if (i >= name.size()) {
                    return false;
                }
                if (p != 0x3FU && name[i] != p) { // '?' matches any single char
                    return false;
                }
                ++i;
            }
            // The remaining filename characters must be padding.
            for (std::size_t j = i; j < name.size(); ++j) {
                if (name[j] != pad && name[j] != 0x00U) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    bool d64_image::load(std::span<const std::uint8_t> data) {
        if (data.size() != size_35_tracks && data.size() != size_40_tracks) {
            data_.clear();
            return false;
        }
        data_.assign(data.begin(), data.end());
        return true;
    }

    std::uint8_t d64_image::track_count() const noexcept {
        return data_.size() >= size_40_tracks ? 40U : 35U;
    }

    std::uint8_t d64_image::sectors_per_track(std::uint8_t track) noexcept {
        if (track >= 1U && track <= 17U) {
            return 21U;
        }
        if (track >= 18U && track <= 24U) {
            return 19U;
        }
        if (track >= 25U && track <= 30U) {
            return 18U;
        }
        return 17U; // 31-40
    }

    std::int32_t d64_image::sector_offset(std::uint8_t track, std::uint8_t sector) const noexcept {
        if (track < 1U || track > track_count() || sector >= sectors_per_track(track)) {
            return -1;
        }
        std::int32_t blocks = 0;
        for (std::uint8_t t = 1U; t < track; ++t) {
            blocks += sectors_per_track(t);
        }
        blocks += sector;
        return blocks * sector_size;
    }

    std::span<const std::uint8_t> d64_image::sector(std::uint8_t track,
                                                    std::uint8_t sector) const noexcept {
        const std::int32_t offset = sector_offset(track, sector);
        if (offset < 0 || static_cast<std::size_t>(offset) + sector_size > data_.size()) {
            return {};
        }
        return std::span<const std::uint8_t>(data_.data() + offset, sector_size);
    }

    std::array<std::uint8_t, 16> d64_image::disk_name() const {
        std::array<std::uint8_t, 16> out{};
        const auto bam = sector(directory_track, 0U);
        if (bam.size() >= 0xA0U) {
            for (std::size_t i = 0; i < 16U; ++i) {
                out[i] = bam[0x90U + i];
            }
        }
        return out;
    }

    std::array<std::uint8_t, 2> d64_image::disk_id() const {
        std::array<std::uint8_t, 2> out{};
        const auto bam = sector(directory_track, 0U);
        if (bam.size() >= 0xA4U) {
            out[0] = bam[0xA2U];
            out[1] = bam[0xA3U];
        }
        return out;
    }

    std::vector<d64_image::dir_entry> d64_image::directory() const {
        std::vector<dir_entry> entries;
        std::uint8_t track = directory_track;
        std::uint8_t sec = 1U;
        for (int guard = 0; guard < 19 && track != 0U; ++guard) {
            const auto s = sector(track, sec);
            if (s.empty()) {
                break;
            }
            for (std::size_t e = 0; e < 8U; ++e) {
                const std::size_t base = e * 32U;
                const std::uint8_t type = s[base + 0x02U];
                if (type == 0U) {
                    continue; // empty / scratched slot
                }
                dir_entry entry;
                entry.type = type;
                entry.first_track = s[base + 0x03U];
                entry.first_sector = s[base + 0x04U];
                for (std::size_t i = 0; i < 16U; ++i) {
                    entry.name[i] = s[base + 0x05U + i];
                }
                entry.blocks =
                    static_cast<std::uint16_t>(s[base + 0x1EU] | (s[base + 0x1FU] << 8U));
                entries.push_back(entry);
            }
            track = s[0];
            sec = s[1];
        }
        return entries;
    }

    std::optional<d64_image::dir_entry> d64_image::find_first_prg() const {
        for (const dir_entry& e : directory()) {
            if (e.is_prg_closed()) {
                return e;
            }
        }
        return std::nullopt;
    }

    std::optional<d64_image::dir_entry>
    d64_image::find_by_name(std::span<const std::uint8_t> pattern) const {
        for (const dir_entry& e : directory()) {
            if ((e.type & 0x07U) != 0U && name_matches(e.name, pattern)) {
                return e;
            }
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> d64_image::read_chain(std::uint8_t track, std::uint8_t sector) const {
        std::vector<std::uint8_t> out;
        std::uint8_t t = track;
        std::uint8_t s = sector;
        for (int guard = 0; guard < 768 && t != 0U; ++guard) {
            const auto block = this->sector(t, s);
            if (block.size() < sector_size) {
                break;
            }
            const std::uint8_t next_t = block[0];
            const std::uint8_t next_s = block[1];
            if (next_t == 0U) {
                const std::size_t used = next_s >= 1U ? static_cast<std::size_t>(next_s - 1U) : 0U;
                out.insert(out.end(), block.begin() + 2, block.begin() + 2 + used);
                break;
            }
            out.insert(out.end(), block.begin() + 2, block.end());
            t = next_t;
            s = next_s;
        }
        return out;
    }

    std::vector<std::uint8_t> d64_image::render_directory_listing() const {
        struct line final {
            std::uint16_t number{};
            std::vector<std::uint8_t> text;
        };
        std::vector<line> lines;

        // Header line: reverse-on, "DISKNAME", id, dos type.
        {
            line header;
            header.number = 0U;
            header.text.push_back(0x12U); // RVS ON
            header.text.push_back('"');
            for (const std::uint8_t c : disk_name()) {
                header.text.push_back(display_char(c));
            }
            header.text.push_back('"');
            header.text.push_back(' ');
            const auto id = disk_id();
            header.text.push_back(display_char(id[0]));
            header.text.push_back(display_char(id[1]));
            header.text.push_back(' ');
            header.text.push_back('2');
            header.text.push_back('A');
            lines.push_back(std::move(header));
        }

        const std::array<const char*, 8> types = {"DEL", "SEQ", "PRG", "USR",
                                                  "REL", "???", "???", "???"};
        for (const dir_entry& e : directory()) {
            line row;
            row.number = e.blocks;
            row.text.push_back(' ');
            row.text.push_back('"');
            std::size_t n = 0;
            for (; n < e.name.size() && e.name[n] != pad && e.name[n] != 0x00U; ++n) {
                row.text.push_back(e.name[n]);
            }
            row.text.push_back('"');
            for (std::size_t pad_i = n; pad_i < 16U; ++pad_i) {
                row.text.push_back(' ');
            }
            const char* type = types[e.type & 0x07U];
            row.text.push_back(static_cast<std::uint8_t>(type[0]));
            row.text.push_back(static_cast<std::uint8_t>(type[1]));
            row.text.push_back(static_cast<std::uint8_t>(type[2]));
            lines.push_back(std::move(row));
        }

        // BLOCKS FREE line.
        {
            std::uint16_t free_blocks = 0U;
            const auto bam = sector(directory_track, 0U);
            if (!bam.empty()) {
                for (std::uint8_t t = 1U; t <= track_count(); ++t) {
                    if (t == directory_track) {
                        continue;
                    }
                    free_blocks =
                        static_cast<std::uint16_t>(free_blocks + bam[0x04U + (t - 1U) * 4U]);
                }
            }
            line footer;
            footer.number = free_blocks;
            for (const char c : std::string("BLOCKS FREE.")) {
                footer.text.push_back(static_cast<std::uint8_t>(c));
            }
            lines.push_back(std::move(footer));
        }

        // Serialise as a BASIC program at $0801.
        std::vector<std::uint8_t> out = {0x01U, 0x08U};
        std::uint16_t addr = 0x0801U;
        for (const line& l : lines) {
            const auto length = static_cast<std::uint16_t>(2U + 2U + l.text.size() + 1U);
            const auto next = static_cast<std::uint16_t>(addr + length);
            out.push_back(static_cast<std::uint8_t>(next & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((next >> 8U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>(l.number & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((l.number >> 8U) & 0xFFU));
            out.insert(out.end(), l.text.begin(), l.text.end());
            out.push_back(0x00U);
            addr = next;
        }
        out.push_back(0x00U); // end-of-program link
        out.push_back(0x00U);
        return out;
    }

} // namespace mnemos::chips::storage::c1541
