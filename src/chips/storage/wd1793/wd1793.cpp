#include "wd1793.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace mnemos::chips::storage {
    namespace {
        constexpr std::uint32_t k_state_version = 5U;
        constexpr std::uint8_t k_mfm_gap = 0x4EU;
        constexpr std::uint8_t k_mfm_sync_pad = 0x00U;
        constexpr std::uint8_t k_mfm_sync_mark = 0xA1U;
        constexpr std::uint8_t k_mfm_index_mark = 0xFCU;
        constexpr std::uint8_t k_mfm_id_mark = 0xFEU;
        constexpr std::uint8_t k_mfm_data_mark = 0xFBU;
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

        [[nodiscard]] bool has_mfm_sync_preamble(std::span<const std::uint8_t> bytes,
                                                 std::size_t offset) noexcept {
            return offset >= 3U && mfm_sync_byte(bytes[offset - 1U]) &&
                   mfm_sync_byte(bytes[offset - 2U]) && mfm_sync_byte(bytes[offset - 3U]);
        }
    } // namespace

    std::optional<wd1793::dsk_geometry>
    wd1793::infer_dsk_geometry(std::span<const std::uint8_t> image) noexcept {
        if (image.empty() || (image.size() % sector_size) != 0U) {
            return std::nullopt;
        }
        const std::size_t sector_count = image.size() / sector_size;
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

    void wd1793::tick(std::uint64_t cycles) { (void)cycles; }

    void wd1793::reset(reset_kind /*kind*/) {
        command_ = 0U;
        status_latch_ = 0U;
        track_ = 0U;
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
        clear_transfer();
    }

    bool wd1793::mount_dsk(std::span<const std::uint8_t> image, bool write_protected) {
        const auto inferred = infer_dsk_geometry(image);
        if (!inferred) {
            return false;
        }
        geometry_ = *inferred;
        disk_.assign(image.begin(), image.end());
        reset(reset_kind::power_on);
        write_protected_ = write_protected; // set after reset: WP is a property of the mounted media
        transfer_.reserve(standard_mfm_track_size);
        return true;
    }

    void wd1793::eject() noexcept {
        disk_.clear();
        geometry_ = {};
        write_protected_ = false;
        clear_transfer();
        busy_ = false;
        drq_ = false;
        intrq_ = false;
        status_latch_ = status_not_ready;
    }

    bool wd1793::ready() const noexcept { return mounted() && selected_drive_ == 0U; }

    std::size_t wd1793::expected_size(dsk_geometry geometry) noexcept {
        return static_cast<std::size_t>(geometry.tracks) * geometry.sides *
               geometry.sectors_per_track * geometry.bytes_per_sector;
    }

    std::uint8_t wd1793::composed_status() const noexcept {
        std::uint8_t status = status_latch_;
        if (busy_) {
            status |= status_busy;
        }
        if (drq_) {
            status |= status_drq;
        }
        if (type_i_status_ && track_ == 0U) {
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
            track_ >= geometry_.tracks || selected_side_ >= geometry_.sides ||
            sector_ > geometry_.sectors_per_track ||
            (compare_side && command_side != selected_side_)) {
            return false;
        }
        const std::size_t zero_based_sector = static_cast<std::size_t>(sector_ - 1U);
        offset = (((static_cast<std::size_t>(track_) * geometry_.sides) + selected_side_) *
                      geometry_.sectors_per_track +
                  zero_based_sector) *
                 sector_size;
        return offset + sector_size <= disk_.size();
    }

    void wd1793::clear_transfer() noexcept {
        transfer_.clear();
        transfer_index_ = 0U;
        transfer_size_ = 0U;
        transfer_disk_offset_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        write_track_transfer_ = false;
    }

    void wd1793::complete_transfer() noexcept {
        busy_ = false;
        drq_ = false;
        intrq_ = true;
        status_latch_ = 0U;
        clear_transfer();
    }

    void wd1793::fail_command(std::uint8_t status_bits) noexcept {
        clear_transfer();
        busy_ = false;
        drq_ = false;
        intrq_ = true;
        status_latch_ = status_bits;
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
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        status_latch_ = 0U;
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
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        status_latch_ = 0U;
    }

    void wd1793::begin_read_address() noexcept {
        type_i_status_ = false;
        if (!ready() || geometry_.tracks == 0U || geometry_.sides == 0U ||
            geometry_.sectors_per_track == 0U || track_ >= geometry_.tracks ||
            selected_side_ >= geometry_.sides) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }

        const std::uint8_t id_sector =
            sector_ == 0U || sector_ > geometry_.sectors_per_track ? 1U : sector_;
        transfer_.assign(6U, std::uint8_t{0});
        transfer_[0] = track_;
        transfer_[1] = selected_side_;
        transfer_[2] = id_sector;
        transfer_[3] = 0x02U; // 512-byte sectors.
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        transfer_disk_offset_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        status_latch_ = 0U;
    }

    void wd1793::begin_write_track() noexcept {
        type_i_status_ = false;
        if (write_protected_) {
            fail_command(status_write_protect);
            return;
        }
        if (!ready() || geometry_.bytes_per_sector != sector_size || geometry_.tracks == 0U ||
            geometry_.sides == 0U || geometry_.sectors_per_track == 0U ||
            geometry_.sectors_per_track != standard_sectors_per_track ||
            track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
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
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        status_latch_ = 0U;
    }

    void wd1793::append_transfer(std::uint8_t value, std::size_t count) {
        transfer_.insert(transfer_.end(), count, value);
    }

    void wd1793::build_mfm_track_image() {
        transfer_.clear();
        transfer_.reserve(standard_mfm_track_size);

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
                track_,          selected_side_,  sector,          k_mfm_sector_length_512,
            };
            transfer_.push_back(k_mfm_id_mark);
            transfer_.push_back(track_);
            transfer_.push_back(selected_side_);
            transfer_.push_back(sector);
            transfer_.push_back(k_mfm_sector_length_512);
            append_crc(transfer_, crc16_ccitt(id_crc_bytes));

            append_transfer(k_mfm_gap, 22U);
            append_transfer(k_mfm_sync_pad, 12U);
            append_transfer(k_mfm_sync_mark, 3U);
            transfer_.push_back(k_mfm_data_mark);

            const std::size_t zero_based_sector = static_cast<std::size_t>(sector - 1U);
            const std::size_t sector_offset =
                (((static_cast<std::size_t>(track_) * geometry_.sides) + selected_side_) *
                     geometry_.sectors_per_track +
                 zero_based_sector) *
                sector_size;

            std::uint16_t data_crc = 0xFFFFU;
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, k_mfm_sync_mark);
            data_crc = crc16_ccitt_update(data_crc, k_mfm_data_mark);
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

    void wd1793::begin_read_track() noexcept {
        type_i_status_ = false;
        if (!ready() || geometry_.bytes_per_sector != sector_size || geometry_.tracks == 0U ||
            geometry_.sides == 0U || geometry_.sectors_per_track == 0U ||
            geometry_.sectors_per_track != standard_sectors_per_track ||
            track_ >= geometry_.tracks || selected_side_ >= geometry_.sides) {
            fail_command(ready() ? status_record_not_found : status_not_ready);
            return;
        }

        build_mfm_track_image();
        transfer_index_ = 0U;
        transfer_size_ = transfer_.size();
        transfer_disk_offset_ = 0U;
        write_transfer_ = false;
        multi_sector_ = false;
        busy_ = true;
        drq_ = true;
        intrq_ = false;
        status_latch_ = 0U;
    }

    void wd1793::commit_write_track() noexcept {
        if (geometry_.bytes_per_sector != sector_size || geometry_.sectors_per_track == 0U ||
            transfer_size_ > transfer_.size()) {
            return;
        }

        std::uint8_t id_track = track_;
        std::uint8_t id_side = selected_side_;
        std::uint8_t id_sector = 1U;
        std::uint8_t id_length = k_mfm_sector_length_512;
        bool have_id = false;

        for (std::size_t i = 0U; i < transfer_size_; ++i) {
            const std::uint8_t value = transfer_[i];
            if (value == k_mfm_id_mark && i + 4U < transfer_size_ &&
                has_mfm_sync_preamble(transfer_, i)) {
                id_track = transfer_[i + 1U];
                id_side = transfer_[i + 2U];
                id_sector = transfer_[i + 3U];
                id_length = transfer_[i + 4U];
                have_id = true;
                i += 4U;
                continue;
            }

            if ((value == k_mfm_data_mark || value == 0xF8U) && have_id &&
                has_mfm_sync_preamble(transfer_, i)) {
                const std::size_t data_size = sector_length_bytes(id_length);
                if (data_size == sector_size && i + data_size < transfer_size_ &&
                    id_track == track_ && id_side == selected_side_ && id_sector != 0U &&
                    id_sector <= geometry_.sectors_per_track) {
                    const std::size_t zero_based_sector = static_cast<std::size_t>(id_sector - 1U);
                    const std::size_t disk_offset =
                        (((static_cast<std::size_t>(track_) * geometry_.sides) + selected_side_) *
                             geometry_.sectors_per_track +
                         zero_based_sector) *
                        sector_size;
                    if (disk_offset + sector_size <= disk_.size()) {
                        std::copy_n(transfer_.begin() + static_cast<std::ptrdiff_t>(i + 1U),
                                    sector_size,
                                    disk_.begin() + static_cast<std::ptrdiff_t>(disk_offset));
                    }
                }
                have_id = false;
                i += data_size;
            }
        }
    }

    void wd1793::step_track(int delta) noexcept {
        const int next = static_cast<int>(track_) + delta;
        track_ = static_cast<std::uint8_t>(std::clamp(next, 0, 255));
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
                if (transfer_index_ >= transfer_size_) {
                    finish_sector_transfer();
                }
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
                data_ = 0U;
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x10U) {
                track_ = data_;
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x20U || command_class == 0x30U) {
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x40U || command_class == 0x50U) {
                step_track(1);
                finish_type_i(ready() ? 0U : status_not_ready);
            } else if (command_class == 0x60U || command_class == 0x70U) {
                step_track(-1);
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
            break;
        case 2U:
            sector_ = value;
            break;
        case 3U:
            data_ = value;
            if (drq_ && write_transfer_ && transfer_index_ < transfer_size_) {
                transfer_[transfer_index_++] = value;
                if (transfer_index_ >= transfer_size_) {
                    finish_sector_transfer();
                }
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

        if (!reader.ok()) {
            return;
        }
        if (disk_.empty()) {
            geometry_ = {};
        } else if (geometry_.bytes_per_sector != sector_size ||
                   expected_size(geometry_) != disk_.size()) {
            reader.fail();
            return;
        }
        if (transfer_size_ > transfer_.size() || transfer_index_ > transfer_size_ ||
            (write_transfer_ && transfer_size_ != 0U &&
             transfer_disk_offset_ + transfer_size_ > disk_.size())) {
            reader.fail();
        }
        if (reader.ok() && mounted()) {
            transfer_.reserve(standard_mfm_track_size);
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
