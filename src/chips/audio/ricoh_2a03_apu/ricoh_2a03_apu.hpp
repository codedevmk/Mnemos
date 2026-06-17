#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Ricoh 2A03 APU -- the NES audio processor that shares the 2A03 package with a
    // BCD-disabled 6502. Five channels: two pulse (square) generators with a duty
    // selector, sweep, length counter and volume envelope; a triangle generator
    // with a linear counter; a noise generator built from a 15-bit LFSR; and a DMC
    // delta-modulation channel with a 7-bit DAC. Programmed through the $4000..$4017
    // register window in CPU address space; $4015 is the channel-enable / status
    // port and $4017 the frame-counter control. A frame-sequencer clocked off the
    // CPU clock issues quarter-frame (envelope + triangle linear) and half-frame
    // (length + sweep) ticks and, in 4-step mode, a frame IRQ.
    //
    // The native mix is mono; like the volume-table chips it duplicates the single
    // sample into both stereo lanes, so capture counts are STEREO PAIRS.
    //
    // Ported from the Emu reference (chips/ricoh_2a03_apu); clean-room per the
    // Ricoh 2A03 APU datasheet. The register-decode + frame-counter math is taken
    // integer-exact from the reference; the channel oscillators and the mixer are
    // clean-room from the datasheet (the reference left synthesis to a follow-up).
    class ricoh_2a03_apu final : public iaudio_synth {
      public:
        static constexpr int default_clock_divider = 1;

        // CPU-space register addresses ($4000..$4017).
        enum reg : std::uint16_t {
            reg_pulse1_0 = 0x4000U, // duty / loop / constant-vol / volume-env
            reg_pulse1_1 = 0x4001U, // sweep
            reg_pulse1_2 = 0x4002U, // timer low
            reg_pulse1_3 = 0x4003U, // timer high + length load
            reg_pulse2_0 = 0x4004U,
            reg_pulse2_1 = 0x4005U,
            reg_pulse2_2 = 0x4006U,
            reg_pulse2_3 = 0x4007U,
            reg_tri_0 = 0x4008U, // linear-counter control + reload
            reg_tri_2 = 0x400AU, // timer low
            reg_tri_3 = 0x400BU, // timer high + length load
            reg_noise_0 = 0x400CU,
            reg_noise_2 = 0x400EU, // mode + period index
            reg_noise_3 = 0x400FU, // length load
            reg_dmc_0 = 0x4010U,   // IRQ enable + loop + rate index
            reg_dmc_1 = 0x4011U,   // direct DAC load
            reg_dmc_2 = 0x4012U,   // sample address
            reg_dmc_3 = 0x4013U,   // sample length
            reg_status = 0x4015U,  // channel enable (write) / status (read)
            reg_frame_counter = 0x4017U,
        };

        // Status-register bits ($4015).
        static constexpr std::uint8_t status_pulse1 = 1U << 0U;
        static constexpr std::uint8_t status_pulse2 = 1U << 1U;
        static constexpr std::uint8_t status_triangle = 1U << 2U;
        static constexpr std::uint8_t status_noise = 1U << 3U;
        static constexpr std::uint8_t status_dmc = 1U << 4U;
        static constexpr std::uint8_t status_frame_irq = 1U << 6U; // read-only
        static constexpr std::uint8_t status_dmc_irq = 1U << 7U;   // read-only

        // Frame-counter bits ($4017).
        static constexpr std::uint8_t frame_mode_bit = 1U << 7U; // 0=4-step, 1=5-step
        static constexpr std::uint8_t frame_irq_inhibit_bit = 1U << 6U;

        ricoh_2a03_apu() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // CPU-bus register access. `addr` is the full CPU address (only the low
        // bits inside $4000..$4017 are decoded; everything else is open-bus).
        [[nodiscard]] std::uint8_t read_reg(std::uint16_t addr) noexcept;
        void write_reg(std::uint16_t addr, std::uint8_t value) noexcept;

        // The /IRQ line: set by the 4-step-mode frame IRQ or the DMC IRQ.
        [[nodiscard]] bool irq_asserted() const noexcept {
            return frame_irq_flag_ || dmc_irq_flag_;
        }

        // Generate one native mono sample, updating last_sample()/last_left()/
        // last_right() and advancing every enabled channel's oscillator.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_sample_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_sample_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even). The mono
        // mix is duplicated to both lanes.
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors rf5c68): when enabled, tick() queues one
        // interleaved (L,R) stereo frame per native sample step. The counts below
        // are in STEREO FRAMES (pairs) -- matching the player's add_source()
        // contract -- NOT raw int16 samples.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        // Copies up to `max_pairs` (L,R) pairs into `out` (2*max_pairs int16) and
        // removes them from the queue; returns the number of pairs copied.
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        // Input cycles per native sample step.
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // A pulse (square) channel: 11-bit timer, 4-bit duty, length counter and a
        // volume envelope. The oscillator counts the timer down at half the native
        // step rate (the hardware clocks the sequencer every other APU cycle) and
        // advances an 8-step duty sequence.
        struct pulse_channel {
            std::uint8_t r0{}, r1{}, r2{}, r3{};
            std::uint8_t duty{};
            bool length_halt{};
            bool constant_volume{};
            std::uint8_t volume_env{};
            bool sweep_enable{};
            std::uint8_t sweep_period{};
            bool sweep_negate{};
            std::uint8_t sweep_shift{};
            std::uint16_t timer{};
            std::uint8_t length_load{};
            bool enabled{};
            std::uint8_t length_counter{};
            // Oscillator runtime.
            std::uint16_t timer_counter{};
            std::uint8_t sequence_step{};
        };

        struct triangle_channel {
            std::uint8_t r0{}, r2{}, r3{};
            bool length_halt{};
            std::uint8_t linear_reload{};
            std::uint16_t timer{};
            std::uint8_t length_load{};
            bool enabled{};
            std::uint8_t length_counter{};
            std::uint16_t timer_counter{};
            std::uint8_t sequence_step{};
            std::uint8_t linear_counter{};
        };

        struct noise_channel {
            std::uint8_t r0{}, r2{}, r3{};
            bool length_halt{};
            bool constant_volume{};
            std::uint8_t volume_env{};
            bool mode{};
            std::uint8_t period_index{};
            std::uint8_t length_load{};
            bool enabled{};
            std::uint8_t length_counter{};
            std::uint16_t timer_counter{};
            std::uint16_t lfsr{1U};
        };

        struct dmc_channel {
            std::uint8_t r0{}, r1{}, r2{}, r3{};
            bool irq_enable{};
            bool loop_sample{};
            std::uint8_t rate_index{};
            std::uint8_t direct_load{};
            std::uint16_t sample_address{};
            std::uint16_t sample_length{};
            bool enabled{};
            std::uint8_t output_level{}; // 7-bit DAC level
        };

        void pulse_decode(pulse_channel& p) noexcept;
        void triangle_decode(triangle_channel& t) noexcept;
        void noise_decode(noise_channel& n) noexcept;
        void dmc_decode(dmc_channel& d) noexcept;
        void status_write(std::uint8_t value) noexcept;
        void frame_counter_write(std::uint8_t value) noexcept;
        // Advance the per-step oscillators and return the mono mix.
        [[nodiscard]] std::int16_t mix_step() noexcept;

        std::array<pulse_channel, 2> pulse_{};
        triangle_channel triangle_{};
        noise_channel noise_{};
        dmc_channel dmc_{};

        bool frame_mode_5step_{};
        bool frame_irq_inhibit_{};
        bool frame_irq_flag_{};
        bool dmc_irq_flag_{};
        std::uint8_t open_bus_latch_{};
        std::uint64_t cpu_cycles_{};

        std::int16_t last_sample_{};

        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
