#include "rp5c01.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::peripheral {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;

        [[nodiscard]] constexpr std::uint8_t clamp_bcd(std::uint8_t value,
                                                       std::uint8_t max) noexcept {
            return static_cast<std::uint8_t>(std::min<std::uint8_t>(value, max));
        }
    } // namespace

    rp5c01::rp5c01() { reset(reset_kind::power_on); }

    chip_metadata rp5c01::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "RP-5C01",
            .family = "rtc",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    void rp5c01::reset(reset_kind /*kind*/) {
        blocks_ = {};
        selected_ = 0U;
        mode_ = timer_enable_bit;
        test_ = 0U;
        subsecond_cycles_ = 0U;

        // MSX firmware treats block 2 register 0 == $A as valid CMOS contents.
        blocks_[2][0] = 0x0AU;
        blocks_[2][12] = 0x02U; // International area code: deterministic neutral default.
        blocks_[1][10] = 0x01U; // 24-hour mode.
        normalize_time();
    }

    std::uint8_t rp5c01::raw_block_register(int block, int index) const noexcept {
        if (block < 0 || block >= block_count || index < 0 || index >= block_register_count) {
            return 0U;
        }
        return blocks_[static_cast<std::size_t>(block)][static_cast<std::size_t>(index)] & 0x0FU;
    }

    std::uint8_t rp5c01::block_register(std::uint8_t index) const noexcept {
        if (index >= block_register_count) {
            return 0U;
        }
        return raw_block_register(block(), index);
    }

    void rp5c01::write_block_register(std::uint8_t index, std::uint8_t value) noexcept {
        if (index >= block_register_count) {
            return;
        }
        blocks_[block()][index] = static_cast<std::uint8_t>(value & 0x0FU);
        if (block() == 0U || (block() == 1U && (index == 10U || index == 11U))) {
            normalize_time();
        }
    }

    std::uint8_t rp5c01::data_read() const noexcept {
        if (selected_ == 0x0DU) {
            return mode_;
        }
        if (selected_ >= 0x0EU) {
            return 0U;
        }
        return block_register(selected_);
    }

    void rp5c01::data_write(std::uint8_t value) noexcept {
        const auto v = static_cast<std::uint8_t>(value & 0x0FU);
        if (selected_ == 0x0DU) {
            mode_ = static_cast<std::uint8_t>(
                v & (timer_enable_bit | alarm_enable_bit | mode_block_mask));
            return;
        }
        if (selected_ == 0x0EU) {
            test_ = v;
            return;
        }
        if (selected_ == 0x0FU) {
            if ((v & 0x01U) != 0U) {
                for (std::size_t i = 0; i <= 8U; ++i) {
                    blocks_[1][i] = 0U;
                }
            }
            if ((v & 0x02U) != 0U) {
                subsecond_cycles_ = 0U;
            }
            return;
        }
        write_block_register(selected_, v);
    }

    std::uint8_t rp5c01::time_value(std::uint8_t lo_index, std::uint8_t hi_index) const noexcept {
        return static_cast<std::uint8_t>(blocks_[0][lo_index] + blocks_[0][hi_index] * 10U);
    }

    void rp5c01::set_time_value(std::uint8_t lo_index, std::uint8_t hi_index,
                                std::uint8_t value) noexcept {
        blocks_[0][lo_index] = lo_digit(value);
        blocks_[0][hi_index] = hi_digit(value);
    }

    std::uint8_t rp5c01::days_in_current_month() const noexcept {
        const std::uint8_t month = time_value(9U, 10U);
        if (month == 2U) {
            return (blocks_[1][11] & 0x03U) == 0U ? 29U : 28U;
        }
        if (month == 4U || month == 6U || month == 9U || month == 11U) {
            return 30U;
        }
        return 31U;
    }

    void rp5c01::normalize_time() noexcept {
        blocks_[0][0] = clamp_bcd(blocks_[0][0], 9U);
        blocks_[0][1] = clamp_bcd(blocks_[0][1], 5U);
        blocks_[0][2] = clamp_bcd(blocks_[0][2], 9U);
        blocks_[0][3] = clamp_bcd(blocks_[0][3], 5U);
        blocks_[0][4] = clamp_bcd(blocks_[0][4], 9U);
        blocks_[0][5] = clamp_bcd(blocks_[0][5], 2U);
        if (time_value(4U, 5U) > 23U) {
            set_time_value(4U, 5U, 23U);
        }
        blocks_[0][6] = clamp_bcd(blocks_[0][6], 6U);
        if (time_value(7U, 8U) == 0U) {
            set_time_value(7U, 8U, 1U);
        }
        if (time_value(7U, 8U) > days_in_current_month()) {
            set_time_value(7U, 8U, days_in_current_month());
        }
        if (time_value(9U, 10U) == 0U) {
            set_time_value(9U, 10U, 1U);
        }
        if (time_value(9U, 10U) > 12U) {
            set_time_value(9U, 10U, 12U);
        }
        blocks_[0][11] = clamp_bcd(blocks_[0][11], 9U);
        blocks_[0][12] = clamp_bcd(blocks_[0][12], 9U);
        blocks_[1][10] &= 0x03U;
        blocks_[1][11] &= 0x03U;
    }

    void rp5c01::set_time_24h(std::uint8_t year, std::uint8_t month, std::uint8_t day,
                              std::uint8_t weekday, std::uint8_t hour, std::uint8_t minute,
                              std::uint8_t second, std::uint8_t leap_counter) noexcept {
        set_time_value(0U, 1U, static_cast<std::uint8_t>(second % 60U));
        set_time_value(2U, 3U, static_cast<std::uint8_t>(minute % 60U));
        set_time_value(4U, 5U, static_cast<std::uint8_t>(hour % 24U));
        blocks_[0][6] = static_cast<std::uint8_t>(weekday % 7U);
        set_time_value(9U, 10U,
                       static_cast<std::uint8_t>(std::clamp<std::uint8_t>(month, 1U, 12U)));
        blocks_[1][11] = static_cast<std::uint8_t>(leap_counter & 0x03U);
        set_time_value(
            7U, 8U,
            static_cast<std::uint8_t>(std::clamp<std::uint8_t>(day, 1U, days_in_current_month())));
        set_time_value(11U, 12U, static_cast<std::uint8_t>(year % 100U));
        blocks_[1][10] = 0x01U; // 24-hour mode.
        normalize_time();
    }

    void rp5c01::advance_second() noexcept {
        std::uint8_t second = static_cast<std::uint8_t>(time_value(0U, 1U) + 1U);
        if (second < 60U) {
            set_time_value(0U, 1U, second);
            return;
        }
        set_time_value(0U, 1U, 0U);

        std::uint8_t minute = static_cast<std::uint8_t>(time_value(2U, 3U) + 1U);
        if (minute < 60U) {
            set_time_value(2U, 3U, minute);
            return;
        }
        set_time_value(2U, 3U, 0U);

        std::uint8_t hour = static_cast<std::uint8_t>(time_value(4U, 5U) + 1U);
        if (hour < 24U) {
            set_time_value(4U, 5U, hour);
            return;
        }
        set_time_value(4U, 5U, 0U);
        blocks_[0][6] = static_cast<std::uint8_t>((blocks_[0][6] + 1U) % 7U);

        std::uint8_t day = static_cast<std::uint8_t>(time_value(7U, 8U) + 1U);
        if (day <= days_in_current_month()) {
            set_time_value(7U, 8U, day);
            return;
        }
        set_time_value(7U, 8U, 1U);

        std::uint8_t month = static_cast<std::uint8_t>(time_value(9U, 10U) + 1U);
        if (month <= 12U) {
            set_time_value(9U, 10U, month);
            return;
        }
        set_time_value(9U, 10U, 1U);

        const auto year = static_cast<std::uint8_t>((time_value(11U, 12U) + 1U) % 100U);
        set_time_value(11U, 12U, year);
        blocks_[1][11] = static_cast<std::uint8_t>((blocks_[1][11] + 1U) & 0x03U);
    }

    void rp5c01::tick(std::uint64_t cycles) {
        if (!timer_enabled()) {
            return;
        }
        while (cycles != 0U) {
            const std::uint32_t needed = cycles_per_second_ - subsecond_cycles_;
            if (cycles < needed) {
                subsecond_cycles_ += static_cast<std::uint32_t>(cycles);
                return;
            }
            cycles -= needed;
            subsecond_cycles_ = 0U;
            advance_second();
        }
    }

    void rp5c01::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        for (const auto& block_regs : blocks_) {
            writer.bytes(block_regs);
        }
        writer.u8(selected_);
        writer.u8(mode_);
        writer.u8(test_);
        writer.u32(cycles_per_second_);
        writer.u32(subsecond_cycles_);
    }

    void rp5c01::load_state(state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        for (auto& block_regs : blocks_) {
            reader.bytes(block_regs);
        }
        selected_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        mode_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        test_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        cycles_per_second_ = reader.u32();
        if (cycles_per_second_ == 0U) {
            cycles_per_second_ = default_cycles_per_second;
        }
        subsecond_cycles_ = reader.u32() % cycles_per_second_;
        normalize_time();
    }

    namespace {
        [[maybe_unused]] const auto rp5c01_registration =
            register_factory("ricoh.rp5c01", chip_class::peripheral,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<rp5c01>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
