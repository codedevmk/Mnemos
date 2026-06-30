#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>

namespace mnemos::chips::peripheral {

    // Ricoh RP-5C01-compatible 4-bit real-time clock and CMOS RAM. MSX maps this
    // through an index latch at port $B4 and a 4-bit data port at $B5; the mode
    // register selects one of four 13-register blocks for time, alarm, system
    // CMOS, and user text/password data.
    class rp5c01 final : public iperipheral {
      public:
        static constexpr int register_count = 16;
        static constexpr int block_count = 4;
        static constexpr int block_register_count = 13;
        static constexpr std::uint32_t default_cycles_per_second = 3'579'545U;

        rp5c01();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void select(std::uint8_t value) noexcept {
            selected_ = static_cast<std::uint8_t>(value & 0x0FU);
        }
        [[nodiscard]] std::uint8_t selected() const noexcept { return selected_; }
        [[nodiscard]] std::uint8_t data_read() const noexcept;
        void data_write(std::uint8_t value) noexcept;

        void set_cycles_per_second(std::uint32_t cycles) noexcept {
            cycles_per_second_ = cycles == 0U ? default_cycles_per_second : cycles;
        }
        [[nodiscard]] std::uint32_t cycles_per_second() const noexcept {
            return cycles_per_second_;
        }

        void set_time_24h(std::uint8_t year, std::uint8_t month, std::uint8_t day,
                          std::uint8_t weekday, std::uint8_t hour, std::uint8_t minute,
                          std::uint8_t second, std::uint8_t leap_counter = 0U) noexcept;

        [[nodiscard]] std::uint8_t mode() const noexcept { return mode_; }
        [[nodiscard]] std::uint8_t block() const noexcept {
            return static_cast<std::uint8_t>(mode_ & 0x03U);
        }
        [[nodiscard]] bool timer_enabled() const noexcept { return (mode_ & 0x08U) != 0U; }
        [[nodiscard]] std::uint8_t raw_block_register(int block, int index) const noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        static constexpr std::uint8_t timer_enable_bit = 0x08U;
        static constexpr std::uint8_t alarm_enable_bit = 0x04U;
        static constexpr std::uint8_t mode_block_mask = 0x03U;

        [[nodiscard]] static constexpr std::uint8_t lo_digit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>(value % 10U);
        }
        [[nodiscard]] static constexpr std::uint8_t hi_digit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value / 10U) % 10U);
        }

        [[nodiscard]] std::uint8_t block_register(std::uint8_t index) const noexcept;
        void write_block_register(std::uint8_t index, std::uint8_t value) noexcept;
        void advance_second() noexcept;
        void normalize_time() noexcept;
        [[nodiscard]] std::uint8_t time_value(std::uint8_t lo_index,
                                              std::uint8_t hi_index) const noexcept;
        void set_time_value(std::uint8_t lo_index, std::uint8_t hi_index,
                            std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t days_in_current_month() const noexcept;

        std::array<std::array<std::uint8_t, block_register_count>, block_count> blocks_{};
        std::uint8_t selected_{};
        std::uint8_t mode_{timer_enable_bit};
        std::uint8_t test_{};
        std::uint32_t cycles_per_second_{default_cycles_per_second};
        std::uint32_t subsecond_cycles_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::peripheral
