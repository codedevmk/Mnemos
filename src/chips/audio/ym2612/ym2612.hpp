#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::audio {

    // Yamaha YM2612 (OPN2) FM synthesizer -- the Sega Genesis / Mega Drive FM sound
    // chip: 6 channels x 4 operators, 8 FM algorithms, per-operator ADSR + SSG-EG,
    // an LFO (vibrato/tremolo), channel-3 per-operator frequency mode, channel-6 DAC
    // (8-bit PCM), two programmable timers, and stereo panning. Ported from the Emu
    // reference core (ADR 0006).
    //
    // Built in phases. THIS phase is the control plane: the full register file (every
    // $20-$B6 register decoded into the operator/channel parameter state), the two
    // timers (Timer A 10-bit / Timer B 8-bit with the chip-accurate master-clock
    // cadence, overflow flags, and the status register), the channel-6 DAC, the
    // LFO/key-on latches, stereo routing, and save/load. The FM tone generation
    // (phase + envelope + algorithm mixing into stereo samples) arrives in phase 2;
    // until then the sample output is silence.
    //
    // tick(cycles) advances the timer prescalers by that many YM master clocks; games
    // poll read_status() for timer overflow. write() takes the two-port address/data
    // protocol the 68000 and Z80 drive.
    class ym2612 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 6;
        static constexpr int operator_count = 4; // per channel

        // Chip-accurate timer prescaler periods, in YM master clocks.
        static constexpr std::uint32_t timer_a_master_period = 1008U;
        static constexpr std::uint32_t timer_b_master_period = 16128U;

        enum class eg_phase : std::uint8_t { attack, decay, sustain, release, off };

        struct stereo_sample final {
            std::int16_t left{};
            std::int16_t right{};
        };

        ym2612() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override; // advance the timers by master clocks
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Register access. port 0 = channels 1-3 ($4000/$4001), port 1 = channels 4-6
        // ($4002/$4003); addr_or_data false = latch the register address, true = write
        // the data byte to the latched register.
        void write(int port, bool addr_or_data, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept { return status_; }

        // Advance the Timer A/B prescalers by `master_clocks`; returns true if a
        // timer overflow raised its (enabled) IRQ during the advance.
        bool tick_timers_master(std::uint32_t master_clocks) noexcept;

        // Generate one stereo output sample (the chip emits one sample every 144
        // master clocks). Phase 1 produces silence -- the FM tone generation lands
        // in phase 2; until then this exists so the audio path has its shape.
        [[nodiscard]] stereo_sample step() noexcept;
        [[nodiscard]] stereo_sample last_sample() const noexcept {
            return {last_left_, last_right_};
        }

        // Configure the analog-character low-pass on the stereo output. cutoff_hz<=0
        // disables it (the YM2612's discrete-DAC "ladder" output is famously dark).
        void set_lowpass_cutoff_hz(int sample_rate_hz, int cutoff_hz) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

        // ---- introspection / test accessors ----
        [[nodiscard]] std::uint16_t timer_a_load() const noexcept { return timer_a_load_; }
        [[nodiscard]] std::uint8_t timer_b_load() const noexcept { return timer_b_load_; }
        [[nodiscard]] bool dac_enabled() const noexcept { return dac_enable_; }
        [[nodiscard]] std::uint8_t dac_data() const noexcept { return dac_data_; }
        [[nodiscard]] bool lfo_enabled() const noexcept { return lfo_enable_; }
        [[nodiscard]] std::uint16_t channel_fnum(int ch) const noexcept;
        [[nodiscard]] std::uint8_t channel_block(int ch) const noexcept;
        [[nodiscard]] std::uint8_t channel_algorithm(int ch) const noexcept;
        [[nodiscard]] std::uint8_t operator_total_level(int ch, int op) const noexcept;
        [[nodiscard]] bool operator_key_on(int ch, int op) const noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        struct operator_state final {
            // Register-programmed parameters.
            std::uint8_t detune{};
            std::uint8_t multiply{};
            std::uint8_t total_level{};
            std::uint8_t key_scale{};
            std::uint8_t attack_rate{};
            std::uint8_t decay_rate{};
            std::uint8_t sustain_rate{};
            std::uint8_t release_rate{};
            std::uint8_t sustain_level{};
            bool am_enable{};
            std::uint8_t ssg_eg{};
            bool key_on{};
            eg_phase phase{eg_phase::off};
            // Synthesis state (driven by phase 2; saved for completeness).
            std::uint32_t pg_phase{};
            std::uint32_t pg_increment{};
            std::uint16_t eg_level{0x3FFU};
        };

        struct channel_state final {
            std::array<operator_state, operator_count> op{};
            std::uint16_t fnum{};
            std::uint8_t block{};
            std::uint8_t algorithm{};
            std::uint8_t feedback{};
            bool left{};
            bool right{};
            std::uint8_t ams{};
            std::uint8_t pms{};
            std::uint8_t fnum_hi_latch{};
            std::uint8_t block_latch{};
        };

        void write_register(int port, std::uint8_t reg, std::uint8_t value) noexcept;
        [[nodiscard]] bool timer_a_tick() noexcept;
        [[nodiscard]] bool timer_b_tick() noexcept;
        static void eg_key_on(operator_state& op) noexcept;
        static void eg_key_off(operator_state& op) noexcept;

        std::array<channel_state, channel_count> ch_{};

        std::array<std::uint16_t, 4> ch3_fnum_{};
        std::array<std::uint8_t, 4> ch3_block_{};
        std::array<std::uint8_t, 4> ch3_fnum_hi_latch_{};
        std::array<std::uint8_t, 4> ch3_block_latch_{};
        std::uint8_t ch3_mode_{};

        bool dac_enable_{};
        std::uint8_t dac_data_{};

        bool lfo_enable_{};
        std::uint8_t lfo_freq_{};

        std::uint16_t timer_a_load_{};
        std::uint16_t timer_a_cnt_{};
        std::uint8_t timer_b_load_{};
        std::uint16_t timer_b_cnt_{};
        std::uint8_t timer_b_div_{};
        bool timer_a_run_{};
        bool timer_b_run_{};
        bool timer_a_ovf_{};
        bool timer_b_ovf_{};
        bool timer_a_irq_en_{};
        bool timer_b_irq_en_{};
        std::uint32_t timer_a_accum_{};
        std::uint32_t timer_b_accum_{};

        std::array<std::uint8_t, 2> addr_latch_{};
        std::uint8_t status_{};
        bool csm_key_pending_{};

        // Analog-output low-pass (per channel of the stereo pair). 0 = pass-through.
        std::int32_t lp_alpha_q15_{};
        std::int32_t lp_state_l_{};
        std::int32_t lp_state_r_{};
        std::int16_t last_left_{};
        std::int16_t last_right_{};

        std::array<register_descriptor, 14> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::audio
