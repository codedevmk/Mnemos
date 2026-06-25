#include "msx_cassette.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace mnemos::chips::storage {
    namespace {
        constexpr std::uint32_t kShortLeaderCycles1200 = 3'840U;
        constexpr std::uint32_t kLongLeaderCycles1200 = 15'360U;
        constexpr std::uint32_t kShortLeaderCycles2400 = 7'936U;
        constexpr std::uint32_t kLongLeaderCycles2400 = 31'744U;

        [[nodiscard]] bool magic_at(std::span<const std::uint8_t> image,
                                    std::size_t offset) noexcept {
            if (offset + msx_cassette::cas_header_magic.size() > image.size()) {
                return false;
            }
            for (std::size_t i = 0; i < msx_cassette::cas_header_magic.size(); ++i) {
                if (image[offset + i] != msx_cassette::cas_header_magic[i]) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    chip_metadata msx_cassette::metadata() const noexcept {
        return {
            .manufacturer = "Microsoft",
            .part_number = "MSX-CAS",
            .family = "cassette",
            .klass = chip_class::storage,
            .revision = 1U,
        };
    }

    bool msx_cassette::has_cas_header(std::span<const std::uint8_t> image) noexcept {
        for (std::size_t i = 0; i + cas_header_magic.size() <= image.size(); ++i) {
            if (magic_at(image, i)) {
                return true;
            }
        }
        return false;
    }

    void msx_cassette::set_cycles_per_second(std::uint32_t cycles_per_second) noexcept {
        cycles_per_second_ =
            cycles_per_second != 0U ? cycles_per_second : default_cycles_per_second;
    }

    std::uint32_t msx_cassette::half_cycle_cycles(baud_rate rate, bool mark) const noexcept {
        const std::uint32_t frequency =
            rate == baud_rate::baud_2400 ? (mark ? 4'800U : 2'400U) : (mark ? 2'400U : 1'200U);
        const std::uint32_t denom = frequency * 2U;
        return std::max<std::uint32_t>(1U, (cycles_per_second_ + (denom / 2U)) / denom);
    }

    void msx_cassette::append_cycle(std::uint32_t half_cycle_duration, std::uint32_t cycles) {
        for (std::uint32_t i = 0; i < cycles * 2U; ++i) {
            half_cycles_.push_back(half_cycle_duration);
        }
    }

    void msx_cassette::append_bit(baud_rate rate, bool bit) {
        const std::uint32_t half_cycle = half_cycle_cycles(rate, bit);
        append_cycle(half_cycle, bit ? 2U : 1U);
    }

    void msx_cassette::append_byte(baud_rate rate, std::uint8_t value) {
        append_bit(rate, false); // start bit
        for (int i = 0; i < 8; ++i) {
            append_bit(rate, ((value >> i) & 1U) != 0U);
        }
        append_bit(rate, true);
        append_bit(rate, true);
    }

    void msx_cassette::append_leader(baud_rate rate, bool long_header) {
        const std::uint32_t cycles =
            rate == baud_rate::baud_2400
                ? (long_header ? kLongLeaderCycles2400 : kShortLeaderCycles2400)
                : (long_header ? kLongLeaderCycles1200 : kShortLeaderCycles1200);
        append_cycle(half_cycle_cycles(rate, true), cycles);
    }

    bool msx_cassette::load_cas(std::span<const std::uint8_t> image, baud_rate rate) {
        if (image.empty()) {
            return false;
        }
        rate_ = rate;
        half_cycles_.clear();
        pulse_index_ = 0U;
        countdown_ = 0U;
        input_high_ = true;

        bool saw_header = false;
        for (std::size_t i = 0; i < image.size();) {
            if (magic_at(image, i)) {
                append_leader(rate, !saw_header);
                saw_header = true;
                i += cas_header_magic.size();
                continue;
            }
            if (!saw_header) {
                append_leader(rate, true);
                saw_header = true;
            }
            append_byte(rate, image[i]);
            ++i;
        }
        return !half_cycles_.empty();
    }

    void msx_cassette::load_half_cycles(std::span<const std::uint32_t> half_cycles) {
        half_cycles_.assign(half_cycles.begin(), half_cycles.end());
        half_cycles_.erase(std::remove(half_cycles_.begin(), half_cycles_.end(), 0U),
                           half_cycles_.end());
        pulse_index_ = 0U;
        countdown_ = 0U;
        input_high_ = true;
    }

    void msx_cassette::eject() noexcept {
        half_cycles_.clear();
        pulse_index_ = 0U;
        countdown_ = 0U;
        input_high_ = true;
    }

    void msx_cassette::reset(reset_kind /*kind*/) {
        pulse_index_ = 0U;
        countdown_ = 0U;
        input_high_ = true;
        play_ = false;
        motor_on_ = false;
        output_high_ = true;
    }

    void msx_cassette::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (!play_ || !motor_on_ || half_cycles_.empty() ||
                (pulse_index_ >= half_cycles_.size() && countdown_ == 0U)) {
                continue;
            }
            if (countdown_ == 0U) {
                countdown_ = half_cycles_[pulse_index_++];
            }
            --countdown_;
            if (countdown_ == 0U) {
                input_high_ = !input_high_;
            }
        }
    }

    void msx_cassette::save_state(state_writer& writer) const {
        writer.u32(cycles_per_second_);
        writer.u8(static_cast<std::uint8_t>(rate_));
        writer.u64(static_cast<std::uint64_t>(pulse_index_));
        writer.u32(countdown_);
        writer.boolean(input_high_);
        writer.boolean(play_);
        writer.boolean(motor_on_);
        writer.boolean(output_high_);
        writer.u64(static_cast<std::uint64_t>(half_cycles_.size()));
        for (const std::uint32_t half_cycle : half_cycles_) {
            writer.u32(half_cycle);
        }
    }

    void msx_cassette::load_state(state_reader& reader) {
        cycles_per_second_ = reader.u32();
        if (cycles_per_second_ == 0U) {
            cycles_per_second_ = default_cycles_per_second;
        }
        rate_ = reader.u8() == static_cast<std::uint8_t>(baud_rate::baud_2400)
                    ? baud_rate::baud_2400
                    : baud_rate::baud_1200;
        const std::uint64_t saved_pulse_index = reader.u64();
        countdown_ = reader.u32();
        input_high_ = reader.boolean();
        play_ = reader.boolean();
        motor_on_ = reader.boolean();
        output_high_ = reader.boolean();
        if (reader.remaining() != 0U) {
            if (reader.remaining() < sizeof(std::uint64_t)) {
                reader.fail();
                return;
            }
            const std::uint64_t count = reader.u64();
            if (count > (reader.remaining() / sizeof(std::uint32_t))) {
                reader.fail();
                return;
            }
            std::vector<std::uint32_t> half_cycles;
            half_cycles.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                half_cycles.push_back(reader.u32());
            }
            half_cycles_ = std::move(half_cycles);
        }
        pulse_index_ =
            std::min<std::size_t>(static_cast<std::size_t>(saved_pulse_index), half_cycles_.size());
        if (pulse_index_ >= half_cycles_.size()) {
            countdown_ = 0U;
        }
    }

    instrumentation::ichip_introspection& msx_cassette::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto msx_cassette_registration =
            register_factory("msx.cassette", chip_class::storage, []() -> std::unique_ptr<ichip> {
                return std::make_unique<msx_cassette>();
            });
    } // namespace

} // namespace mnemos::chips::storage
