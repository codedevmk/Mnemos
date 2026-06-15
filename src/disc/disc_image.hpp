#pragma once

// CD-ROM disc image loader for the CD-based systems (Sega CD first). Opens an
// image, detects its sector format, and exposes an LBA-addressable sector-read
// API. Backed by in-memory buffers (one per track file), so it is fully
// testable without touching the filesystem.
//
// Supported formats:
//   .iso        Mode-1, 2048-byte user-data sectors (size multiple of 2048).
//   .bin/.img   Raw 2352-byte sectors (size multiple of 2352, sync at LBA 0).
//   .cue        Cue sheet referencing one or more .bin tracks (multi-track).
//   .chd        Compressed Hunks of Data, v5 data tracks (none/cdzl/cdlz).
//               Decompressed to a flat raw-sector image at open time; FLAC
//               (cdfl) audio is not yet handled.
//
// Deferred (not needed by the Sega CD, added when a consumer needs them):
//   the Saturn IP.BIN parser and the ISO 9660 file walker. EDC/ECC of
//   synthesised raw sectors is left zeroed (as in the reference);
//   mnemos::disc::circ_ecc can fill it when a caller requires it.
//
// Ported from the Emu reference (chips/disc_image); see NOTES.md.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::disc {

    enum class disc_format : std::uint8_t {
        unknown,
        iso_2048,
        bin_2352,
        cue,
        chd,
    };

    enum class track_type : std::uint8_t {
        mode1_2048,
        mode1_2352,
        mode2_2336,
        mode2_2352,
        audio,
    };

    // Sector mode parsed from a raw sector's header byte.
    enum class sector_mode : std::uint8_t {
        cd_da,       // Red Book audio -- no header
        mode1,       // 2048 user data + ECC
        mode2_form1, // 2048 user data + subheader
        mode2_form2, // 2324 user data, minimal ECC
    };

    class disc_image {
      public:
        static constexpr std::size_t user_data_size = 2048;
        static constexpr std::size_t raw_sector_size = 2352;
        static constexpr int max_tracks = 99;

        struct track {
            track_type type{};
            std::uint32_t start_lba{}; // cumulative LBA where this track begins
            std::uint32_t sector_count{};
            std::uint64_t file_offset{}; // byte offset of INDEX 01 within its file
            std::uint32_t sector_size{}; // 2048 / 2336 / 2352
            std::uint8_t number{};
            std::uint8_t ctrl_adr{}; // CTRL+ADR byte for subcode Q
            int file_index{};        // index into the backing-file buffers
        };

        // Open from a path (.iso/.bin/.img/.cue, or auto-detect by size).
        // Returns nullopt on any open/validation failure.
        [[nodiscard]] static std::optional<disc_image> open(const std::string& path);

        // In-memory single-file builders (tests + preloaded callers).
        [[nodiscard]] static std::optional<disc_image> open_bin(std::vector<std::uint8_t> data);
        [[nodiscard]] static std::optional<disc_image> open_iso(std::vector<std::uint8_t> data);
        // Decode a whole CHD (v5 data tracks) held in memory into a flat
        // raw-sector image. Returns nullopt on a non-CHD / unsupported codec.
        [[nodiscard]] static std::optional<disc_image> open_chd(std::vector<std::uint8_t> data);

        [[nodiscard]] disc_format format() const noexcept { return format_; }
        [[nodiscard]] std::uint32_t total_sectors() const noexcept { return total_sectors_; }
        [[nodiscard]] int track_count() const noexcept { return static_cast<int>(tracks_.size()); }
        [[nodiscard]] std::span<const track> tracks() const noexcept { return tracks_; }

        // Track containing `lba`, or nullptr if out of range.
        [[nodiscard]] const track* find_track(std::uint32_t lba) const noexcept;

        // Read 2048 bytes of user data for `lba` into `dst`. Extracts the
        // payload window per the source format. Returns false out of range.
        [[nodiscard]] bool read_sector(std::uint32_t lba,
                                       std::span<std::uint8_t, user_data_size> dst) const;

        // Read a full 2352-byte raw sector. 2352 tracks copy verbatim; 2048/2336
        // tracks synthesise the sync/MSF/mode header (EDC/ECC zeroed).
        [[nodiscard]] bool read_raw_sector(std::uint32_t lba,
                                           std::span<std::uint8_t, raw_sector_size> dst) const;

        // Sector mode at `lba` (parses the header byte for raw formats).
        [[nodiscard]] sector_mode mode_at(std::uint32_t lba) const;

      private:
        disc_image() = default;

        [[nodiscard]] bool open_cue(const std::string& cue_path,
                                    const std::vector<std::uint8_t>& cue_bytes);
        // Copy `out.size()` bytes from backing file `file_index` at `offset`.
        [[nodiscard]] bool read_bytes(int file_index, std::uint64_t offset,
                                      std::span<std::uint8_t> out) const noexcept;

        std::vector<std::vector<std::uint8_t>> files_;
        std::vector<track> tracks_;
        disc_format format_{disc_format::unknown};
        std::uint32_t total_sectors_{};
    };

    // MSF <-> LBA helpers (no pre-gap adjustment).
    [[nodiscard]] std::uint32_t lba_from_msf(std::uint8_t m, std::uint8_t s,
                                             std::uint8_t f) noexcept;
    void msf_from_lba(std::uint32_t lba, std::uint8_t& m, std::uint8_t& s,
                      std::uint8_t& f) noexcept;

} // namespace mnemos::disc
