#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha YM2612 (OPN2) FM synthesizer -- the Sega Genesis / Mega Drive FM sound
    // chip: 6 channels x 4 operators, 8 FM algorithms, per-operator ADSR + SSG-EG,
    // an LFO (vibrato/tremolo), channel-3 per-operator frequency mode, channel-6 DAC
    // (8-bit PCM), two programmable timers, and stereo panning.
    //
    // The control plane: the full register file (every $20-$B6 register decoded into
    // the operator/channel parameter state), the two timers (Timer A 10-bit / Timer B
    // 8-bit with the chip-accurate master-clock cadence, overflow flags, and the
    // status register), the channel-6 DAC, the LFO/key-on latches, stereo routing,
    // and save/load.
    //
    // The synthesis core: the phase generator (fnum/block/detune/multiply with LFO
    // vibrato), the per-operator envelope generator (attack/decay/sustain/release with
    // key-scaling and SSG-EG), the eight FM algorithms with operator-0 feedback, LFO
    // tremolo, and the log-sine/exp output pipeline -- mixed per channel, soft-clipped,
    // and run through the analog low-pass into stereo samples. Built from the canonical
    // hardware-verified model and tables (see THIRD-PARTY-REFERENCES.md).
    //
    // tick(cycles) advances the timer prescalers by that many YM master clocks; games
    // poll read_status() for timer overflow. step()/update() render audio (one stereo
    // sample per 1008 master clocks). write() takes the two-port address/data protocol
    // the 68000 and Z80 drive.
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

        ym2612() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

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
        // master clocks): advances the LFO and envelope generators, runs all six
        // channels' FM algorithms (channel 6 may be the DAC), soft-clips the mix,
        // and applies the analog low-pass.
        [[nodiscard]] stereo_sample step() noexcept;
        // Render out.size()/2 interleaved stereo frames (L,R,L,R,...).
        void update(std::span<std::int16_t> out) noexcept;
        [[nodiscard]] stereo_sample last_sample() const noexcept {
            return {last_left_, last_right_};
        }

        // Configure the analog-character low-pass on the stereo output. cutoff_hz<=0
        // disables it (the YM2612's discrete-DAC "ladder" output is famously dark).
        void set_lowpass_cutoff_hz(int sample_rate_hz, int cutoff_hz) noexcept;

        // ---- audio sink for real-time playback ----
        //
        // When enabled, tick(cycles) also runs step() at the chip's native
        // cadence (one stereo sample per 1008 master clocks) and appends the
        // resulting (L,R) pair to an internal queue. drain_samples() copies
        // up to `max_pairs` (L,R) pairs into `out` and removes them from the
        // queue; pending_samples() reports how many pairs are queued. Used
        // by the windowed player to feed SDL_AudioStream from the runtime
        // schedule. Disabled until enable_audio_capture(true) is called so
        // headless tests don't pay the cost.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;

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
        // Exposes the chip's register file (register_snapshot) through the
        // introspection register_view, so debuggers and the audio exporter can
        // read voice state without downcasting.

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
            // Synthesis state.
            std::uint32_t pg_phase{};       // 20-bit phase accumulator
            std::uint32_t pg_increment{};   // per-sample phase delta (from freq/detune/mul)
            std::uint16_t eg_level{0x3FFU}; // envelope attenuation (0=loud, 1023=silent)
            std::int32_t output{};          // last operator output (feedback source)
            std::int32_t prev_output{};     // output one sample earlier (feedback source)
            bool ssg_inv{};                 // SSG-EG inversion latch
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

        // ---- FM synthesis (phase 2) ----
        // Recompute every operator's phase increment for a channel from its current
        // frequency/detune/multiply (and the channel-3 per-operator frequencies).
        void update_channel_freq(int ch_idx) noexcept;
        // Mix one channel's operators per its FM algorithm for the current sample.
        [[nodiscard]] std::int32_t channel_calc(int ch_idx, std::uint32_t am_offset) noexcept;
        void lfo_tick() noexcept;
        [[nodiscard]] std::uint32_t lfo_am_offset(std::uint8_t ams) const noexcept;
        [[nodiscard]] static std::uint32_t calc_phase_inc_value(const operator_state& op,
                                                                std::uint16_t fnum,
                                                                std::uint8_t block, std::uint8_t kc,
                                                                bool extra_precision) noexcept;
        [[nodiscard]] static std::uint32_t
        calc_lfo_phase_inc(const operator_state& op, std::uint16_t fnum, std::uint8_t block,
                           std::uint8_t kc, std::uint8_t pms, std::uint8_t lfo_counter) noexcept;
        [[nodiscard]] static bool ssg_output_inverted(const operator_state& op) noexcept;
        [[nodiscard]] static std::uint16_t ssg_eg_inc(const operator_state& op,
                                                      std::uint8_t inc) noexcept;
        static void ssg_boundary_step(operator_state& op) noexcept;
        static void eg_step(operator_state& op, std::uint8_t kc, std::uint32_t eg_cnt) noexcept;
        [[nodiscard]] static std::int32_t op_calc(operator_state& op, std::int32_t modulation,
                                                  std::uint32_t am_offset) noexcept;

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
        std::uint8_t lfo_counter_{}; // 7-bit LFO step counter
        std::uint8_t lfo_phase_{};
        std::uint8_t lfo_divider_{}; // sub-counter gating lfo_counter_ steps

        std::uint8_t eg_timer_{};  // /3 prescaler: EG advances once per 3 samples
        std::uint32_t eg_clock_{}; // 12-bit envelope-generator clock

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

        // Real-time audio sink (enabled via enable_audio_capture). The chip
        // emits one stereo sample every 144 internal chip cycles (the scheduler
        // calls tick(cycles) in chip cycles, not master clocks -- the divider
        // /7 happens upstream). 144 chip cycles = 1008 master clocks = the
        // hardware sample period. Samples are pushed to `sample_queue_`
        // interleaved (L,R,L,R,...) and drained by the host.
        static constexpr std::uint32_t chip_cycles_per_sample = 144U;
        bool audio_capture_{};
        std::uint32_t sample_accum_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 14> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
