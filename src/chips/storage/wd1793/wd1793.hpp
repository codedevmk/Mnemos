#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::storage {

    // WD1793-compatible floppy disk controller as used by common MSX/MSX2 disk
    // interfaces. The media contract is a raw 512-byte-sector `.dsk` image; flux
    // timing, weak bits, and CRC byte preservation are intentionally outside this
    // sector-level chip model.
    class wd1793 final : public istorage {
      public:
        struct disk_geometry final {
            std::uint16_t tracks{};
            std::uint8_t sides{};
            std::uint8_t sectors_per_track{};
        };

        static constexpr std::uint16_t sector_size = 512U;

        wd1793() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        [[nodiscard]] bool mount(std::span<const std::uint8_t> disk, bool write_protected = false);
        void eject() noexcept;
        [[nodiscard]] bool disk_loaded() const noexcept { return disk_loaded_; }
        [[nodiscard]] bool write_protected() const noexcept { return disk_write_protected_; }
        [[nodiscard]] std::span<const std::uint8_t> disk_image() const noexcept { return disk_; }
        [[nodiscard]] disk_geometry geometry() const noexcept { return geometry_; }

        [[nodiscard]] std::uint8_t read_register(std::uint8_t offset) noexcept;
        void write_register(std::uint8_t offset, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t read_io_status() const noexcept;
        [[nodiscard]] std::uint8_t read_memory_status() const noexcept;

        void select_drive(std::uint8_t drive) noexcept;
        void set_side(std::uint8_t side) noexcept;
        void set_motor(bool on) noexcept { motor_on_ = on; }

        [[nodiscard]] std::uint8_t selected_drive() const noexcept { return selected_drive_; }
        [[nodiscard]] std::uint8_t selected_side() const noexcept { return selected_side_; }
        [[nodiscard]] bool motor_on() const noexcept { return motor_on_; }
        [[nodiscard]] std::uint8_t status() const noexcept { return status_; }
        [[nodiscard]] std::uint8_t track() const noexcept { return track_; }
        [[nodiscard]] std::uint8_t sector() const noexcept { return sector_; }
        [[nodiscard]] std::uint8_t data() const noexcept { return data_; }
        [[nodiscard]] bool drq() const noexcept { return drq_; }
        [[nodiscard]] bool intrq() const noexcept { return intrq_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        enum class transfer_kind : std::uint8_t {
            none,
            read_sector,
            write_sector,
            read_address,
            write_track,
            read_track,
        };

        static constexpr std::uint8_t k_status_busy = 0x01U;
        static constexpr std::uint8_t k_status_drq = 0x02U;
        static constexpr std::uint8_t k_status_track_zero = 0x04U;
        static constexpr std::uint8_t k_status_lost_data = 0x04U;
        static constexpr std::uint8_t k_status_record_not_found = 0x10U;
        static constexpr std::uint8_t k_status_write_protect = 0x40U;
        static constexpr std::uint8_t k_status_not_ready = 0x80U;
        static constexpr std::size_t k_max_track_format_bytes = 8192U;

        [[nodiscard]] static bool detect_geometry(std::size_t byte_count,
                                                  disk_geometry& out) noexcept;
        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] bool side_compare_matches(std::uint8_t command) const noexcept;
        [[nodiscard]] bool sector_valid(std::uint8_t track, std::uint8_t side,
                                        std::uint8_t sector) const noexcept;
        [[nodiscard]] std::size_t sector_offset(std::uint8_t track, std::uint8_t side,
                                                std::uint8_t sector) const noexcept;

        void execute_command(std::uint8_t command) noexcept;
        void complete_type_i(std::uint8_t extra_status = 0U) noexcept;
        void finish_transfer(std::uint8_t extra_status = 0U) noexcept;
        void fail_command(std::uint8_t error_status) noexcept;
        void clear_transfer() noexcept;
        void begin_read_sector(bool multiple) noexcept;
        void begin_write_sector(bool multiple) noexcept;
        void begin_read_address() noexcept;
        void begin_write_track() noexcept;
        void begin_read_track() noexcept;
        void commit_write_sector(std::uint8_t sector, std::size_t buffer_offset) noexcept;
        [[nodiscard]] std::uint16_t full_track_sector_mask() const noexcept;
        [[nodiscard]] bool scan_and_commit_format_track() noexcept;
        void finish_data_transfer() noexcept;
        void advance_transfer_sector() noexcept;
        void step_track(int delta) noexcept;
        void rebuild_status(std::uint8_t extra_status = 0U) noexcept;
        [[nodiscard]] bool normalise_loaded_state() noexcept;

        disk_geometry geometry_{};
        std::vector<std::uint8_t> disk_{};
        std::vector<std::uint8_t> transfer_buffer_{};
        std::size_t transfer_pos_{};
        std::uint8_t transfer_start_sector_{1U};
        std::uint8_t transfer_sector_count_{};

        std::uint8_t command_{};
        std::uint8_t status_{k_status_not_ready};
        std::uint8_t track_{};
        std::uint8_t sector_{1U};
        std::uint8_t data_{};
        std::uint8_t selected_drive_{};
        std::uint8_t selected_side_{};
        int step_direction_{1};

        transfer_kind transfer_{transfer_kind::none};
        bool disk_loaded_{};
        bool motor_on_{};
        bool transfer_multiple_{};
        bool disk_write_protected_{};
        bool busy_{};
        bool drq_{};
        bool intrq_{true};

        std::array<register_descriptor, 10> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::storage
