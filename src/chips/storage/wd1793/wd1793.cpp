#include "wd1793.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>

namespace mnemos::chips::storage {

    namespace {
        constexpr std::array<wd1793::disk_geometry, 8> k_supported_geometries{{
            {.tracks = 80U, .sides = 2U, .sectors_per_track = 9U}, // 720 KiB 2DD
            {.tracks = 80U, .sides = 2U, .sectors_per_track = 8U}, // 640 KiB
            {.tracks = 80U, .sides = 1U, .sectors_per_track = 9U}, // 360 KiB
            {.tracks = 80U, .sides = 1U, .sectors_per_track = 8U}, // 320 KiB
            {.tracks = 40U, .sides = 2U, .sectors_per_track = 9U},
            {.tracks = 40U, .sides = 2U, .sectors_per_track = 8U},
            {.tracks = 40U, .sides = 1U, .sectors_per_track = 9U},
            {.tracks = 40U, .sides = 1U, .sectors_per_track = 8U},
        }};

        [[nodiscard]] std::uint8_t clamp_track(int value) noexcept {
            if (value <= 0) {
                return 0U;
            }
            if (value >= 0xFF) {
                return 0xFFU;
            }
            return static_cast<std::uint8_t>(value);
        }
    } // namespace

    chip_metadata wd1793::metadata() const noexcept {
        return {
            .manufacturer = "Western Digital",
            .part_number = "WD1793",
            .family = "floppy_disk_controller",
            .klass = chip_class::storage,
            .revision = 1U,
        };
    }

    void wd1793::tick(std::uint64_t /*cycles*/) {}

    void wd1793::reset(reset_kind /*kind*/) {
        command_ = 0U;
        track_ = 0U;
        sector_ = 1U;
        data_ = 0U;
        selected_drive_ = 0U;
        selected_side_ = 0U;
        step_direction_ = 1;
        motor_on_ = false;
        intrq_ = true;
        clear_transfer();
        rebuild_status(track_ == 0U ? k_status_track_zero : 0U);
    }

    bool wd1793::detect_geometry(std::size_t byte_count, disk_geometry& out) noexcept {
        for (const disk_geometry& candidate : k_supported_geometries) {
            const std::size_t expected = static_cast<std::size_t>(candidate.tracks) *
                                         candidate.sides * candidate.sectors_per_track *
                                         sector_size;
            if (byte_count == expected) {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    bool wd1793::mount(std::span<const std::uint8_t> disk, bool write_protected) {
        disk_geometry detected{};
        if (!detect_geometry(disk.size(), detected)) {
            return false;
        }

        disk_.assign(disk.begin(), disk.end());
        geometry_ = detected;
        disk_loaded_ = true;
        disk_write_protected_ = write_protected;
        reset(reset_kind::power_on);
        return true;
    }

    void wd1793::eject() noexcept {
        disk_.clear();
        geometry_ = {};
        disk_loaded_ = false;
        disk_write_protected_ = false;
        clear_transfer();
        rebuild_status(k_status_not_ready);
    }

    bool wd1793::ready() const noexcept {
        // The mounted DSK is drive A. Motor state is preserved for observability,
        // but readiness is not gated on it because most MSX disk ROMs command the
        // FDC immediately after changing the motor/drive latch.
        return disk_loaded_ && selected_drive_ == 0U;
    }

    bool wd1793::side_compare_matches(std::uint8_t command) const noexcept {
        if ((command & 0x02U) == 0U) {
            return true;
        }
        const std::uint8_t expected_side = static_cast<std::uint8_t>((command >> 3U) & 0x01U);
        return expected_side == (selected_side_ & 0x01U);
    }

    bool wd1793::sector_valid(std::uint8_t track, std::uint8_t side,
                              std::uint8_t sector) const noexcept {
        return ready() && track < geometry_.tracks && side < geometry_.sides && sector != 0U &&
               sector <= geometry_.sectors_per_track;
    }

    std::size_t wd1793::sector_offset(std::uint8_t track, std::uint8_t side,
                                      std::uint8_t sector) const noexcept {
        const std::size_t logical_sector =
            ((static_cast<std::size_t>(track) * geometry_.sides + side) *
             geometry_.sectors_per_track) +
            (static_cast<std::size_t>(sector) - 1U);
        return logical_sector * sector_size;
    }

    void wd1793::clear_transfer() noexcept {
        transfer_ = transfer_kind::none;
        transfer_buffer_.clear();
        transfer_pos_ = 0U;
        transfer_start_sector_ = sector_;
        transfer_sector_count_ = 0U;
        transfer_multiple_ = false;
        busy_ = false;
        drq_ = false;
    }

    void wd1793::rebuild_status(std::uint8_t extra_status) noexcept {
        status_ = extra_status;
        if (!ready()) {
            status_ = static_cast<std::uint8_t>(status_ | k_status_not_ready);
        }
        if (busy_) {
            status_ = static_cast<std::uint8_t>(status_ | k_status_busy);
        }
        if (drq_) {
            status_ = static_cast<std::uint8_t>(status_ | k_status_drq);
        }
    }

    void wd1793::complete_type_i(std::uint8_t extra_status) noexcept {
        clear_transfer();
        intrq_ = true;
        if (track_ == 0U) {
            extra_status = static_cast<std::uint8_t>(extra_status | k_status_track_zero);
        }
        rebuild_status(extra_status);
    }

    void wd1793::finish_transfer(std::uint8_t extra_status) noexcept {
        clear_transfer();
        intrq_ = true;
        rebuild_status(extra_status);
    }

    void wd1793::fail_command(std::uint8_t error_status) noexcept {
        clear_transfer();
        intrq_ = true;
        rebuild_status(error_status);
    }

    void wd1793::step_track(int delta) noexcept {
        track_ = clamp_track(static_cast<int>(track_) + delta);
        step_direction_ = delta < 0 ? -1 : 1;
    }

    void wd1793::begin_read_sector(bool multiple) noexcept {
        if (!ready() || !side_compare_matches(command_) ||
            !sector_valid(track_, selected_side_, sector_)) {
            fail_command(k_status_record_not_found);
            return;
        }

        transfer_start_sector_ = sector_;
        transfer_sector_count_ =
            multiple ? static_cast<std::uint8_t>(geometry_.sectors_per_track - sector_ + 1U) : 1U;
        transfer_buffer_.clear();
        transfer_buffer_.reserve(static_cast<std::size_t>(transfer_sector_count_) * sector_size);
        for (std::uint8_t i = 0; i < transfer_sector_count_; ++i) {
            const std::uint8_t logical_sector =
                static_cast<std::uint8_t>(transfer_start_sector_ + i);
            const std::size_t offset = sector_offset(track_, selected_side_, logical_sector);
            transfer_buffer_.insert(
                transfer_buffer_.end(), disk_.begin() + static_cast<std::ptrdiff_t>(offset),
                disk_.begin() + static_cast<std::ptrdiff_t>(offset + sector_size));
        }
        transfer_ = transfer_kind::read_sector;
        transfer_pos_ = 0U;
        transfer_multiple_ = multiple;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        data_ = transfer_buffer_.front();
        rebuild_status();
    }

    void wd1793::begin_write_sector(bool multiple) noexcept {
        if (!ready()) {
            fail_command(k_status_record_not_found);
            return;
        }
        if (disk_write_protected_) {
            fail_command(k_status_write_protect);
            return;
        }
        if (!side_compare_matches(command_) || !sector_valid(track_, selected_side_, sector_)) {
            fail_command(k_status_record_not_found);
            return;
        }

        transfer_start_sector_ = sector_;
        transfer_sector_count_ =
            multiple ? static_cast<std::uint8_t>(geometry_.sectors_per_track - sector_ + 1U) : 1U;
        transfer_buffer_.assign(static_cast<std::size_t>(transfer_sector_count_) * sector_size, 0U);
        transfer_ = transfer_kind::write_sector;
        transfer_pos_ = 0U;
        transfer_multiple_ = multiple;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        rebuild_status();
    }

    void wd1793::begin_read_address() noexcept {
        if (!ready() || track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(k_status_record_not_found);
            return;
        }

        transfer_buffer_ = {
            track_, selected_side_, sector_,
            0x02U, // sector length code: 512 bytes
            0x00U,  0x00U,
        };
        transfer_ = transfer_kind::read_address;
        transfer_pos_ = 0U;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        data_ = transfer_buffer_.front();
        rebuild_status();
    }

    void wd1793::begin_write_track() noexcept {
        if (!ready() || track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(k_status_record_not_found);
            return;
        }
        if (disk_write_protected_) {
            fail_command(k_status_write_protect);
            return;
        }

        transfer_buffer_.clear();
        transfer_buffer_.reserve(k_max_track_format_bytes);
        transfer_ = transfer_kind::write_track;
        transfer_pos_ = 0U;
        transfer_multiple_ = false;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        rebuild_status();
    }

    void wd1793::begin_read_track() noexcept {
        if (!ready() || track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(k_status_record_not_found);
            return;
        }

        const auto append_repeat = [this](std::uint8_t value, std::size_t count) {
            transfer_buffer_.insert(transfer_buffer_.end(), count, value);
        };
        const std::uint8_t side = static_cast<std::uint8_t>(selected_side_ & 0x01U);

        transfer_buffer_.clear();
        transfer_buffer_.reserve(k_max_track_format_bytes);
        append_repeat(0x4EU, 80U);
        append_repeat(0x00U, 12U);
        append_repeat(0x00U, 3U);
        transfer_buffer_.push_back(0xFCU);
        append_repeat(0x4EU, 50U);

        for (std::uint8_t logical_sector = 1U; logical_sector <= geometry_.sectors_per_track;
             ++logical_sector) {
            append_repeat(0x00U, 12U);
            append_repeat(0x00U, 3U);
            transfer_buffer_.push_back(0xFEU);
            transfer_buffer_.push_back(track_);
            transfer_buffer_.push_back(side);
            transfer_buffer_.push_back(logical_sector);
            transfer_buffer_.push_back(0x02U);
            transfer_buffer_.push_back(0x00U);
            transfer_buffer_.push_back(0x00U);
            append_repeat(0x4EU, 22U);
            append_repeat(0x00U, 12U);
            append_repeat(0x00U, 3U);
            transfer_buffer_.push_back(0xFBU);

            const std::size_t offset = sector_offset(track_, side, logical_sector);
            transfer_buffer_.insert(
                transfer_buffer_.end(), disk_.begin() + static_cast<std::ptrdiff_t>(offset),
                disk_.begin() + static_cast<std::ptrdiff_t>(offset + sector_size));
            transfer_buffer_.push_back(0x00U);
            transfer_buffer_.push_back(0x00U);
            append_repeat(0x4EU, 54U);
        }
        append_repeat(0x4EU, 160U);

        transfer_ = transfer_kind::read_track;
        transfer_pos_ = 0U;
        transfer_start_sector_ = 1U;
        transfer_sector_count_ = geometry_.sectors_per_track;
        transfer_multiple_ = false;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        data_ = transfer_buffer_.front();
        rebuild_status();
    }

    void wd1793::commit_write_sector(std::uint8_t sector, std::size_t buffer_offset) noexcept {
        const std::size_t offset = sector_offset(track_, selected_side_, sector);
        std::copy(transfer_buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_offset),
                  transfer_buffer_.begin() +
                      static_cast<std::ptrdiff_t>(buffer_offset + sector_size),
                  disk_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    std::uint16_t wd1793::full_track_sector_mask() const noexcept {
        std::uint16_t mask = 0U;
        for (std::uint8_t sector = 1U; sector <= geometry_.sectors_per_track && sector <= 16U;
             ++sector) {
            mask = static_cast<std::uint16_t>(mask | (1U << (sector - 1U)));
        }
        return mask;
    }

    bool wd1793::scan_and_commit_format_track() noexcept {
        std::uint16_t seen_mask = 0U;
        std::size_t pos = 0U;
        while (pos < transfer_buffer_.size()) {
            const auto id = std::find(transfer_buffer_.begin() + static_cast<std::ptrdiff_t>(pos),
                                      transfer_buffer_.end(), 0xFEU);
            if (id == transfer_buffer_.end()) {
                break;
            }
            pos = static_cast<std::size_t>(std::distance(transfer_buffer_.begin(), id));
            if (pos + 5U > transfer_buffer_.size()) {
                break;
            }

            const std::uint8_t id_track = transfer_buffer_[pos + 1U];
            const std::uint8_t id_side = static_cast<std::uint8_t>(transfer_buffer_[pos + 2U] & 1U);
            const std::uint8_t id_sector = transfer_buffer_[pos + 3U];
            const std::uint8_t id_size = transfer_buffer_[pos + 4U];

            std::size_t data_mark = pos + 5U;
            while (data_mark < transfer_buffer_.size() && transfer_buffer_[data_mark] != 0xFBU &&
                   transfer_buffer_[data_mark] != 0xF8U) {
                ++data_mark;
            }
            if (data_mark >= transfer_buffer_.size()) {
                break;
            }

            const std::size_t data_begin = data_mark + 1U;
            if (data_begin + sector_size > transfer_buffer_.size()) {
                break;
            }

            if (id_track == track_ && id_side == (selected_side_ & 1U) && id_size == 0x02U &&
                sector_valid(id_track, id_side, id_sector)) {
                const std::size_t offset = sector_offset(id_track, id_side, id_sector);
                std::copy(transfer_buffer_.begin() + static_cast<std::ptrdiff_t>(data_begin),
                          transfer_buffer_.begin() +
                              static_cast<std::ptrdiff_t>(data_begin + sector_size),
                          disk_.begin() + static_cast<std::ptrdiff_t>(offset));
                seen_mask = static_cast<std::uint16_t>(seen_mask | (1U << (id_sector - 1U)));
            }

            pos = data_begin + sector_size;
        }

        const std::uint16_t expected_mask = full_track_sector_mask();
        return expected_mask != 0U && (seen_mask & expected_mask) == expected_mask;
    }

    void wd1793::finish_data_transfer() noexcept {
        std::uint8_t extra_status = 0U;
        if (transfer_multiple_) {
            sector_ = static_cast<std::uint8_t>(transfer_start_sector_ + transfer_sector_count_);
            extra_status = k_status_record_not_found;
        }
        finish_transfer(extra_status);
    }

    void wd1793::advance_transfer_sector() noexcept {
        if (!transfer_multiple_ || transfer_sector_count_ == 0U) {
            return;
        }
        const std::size_t completed_sectors = transfer_pos_ / sector_size;
        if (completed_sectors < transfer_sector_count_) {
            sector_ = static_cast<std::uint8_t>(transfer_start_sector_ + completed_sectors);
        }
    }

    void wd1793::execute_command(std::uint8_t command) noexcept {
        command_ = command;
        intrq_ = false;
        clear_transfer();

        switch (command & 0xF0U) {
        case 0x00U: // restore
            track_ = 0U;
            step_direction_ = -1;
            complete_type_i();
            break;
        case 0x10U: // seek: data register carries the destination track
            track_ = data_;
            complete_type_i();
            break;
        case 0x20U:
        case 0x30U: // step in the previous direction
            step_track(step_direction_);
            complete_type_i();
            break;
        case 0x40U:
        case 0x50U: // step in
            step_track(1);
            complete_type_i();
            break;
        case 0x60U:
        case 0x70U: // step out
            step_track(-1);
            complete_type_i();
            break;
        case 0x80U:
        case 0x90U: // read sector
            begin_read_sector((command & 0x10U) != 0U);
            break;
        case 0xA0U:
        case 0xB0U: // write sector
            begin_write_sector((command & 0x10U) != 0U);
            break;
        case 0xC0U: // read address
            begin_read_address();
            break;
        case 0xD0U: // force interrupt
            intrq_ = true;
            rebuild_status();
            break;
        case 0xE0U: // read track
            begin_read_track();
            break;
        case 0xF0U: // write track / format
            begin_write_track();
            break;
        default:
            fail_command(k_status_record_not_found);
            break;
        }
    }

    std::uint8_t wd1793::read_register(std::uint8_t offset) noexcept {
        switch (offset & 0x03U) {
        case 0U: {
            const std::uint8_t value = status_;
            intrq_ = false;
            return value;
        }
        case 1U:
            return track_;
        case 2U:
            return sector_;
        case 3U:
            if ((transfer_ == transfer_kind::read_sector ||
                 transfer_ == transfer_kind::read_address ||
                 transfer_ == transfer_kind::read_track) &&
                drq_) {
                data_ = transfer_buffer_[transfer_pos_++];
                if (transfer_pos_ >= transfer_buffer_.size()) {
                    if (transfer_ == transfer_kind::read_sector) {
                        finish_data_transfer();
                    } else {
                        finish_transfer();
                    }
                } else if (transfer_ == transfer_kind::read_sector) {
                    advance_transfer_sector();
                }
                return data_;
            }
            return data_;
        default:
            return 0xFFU;
        }
    }

    void wd1793::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
        switch (offset & 0x03U) {
        case 0U:
            execute_command(value);
            break;
        case 1U:
            track_ = value;
            rebuild_status(track_ == 0U ? k_status_track_zero : 0U);
            break;
        case 2U:
            sector_ = value;
            break;
        case 3U:
            data_ = value;
            if (transfer_ == transfer_kind::write_sector && drq_) {
                transfer_buffer_[transfer_pos_++] = value;
                if ((transfer_pos_ % sector_size) == 0U) {
                    const std::size_t sector_index = (transfer_pos_ / sector_size) - 1U;
                    const std::uint8_t committed_sector =
                        static_cast<std::uint8_t>(transfer_start_sector_ + sector_index);
                    commit_write_sector(committed_sector, sector_index * sector_size);
                }
                if (transfer_pos_ >= transfer_buffer_.size()) {
                    finish_data_transfer();
                } else {
                    advance_transfer_sector();
                }
            } else if (transfer_ == transfer_kind::write_track && drq_) {
                transfer_buffer_.push_back(value);
                transfer_pos_ = transfer_buffer_.size();
                if (scan_and_commit_format_track()) {
                    finish_transfer();
                } else if (transfer_buffer_.size() >= k_max_track_format_bytes) {
                    fail_command(k_status_lost_data);
                }
            }
            break;
        default:
            break;
        }
    }

    std::uint8_t wd1793::read_io_status() const noexcept {
        std::uint8_t value = 0U;
        if (intrq_ || !busy_) {
            value = static_cast<std::uint8_t>(value | 0x80U);
        }
        if (drq_) {
            value = static_cast<std::uint8_t>(value | 0x40U);
        }
        return value;
    }

    std::uint8_t wd1793::read_memory_status() const noexcept {
        std::uint8_t value = 0U;
        if (drq_) {
            value = static_cast<std::uint8_t>(value | 0x80U);
        }
        if (busy_) {
            value = static_cast<std::uint8_t>(value | 0x40U);
        }
        return value;
    }

    void wd1793::select_drive(std::uint8_t drive) noexcept {
        selected_drive_ = static_cast<std::uint8_t>(drive & 0x03U);
        rebuild_status(track_ == 0U ? k_status_track_zero : 0U);
    }

    void wd1793::set_side(std::uint8_t side) noexcept {
        selected_side_ = static_cast<std::uint8_t>(side & 0x01U);
    }

    void wd1793::save_state(state_writer& writer) const {
        writer.boolean(disk_loaded_);
        writer.u16(geometry_.tracks);
        writer.u8(geometry_.sides);
        writer.u8(geometry_.sectors_per_track);
        writer.blob(disk_);
        writer.blob(transfer_buffer_);
        writer.u64(static_cast<std::uint64_t>(transfer_pos_));
        writer.u8(transfer_start_sector_);
        writer.u8(transfer_sector_count_);
        writer.u8(command_);
        writer.u8(status_);
        writer.u8(track_);
        writer.u8(sector_);
        writer.u8(data_);
        writer.u8(selected_drive_);
        writer.u8(selected_side_);
        writer.u8(static_cast<std::uint8_t>(step_direction_ < 0 ? 0U : 1U));
        writer.u8(static_cast<std::uint8_t>(transfer_));
        writer.boolean(motor_on_);
        writer.boolean(transfer_multiple_);
        writer.boolean(busy_);
        writer.boolean(drq_);
        writer.boolean(intrq_);
        writer.boolean(disk_write_protected_);
    }

    void wd1793::load_state(state_reader& reader) {
        disk_loaded_ = reader.boolean();
        geometry_.tracks = reader.u16();
        geometry_.sides = reader.u8();
        geometry_.sectors_per_track = reader.u8();
        disk_ = reader.blob();
        transfer_buffer_ = reader.blob();
        transfer_pos_ = static_cast<std::size_t>(reader.u64());
        transfer_start_sector_ = reader.u8();
        transfer_sector_count_ = reader.u8();
        command_ = reader.u8();
        status_ = reader.u8();
        track_ = reader.u8();
        sector_ = reader.u8();
        data_ = reader.u8();
        selected_drive_ = static_cast<std::uint8_t>(reader.u8() & 0x03U);
        selected_side_ = static_cast<std::uint8_t>(reader.u8() & 0x01U);
        step_direction_ = reader.u8() == 0U ? -1 : 1;
        const std::uint8_t transfer = reader.u8();
        motor_on_ = reader.boolean();
        transfer_multiple_ = reader.boolean();
        busy_ = reader.boolean();
        drq_ = reader.boolean();
        intrq_ = reader.boolean();
        disk_write_protected_ = reader.remaining() >= 1U ? reader.boolean() : false;

        if (transfer > static_cast<std::uint8_t>(transfer_kind::read_track)) {
            reader.fail();
            return;
        }
        transfer_ = static_cast<transfer_kind>(transfer);
        if (!normalise_loaded_state()) {
            reader.fail();
            return;
        }
        if (!reader.ok()) {
            return;
        }
        const bool fixed_buffer_transfer =
            transfer_ == transfer_kind::read_sector || transfer_ == transfer_kind::write_sector ||
            transfer_ == transfer_kind::read_address || transfer_ == transfer_kind::read_track;
        if (transfer_pos_ > transfer_buffer_.size() ||
            (fixed_buffer_transfer && transfer_buffer_.empty()) ||
            (drq_ && transfer_ != transfer_kind::write_track &&
             transfer_pos_ >= transfer_buffer_.size()) ||
            (transfer_ == transfer_kind::write_track &&
             (transfer_pos_ != transfer_buffer_.size() ||
              transfer_buffer_.size() > k_max_track_format_bytes)) ||
            (transfer_ == transfer_kind::read_track &&
             (transfer_buffer_.size() > k_max_track_format_bytes || transfer_sector_count_ == 0U ||
              transfer_sector_count_ > geometry_.sectors_per_track)) ||
            (transfer_ == transfer_kind::read_sector || transfer_ == transfer_kind::write_sector
                 ? (transfer_sector_count_ == 0U ||
                    transfer_buffer_.size() !=
                        static_cast<std::size_t>(transfer_sector_count_) * sector_size ||
                    transfer_start_sector_ == 0U ||
                    transfer_start_sector_ + transfer_sector_count_ - 1U >
                        geometry_.sectors_per_track)
                 : false)) {
            reader.fail();
        }
    }

    bool wd1793::normalise_loaded_state() noexcept {
        if (!disk_loaded_) {
            disk_.clear();
            geometry_ = {};
            disk_write_protected_ = false;
            if (transfer_ != transfer_kind::none) {
                clear_transfer();
            }
            rebuild_status(k_status_not_ready);
            return true;
        }

        disk_geometry detected{};
        if (!detect_geometry(disk_.size(), detected) || detected.tracks != geometry_.tracks ||
            detected.sides != geometry_.sides ||
            detected.sectors_per_track != geometry_.sectors_per_track) {
            disk_loaded_ = false;
            disk_.clear();
            geometry_ = {};
            disk_write_protected_ = false;
            clear_transfer();
            status_ = k_status_not_ready;
            return false;
        }

        selected_side_ = static_cast<std::uint8_t>(selected_side_ % geometry_.sides);
        return true;
    }

    std::span<const register_descriptor> wd1793::register_snapshot() noexcept {
        register_view_[0] = {"STATUS", status_, 8U, register_value_format::flags};
        register_view_[1] = {"CMD", command_, 8U, register_value_format::flags};
        register_view_[2] = {"TRACK", track_, 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"SECTOR", sector_, 8U, register_value_format::unsigned_integer};
        register_view_[4] = {"DATA", data_, 8U, register_value_format::unsigned_integer};
        register_view_[5] = {"SIDE", selected_side_, 1U, register_value_format::unsigned_integer};
        register_view_[6] = {"DRIVE", selected_drive_, 2U, register_value_format::unsigned_integer};
        register_view_[7] = {"DRQ", drq_ ? 1U : 0U, 1U, register_value_format::flags};
        register_view_[8] = {"INTRQ", intrq_ ? 1U : 0U, 1U, register_value_format::flags};
        register_view_[9] = {"WPROT", disk_write_protected_ ? 1U : 0U, 1U,
                             register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto wd1793_registration =
            register_factory("western_digital.wd1793", chip_class::storage,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<wd1793>(); });
    } // namespace

} // namespace mnemos::chips::storage
