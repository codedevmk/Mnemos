#pragma once

#include "adpcm_a.hpp"
#include "adpcm_b.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"
#include "ssg.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha YM2610 (OPNB) -- the Neo-Geo (and a handful of other boards') sound
    // chip. It bundles four synthesis blocks behind one CPU-facing port set:
    //
    //   - 4 FM channels (OPN-family operator core: 4 operators/channel, the
    //     log-sine/exp output pipeline shared with the OPN/OPM family);
    //   - 3 SSG (PSG) channels (the AY-3-8910 / YM2149 square+noise+envelope
    //     block);
    //   - 6 ADPCM-A fixed-rate rhythm/voice channels (external sample ROM);
    //   - 1 ADPCM-B streaming/variable-rate channel (external sample ROM).
    //
    // CPU-facing port set (standard OPNB pinout, two banked address/data pairs):
    //   $00/$01  bank-A address / data
    //   $02/$03  bank-B address / data
    //
    // Bank-A register layout:
    //   $00..$0F  SSG          (delegated to the ssg sub-chip)
    //   $10..$1B  ADPCM-B      (delegated to the adpcm_b sub-chip; bus offset
    //                           $10 maps the block's local $00..$0B)
    //   $22..$2F  FM common: LFO / timers / FM key-on/off ($28)
    //   $30..$B6  FM channels 1+2 operator + frequency + algorithm registers
    //
    // Bank-B register layout:
    //   $00..$2D  ADPCM-A      (delegated to the adpcm_a sub-chip; bus offset
    //                           is the block's local $00..$2D)
    //   $30..$B6  FM channels 3+4 operator + frequency + algorithm registers
    //
    // This is a Mnemos iaudio_synth in the rf5c68 idiom: tick() advances chip
    // clocks and (when capture is enabled) queues one interleaved (L,R) stereo
    // frame per native sample; step()/generate() render on demand. The three
    // PSG/ADPCM blocks are composed as member sub-chips and mixed with the FM
    // core into the final stereo pair. Save/load serialises the FM core plus
    // every sub-chip's state.
    //
    // Clean-room per the Yamaha YM2610 application manual + the OPN-family
    // datasheets; the FM core mirrors the in-tree OPM/OPN idiom. No emulator
    // source consulted.
    class ym2610 final : public iaudio_synth {
      public:
        static constexpr int fm_channel_count = 4;
        static constexpr int operator_count = 4; // S1, S2, S3, S4

        // One stereo output sample per 24 *prescaled* FM clocks. The native FM
        // sample rate is the input clock / 6 (the OPN prescaler) / 24 (one
        // sample per 24 internal cycles); the divider here is the input-clock
        // count per native sample step.
        static constexpr int default_clock_divider = 144;
        static constexpr int adpcm_a_sample_divider = 3;

        // Status register bits (timer flags + busy).
        static constexpr std::uint8_t status_timer_a = 0x01U;
        static constexpr std::uint8_t status_timer_b = 0x02U;
        static constexpr std::uint8_t status_busy = 0x80U;

        using irq_fn = std::function<void(bool asserted)>;

        ym2610() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_reg_writes([this](instrumentation::reg_write_trace::callback cb) {
                    reg_write_callback_ = std::move(cb);
                });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Bus protocol: latch a register address into bank A or B, then write
        // the data byte. Bank A carries SSG / ADPCM-B / FM-ch-1+2; bank B carries
        // ADPCM-A / FM-ch-3+4.
        void write_address_a(std::uint8_t address) noexcept { bank_a_addr_ = address; }
        void write_data_a(std::uint8_t value) noexcept;
        void write_address_b(std::uint8_t address) noexcept { bank_b_addr_ = address; }
        void write_data_b(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept;

        // IRQ line transitions ((timer A flag & enable) | (timer B flag &
        // enable)); fired only when the level changes.
        void set_irq(irq_fn handler) noexcept { irq_ = std::move(handler); }
        [[nodiscard]] bool irq_asserted() const noexcept { return irq_line_; }

        // Sub-chip access for the integrator (sample-ROM loading, introspection).
        [[nodiscard]] ssg& ssg_block() noexcept { return ssg_; }
        [[nodiscard]] adpcm_a& adpcm_a_block() noexcept { return adpcm_a_; }
        [[nodiscard]] adpcm_b& adpcm_b_block() noexcept { return adpcm_b_; }

        // Debug / test view of an FM register (bank A < 0x100, bank B at +0x100).
        [[nodiscard]] std::uint8_t fm_register(std::uint16_t address) const noexcept {
            return fm_regs_[address & 0x1FFU];
        }

        // Render one stereo sample (advances the FM phase/envelope generators and
        // steps every sub-chip once), updating last_left()/last_right().
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even).
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors rf5c68 / ym2612): when enabled, tick()
        // queues one interleaved (L,R) stereo frame per native sample step. The
        // counts below are in STEREO FRAMES (pairs) -- matching the player's
        // add_source() contract -- NOT raw int16 samples.
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
        enum class eg_state : std::uint8_t { attack, decay, sustain, release, off };

        struct operator_state final {
            std::uint32_t phase{};          // 20-bit phase accumulator
            std::int32_t output{};          // last output (feedback source)
            std::int32_t prev_output{};     // previous output (feedback source)
            std::uint16_t eg_level{0x3FFU}; // 10-bit attenuation, 0 = loudest
            eg_state state{eg_state::off};
            bool key_on{};
            // Parameters decoded from the FM register file.
            std::uint8_t dt{};      // 3-bit detune
            std::uint8_t mul{};     // 4-bit multiple
            std::uint8_t tl{0x7FU}; // 7-bit total level
            std::uint8_t ks{};      // 2-bit key-scale
            std::uint8_t ar{};      // 5-bit attack rate
            std::uint8_t d1r{};     // 5-bit first decay rate
            std::uint8_t d2r{};     // 5-bit second decay (sustain) rate
            std::uint8_t d1l{};     // 4-bit sustain level
            std::uint8_t rr{};      // 4-bit release rate
        };

        struct channel_state final {
            std::array<operator_state, 4> op{}; // S1, S2, S3, S4
            std::uint16_t fnum{};               // 11-bit F-number
            std::uint8_t block{};               // 3-bit block (octave)
            std::uint8_t feedback{};
            std::uint8_t algorithm{};
            std::uint8_t pan{0x03U}; // bit0 = left enable, bit1 = right enable
        };

        // FM bus write decode (one bank). `bank` selects the register image and
        // the channel-pair the operator/frequency registers address.
        void write_fm(int bank, std::uint8_t reg, std::uint8_t value) noexcept;
        void fm_key_on_off(std::uint8_t value) noexcept;
        void note_write(std::uint16_t port, std::uint8_t value) {
            if (reg_write_callback_) {
                reg_write_callback_({.port = port, .value = value});
            }
        }
        void write_timer_control(std::uint8_t value) noexcept;
        void update_irq() noexcept;
        void step_timer_a() noexcept;
        void step_timer_b() noexcept;
        [[nodiscard]] std::uint16_t timer_a_load() const noexcept;

        [[nodiscard]] std::uint32_t phase_increment(const channel_state& ch,
                                                    const operator_state& op) const noexcept;
        void eg_step(operator_state& op, std::uint8_t kc) noexcept;
        [[nodiscard]] static std::int32_t op_calc(const operator_state& op, std::uint32_t phase,
                                                  std::int32_t modulation) noexcept;
        [[nodiscard]] std::int32_t channel_calc(channel_state& ch) noexcept;
        [[nodiscard]] static std::uint8_t key_code(const channel_state& ch) noexcept;

        // Member sub-chips (composition).
        ssg ssg_{};
        adpcm_a adpcm_a_{};
        adpcm_b adpcm_b_{};

        // FM core state.
        std::array<channel_state, fm_channel_count> channels_{};
        std::uint32_t eg_counter_{};

        // FM register shadow: 0x000..0x0FF = bank A, 0x100..0x1FF = bank B. Bank A
        // low registers are routed to the sub-chips and not synthesised from here.
        std::array<std::uint8_t, 0x200> fm_regs_{};
        std::uint8_t bank_a_addr_{};
        std::uint8_t bank_b_addr_{};

        // FM timers (bank-A $24..$27 CLKA, $26 CLKB, $27 control).
        std::uint16_t timer_a_counter_{};
        std::uint16_t timer_b_counter_{};
        bool timer_a_running_{};
        bool timer_b_running_{};
        bool timer_a_flag_{};
        bool timer_b_flag_{};
        bool irq_enable_a_{};
        bool irq_enable_b_{};
        bool irq_line_{};
        std::uint32_t timer_prescale_{}; // input clocks toward the next timer step
        std::uint32_t timer_b_sub_{};
        std::uint32_t busy_remaining_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        int clock_divider_{default_clock_divider};
        int prescaler_{};
        int adpcm_a_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 99> register_view_{};
        instrumentation::introspection_builder introspection_;
        instrumentation::reg_write_trace::callback reg_write_callback_{};
        irq_fn irq_{};
    };

} // namespace mnemos::chips::audio
