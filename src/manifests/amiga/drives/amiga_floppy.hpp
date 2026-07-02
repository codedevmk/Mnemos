#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mnemos::manifests::amiga {

    inline constexpr std::size_t amiga_floppy_cylinders = 80U;
    inline constexpr std::size_t amiga_floppy_heads = 2U;
    inline constexpr std::size_t amiga_floppy_track_count =
        amiga_floppy_cylinders * amiga_floppy_heads;
    inline constexpr std::size_t amiga_floppy_sectors_per_track = 11U;
    inline constexpr std::size_t amiga_floppy_sector_size = 512U;
    inline constexpr std::size_t amiga_floppy_dd_size =
        amiga_floppy_cylinders * amiga_floppy_heads * amiga_floppy_sectors_per_track *
        amiga_floppy_sector_size;
    // Nominal raw MFM bytes per 300 RPM DD revolution for synthesized AmigaDOS tracks.
    inline constexpr std::size_t amiga_floppy_standard_raw_track_bytes = 12668U;
    inline constexpr std::size_t amiga_floppy_drive_count = 4U;
    inline constexpr std::uint8_t amiga_no_floppy_drive = 0xFFU;
    inline constexpr std::uint32_t amiga_floppy_index_pulses_per_second = 5U;
    inline constexpr std::uint16_t amiga_floppy_weak_bit_lfsr_seed = 0xACE1U;

    struct amiga_floppy_drive_state final {
        std::vector<std::uint8_t> image{};
        std::vector<std::uint8_t> track_stream{};
        std::vector<std::uint8_t> weak_bit_stream{};
        std::array<std::vector<std::uint8_t>, amiga_floppy_track_count> raw_track_cache{};
        std::array<std::vector<std::uint8_t>, amiga_floppy_track_count> weak_bit_cache{};
        std::size_t stream_offset{};
        std::size_t track_stream_track_index{};
        std::uint8_t stream_bit_offset{};
        std::uint8_t stream_read_shift{};
        std::uint8_t stream_read_bit_count{};
        std::uint8_t stream_write_latch{};
        std::uint8_t stream_write_shift{};
        std::uint8_t stream_write_bits_remaining{};
        std::uint16_t weak_bit_lfsr{amiga_floppy_weak_bit_lfsr_seed};
        std::uint8_t cylinder_pos{};
        bool connected{};
        bool motor_on{};
        bool write_protected{true};
        bool change_latch{true};
        bool track_stream_dirty{};
        std::uint32_t index_line_accumulator{};
        std::uint64_t byte_clock_accumulator{};
    };

    void amiga_clear_floppy_track_cache(amiga_floppy_drive_state& drive) noexcept;

    void amiga_reset_floppy_stream_phase(amiga_floppy_drive_state& drive,
                                         std::size_t offset = 0U,
                                         std::uint8_t bit_offset = 0U) noexcept;

} // namespace mnemos::manifests::amiga
