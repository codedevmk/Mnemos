#include "drives/amiga_floppy.hpp"

namespace mnemos::manifests::amiga {

    void amiga_clear_floppy_track_cache(amiga_floppy_drive_state& drive) noexcept {
        for (auto& cached_track : drive.raw_track_cache) {
            cached_track.clear();
        }
        for (auto& cached_weak_bits : drive.weak_bit_cache) {
            cached_weak_bits.clear();
        }
    }

    void amiga_reset_floppy_stream_phase(amiga_floppy_drive_state& drive,
                                         std::size_t offset,
                                         std::uint8_t bit_offset) noexcept {
        drive.stream_offset = offset;
        drive.stream_bit_offset = static_cast<std::uint8_t>(bit_offset & 0x07U);
        drive.stream_read_shift = 0U;
        drive.stream_read_bit_count = 0U;
        drive.stream_write_latch = 0U;
        drive.stream_write_shift = 0U;
        drive.stream_write_bits_remaining = 0U;
        drive.byte_clock_accumulator = 0U;
    }

} // namespace mnemos::manifests::amiga
