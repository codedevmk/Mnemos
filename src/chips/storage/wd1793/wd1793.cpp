#include "wd1793.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace mnemos::chips::storage {
    namespace {
        constexpr std::uint32_t k_state_version = 12U;
        constexpr std::uint64_t k_drq_lost_data_cycles = 512U;
        constexpr std::uint8_t k_mfm_gap = 0x4EU;
        constexpr std::uint8_t k_mfm_sync_pad = 0x00U;
        constexpr std::uint8_t k_mfm_index_sync_mark = 0xC2U;
        constexpr std::uint8_t k_mfm_sync_mark = 0xA1U;
        constexpr std::uint8_t k_mfm_index_mark = 0xFCU;
        constexpr std::uint8_t k_mfm_id_mark = 0xFEU;
        constexpr std::uint8_t k_mfm_deleted_data_mark = 0xF8U;
        constexpr std::uint8_t k_mfm_data_mark = 0xFBU;
        constexpr std::uint8_t k_mfm_crc_mark = 0xF7U;
        constexpr std::uint8_t k_mfm_sector_length_512 = 0x02U;

        [[nodiscard]] std::uint16_t crc16_ccitt_update(std::uint16_t crc,
                                                       std::uint8_t value) noexcept {
            crc ^= static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 8U);
            for (int bit = 0; bit < 8; ++bit) {
                if ((crc & 0x8000U) != 0U) {
                    crc = static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U);
                } else {
                    crc = static_cast<std::uint16_t>(crc << 1U);
                }
            }
            return crc;
        }

        [[nodiscard]] std::uint16_t crc16_ccitt(std::span<const std::uint8_t> bytes) noexcept {
            std::uint16_t crc = 0xFFFFU;
            for (const std::uint8_t value : bytes) {
                crc = crc16_ccitt_update(crc, value);
            }
            return crc;
        }

        void append_crc(std::vector<std::uint8_t>& out, std::uint16_t crc) {
            out.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0xFFU));
            out.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
        }

        [[nodiscard]] std::size_t sector_length_bytes(std::uint8_t length_code) noexcept {
            if (length_code > 3U) {
                return 0U;
            }
            return std::size_t{128U} << length_code;
        }

        [[nodiscard]] bool mfm_sync_byte(std::uint8_t value) noexcept {
            return value == 0xF5U || value == k_mfm_sync_mark;
        }

        [[nodiscard]] bool readable_mfm_sync_byte(std::uint8_t value) noexcept {
            return value == k_mfm_sync_mark || value == k_mfm_index_sync_mark;
        }

        [[nodiscard]] bool has_mfm_sync_preamble(std::span<const std::uint8_t> bytes,
                                                 std::size_t offset) noexcept {
            return offset >= 3U && mfm_sync_byte(bytes[offset - 1U]) &&
                   mfm_sync_byte(bytes[offset - 2U]) && mfm_sync_byte(bytes[offset - 3U]);
        }

        [[nodiscard]] bool write_track_control_byte(std::uint8_t value) noexcept {
            return value >= 0xF5U && value <= 0xFEU;
        }

        [[nodiscard]] bool has_literal_write_track_payload(std::span<const std::uint8_t> bytes,
                                                           std::size_t offset,
                                                           std::size_t size) noexcept {
            if (offset > bytes.size() || size > bytes.size() - offset) {
                return false;
            }
            for (std::size_t i = 0U; i < size; ++i) {
                if (write_track_control_byte(bytes[offset + i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::uint16_t le16(std::span<const std::uint8_t> bytes,
                                         std::size_t offset) noexcept {
            if (offset + 1U >= bytes.size()) {
                return 0U;
            }
            return static_cast<std::uint16_t>(
                bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
        }

        [[nodiscard]] std::optional<wd1793::dsk_geometry>
        geometry_from_dimensions(std::size_t sector_count, std::uint16_t tracks, std::uint8_t sides,
                                 std::uint8_t sectors_per_track) noexcept {
            if ((tracks != 40U && tracks != 80U) || (sides != 1U && sides != 2U) ||
                (sectors_per_track != 8U && sectors_per_track != 9U)) {
                return std::nullopt;
            }
            const std::size_t expected =
                static_cast<std::size_t>(tracks) * sides * sectors_per_track;
            if (expected != sector_count) {
                return std::nullopt;
            }
            return wd1793::dsk_geometry{.tracks = tracks,
                                        .sides = sides,
                                        .sectors_per_track = sectors_per_track,
                                        .bytes_per_sector =
                                            static_cast<std::uint16_t>(wd1793::sector_size)};
        }

        [[nodiscard]] std::optional<wd1793::dsk_geometry>
        infer_msx_bpb_geometry(std::span<const std::uint8_t> image,
                               std::size_t sector_count) noexcept {
            if (image.size() < wd1793::sector_size || le16(image, 0x0BU) != wd1793::sector_size) {
                return std::nullopt;
            }
            const std::uint16_t total_sectors = le16(image, 0x13U);
            if (total_sectors != 0U && total_sectors != sector_count) {
                return std::nullopt;
            }

            const std::uint16_t bpb_spt = le16(image, 0x18U);
            const std::uint16_t bpb_heads = le16(image, 0x1AU);
            if (bpb_spt != 0U && bpb_heads != 0U && bpb_spt <= 0xFFU && bpb_heads <= 0xFFU) {
                if (auto geometry = geometry_from_dimensions(
                        sector_count,
                        static_cast<std::uint16_t>(sector_count / (bpb_spt * bpb_heads)),
                        static_cast<std::uint8_t>(bpb_heads), static_cast<std::uint8_t>(bpb_spt))) {
                    return geometry;
                }
            }

            switch (image[0x15U]) {
            case 0xF8U:
                return geometry_from_dimensions(sector_count, 80U, 1U, 9U);
            case 0xF9U:
                return geometry_from_dimensions(sector_count, 80U, 2U, 9U);
            case 0xFAU:
                return geometry_from_dimensions(sector_count, 80U, 1U, 8U);
            case 0xFBU:
                return geometry_from_dimensions(sector_count, 80U, 2U, 8U);
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] bool crc_field_mark(std::uint8_t value) noexcept {
            return value == k_mfm_id_mark || value == k_mfm_data_mark ||
                   value == k_mfm_deleted_data_mark;
        }

        [[nodiscard]] std::vector<std::uint8_t>
        canonicalize_write_track_stream(std::span<const std::uint8_t> stream) {
            std::vector<std::uint8_t> out;
            out.reserve(wd1793::standard_mfm_track_size);
            std::array<std::uint8_t, 3U> previous{};
            std::size_t previous_count = 0U;
            std::optional<std::uint16_t> active_crc{};

            const auto shift_previous = [&](std::uint8_t value) noexcept {
                previous[0] = previous[1];
                previous[1] = previous[2];
                previous[2] = value;
                previous_count = std::min<std::size_t>(previous_count + 1U, previous.size());
            };
            const auto previous_is_sync = [&]() noexcept {
                return previous_count == previous.size() && readable_mfm_sync_byte(previous[0]) &&
                       readable_mfm_sync_byte(previous[1]) && readable_mfm_sync_byte(previous[2]);
            };
            const auto append_readable = [&](std::uint8_t value) {
                if (out.size() >= wd1793::standard_mfm_track_size) {
                    return;
                }
                out.push_back(value);
                if (crc_field_mark(value) && previous_is_sync()) {
                    std::uint16_t crc = 0xFFFFU;
                    crc = crc16_ccitt_update(crc, previous[0]);
                    crc = crc16_ccitt_update(crc, previous[1]);
                    crc = crc16_ccitt_update(crc, previous[2]);
                    active_crc = crc16_ccitt_update(crc, value);
                } else if (active_crc.has_value()) {
                    active_crc = crc16_ccitt_update(*active_crc, value);
                }
                shift_previous(value);
            };
            const auto append_generated_crc = [&](std::uint16_t crc) {
                if (out.size() >= wd1793::standard_mfm_track_size) {
                    return;
                }
                const std::uint8_t high = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
                out.push_back(high);
                shift_previous(high);
                if (out.size() < wd1793::standard_mfm_track_size) {
                    const std::uint8_t low = static_cast<std::uint8_t>(crc & 0xFFU);
                    out.push_back(low);
                    shift_previous(low);
                }
            };

            for (const std::uint8_t value : stream) {
                if (out.size() >= wd1793::standard_mfm_track_size) {
                    break;
                }

                if (value == 0xF5U) {
                    append_readable(k_mfm_sync_mark);
                    continue;
                }
                if (value == 0xF6U) {
                    append_readable(k_mfm_index_sync_mark);
                    continue;
                }
                if (value == k_mfm_crc_mark) {
                    append_generated_crc(active_crc.value_or(0U));
                    active_crc.reset();
                    continue;
                }

                append_readable(value);
            }

            if (out.size() < wd1793::standard_mfm_track_size) {
                out.insert(out.end(), wd1793::standard_mfm_track_size - out.size(), k_mfm_gap);
            }
            return out;
        }
    } // namespace

    std::optional<wd1793::dsk_geometry>
    wd1793::infer_dsk_geometry(std::span<const std::uint8_t> image) noexcept {
        if (image.empty() || (image.size() % sector_size) != 0U) {
            return std::nullopt;
        }
        const std::size_t sector_count = image.size() / sector_size;
        if (auto bpb_geometry = infer_msx_bpb_geometry(image, sector_count)) {
            return bpb_geometry;
        }
        if (sector_count == 80U * 1U * 8U) {
            return dsk_geometry{.tracks = 80U,
                                .sides = 1U,
                                .sectors_per_track = 8U,
                                .bytes_per_sector = static_cast<std::uint16_t>(sector_size)};
        }
        if (sector_count == 80U * 2U * 8U) {
            return dsk_geometry{.tracks = 80U,
                                .sides = 2U,
                                .sectors_per_track = 8U,
                                .bytes_per_sector = static_cast<std::uint16_t>(sector_size)};
        }
        if ((sector_count % standard_sectors_per_track) != 0U) {
            return std::nullopt;
        }
        const std::size_t track_side_count = sector_count / standard_sectors_per_track;
        if (track_side_count == 40U) {
            return dsk_geometry{.tracks = 40U,
                                .sides = 1U,
                                .sectors_per_track = standard_sectors_per_track,
                                .bytes_per_sector = static_cast<std::uint16_t>(sector_size)};
        }
        if (track_side_count == 80U) {
            return dsk_geometry{.tracks = 40U,
                                .sides = 2U,
                                .sectors_per_track = standard_sectors_per_track,
                                .bytes_per_sector = static_cast<std::uint16_t>(sector_size)};
        }
        if (track_side_count == 160U) {
            return dsk_geometry{.tracks = 80U,
                                .sides = 2U,
                                .sectors_per_track = standard_sectors_per_track,
                                .bytes_per_sector = static_cast<std::uint16_t>(sector_size)};
        }
        return std::nullopt;
    }

    bool wd1793::is_supported_dsk(std::span<const std::uint8_t> image) noexcept {
        return infer_dsk_geometry(image).has_value();
    }

    chip_metadata wd1793::metadata() const noexcept {
        return {
            .manufacturer = "Western Digital",
            .part_number = "FD1793",
            .family = "floppy_disk_controller",
            .klass = chip_class::storage,
            .revision = 1U,
        };
    }

    void wd1793::tick(std::uint64_t cycles) {
        if (cycles == 0U) {
            return;
        }

        if (mounted()) {
            index_cycle_ = (index_cycle_ + cycles) % wd1793::nominal_index_revolution_cycles;
        }

        if (!busy_) {
            return;
        }

        if (drq_) {
            drq_age_cycles_ =
                std::min<std::uint64_t>(drq_age_cycles_ + cycles, k_drq_lost_data_cycles);
            if (drq_age_cycles_ >= k_drq_lost_data_cycles) {
                transfer_completion_status_ |= status_lost_data;
            }
            return;
        }

        advance_transfer_clock(cycles);
    }

    void wd1793::reset(reset_kind /*kind*/) {
        command_ = 0U;
        status_latch_ = 0U;
        track_ = 0U;
        head_track_ = 0U;
        sector_ = 1U;
        data_ = 0U;
        selected_drive_ = 0U;
        selected_side_ = 0U;
        busy_ = false;
        drq_ = false;
        intrq_ = false;
        write_transfer_ = false;
        multi_sector_ = false;
        write_track_transfer_ = false;
        type_i_status_ = true;
        last_step_delta_ = 1;
        index_cycle_ = 0U;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
        clear_transfer();
    }

    bool wd1793::mount_dsk(std::span<const std::uint8_t> image, bool write_protected) {
        const auto inferred = infer_dsk_geometry(image);
        if (!inferred) {
            return false;
        }
        geometry_ = *inferred;
        disk_.assign(image.begin(), image.end());
        deleted_data_marks_.assign(expected_sector_count(geometry_), 0U);
        raw_track_valid_.assign(static_cast<std::size_t>(geometry_.tracks) * geometry_.sides, 0U);
        raw_track_streams_.assign(raw_track_valid_.size() * standard_mfm_track_size, 0U);
        reset(reset_kind::power_on);
        // Set after reset: write-protect is a mounted-media property.
        write_protected_ = write_protected;
        transfer_.reserve(standard_mfm_track_size);
        return true;
    }

    void wd1793::eject() noexcept {
        disk_.clear();
        geometry_ = {};
        deleted_data_marks_.clear();
        raw_track_valid_.clear();
        raw_track_streams_.clear();
        write_protected_ = false;
        clear_transfer();
        busy_ = false;
        drq_ = false;
        intrq_ = false;
        status_latch_ = status_not_ready;
        index_cycle_ = 0U;
    }

    bool wd1793::ready() const noexcept { return mounted() && selected_drive_ == 0U; }

    bool wd1793::index_pulse_active() const noexcept {
        return ready() && index_cycle_ < nominal_index_pulse_cycles;
    }

    std::size_t wd1793::expected_size(dsk_geometry geometry) noexcept {
        return expected_sector_count(geometry) * geometry.bytes_per_sector;
    }

    std::size_t wd1793::expected_sector_count(dsk_geometry geometry) noexcept {
        return static_cast<std::size_t>(geometry.tracks) * geometry.sides *
               geometry.sectors_per_track;
    }

    std::uint8_t wd1793::composed_status() const noexcept {
        std::uint8_t status = status_latch_;
        if (busy_) {
            status |= status_busy;
        }
        if (type_i_status_ && index_pulse_active()) {
            status |= status_index;
        }
        if (!type_i_status_ && drq_) {
            status |= status_drq;
        }
        if (!type_i_status_) {
            status |= transfer_completion_status_;
        }
        if (type_i_status_ && head_track_ == 0U) {
            status |= status_track0;
        }
        if (type_i_status_ && write_protected_) {
            status |= status_write_protect;
        }
        if (!ready()) {
            status |= status_not_ready;
        }
        return status;
    }

    bool wd1793::sector_offset(std::size_t& offset) const noexcept {
        const bool compare_side = (command_ & 0x02U) != 0U;
        const std::uint8_t command_side = static_cast<std::uint8_t>((command_ >> 3U) & 0x01U);
        if (!ready() || geometry_.bytes_per_sector != sector_size || geometry_.tracks == 0U ||
            geometry_.sides == 0U || geometry_.sectors_per_track == 0U || sector_ == 0U ||
            head_track_ >= geometry_.tracks || track_ != head_track_ ||
            selected_side_ >= geometry_.sides || sector_ > geometry_.sectors_per_track ||
            (compare_side && command_side != selected_side_)) {
            return false;
        }
        const std::size_t zero_based_sector = static_cast<std::size_t>(sector_ - 1U);
        offset = (((static_cast<std::size_t>(head_track_) * geometry_.sides) + selected_side_) *
                      geometry_.sectors_per_track +
                  zero_based_sector) *
                 sector_size;
        return offset + sector_size <= disk_.size();
    }

    bool wd1793::sector_index_for_offset(std::size_t offset, std::size_t& index) const noexcept {
        if (geometry_.bytes_per_sector != sector_size || (offset % sector_size) != 0U) {
            return false;
        }
        const std::size_t sector = offset / sector_size;
        if (sector >= expected_sector_count(geometry_)) {
            return false;
        }
        index = sector;
        return true;
    }

    bool wd1793::deleted_data_mark_for_offset(std::size_t offset) const noexcept {
        std::size_t index = 0U;
        return sector_index_for_offset(offset, index) && index < deleted_data_marks_.size() &&
               deleted_data_marks_[index] != 0U;
    }

    bool wd1793::track_side_index(std::uint8_t track, std::uint8_t side,
                                  std::size_t& index) const noexcept {
        if (geometry_.tracks == 0U || geometry_.sides == 0U || track >= geometry_.tracks ||
            side >= geometry_.sides) {
            return false;
        }
        index = (static_cast<std::size_t>(track) * geometry_.sides) + side;
        return index < raw_track_valid_.size() &&
               ((index + 1U) * standard_mfm_track_size) <= raw_track_streams_.size();
    }

    void wd1793::set_deleted_data_mark_for_offset(std::size_t offset, bool deleted) noexcept {
        std::size_t index = 0U;
        if (sector_index_for_offset(offset, index) && index < deleted_data_marks_.size()) {
            deleted_data_marks_[index] = deleted ? std::uint8_t{1U} : std::uint8_t{0U};
        }
    }

    void wd1793::clear_raw_track_for_offset(std::size_t offset) noexcept {
        if (geometry_.bytes_per_sector != sector_size || geometry_.sectors_per_track == 0U ||
            geometry_.sides == 0U || (offset % sector_size) != 0U) {
            return;
        }

        const std::size_t sector_index = offset / sector_size;
        const std::size_t sectors_per_track_side =
            static_cast<std::size_t>(geometry_.sectors_per_track);
        if (sectors_per_track_side == 0U) {
            return;
        }

        const std::size_t track_side = sector_index / sectors_per_track_side;
        if (track_side < raw_track_valid_.size()) {
            raw_track_valid_[track_side] = 0U;
        }
    }

    void wd1793::store_raw_track_for_current_head() {
        std::size_t index = 0U;
        if (!track_side_index(head_track_, selected_side_, index) ||
            transfer_size_ > transfer_.size()) {
            return;
        }

        const std::span<const std::uint8_t> track_bytes{transfer_.data(), transfer_size_};
        const std::vector<std::uint8_t> raw = canonicalize_write_track_stream(track_bytes);
        const std::size_t begin = index * standard_mfm_track_size;
        std::copy(raw.begin(), raw.end(),
                  raw_track_streams_.begin() + static_cast<std::ptrdiff_t>(begin));
        raw_track_valid_[index] = 1U;
    }

    void wd1793::clear_transfer() noexcept {
        transfer_.clear();
        transfer_index_ = 0U;
        transfer_size_ = 0U;
        transfer_disk_offset_ = 0U;
        transfer_completion_status_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        write_track_transfer_ = false;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
    }

    void wd1793::complete_transfer() noexcept {
        const std::uint8_t completion_status = transfer_completion_status_;
        busy_ = false;
        drq_ = false;
        intrq_ = true;
        clear_transfer();
        status_latch_ = completion_status;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
    }

    void wd1793::fail_command(std::uint8_t status_bits) noexcept {
        clear_transfer();
        busy_ = false;
        drq_ = false;
        intrq_ = true;
        status_latch_ = status_bits;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
    }

    void wd1793::finish_type_i(std::uint8_t status_bits) noexcept {
        type_i_status_ = true;
        fail_command(status_bits);
    }

    void wd1793::begin_read_sector() noexcept {
        type_i_status_ = false;
        std::size_t offset = 0U;
        if (!sector_offset(offset)) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }
        transfer_.assign(disk_.begin() + static_cast<std::ptrdiff_t>(offset),
                         disk_.begin() + static_cast<std::ptrdiff_t>(offset + sector_size));
        transfer_disk_offset_ = offset;
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        write_transfer_ = false;
        const std::uint8_t sticky_status =
            busy_ && multi_sector_
                ? static_cast<std::uint8_t>(transfer_completion_status_ & status_lost_data)
                : std::uint8_t{0U};
        transfer_completion_status_ = static_cast<std::uint8_t>(
            sticky_status |
            (deleted_data_mark_for_offset(offset) ? status_record_type : std::uint8_t{0U}));
        busy_ = true;
        intrq_ = false;
        status_latch_ = 0U;
        arm_transfer_byte();
    }

    void wd1793::finish_sector_transfer() noexcept {
        if (write_track_transfer_) {
            commit_write_track();
            complete_transfer();
            return;
        }

        if (write_transfer_ && transfer_disk_offset_ + transfer_size_ <= disk_.size()) {
            std::copy_n(transfer_.begin(), transfer_size_,
                        disk_.begin() + static_cast<std::ptrdiff_t>(transfer_disk_offset_));
            set_deleted_data_mark_for_offset(transfer_disk_offset_, (command_ & 0x01U) != 0U);
            clear_raw_track_for_offset(transfer_disk_offset_);
        }

        if (!multi_sector_) {
            complete_transfer();
            return;
        }

        if (sector_ < geometry_.sectors_per_track) {
            ++sector_;
            if (write_transfer_) {
                begin_write_sector();
            } else {
                begin_read_sector();
            }
            return;
        }

        ++sector_;
        fail_command(status_record_not_found);
    }

    void wd1793::begin_write_sector() noexcept {
        type_i_status_ = false;
        if (write_protected_) {
            fail_command(status_write_protect);
            return;
        }
        std::size_t offset = 0U;
        if (!sector_offset(offset)) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }
        transfer_.assign(sector_size, std::uint8_t{0});
        transfer_disk_offset_ = offset;
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        write_transfer_ = true;
        transfer_completion_status_ =
            busy_ && multi_sector_
                ? static_cast<std::uint8_t>(transfer_completion_status_ & status_lost_data)
                : std::uint8_t{0U};
        busy_ = true;
        intrq_ = false;
        status_latch_ = 0U;
        arm_transfer_byte();
    }

    void wd1793::begin_read_address() noexcept {
        type_i_status_ = false;
        if (!ready() || geometry_.tracks == 0U || geometry_.sides == 0U ||
            geometry_.sectors_per_track == 0U || head_track_ >= geometry_.tracks ||
            selected_side_ >= geometry_.sides) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }

        const std::uint8_t id_sector =
            sector_ == 0U || sector_ > geometry_.sectors_per_track ? 1U : sector_;
        transfer_.assign(6U, std::uint8_t{0});
        transfer_[0] = head_track_;
        transfer_[1] = selected_side_;
        transfer_[2] = id_sector;
        transfer_[3] = 0x02U; // 512-byte sectors.
        const std::array<std::uint8_t, 8U> id_crc_bytes{
            k_mfm_sync_mark, k_mfm_sync_mark, k_mfm_sync_mark, k_mfm_id_mark,
            head_track_,     selected_side_,  id_sector,       k_mfm_sector_length_512,
        };
        const std::uint16_t crc = crc16_ccitt(id_crc_bytes);
        transfer_[4] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
        transfer_[5] = static_cast<std::uint8_t>(crc & 0xFFU);
        sector_ = head_track_;
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        transfer_disk_offset_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        transfer_completion_status_ = 0U;
        busy_ = true;
        intrq_ = false;
        status_latch_ = 0U;
        arm_transfer_byte();
    }

    void wd1793::begin_write_track() noexcept {
        type_i_status_ = false;
        if (write_protected_) {
            fail_command(status_write_protect);
            return;
        }
        if (!ready() || geometry_.bytes_per_sector != sector_size || geometry_.tracks == 0U ||
            geometry_.sides == 0U || geometry_.sectors_per_track == 0U ||
            head_track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }

        transfer_.assign(standard_mfm_track_size, std::uint8_t{0});
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        transfer_disk_offset_ = 0U;
        write_transfer_ = true;
        write_track_transfer_ = true;
        multi_sector_ = false;
        transfer_completion_status_ = 0U;
        busy_ = true;
        intrq_ = false;
        status_latch_ = 0U;
        arm_transfer_byte();
    }

    void wd1793::append_transfer(std::uint8_t value, std::size_t count) {
        transfer_.insert(transfer_.end(), count, value);
    }

    void wd1793::build_mfm_track_image() {
        transfer_.clear();
        transfer_.reserve(standard_mfm_track_size);

        std::size_t track_index = 0U;
        if (track_side_index(head_track_, selected_side_, track_index) &&
            raw_track_valid_[track_index] != 0U) {
            const std::size_t begin = track_index * standard_mfm_track_size;
            transfer_.assign(raw_track_streams_.begin() + static_cast<std::ptrdiff_t>(begin),
                             raw_track_streams_.begin() +
                                 static_cast<std::ptrdiff_t>(begin + standard_mfm_track_size));
            return;
        }

        append_transfer(k_mfm_gap, 80U);
        append_transfer(k_mfm_sync_pad, 12U);
        append_transfer(k_mfm_sync_mark, 3U);
        transfer_.push_back(k_mfm_index_mark);
        append_transfer(k_mfm_gap, 50U);

        for (std::uint8_t sector = 1U; sector <= geometry_.sectors_per_track; ++sector) {
            append_transfer(k_mfm_sync_pad, 12U);
            append_transfer(k_mfm_sync_mark, 3U);

            const std::array<std::uint8_t, 8U> id_crc_bytes{
                k_mfm_sync_mark, k_mfm_sync_mark, k_mfm_sync_mark, k_mfm_id_mark,
                head_track_,     selected_side_,  sector,          k_mfm_sector_length_512,
            };
            transfer_.push_back(k_mfm_id_mark);
            transfer_.push_back(head_track_);
            transfer_.push_back(selected_side_);
            transfer_.push_back(sector);
            transfer_.push_back(k_mfm_sector_length_512);
            append_crc(transfer_, crc16_ccitt(id_crc_bytes));

            append_transfer(k_mfm_gap, 22U);
            append_transfer(k_mfm_sync_pad, 12U);
            const std::size_t zero_based_sector = static_cast<std::size_t>(sector - 1U);
            const std::size_t sector_offset =
                (((static_cast<std::size_t>(head_track_) * geometry_.sides) + selected_side_) *
                     geometry_.sectors_per_track +
                 zero_based_sector) *
                sector_size;
            const std::uint8_t data_mark = deleted_data_mark_for_offset(sector_offset)
                                               ? k_mfm_deleted_data_mark
                                               : k_mfm_data_mark;
            append_transfer(k_mfm_sync_mark, 3U);
            transfer_.push_back(data_mark);

            std::uint16_t data_crc = 0xFFFFU;
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, data_mark);
            for (std::size_t i = 0U; i < sector_size; ++i) {
                const std::uint8_t value = disk_[sector_offset + i];
                transfer_.push_back(value);
                data_crc = crc16_ccitt_update(data_crc, value);
            }
            append_crc(transfer_, data_crc);
            append_transfer(k_mfm_gap, 84U);
        }

        if (transfer_.size() < standard_mfm_track_size) {
            append_transfer(k_mfm_gap, standard_mfm_track_size - transfer_.size());
        }
    }

    void wd1793::rotate_transfer_to_index_phase() noexcept {
        if (transfer_.empty()) {
            return;
        }

        const std::size_t byte_phase =
            static_cast<std::size_t>((index_cycle_ * static_cast<std::uint64_t>(transfer_.size())) /
                                     nominal_index_revolution_cycles);
        if (byte_phase == 0U || byte_phase >= transfer_.size()) {
            return;
        }

        std::rotate(transfer_.begin(), transfer_.begin() + static_cast<std::ptrdiff_t>(byte_phase),
                    transfer_.end());
    }

    void wd1793::begin_read_track() noexcept {
        type_i_status_ = false;
        if (!ready() || geometry_.bytes_per_sector != sector_size || geometry_.tracks == 0U ||
            geometry_.sides == 0U || geometry_.sectors_per_track == 0U ||
            head_track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }

        build_mfm_track_image();
        rotate_transfer_to_index_phase();
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        transfer_disk_offset_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        transfer_completion_status_ = 0U;
        busy_ = true;
        intrq_ = false;
        status_latch_ = 0U;
        arm_transfer_byte();
    }

    void wd1793::commit_write_track() noexcept {
        if (geometry_.bytes_per_sector != sector_size || geometry_.sectors_per_track == 0U ||
            transfer_size_ > transfer_.size()) {
            return;
        }

        store_raw_track_for_current_head();

        std::uint8_t id_track = head_track_;
        std::uint8_t id_side = selected_side_;
        std::uint8_t id_sector = 1U;
        std::uint8_t id_length = k_mfm_sector_length_512;
        bool have_id = false;

        for (std::size_t i = 0U; i < transfer_size_; ++i) {
            const std::uint8_t value = transfer_[i];
            if (value == k_mfm_id_mark && i + 4U < transfer_size_ &&
                has_mfm_sync_preamble(transfer_, i)) {
                const std::size_t id_crc_mark = i + 5U;
                if (id_crc_mark < transfer_size_ && transfer_[id_crc_mark] == k_mfm_crc_mark) {
                    id_track = transfer_[i + 1U];
                    id_side = transfer_[i + 2U];
                    id_sector = transfer_[i + 3U];
                    id_length = transfer_[i + 4U];
                    have_id = true;
                    i = id_crc_mark;
                } else {
                    have_id = false;
                    i += 4U;
                }
                continue;
            }

            if ((value == k_mfm_data_mark || value == k_mfm_deleted_data_mark) && have_id &&
                has_mfm_sync_preamble(transfer_, i)) {
                const std::size_t data_size = sector_length_bytes(id_length);
                const std::size_t data_begin = i + 1U;
                const std::size_t data_crc_mark = data_begin + data_size;
                const bool has_crc_mark =
                    data_crc_mark < transfer_size_ && transfer_[data_crc_mark] == k_mfm_crc_mark;
                if (data_size == sector_size && has_crc_mark &&
                    has_literal_write_track_payload(transfer_, data_begin, data_size) &&
                    id_track == head_track_ && id_side == selected_side_ && id_sector != 0U &&
                    id_sector <= geometry_.sectors_per_track) {
                    const std::size_t zero_based_sector = static_cast<std::size_t>(id_sector - 1U);
                    const std::size_t disk_offset =
                        (((static_cast<std::size_t>(head_track_) * geometry_.sides) +
                          selected_side_) *
                             geometry_.sectors_per_track +
                         zero_based_sector) *
                        sector_size;
                    if (disk_offset + sector_size <= disk_.size()) {
                        std::copy_n(transfer_.begin() + static_cast<std::ptrdiff_t>(data_begin),
                                    sector_size,
                                    disk_.begin() + static_cast<std::ptrdiff_t>(disk_offset));
                        set_deleted_data_mark_for_offset(disk_offset,
                                                         value == k_mfm_deleted_data_mark);
                    }
                }
                have_id = false;
                if (data_size != 0U) {
                    i = has_crc_mark ? data_crc_mark : (data_begin + data_size - 1U);
                }
            }
        }
    }

    void wd1793::step_head(int delta) noexcept {
        const int next = static_cast<int>(head_track_) + delta;
        head_track_ = static_cast<std::uint8_t>(std::clamp(next, 0, 255));
    }

    void wd1793::step_track(int delta) noexcept {
        const int next = static_cast<int>(track_) + delta;
        track_ = static_cast<std::uint8_t>(std::clamp(next, 0, 255));
    }

    void wd1793::step_head_and_maybe_track(int delta, bool update_track) noexcept {
        step_head(delta);
        if (update_track) {
            step_track(delta);
        }
    }

    void wd1793::advance_transfer_clock(std::uint64_t cycles) noexcept {
        if (cycles == 0U || !busy_ || drq_ || transfer_index_ >= transfer_size_) {
            return;
        }

        constexpr std::uint64_t units_per_cycle =
            static_cast<std::uint64_t>(standard_mfm_track_size);
        constexpr std::uint64_t units_per_byte = nominal_index_revolution_cycles;
        transfer_byte_cycle_accum_ %= units_per_byte;
        const std::uint64_t remaining_units = units_per_byte - transfer_byte_cycle_accum_;
        const std::uint64_t cycles_to_next =
            (remaining_units + units_per_cycle - 1U) / units_per_cycle;
        if (cycles >= cycles_to_next) {
            arm_transfer_byte();
            return;
        }

        transfer_byte_cycle_accum_ += cycles * units_per_cycle;
    }

    void wd1793::arm_transfer_byte() noexcept {
        drq_ = transfer_index_ < transfer_size_;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
    }

    void wd1793::service_transfer_byte() noexcept {
        drq_ = false;
        drq_age_cycles_ = 0U;
        transfer_byte_cycle_accum_ = 0U;
        if (transfer_index_ >= transfer_size_) {
            finish_sector_transfer();
        }
    }

    std::uint8_t wd1793::read_register(std::uint8_t index) noexcept {
        switch (index & 0x03U) {
        case 0U: {
            const std::uint8_t status = composed_status();
            intrq_ = false;
            return status;
        }
        case 1U:
            return track_;
        case 2U:
            return sector_;
        case 3U:
            if (drq_ && !write_transfer_ && transfer_index_ < transfer_size_) {
                data_ = transfer_[transfer_index_++];
                service_transfer_byte();
            }
            return data_;
        default:
            return 0xFFU;
        }
    }

    void wd1793::write_register(std::uint8_t index, std::uint8_t value) noexcept {
        switch (index & 0x03U) {
        case 0U: {
            command_ = value;
            const std::uint8_t command_class = static_cast<std::uint8_t>(value & 0xF0U);
            if (command_class == 0x00U) {
                track_ = 0U;
                head_track_ = 0U;
                data_ = 0U;
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x10U) {
                track_ = data_;
                head_track_ = data_;
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x20U || command_class == 0x30U) {
                step_head_and_maybe_track(last_step_delta_, (value & 0x10U) != 0U);
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x40U || command_class == 0x50U) {
                last_step_delta_ = 1;
                step_head_and_maybe_track(last_step_delta_, (value & 0x10U) != 0U);
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x60U || command_class == 0x70U) {
                last_step_delta_ = -1;
                step_head_and_maybe_track(last_step_delta_, (value & 0x10U) != 0U);
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x80U || command_class == 0x90U) {
                multi_sector_ = (value & 0x10U) != 0U;
                begin_read_sector();
            } else if (command_class == 0xA0U || command_class == 0xB0U) {
                multi_sector_ = (value & 0x10U) != 0U;
                begin_write_sector();
            } else if (command_class == 0xC0U) {
                begin_read_address();
            } else if (command_class == 0xD0U) {
                type_i_status_ = false;
                clear_transfer();
                busy_ = false;
                drq_ = false;
                intrq_ = (value & 0x08U) != 0U;
                status_latch_ = 0U;
            } else if (command_class == 0xE0U) {
                begin_read_track();
            } else if (command_class == 0xF0U) {
                begin_write_track();
            } else {
                type_i_status_ = false;
                fail_command(status_record_not_found);
            }
            break;
        }
        case 1U:
            track_ = value;
            head_track_ = value;
            break;
        case 2U:
            sector_ = value;
            break;
        case 3U:
            data_ = value;
            if (drq_ && write_transfer_ && transfer_index_ < transfer_size_) {
                transfer_[transfer_index_++] = value;
                service_transfer_byte();
            }
            break;
        default:
            break;
        }
    }

    std::uint8_t wd1793::read_memory_register(std::uint8_t offset) noexcept {
        switch (offset & 0x07U) {
        case 0U:
            return read_register(0U);
        case 1U:
            return read_register(1U);
        case 2U:
            return read_register(2U);
        case 3U:
            return read_register(3U);
        case 4U:
            return selected_side_;
        case 5U:
            return selected_drive_;
        case 7U:
            return read_control_register();
        default:
            return 0xFFU;
        }
    }

    void wd1793::write_memory_register(std::uint8_t offset, std::uint8_t value) noexcept {
        switch (offset & 0x07U) {
        case 0U:
            write_register(0U, value);
            break;
        case 1U:
            write_register(1U, value);
            break;
        case 2U:
            write_register(2U, value);
            break;
        case 3U:
            write_register(3U, value);
            break;
        case 4U:
            selected_side_ = static_cast<std::uint8_t>(value & 0x01U);
            break;
        case 5U:
            selected_drive_ = static_cast<std::uint8_t>(value & 0x01U);
            break;
        default:
            break;
        }
    }

    std::uint8_t wd1793::read_control_register() const noexcept {
        std::uint8_t value = 0x3FU;
        if (!busy_ || intrq_) {
            value |= 0x40U;
        }
        if (drq_) {
            value |= 0x80U;
        }
        return value;
    }

    void wd1793::write_control_register(std::uint8_t value) noexcept {
        if ((value & 0x02U) != 0U) {
            selected_drive_ = 1U;
        } else {
            selected_drive_ = 0U;
        }
        selected_side_ = static_cast<std::uint8_t>((value >> 4U) & 0x01U);
    }

    void wd1793::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u16(geometry_.tracks);
        writer.u8(geometry_.sides);
        writer.u8(geometry_.sectors_per_track);
        writer.u16(geometry_.bytes_per_sector);
        writer.blob(disk_);
        writer.blob(transfer_);
        writer.u64(static_cast<std::uint64_t>(transfer_index_));
        writer.u64(static_cast<std::uint64_t>(transfer_size_));
        writer.u64(static_cast<std::uint64_t>(transfer_disk_offset_));
        writer.u8(command_);
        writer.u8(status_latch_);
        writer.u8(track_);
        writer.u8(head_track_);
        writer.u8(sector_);
        writer.u8(data_);
        writer.u8(selected_drive_);
        writer.u8(selected_side_);
        writer.boolean(busy_);
        writer.boolean(drq_);
        writer.boolean(intrq_);
        writer.boolean(write_transfer_);
        writer.boolean(multi_sector_);
        writer.boolean(type_i_status_);
        writer.boolean(write_track_transfer_);
        writer.boolean(write_protected_);
        writer.u8(last_step_delta_ < 0 ? 1U : 0U);
        writer.u64(index_cycle_);
        writer.blob(deleted_data_marks_);
        writer.u8(transfer_completion_status_);
        writer.blob(raw_track_valid_);
        writer.blob(raw_track_streams_);
        writer.u64(drq_age_cycles_);
        writer.u64(transfer_byte_cycle_accum_);
    }

    void wd1793::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version < 1U || version > k_state_version) {
            reader.fail();
            return;
        }
        geometry_.tracks = reader.u16();
        geometry_.sides = reader.u8();
        geometry_.sectors_per_track = reader.u8();
        geometry_.bytes_per_sector = reader.u16();
        disk_ = reader.blob();
        if (version >= 3U) {
            transfer_ = reader.blob();
        } else {
            std::array<std::uint8_t, sector_size> fixed_transfer{};
            reader.bytes(fixed_transfer);
            transfer_.assign(fixed_transfer.begin(), fixed_transfer.end());
        }
        transfer_index_ = static_cast<std::size_t>(reader.u64());
        transfer_size_ = static_cast<std::size_t>(reader.u64());
        transfer_disk_offset_ = static_cast<std::size_t>(reader.u64());
        command_ = reader.u8();
        status_latch_ = reader.u8();
        track_ = reader.u8();
        head_track_ = version >= 7U ? reader.u8() : track_;
        sector_ = reader.u8();
        data_ = reader.u8();
        selected_drive_ = reader.u8();
        selected_side_ = reader.u8();
        busy_ = reader.boolean();
        drq_ = reader.boolean();
        intrq_ = reader.boolean();
        write_transfer_ = reader.boolean();
        multi_sector_ = version >= 2U ? reader.boolean() : false;
        type_i_status_ = reader.boolean();
        write_track_transfer_ = version >= 4U ? reader.boolean() : false;
        write_protected_ = version >= 5U ? reader.boolean() : false;
        last_step_delta_ = version >= 6U && reader.u8() != 0U ? -1 : 1;
        index_cycle_ = version >= 11U ? reader.u64() : 0U;
        if (version >= 8U) {
            deleted_data_marks_ = reader.blob();
            transfer_completion_status_ = static_cast<std::uint8_t>(
                reader.u8() & static_cast<std::uint8_t>(status_record_type | status_lost_data));
        } else {
            deleted_data_marks_.clear();
            transfer_completion_status_ = 0U;
        }
        if (version >= 9U) {
            raw_track_valid_ = reader.blob();
            raw_track_streams_ = reader.blob();
        } else {
            raw_track_valid_.clear();
            raw_track_streams_.clear();
        }
        drq_age_cycles_ = version >= 10U ? reader.u64() : 0U;
        transfer_byte_cycle_accum_ = version >= 12U ? reader.u64() : 0U;

        if (!reader.ok()) {
            return;
        }
        if (disk_.empty()) {
            geometry_ = {};
            deleted_data_marks_.clear();
            raw_track_valid_.clear();
            raw_track_streams_.clear();
        } else if (geometry_.bytes_per_sector != sector_size ||
                   expected_size(geometry_) != disk_.size()) {
            reader.fail();
            return;
        } else {
            const std::size_t sectors = expected_sector_count(geometry_);
            const std::size_t track_sides =
                static_cast<std::size_t>(geometry_.tracks) * geometry_.sides;
            if (version < 8U) {
                deleted_data_marks_.assign(sectors, 0U);
            } else if (deleted_data_marks_.size() != sectors) {
                reader.fail();
                return;
            }
            if (version < 9U) {
                raw_track_valid_.assign(track_sides, 0U);
                raw_track_streams_.assign(track_sides * standard_mfm_track_size, 0U);
            } else if (raw_track_valid_.size() != track_sides ||
                       raw_track_streams_.size() != track_sides * standard_mfm_track_size) {
                reader.fail();
                return;
            }
        }
        if (transfer_size_ > transfer_.size() || transfer_index_ > transfer_size_ ||
            (write_transfer_ && transfer_size_ != 0U &&
             transfer_disk_offset_ + transfer_size_ > disk_.size())) {
            reader.fail();
        }
        if (reader.ok() && mounted()) {
            transfer_.reserve(standard_mfm_track_size);
        }
        if (!mounted()) {
            index_cycle_ = 0U;
        } else {
            index_cycle_ %= nominal_index_revolution_cycles;
        }
        if (!busy_ || !drq_) {
            drq_age_cycles_ = 0U;
        } else {
            drq_age_cycles_ = std::min<std::uint64_t>(drq_age_cycles_, k_drq_lost_data_cycles);
        }
        if (!busy_ || drq_ || transfer_index_ >= transfer_size_) {
            transfer_byte_cycle_accum_ = 0U;
        } else {
            transfer_byte_cycle_accum_ %= nominal_index_revolution_cycles;
        }
    }

    instrumentation::ichip_introspection& wd1793::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto wd1793_registration =
            register_factory("wd1793", chip_class::storage,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<wd1793>(); });
    } // namespace

} // namespace mnemos::chips::storage
