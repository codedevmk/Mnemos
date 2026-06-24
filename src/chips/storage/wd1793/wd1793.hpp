#pragma once

#include "chip.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::chips::storage {

    class wd1793 final : public istorage {
      public:
        struct dsk_geometry final {
            std::uint16_t tracks{};
            std::uint8_t sides{};
            std::uint8_t sectors_per_track{};
            std::uint16_t bytes_per_sector{};
        };

        static constexpr std::size_t sector_size = 512U;
        static constexpr std::uint8_t standard_sectors_per_track = 9U;
        static constexpr std::size_t standard_mfm_track_size = 6250U;

        [[nodiscard]] static std::optional<dsk_geometry>
        infer_dsk_geometry(std::span<const std::uint8_t> image) noexcept;
        [[nodiscard]] static bool is_supported_dsk(std::span<const std::uint8_t> image) noexcept;

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] bool mount_dsk(std::span<const std::uint8_t> image,
                                     bool write_protected = false);
        void eject() noexcept;
        [[nodiscard]] bool mounted() const noexcept { return !disk_.empty(); }
        [[nodiscard]] bool write_protected() const noexcept { return write_protected_; }
        [[nodiscard]] dsk_geometry geometry() const noexcept { return geometry_; }
        [[nodiscard]] std::span<const std::uint8_t> disk_image() const noexcept { return disk_; }

        [[nodiscard]] std::uint8_t read_register(std::uint8_t index) noexcept;
        void write_register(std::uint8_t index, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t read_memory_register(std::uint8_t offset) noexcept;
        void write_memory_register(std::uint8_t offset, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t read_control_register() const noexcept;
        void write_control_register(std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t track_register() const noexcept { return track_; }
        [[nodiscard]] std::uint8_t sector_register() const noexcept { return sector_; }
        [[nodiscard]] std::uint8_t data_register() const noexcept { return data_; }
        [[nodiscard]] std::uint8_t selected_drive() const noexcept { return selected_drive_; }
        [[nodiscard]] std::uint8_t selected_side() const noexcept { return selected_side_; }
        [[nodiscard]] bool busy() const noexcept { return busy_; }
        [[nodiscard]] bool drq() const noexcept { return drq_; }
        [[nodiscard]] bool intrq() const noexcept { return intrq_; }
        // Side-effect-free WD179x status view (unlike read_register(0), which clears INTRQ).
        [[nodiscard]] std::uint8_t composed_status() const noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        static constexpr std::uint8_t status_busy = 0x01U;
        static constexpr std::uint8_t status_drq = 0x02U;
        static constexpr std::uint8_t status_track0 = 0x04U;
        static constexpr std::uint8_t status_crc_error = 0x08U;
        static constexpr std::uint8_t status_record_not_found = 0x10U;
        static constexpr std::uint8_t status_head_loaded = 0x20U;
        static constexpr std::uint8_t status_write_protect = 0x40U;
        static constexpr std::uint8_t status_not_ready = 0x80U;

        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] bool sector_offset(std::size_t& offset) const noexcept;
        [[nodiscard]] static std::size_t expected_size(dsk_geometry geometry) noexcept;

        void clear_transfer() noexcept;
        void complete_transfer() noexcept;
        void fail_command(std::uint8_t status_bits) noexcept;
        void begin_read_sector() noexcept;
        void begin_write_sector() noexcept;
        void begin_read_address() noexcept;
        void begin_read_track() noexcept;
        void begin_write_track() noexcept;
        void build_mfm_track_image();
        void append_transfer(std::uint8_t value, std::size_t count);
        void commit_write_track() noexcept;
        void finish_sector_transfer() noexcept;
        void finish_type_i(std::uint8_t status_bits) noexcept;
        void step_track(int delta) noexcept;

        dsk_geometry geometry_{};
        std::vector<std::uint8_t> disk_{};
        bool write_protected_{}; // media write-protect (WD179x status bit 6); media property, not cleared by reset
        std::vector<std::uint8_t> transfer_{};
        std::size_t transfer_index_{};
        std::size_t transfer_size_{};
        std::size_t transfer_disk_offset_{};

        std::uint8_t command_{};
        std::uint8_t status_latch_{};
        std::uint8_t track_{};
        std::uint8_t sector_{1U};
        std::uint8_t data_{};
        std::uint8_t selected_drive_{};
        std::uint8_t selected_side_{};
        bool busy_{};
        bool drq_{};
        bool intrq_{};
        bool write_transfer_{};
        bool multi_sector_{};
        bool write_track_transfer_{};
        bool type_i_status_{true};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::storage
