#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::peripheral {

    // Ricoh RP-5C01-compatible RTC used by MSX2 machines. The CPU-facing MSX
    // contract is a 4-bit register index at port #B4 and a 4-bit data port at
    // #B5. Registers 0-C are banked into four CMOS blocks by mode register D.
    class rp5c01 final : public iperipheral {
      public:
        static constexpr std::uint32_t input_clock_hz = 3'579'545U;
        static constexpr int block_count = 4;
        static constexpr int block_register_count = 13;

        rp5c01() {
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

        void write_address(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t address_latch() const noexcept { return address_latch_; }
        [[nodiscard]] std::uint8_t read_data() const noexcept;
        void write_data(std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t mode() const noexcept { return mode_; }
        [[nodiscard]] std::uint8_t selected_block() const noexcept {
            return static_cast<std::uint8_t>(mode_ & 0x03U);
        }
        [[nodiscard]] bool timer_enabled() const noexcept { return (mode_ & 0x08U) != 0U; }
        [[nodiscard]] std::uint8_t block_register(int block, int reg) const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        static constexpr std::uint8_t k_mode_timer_enable = 0x08U;
        static constexpr std::uint32_t k_test_tick_hz = 16'384U;

        [[nodiscard]] static std::uint8_t nibble(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>(value & 0x0FU);
        }
        [[nodiscard]] static std::uint8_t decimal_from_bcd(std::uint8_t low,
                                                           std::uint8_t high) noexcept;
        static void set_bcd(std::uint8_t value, std::uint8_t& low, std::uint8_t& high) noexcept;
        [[nodiscard]] static bool leap_year(std::uint8_t year) noexcept;
        [[nodiscard]] static std::uint8_t days_in_month(std::uint8_t month,
                                                        std::uint8_t year) noexcept;

        [[nodiscard]] std::array<std::uint8_t, block_register_count>& clock_block() noexcept {
            return blocks_[0];
        }
        [[nodiscard]] const std::array<std::uint8_t, block_register_count>&
        clock_block() const noexcept {
            return blocks_[0];
        }

        void initialise_cmos_defaults() noexcept;
        void mask_registers() noexcept;
        void increment_seconds(std::uint32_t count = 1U) noexcept;
        void increment_minutes(std::uint32_t count = 1U) noexcept;
        void increment_hours(std::uint32_t count = 1U) noexcept;
        void increment_days(std::uint32_t count = 1U) noexcept;
        void apply_reset_register(std::uint8_t value) noexcept;
        void apply_test_pulses(std::uint32_t pulses) noexcept;

        std::array<std::array<std::uint8_t, block_register_count>, block_count> blocks_{};
        std::uint8_t address_latch_{};
        std::uint8_t mode_{k_mode_timer_enable};
        std::uint8_t test_{};
        std::uint8_t reset_{};
        std::uint64_t second_cycle_accumulator_{};
        std::uint64_t test_cycle_accumulator_{};

        std::array<register_descriptor, 9> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::peripheral
