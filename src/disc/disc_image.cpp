// CD-ROM disc image loader. Ported from the Emu reference (chips/disc_image);
// see disc_image.hpp for the contract and NOTES.md for provenance. Backed by
// in-memory buffers (one per track file) loaded via mnemos::io::read_file.

#include "disc_image.hpp"

#include "chd_reader.hpp"
#include "file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::disc {
    namespace {

        // Mode-1 sync pattern at the start of every raw 2352-byte sector.
        constexpr std::array<std::uint8_t, 12> kSyncPattern = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                               0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

        [[nodiscard]] bool ends_with_ci(const std::string& s, std::string_view suffix) {
            if (s.size() < suffix.size()) {
                return false;
            }
            const std::size_t off = s.size() - suffix.size();
            for (std::size_t i = 0; i < suffix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
                    std::tolower(static_cast<unsigned char>(suffix[i]))) {
                    return false;
                }
            }
            return true;
        }

        void write_raw_header(std::span<std::uint8_t, disc_image::raw_sector_size> dst,
                              std::uint32_t lba, std::uint8_t mode) {
            std::fill(dst.begin(), dst.end(), std::uint8_t{0});
            std::copy(kSyncPattern.begin(), kSyncPattern.end(), dst.begin());
            std::uint8_t m = 0;
            std::uint8_t s = 0;
            std::uint8_t f = 0;
            msf_from_lba(lba + 150U, m, s, f); // +150 = 2-second pre-gap
            const auto bcd = [](std::uint8_t v) {
                return static_cast<std::uint8_t>(((v / 10U) << 4U) | (v % 10U));
            };
            dst[12] = bcd(m);
            dst[13] = bcd(s);
            dst[14] = bcd(f);
            dst[15] = mode;
        }

        // ---- CUE tokeniser ----
        const char* skip_ws(const char* p) {
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            return p;
        }

        bool parse_token(const char** p, std::string& out) {
            const char* s = skip_ws(*p);
            out.clear();
            if (*s == 0) {
                return false;
            }
            if (*s == '"') {
                ++s;
                while (*s != 0 && *s != '"') {
                    out.push_back(*s++);
                }
                if (*s == '"') {
                    ++s;
                }
            } else {
                while (*s != 0 && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
                    out.push_back(*s++);
                }
            }
            *p = s;
            return !out.empty();
        }

        // Parse "mm:ss:ff" (decimal) into an LBA. Stops at trailing whitespace.
        bool parse_msf(const std::string& s, std::uint32_t& out) {
            std::array<unsigned, 3> v{0, 0, 0};
            int field = 0;
            bool any_digit = false;
            for (const char c : s) {
                if (c >= '0' && c <= '9') {
                    v[static_cast<std::size_t>(field)] =
                        v[static_cast<std::size_t>(field)] * 10U + static_cast<unsigned>(c - '0');
                    any_digit = true;
                } else if (c == ':') {
                    if (++field >= 3) {
                        break;
                    }
                } else {
                    break; // whitespace / trailing content ends the token
                }
            }
            if (field < 2 || !any_digit) {
                return false; // need all three mm:ss:ff fields
            }
            out = v[0] * 60U * 75U + v[1] * 75U + v[2];
            return true;
        }

        std::string to_upper(std::string s) {
            for (char& c : s) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return s;
        }

        bool cue_mode_decode(const std::string& mode, track_type& type, std::uint32_t& ssize) {
            if (mode == "MODE1/2048") {
                type = track_type::mode1_2048;
                ssize = 2048;
            } else if (mode == "MODE1/2352") {
                type = track_type::mode1_2352;
                ssize = 2352;
            } else if (mode == "MODE2/2336") {
                type = track_type::mode2_2336;
                ssize = 2336;
            } else if (mode == "MODE2/2352") {
                type = track_type::mode2_2352;
                ssize = 2352;
            } else if (mode == "AUDIO") {
                type = track_type::audio;
                ssize = 2352;
            } else {
                return false;
            }
            return true;
        }

    } // namespace

    std::uint32_t lba_from_msf(std::uint8_t m, std::uint8_t s, std::uint8_t f) noexcept {
        return static_cast<std::uint32_t>(m) * 60U * 75U + static_cast<std::uint32_t>(s) * 75U + f;
    }

    void msf_from_lba(std::uint32_t lba, std::uint8_t& m, std::uint8_t& s,
                      std::uint8_t& f) noexcept {
        m = static_cast<std::uint8_t>(lba / (60U * 75U));
        const std::uint32_t rem = lba % (60U * 75U);
        s = static_cast<std::uint8_t>(rem / 75U);
        f = static_cast<std::uint8_t>(rem % 75U);
    }

    bool disc_image::read_bytes(int file_index, std::uint64_t offset,
                                std::span<std::uint8_t> out) const noexcept {
        if (file_index < 0 || static_cast<std::size_t>(file_index) >= files_.size()) {
            return false;
        }
        const std::vector<std::uint8_t>& buf = files_[static_cast<std::size_t>(file_index)];
        if (offset + out.size() > buf.size()) {
            return false;
        }
        std::memcpy(out.data(), buf.data() + offset, out.size());
        return true;
    }

    std::optional<disc_image> disc_image::open_bin(std::vector<std::uint8_t> data) {
        if (data.empty() || (data.size() % raw_sector_size) != 0) {
            return std::nullopt;
        }
        if (!std::equal(kSyncPattern.begin(), kSyncPattern.end(), data.begin())) {
            return std::nullopt; // not a raw Mode-1 image
        }
        disc_image img;
        img.format_ = disc_format::bin_2352;
        img.total_sectors_ = static_cast<std::uint32_t>(data.size() / raw_sector_size);
        img.files_.push_back(std::move(data));
        img.tracks_.push_back(track{.type = track_type::mode1_2352,
                                    .start_lba = 0,
                                    .sector_count = img.total_sectors_,
                                    .file_offset = 0,
                                    .sector_size = raw_sector_size,
                                    .number = 1,
                                    .ctrl_adr = 0x41,
                                    .file_index = 0});
        return img;
    }

    std::optional<disc_image> disc_image::open_iso(std::vector<std::uint8_t> data) {
        if (data.empty() || (data.size() % user_data_size) != 0) {
            return std::nullopt;
        }
        disc_image img;
        img.format_ = disc_format::iso_2048;
        img.total_sectors_ = static_cast<std::uint32_t>(data.size() / user_data_size);
        img.files_.push_back(std::move(data));
        img.tracks_.push_back(track{.type = track_type::mode1_2048,
                                    .start_lba = 0,
                                    .sector_count = img.total_sectors_,
                                    .file_offset = 0,
                                    .sector_size = user_data_size,
                                    .number = 1,
                                    .ctrl_adr = 0x41,
                                    .file_index = 0});
        return img;
    }

    std::optional<disc_image> disc_image::open_chd(std::vector<std::uint8_t> data) {
        auto decoded = chd::decode(data);
        if (!decoded || decoded->tracks.empty()) {
            return std::nullopt;
        }
        disc_image img;
        img.format_ = disc_format::chd;
        img.total_sectors_ = decoded->total_sectors;
        // The whole CHD is decompressed into one flat raw-2352 buffer; every
        // track indexes into that single backing file.
        img.files_.push_back(std::move(decoded->data));
        for (const chd::chd_track& ct : decoded->tracks) {
            track t;
            t.type = ct.is_audio ? track_type::audio : track_type::mode1_2352;
            t.start_lba = ct.start_lba;
            t.sector_count = ct.sector_count;
            t.file_offset = ct.data_offset;
            t.sector_size = disc_image::raw_sector_size; // flat image is raw 2352
            t.number = static_cast<std::uint8_t>(ct.number);
            t.ctrl_adr = ct.is_audio ? 0x01U : 0x41U;
            t.file_index = 0;
            img.tracks_.push_back(t);
        }
        return img;
    }

    std::optional<disc_image> disc_image::open(const std::string& path) {
        if (ends_with_ci(path, ".cue")) {
            const auto bytes = io::read_file(path);
            if (!bytes) {
                return std::nullopt;
            }
            disc_image img;
            if (!img.open_cue(path, *bytes)) {
                return std::nullopt;
            }
            return img;
        }
        auto bytes = io::read_file(path);
        if (!bytes || bytes->empty()) {
            return std::nullopt;
        }
        if (ends_with_ci(path, ".chd")) {
            return open_chd(std::move(*bytes));
        }
        if (ends_with_ci(path, ".iso")) {
            return open_iso(std::move(*bytes));
        }
        if (ends_with_ci(path, ".bin") || ends_with_ci(path, ".img")) {
            return open_bin(std::move(*bytes));
        }
        // Unknown extension -- auto-detect from size divisibility (2048 first).
        if ((bytes->size() % user_data_size) == 0) {
            return open_iso(std::move(*bytes));
        }
        if ((bytes->size() % raw_sector_size) == 0) {
            return open_bin(std::move(*bytes));
        }
        return std::nullopt;
    }

    bool disc_image::open_cue(const std::string& cue_path,
                              const std::vector<std::uint8_t>& cue_bytes) {
        namespace fs = std::filesystem;
        const fs::path cue_dir = fs::path(cue_path).parent_path();

        int current_file = -1;
        int track_idx = -1;
        const std::string text(cue_bytes.begin(), cue_bytes.end());

        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t eol = text.find('\n', pos);
            const std::string line =
                text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
            pos = (eol == std::string::npos) ? text.size() : eol + 1;

            const char* p = line.c_str();
            std::string tok;
            if (!parse_token(&p, tok)) {
                continue;
            }
            const std::string cmd = to_upper(tok);

            if (cmd == "FILE") {
                std::string fname;
                if (!parse_token(&p, fname)) {
                    continue;
                }
                std::string type;
                (void)parse_token(&p, type); // BINARY/WAVE -- ignored
                const fs::path rel(fname);
                const fs::path resolved = rel.is_absolute() ? rel : (cue_dir / rel);
                auto data = io::read_file(resolved.string());
                if (!data) {
                    return false;
                }
                if (files_.size() >= static_cast<std::size_t>(max_tracks)) {
                    return false;
                }
                files_.push_back(std::move(*data));
                current_file = static_cast<int>(files_.size()) - 1;
            } else if (cmd == "TRACK") {
                std::string numstr;
                std::string mode;
                if (!parse_token(&p, numstr) || !parse_token(&p, mode)) {
                    continue;
                }
                if (current_file < 0 || tracks_.size() >= static_cast<std::size_t>(max_tracks)) {
                    return false;
                }
                track t;
                t.number = static_cast<std::uint8_t>(std::atoi(numstr.c_str()));
                if (!cue_mode_decode(mode, t.type, t.sector_size)) {
                    return false;
                }
                t.file_index = current_file;
                t.ctrl_adr = (t.type == track_type::audio) ? 0x01U : 0x41U;
                tracks_.push_back(t);
                track_idx = static_cast<int>(tracks_.size()) - 1;
            } else if (cmd == "INDEX") {
                std::string idxstr;
                std::string msfstr;
                if (!parse_token(&p, idxstr) || !parse_token(&p, msfstr)) {
                    continue;
                }
                std::uint32_t msf_lba = 0;
                if (!parse_msf(msfstr, msf_lba)) {
                    continue;
                }
                if (std::atoi(idxstr.c_str()) == 1 && track_idx >= 0) {
                    track& t = tracks_[static_cast<std::size_t>(track_idx)];
                    t.file_offset = static_cast<std::uint64_t>(msf_lba) * t.sector_size;
                }
            }
        }

        if (tracks_.empty()) {
            return false;
        }

        // Post-pass: per-track sector_count + cumulative start_lba. A track ends
        // where the next track in the same file begins, else at end-of-file.
        std::uint32_t cumulative = 0;
        std::uint32_t file_base_lba = 0; // absolute LBA where the current file starts
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            track& t = tracks_[i];
            std::uint64_t end_byte = files_[static_cast<std::size_t>(t.file_index)].size();
            for (std::size_t j = i + 1; j < tracks_.size(); ++j) {
                if (tracks_[j].file_index == t.file_index) {
                    end_byte = tracks_[j].file_offset;
                    break;
                }
            }
            const std::uint64_t span = end_byte > t.file_offset ? end_byte - t.file_offset : 0;
            t.sector_count = static_cast<std::uint32_t>(span / t.sector_size);

            // A track's absolute LBA is the file's base plus its file-relative
            // sector offset; the file-relative offset alone is only correct for
            // the first file (base 0), and using it for a later file would
            // overlap earlier tracks' LBA ranges.
            const bool new_file = (i == 0) || (tracks_[i - 1].file_index != t.file_index);
            if (new_file) {
                file_base_lba = cumulative;
            }
            t.start_lba = file_base_lba + static_cast<std::uint32_t>(t.file_offset / t.sector_size);
            cumulative = t.start_lba + t.sector_count;
        }

        format_ = disc_format::cue;
        total_sectors_ = cumulative;
        return true;
    }

    const disc_image::track* disc_image::find_track(std::uint32_t lba) const noexcept {
        for (const track& t : tracks_) {
            if (lba >= t.start_lba && lba < t.start_lba + t.sector_count) {
                return &t;
            }
        }
        return nullptr;
    }

    bool disc_image::read_sector(std::uint32_t lba,
                                 std::span<std::uint8_t, user_data_size> dst) const {
        const track* t = find_track(lba);
        if (t == nullptr) {
            return false;
        }
        const std::uint64_t in_track = lba - t->start_lba;
        const std::uint64_t base = t->file_offset + in_track * t->sector_size;
        if (t->sector_size == 2048) {
            return read_bytes(t->file_index, base, dst);
        }
        if (t->sector_size == 2352) {
            std::array<std::uint8_t, 16> hdr{};
            if (!read_bytes(t->file_index, base, hdr)) {
                return false;
            }
            const std::uint64_t data_off = (hdr[15] == 2U) ? base + 24U : base + 16U;
            return read_bytes(t->file_index, data_off, dst);
        }
        if (t->sector_size == 2336) {
            return read_bytes(t->file_index, base + 8U, dst); // 8-byte subheader
        }
        return false;
    }

    bool disc_image::read_raw_sector(std::uint32_t lba,
                                     std::span<std::uint8_t, raw_sector_size> dst) const {
        const track* t = find_track(lba);
        if (t == nullptr) {
            return false;
        }
        const std::uint64_t in_track = lba - t->start_lba;
        const std::uint64_t base = t->file_offset + in_track * t->sector_size;
        if (t->sector_size == raw_sector_size) {
            return read_bytes(t->file_index, base, dst);
        }
        if (t->sector_size == user_data_size) {
            write_raw_header(dst, lba, 0x01U);
            return read_bytes(t->file_index, base, dst.subspan(16, user_data_size));
        }
        if (t->sector_size == 2336) {
            write_raw_header(dst, lba, 0x02U);
            return read_bytes(t->file_index, base, dst.subspan(16, 2336));
        }
        return false;
    }

    sector_mode disc_image::mode_at(std::uint32_t lba) const {
        const track* t = find_track(lba);
        if (t == nullptr || t->type == track_type::audio) {
            return sector_mode::cd_da;
        }
        if (t->sector_size == 2048) {
            return sector_mode::mode1;
        }
        const std::uint64_t in_track = lba - t->start_lba;
        const std::uint64_t base = t->file_offset + in_track * t->sector_size;
        const std::uint64_t hdr_off = (t->sector_size == 2336) ? base : base + 15U;
        if (t->sector_size == 2336) {
            // 2336 = subheader (8) + data; form distinguished by subheader byte 2.
            std::array<std::uint8_t, 3> sub{};
            if (!read_bytes(t->file_index, base, sub)) {
                return sector_mode::mode2_form1;
            }
            return (sub[2] & 0x20U) ? sector_mode::mode2_form2 : sector_mode::mode2_form1;
        }
        std::array<std::uint8_t, 1> mb{};
        if (!read_bytes(t->file_index, hdr_off, mb)) {
            return sector_mode::cd_da;
        }
        if (mb[0] == 1U) {
            return sector_mode::mode1;
        }
        if (mb[0] == 2U) {
            std::array<std::uint8_t, 1> sb{};
            if (!read_bytes(t->file_index, base + 18U, sb)) {
                return sector_mode::mode2_form1;
            }
            return (sb[0] & 0x20U) ? sector_mode::mode2_form2 : sector_mode::mode2_form1;
        }
        return sector_mode::cd_da;
    }

} // namespace mnemos::disc
