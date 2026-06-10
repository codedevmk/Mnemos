#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::audio {

    // Yamaha YM2151 (OPM) FM synthesizer -- the Irem M72 (and a long list of
    // other arcade boards') FM sound chip: 8 channels x 4 operators.
    //
    // First increment: the CONTROL PLANE -- the address/data register
    // protocol with the BUSY flag, the full register file stored and
    // readable for debugging, Timer A (10-bit, one count per 64 chip clocks)
    // and Timer B (8-bit, one count per 1024 chip clocks) with their overflow
    // flags, the $14 control register (run/IRQ-enable/flag-reset bits), the
    // status register, and the IRQ line surfaced through a callback -- enough
    // for a sound program's timer-driven tempo loop to run correctly.
    //
    // The FM synthesis core (phase generator from KC/KF, ADSR envelopes, the
    // 8 connection algorithms, LFO, noise) is the next increment; step() and
    // update() emit silence meanwhile so the board's audio path can be wired
    // and exercised end to end.
    //
    // tick(cycles) advances the chip by that many chip clocks (3.579545 MHz
    // on the M72, scheduled via the rational-rate scheduler entry).
    class ym2151 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 8;
        static constexpr int operator_count = 4; // per channel

        // One stereo output sample per 64 chip clocks (~55.93 kHz at
        // 3.579545 MHz).
        static constexpr std::uint32_t clocks_per_sample = 64U;
        // Timer cadence in chip clocks: Timer A counts once per 64, Timer B
        // once per 1024 (so a full sweep is 64*(1024-CLKA) / 1024*(256-CLKB)).
        static constexpr std::uint32_t timer_a_step_clocks = 64U;
        static constexpr std::uint32_t timer_b_step_clocks = 1024U;
        // BUSY holds for one sample period after a data write.
        static constexpr std::uint32_t busy_clocks = 64U;

        // Status register bits.
        static constexpr std::uint8_t status_timer_a = 0x01U;
        static constexpr std::uint8_t status_timer_b = 0x02U;
        static constexpr std::uint8_t status_busy = 0x80U;

        struct stereo_sample final {
            std::int16_t left{};
            std::int16_t right{};
        };

        using irq_fn = std::function<void(bool asserted)>;

        ym2151() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override; // advance by chip clocks
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Bus protocol: latch a register address, then write its data byte.
        void write_address(std::uint8_t address) noexcept { address_ = address; }
        void write_data(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept;

        // IRQ line transitions ((timer A flag & enable) | (timer B flag &
        // enable)); fired only on changes.
        void set_irq(irq_fn handler) noexcept { irq_ = std::move(handler); }
        [[nodiscard]] bool irq_asserted() const noexcept { return irq_line_; }

        // Debug / test view of the stored register file.
        [[nodiscard]] std::uint8_t register_value(std::uint8_t address) const noexcept {
            return registers_[address];
        }
        [[nodiscard]] std::uint64_t elapsed_clocks() const noexcept { return elapsed_; }

        // Audio output -- silence until the synthesis increment lands.
        [[nodiscard]] stereo_sample step() noexcept { return {}; }
        // Render out.size()/2 interleaved stereo frames (L,R,L,R,...).
        void update(std::span<std::int16_t> out) noexcept {
            for (std::int16_t& s : out) {
                s = 0;
            }
        }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(ym2151& owner) noexcept : registers_impl_(owner) {}
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }

          private:
            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(ym2151& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                ym2151* owner_;
            };

            registers_impl registers_impl_;
        };

        void step_timer_a() noexcept;
        void step_timer_b() noexcept;
        void update_irq() noexcept;
        [[nodiscard]] std::uint16_t timer_a_load() const noexcept {
            // CLKA: 10 bits across $10 (high 8) and $11 (low 2).
            return static_cast<std::uint16_t>((registers_[0x10] << 2U) | (registers_[0x11] & 3U));
        }

        std::array<std::uint8_t, 256> registers_{};
        std::uint8_t address_{};

        std::uint16_t timer_a_counter_{}; // counts 0..1023, reloads from CLKA
        std::uint16_t timer_b_counter_{}; // counts 0..255, reloads from CLKB
        bool timer_a_running_{};
        bool timer_b_running_{};
        bool timer_a_flag_{};
        bool timer_b_flag_{};
        bool irq_enable_a_{};
        bool irq_enable_b_{};
        bool irq_line_{};

        std::uint32_t prescale_64_{}; // chip clocks toward the next 64-clock step
        std::uint32_t timer_b_sub_{}; // 64-clock steps toward the next 1024-clock step
        std::uint32_t busy_remaining_{};
        std::uint64_t elapsed_{};

        irq_fn irq_{};

        friend class introspection_surface;
        std::array<register_descriptor, 6> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::audio
