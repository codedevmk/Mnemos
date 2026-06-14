#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::audio {

    // Yamaha YM2151 (OPM) FM synthesizer -- the Irem M72 (and a long list of
    // other arcade boards') FM sound chip: 8 channels x 4 operators.
    //
    // Control plane: the address/data register protocol with the BUSY flag,
    // the stored register file, Timer A (10-bit, one count per 64 chip
    // clocks) and Timer B (8-bit, one count per 1024) with the $14
    // run/IRQ-enable/flag-reset control, status reads, and the IRQ line
    // through an edge callback.
    //
    // Synthesis core: the OPM phase generator (7-bit KC octave/note + 6-bit
    // KF fraction + DT2 coarse and DT1 fine detune + MUL), per-operator ADSR
    // envelopes (AR/D1R/D1L/D2R/RR with key scaling, the hardware
    // exponential attack convergence), the 8 OPM connection algorithms with
    // M1 feedback, the log-sine/exp output pipeline shared with the OPN
    // family, and per-channel L/R routing. One stereo sample per 64 chip
    // clocks (~55.93 kHz at 3.579545 MHz). Deferred to the conformance pass:
    // the LFO (PMS/AMS registers are stored, not applied), the channel-7
    // noise generator, CSM, and exact EG rate parity.
    //
    // tick(cycles) advances the chip clocks (timers/BUSY); step()/update()
    // render samples on demand -- the board drains one sample per 64 elapsed
    // clocks so envelope time stays locked to emulated time.
    class ym2151 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 8;
        static constexpr int operator_count = 4; // M1, M2, C1, C2

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

        ym2151() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

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

        // Debug view of the register file, surfaced through the introspection
        // register_view (rebuilt per call into register_view_).
        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

        // Render one stereo sample (advances the envelope/phase generators).
        [[nodiscard]] stereo_sample step() noexcept;
        // Render out.size()/2 interleaved stereo frames (L,R,L,R,...).
        void update(std::span<std::int16_t> out) noexcept;

      private:
        enum class eg_state : std::uint8_t { attack, decay, sustain, release, off };

        struct operator_state final {
            std::uint32_t phase{}; // 20-bit phase counter
            std::int32_t output{}; // last output (feedback source)
            std::int32_t prev_output{};
            std::uint16_t eg_level{0x3FFU}; // 10-bit attenuation, 0 = loudest
            eg_state state{eg_state::off};
            bool key_on{};
            // Parameters decoded from the register file.
            std::uint8_t dt1{};
            std::uint8_t dt2{};
            std::uint8_t mul{};
            std::uint8_t tl{0x7FU};
            std::uint8_t ks{};
            std::uint8_t ar{};
            std::uint8_t d1r{};
            std::uint8_t d2r{};
            std::uint8_t d1l{};
            std::uint8_t rr{};
        };

        struct channel_state final {
            std::array<operator_state, 4> op{}; // M1, M2, C1, C2
            std::uint8_t kc{};
            std::uint8_t kf{};
            std::uint8_t feedback{};
            std::uint8_t connection{};
            std::uint8_t rl{}; // bit0 = left enable, bit1 = right enable
        };

        void step_timer_a() noexcept;
        void step_timer_b() noexcept;
        void update_irq() noexcept;
        [[nodiscard]] std::uint16_t timer_a_load() const noexcept {
            // CLKA: 10 bits across $10 (high 8) and $11 (low 2).
            return static_cast<std::uint16_t>((registers_[0x10] << 2U) | (registers_[0x11] & 3U));
        }

        void key_on_off(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint32_t phase_increment(const channel_state& ch,
                                                    const operator_state& op) const noexcept;
        void eg_step(operator_state& op, std::uint8_t kc) noexcept;
        [[nodiscard]] static std::int32_t op_calc(const operator_state& op, std::uint32_t phase,
                                                  std::int32_t modulation) noexcept;
        [[nodiscard]] std::int32_t channel_calc(channel_state& ch) noexcept;

        std::array<std::uint8_t, 256> registers_{};
        std::array<channel_state, 8> channels_{};
        std::uint8_t address_{};
        std::uint32_t eg_counter_{};

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

        std::array<register_descriptor, 6> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
