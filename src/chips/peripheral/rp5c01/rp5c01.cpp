#include "rp5c01.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::peripheral {

    chip_metadata rp5c01::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "RP-5C01",
            .family = "RTC",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    std::uint8_t rp5c01::decimal_from_bcd(std::uint8_t low, std::uint8_t high) noexcept {
        return static_cast<std::uint8_t>((nibble(high) * 10U) + nibble(low));
    }

    void rp5c01::set_bcd(std::uint8_t value, std::uint8_t& low, std::uint8_t& high) noexcept {
        low = static_cast<std::uint8_t>(value % 10U);
        high = static_cast<std::uint8_t>((value / 10U) % 10U);
    }

    bool rp5c01::leap_year(std::uint8_t year) noexcept {
        // The RP-5C01 year field is 1980-based and wraps after 99; within that
        // hardware range every fourth offset is leap.
        return (year & 0x03U) == 0U;
    }

    std::uint8_t rp5c01::days_in_month(std::uint8_t month, std::uint8_t year) noexcept {
        static constexpr std::array<std::uint8_t, 12> k_days{31U, 28U, 31U, 30U, 31U, 30U,
                                                             31U, 31U, 30U, 31U, 30U, 31U};
        if (month < 1U || month > 12U) {
            return 31U;
        }
        if (month == 2U && leap_year(year)) {
            return 29U;
        }
        return k_days[static_cast<std::size_t>(month - 1U)];
    }

    void rp5c01::initialise_cmos_defaults() noexcept {
        blocks_ = {};

        auto& time = clock_block();
        set_bcd(0U, time[0], time[1]);  // seconds
        set_bcd(0U, time[2], time[3]);  // minutes
        set_bcd(0U, time[4], time[5]);  // hours
        time[6] = 6U;                   // Saturday for 2000-01-01
        set_bcd(1U, time[7], time[8]);  // day
        set_bcd(1U, time[9], time[10]); // month
        set_bcd(20U, time[11], time[12]);

        blocks_[1][10] = 0x01U; // MSX uses 24-hour mode.
        blocks_[1][11] = 0x00U; // Leap-year counter mirrors year % 4.

        blocks_[2][0] = 0x0AU;  // RTC CMOS valid marker used by MSX2 BIOS.
        blocks_[2][12] = 0x02U; // International area-code default.
    }

    void rp5c01::mask_registers() noexcept {
        for (auto& block : blocks_) {
            for (std::uint8_t& value : block) {
                value = nibble(value);
            }
        }
        address_latch_ = nibble(address_latch_);
        mode_ = nibble(mode_);
        test_ = nibble(test_);
        reset_ = nibble(reset_);
        second_cycle_accumulator_ %= input_clock_hz;
        test_cycle_accumulator_ %= input_clock_hz;
    }

    void rp5c01::reset(reset_kind /*kind*/) {
        address_latch_ = 0U;
        mode_ = k_mode_timer_enable;
        test_ = 0U;
        reset_ = 0U;
        second_cycle_accumulator_ = 0U;
        test_cycle_accumulator_ = 0U;
        initialise_cmos_defaults();
    }

    void rp5c01::write_address(std::uint8_t value) noexcept { address_latch_ = nibble(value); }

    std::uint8_t rp5c01::read_data() const noexcept {
        if (address_latch_ < block_register_count) {
            return blocks_[selected_block()][address_latch_] & 0x0FU;
        }
        if (address_latch_ == 0x0DU) {
            return mode_ & 0x0FU;
        }
        return 0U;
    }

    void rp5c01::write_data(std::uint8_t value) noexcept {
        const std::uint8_t v = nibble(value);
        if (address_latch_ < block_register_count) {
            blocks_[selected_block()][address_latch_] = v;
            return;
        }
        if (address_latch_ == 0x0DU) {
            mode_ = v;
        } else if (address_latch_ == 0x0EU) {
            test_ = v;
        } else if (address_latch_ == 0x0FU) {
            apply_reset_register(v);
        }
    }

    std::uint8_t rp5c01::block_register(int block, int reg) const noexcept {
        if (block < 0 || block >= block_count || reg < 0 || reg >= block_register_count) {
            return 0U;
        }
        return blocks_[static_cast<std::size_t>(block)][static_cast<std::size_t>(reg)] & 0x0FU;
    }

    void rp5c01::increment_seconds(std::uint32_t count) noexcept {
        auto& time = clock_block();
        std::uint32_t seconds = decimal_from_bcd(time[0], time[1]);
        seconds += count;
        increment_minutes(seconds / 60U);
        set_bcd(static_cast<std::uint8_t>(seconds % 60U), time[0], time[1]);
    }

    void rp5c01::increment_minutes(std::uint32_t count) noexcept {
        if (count == 0U) {
            return;
        }
        auto& time = clock_block();
        std::uint32_t minutes = decimal_from_bcd(time[2], time[3]);
        minutes += count;
        increment_hours(minutes / 60U);
        set_bcd(static_cast<std::uint8_t>(minutes % 60U), time[2], time[3]);
    }

    void rp5c01::increment_hours(std::uint32_t count) noexcept {
        if (count == 0U) {
            return;
        }
        auto& time = clock_block();
        std::uint32_t hours = decimal_from_bcd(time[4], time[5]);
        hours += count;
        increment_days(hours / 24U);
        set_bcd(static_cast<std::uint8_t>(hours % 24U), time[4], time[5]);
    }

    void rp5c01::increment_days(std::uint32_t count) noexcept {
        if (count == 0U) {
            return;
        }

        auto& time = clock_block();
        std::uint8_t year = decimal_from_bcd(time[11], time[12]);
        std::uint8_t month = decimal_from_bcd(time[9], time[10]);
        std::uint8_t day = decimal_from_bcd(time[7], time[8]);
        std::uint8_t weekday = static_cast<std::uint8_t>(time[6] % 7U);
        if (month < 1U || month > 12U) {
            month = 1U;
        }
        day = std::clamp<std::uint8_t>(day, 1U, days_in_month(month, year));

        for (std::uint32_t i = 0; i < count; ++i) {
            weekday = static_cast<std::uint8_t>((weekday + 1U) % 7U);
            ++day;
            if (day > days_in_month(month, year)) {
                day = 1U;
                ++month;
                if (month > 12U) {
                    month = 1U;
                    year = static_cast<std::uint8_t>((year + 1U) % 100U);
                    blocks_[1][11] = static_cast<std::uint8_t>(year & 0x03U);
                }
            }
        }

        time[6] = weekday;
        set_bcd(day, time[7], time[8]);
        set_bcd(month, time[9], time[10]);
        set_bcd(year, time[11], time[12]);
    }

    void rp5c01::apply_reset_register(std::uint8_t value) noexcept {
        reset_ = nibble(value);
        if ((reset_ & 0x01U) != 0U) {
            for (int i = 0; i <= 8; ++i) {
                blocks_[1][static_cast<std::size_t>(i)] = 0U;
            }
            blocks_[1][12] = 0U;
        }
        if ((reset_ & 0x02U) != 0U) {
            second_cycle_accumulator_ = 0U;
            test_cycle_accumulator_ = 0U;
        }
    }

    void rp5c01::apply_test_pulses(std::uint32_t pulses) noexcept {
        if (pulses == 0U || test_ == 0U) {
            return;
        }
        if ((test_ & 0x01U) != 0U) {
            increment_seconds(pulses);
        }
        if ((test_ & 0x02U) != 0U) {
            increment_minutes(pulses);
        }
        if ((test_ & 0x04U) != 0U) {
            increment_hours(pulses);
        }
        if ((test_ & 0x08U) != 0U) {
            increment_days(pulses);
        }
    }

    void rp5c01::tick(std::uint64_t cycles) {
        if (timer_enabled()) {
            second_cycle_accumulator_ += cycles;
            const std::uint64_t seconds = second_cycle_accumulator_ / input_clock_hz;
            if (seconds != 0U) {
                second_cycle_accumulator_ %= input_clock_hz;
                increment_seconds(static_cast<std::uint32_t>(seconds));
            }
        }

        if (test_ != 0U) {
            test_cycle_accumulator_ += cycles * k_test_tick_hz;
            const std::uint64_t pulses = test_cycle_accumulator_ / input_clock_hz;
            if (pulses != 0U) {
                test_cycle_accumulator_ %= input_clock_hz;
                apply_test_pulses(static_cast<std::uint32_t>(pulses));
            }
        }
    }

    void rp5c01::save_state(state_writer& writer) const {
        for (const auto& block : blocks_) {
            writer.bytes(block);
        }
        writer.u8(address_latch_);
        writer.u8(mode_);
        writer.u8(test_);
        writer.u8(reset_);
        writer.u64(second_cycle_accumulator_);
        writer.u64(test_cycle_accumulator_);
    }

    void rp5c01::load_state(state_reader& reader) {
        for (auto& block : blocks_) {
            reader.bytes(block);
        }
        address_latch_ = reader.u8();
        mode_ = reader.u8();
        test_ = reader.u8();
        reset_ = reader.u8();
        second_cycle_accumulator_ = reader.u64();
        test_cycle_accumulator_ = reader.u64();
        mask_registers();
    }

    std::span<const register_descriptor> rp5c01::register_snapshot() noexcept {
        const auto& time = clock_block();
        register_view_[0] = {"ADDR", address_latch_, 4U, register_value_format::unsigned_integer};
        register_view_[1] = {"MODE", mode_, 4U, register_value_format::flags};
        register_view_[2] = {"BLOCK", selected_block(), 2U,
                             register_value_format::unsigned_integer};
        register_view_[3] = {"SEC", decimal_from_bcd(time[0], time[1]), 8U,
                             register_value_format::unsigned_integer};
        register_view_[4] = {"MIN", decimal_from_bcd(time[2], time[3]), 8U,
                             register_value_format::unsigned_integer};
        register_view_[5] = {"HOUR", decimal_from_bcd(time[4], time[5]), 8U,
                             register_value_format::unsigned_integer};
        register_view_[6] = {"DAY", decimal_from_bcd(time[7], time[8]), 8U,
                             register_value_format::unsigned_integer};
        register_view_[7] = {"MON", decimal_from_bcd(time[9], time[10]), 8U,
                             register_value_format::unsigned_integer};
        register_view_[8] = {"YEAR", decimal_from_bcd(time[11], time[12]), 8U,
                             register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto rp5c01_registration =
            register_factory("ricoh.rp5c01", chip_class::peripheral,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<rp5c01>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
