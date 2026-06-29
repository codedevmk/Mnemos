#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::storage {

    // MSX cassette recorder signal model. The MSX BIOS writes software FSK on the
    // PPI CAS OUT bit and reads the demodulated signal from PSG register 14 bit 7.
    // This chip models the external recorder/tape side: a loaded CAS byte stream is
    // converted back into deterministic half-cycle durations, then the signal only
    // advances while PLAY is held and the MSX cassette motor relay is on.
    class msx_cassette final : public istorage {
      public:
        enum class baud_rate : std::uint8_t {
            baud_1200,
            baud_2400,
        };

        static constexpr std::uint32_t default_cycles_per_second = 3'579'545U;
        static constexpr std::array<std::uint8_t, 8> cas_header_magic{0x1FU, 0xA6U, 0xDEU, 0xBAU,
                                                                      0xCCU, 0x13U, 0x7DU, 0x74U};

        msx_cassette() = default;

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] static bool has_cas_header(std::span<const std::uint8_t> image) noexcept;

        [[nodiscard]] bool load_cas(std::span<const std::uint8_t> image,
                                    baud_rate rate = baud_rate::baud_1200);
        void load_half_cycles(std::span<const std::uint32_t> half_cycles);
        void eject() noexcept;
        [[nodiscard]] bool loaded() const noexcept { return !half_cycles_.empty(); }

        void set_cycles_per_second(std::uint32_t cycles_per_second) noexcept;
        void set_input_high(bool high) noexcept { input_high_ = high; }
        void set_play(bool pressed) noexcept { play_ = pressed; }
        void set_motor_on(bool on) noexcept { motor_on_ = on; }
        void set_output_high(bool high) noexcept { output_high_ = high; }

        [[nodiscard]] bool playing() const noexcept { return play_; }
        [[nodiscard]] bool motor_on() const noexcept { return motor_on_; }
        [[nodiscard]] bool output_high() const noexcept { return output_high_; }
        [[nodiscard]] bool input_high() const noexcept { return input_high_; }
        [[nodiscard]] bool at_end() const noexcept {
            return pulse_index_ >= half_cycles_.size() && countdown_ == 0U;
        }

        [[nodiscard]] std::size_t half_cycle_count() const noexcept { return half_cycles_.size(); }
        [[nodiscard]] std::size_t position_half_cycle() const noexcept { return pulse_index_; }
        [[nodiscard]] std::uint32_t countdown() const noexcept { return countdown_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        [[nodiscard]] std::uint32_t half_cycle_cycles(baud_rate rate, bool mark) const noexcept;
        void append_leader(baud_rate rate, bool long_header);
        void append_byte(baud_rate rate, std::uint8_t value);
        void append_bit(baud_rate rate, bool bit);
        void append_cycle(std::uint32_t half_cycle_duration, std::uint32_t cycles);

        std::uint32_t cycles_per_second_{default_cycles_per_second};
        baud_rate rate_{baud_rate::baud_1200};
        std::vector<std::uint32_t> half_cycles_{};
        std::size_t pulse_index_{};
        std::uint32_t countdown_{};
        bool input_high_{true};
        bool play_{};
        bool motor_on_{};
        bool output_high_{true};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::storage
