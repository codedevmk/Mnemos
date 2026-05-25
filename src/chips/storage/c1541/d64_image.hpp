#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::chips::storage::c1541 {

    // A Commodore 1541 .d64 disk image: 35 (or 40) tracks of 256-byte sectors,
    // with the variable sectors-per-track zoning, the BAM + directory on track 18,
    // and PRG file block chains. Pure logical decode (no GCR); the byte-level view
    // the synthetic drive serves and the full drive's disk-bind GCR encoder read.
    //
    // Ported from the Emu reference core (ADR 0006).
    class d64_image final {
      public:
        static constexpr std::uint16_t sector_size = 256U;
        static constexpr std::size_t size_35_tracks = 174848U;
        static constexpr std::size_t size_40_tracks = 196608U;
        static constexpr std::uint8_t directory_track = 18U;

        struct dir_entry final {
            std::uint8_t type{}; // raw file-type byte ($02 = closed PRG)
            std::uint8_t first_track{};
            std::uint8_t first_sector{};
            std::array<std::uint8_t, 16> name{}; // PETSCII, 0xA0-padded
            std::uint16_t blocks{};

            [[nodiscard]] bool is_prg_closed() const noexcept {
                return (type & 0x07U) == 0x02U && (type & 0x80U) != 0U;
            }
        };

        // Adopt a .d64 image. Returns false if the size is not a known geometry.
        [[nodiscard]] bool load(std::span<const std::uint8_t> data);
        [[nodiscard]] bool loaded() const noexcept { return !data_.empty(); }
        [[nodiscard]] std::uint8_t track_count() const noexcept;

        [[nodiscard]] static std::uint8_t sectors_per_track(std::uint8_t track) noexcept;
        // Byte offset of (track, sector), or -1 if out of range.
        [[nodiscard]] std::int32_t sector_offset(std::uint8_t track,
                                                 std::uint8_t sector) const noexcept;
        // A 256-byte view of (track, sector), or empty if out of range.
        [[nodiscard]] std::span<const std::uint8_t> sector(std::uint8_t track,
                                                           std::uint8_t sector) const noexcept;

        // The disk name (16 bytes) and 2-byte disk id from the BAM (track 18/0).
        [[nodiscard]] std::array<std::uint8_t, 16> disk_name() const;
        [[nodiscard]] std::array<std::uint8_t, 2> disk_id() const;

        // Walk the directory chain on track 18.
        [[nodiscard]] std::vector<dir_entry> directory() const;
        [[nodiscard]] std::optional<dir_entry> find_first_prg() const;
        // Match against a PETSCII pattern (`*` = rest, `?` = any one char).
        [[nodiscard]] std::optional<dir_entry>
        find_by_name(std::span<const std::uint8_t> pattern) const;

        // Read a file's block chain into bytes (a PRG includes its 2-byte load addr).
        [[nodiscard]] std::vector<std::uint8_t> read_chain(std::uint8_t track,
                                                           std::uint8_t sector) const;

        // Synthesise the LOAD"$" directory listing as a BASIC PRG (load addr $0801).
        [[nodiscard]] std::vector<std::uint8_t> render_directory_listing() const;

      private:
        std::vector<std::uint8_t> data_;
    };

} // namespace mnemos::chips::storage::c1541
