#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha YM2413 (OPLL) FM sound generator -- a cost-reduced 2-operator FM
    // synth used as an FM add-on module and in home-computer sound expansions.
    // Nine 2-operator melody channels (modulator -> carrier series FM); each
    // operator has AM/vibrato enables, an envelope-type bit, key-scale rate, a
    // 4-bit frequency multiplier, key-scale level, total level, and a 4-bit
    // ADSR. Fifteen
    // instrument patches live in an on-chip mask ROM plus one user-programmable
    // slot. The phase generator is a 19-bit accumulator feeding a log-domain
    // quarter-sine LUT with an exp post-decode; the envelope generator is a
    // 9-bit attenuation ADSR. Native output is mono ~49716 Hz (input clock
    // 3.579545 MHz / 72 on NTSC).
    //
    // The host control plane is the two-write register protocol (address latch
    // then data) plus the audio-select mux port. The chip emits a single mono
    // sample per native step; for the stereo capture/generate contract shared
    // with the other audio chips the mono sample is duplicated onto both lanes
    // (see the volume table mirroring in rf5c68).
    //
    // Ported from the Emu reference (chips/ym2413); clean-room per the Yamaha
    // YM2413 (OPLL) datasheet.
    class ym2413 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 9;
        static constexpr int operator_count = 2; // modulator, carrier
        static constexpr std::size_t user_instrument_size = 8U;

        // Native sample rate at the canonical NTSC input clock (3.579545 MHz /
        // 72). One native step produces one mono sample at this rate.
        static constexpr std::uint32_t native_sample_rate = 49716U;

        // Register addresses (per the OPLL application manual).
        enum reg : std::uint8_t {
            reg_user_inst_base = 0x00, // $00..$07: user-instrument bytes
            reg_rhythm = 0x0E,         // rhythm mode + percussion key-on
            reg_test = 0x0F,           // test register
            reg_fnum_low_base = 0x10,  // $10..$18: channel F-Number low 8 bits
            reg_control_base = 0x20,   // $20..$28: sustain/key-on/block/F-Number high
            reg_inst_vol_base = 0x30,  // $30..$38: instrument select + volume
        };

        // Envelope-generator state machine (OPLL EG states).
        enum class eg_state : std::uint8_t { damp, attack, decay, sustain, release, off };

        ym2413() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Host register protocol: latch a register address, then write a data
        // byte to the latched register.
        void write_address(std::uint8_t address) noexcept { addr_latch_ = address; }
        void write_data(std::uint8_t value) noexcept;

        // Audio-select mux port (bit 0 = 1 selects FM output over PSG).
        void write_audio_select(std::uint8_t value) noexcept { audio_select_ = value; }
        [[nodiscard]] std::uint8_t read_audio_select() const noexcept { return audio_select_; }

        // Debug view of the latched register address and user-instrument bytes.
        [[nodiscard]] std::uint8_t address_latch() const noexcept { return addr_latch_; }

        // Generate one native mono sample, updating last_sample() and advancing
        // every active channel's phase + envelope generators.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even). The
        // chip is mono: each native sample is written to both lanes.
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors rf5c68/ym2612): when enabled, tick()
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
        struct op_state final {
            // Decoded instrument parameters.
            std::uint8_t am{};    // AM enable
            std::uint8_t vib{};   // vibrato enable
            std::uint8_t egt{};   // envelope type (0 = percussive, 1 = sustained)
            std::uint8_t ksr{};   // key-scale rate
            std::uint8_t multi{}; // frequency multiplier (4-bit)
            std::uint8_t ksl{};   // key-scale level (2-bit)
            std::uint8_t tl{};    // total level (6-bit; modulator only)
            std::uint8_t fb{};    // feedback level (3-bit; modulator only)
            std::uint8_t ar{};    // attack rate (4-bit)
            std::uint8_t dr{};    // decay rate (4-bit)
            std::uint8_t sl{};    // sustain level (4-bit)
            std::uint8_t rr{};    // release rate (4-bit)
            std::uint8_t wf{};    // waveform select (1-bit)

            // Runtime state.
            std::uint32_t phase{};                  // 19-bit phase accumulator
            eg_state state{eg_state::off};          // envelope state
            std::uint16_t eg_level{0x1FFU};         // 9-bit attenuation (0x1FF = silent)
            std::array<std::int16_t, 2> feedback{}; // last 2 modulator outputs
        };

        struct channel_state final {
            std::uint16_t f_number{};                  // 9-bit F-Number ($10 + $20 bit 0)
            std::uint8_t block{};                      // 3-bit octave / block
            bool key_on{};                             // $20 bit 4
            bool sustain{};                            // $20 bit 5
            std::uint8_t instrument{};                 // $30 bits 7-4 (0 = user)
            std::uint8_t volume{0xFU};                 // $30 bits 3-0 (attenuation, 0 = loud)
            bool prev_key_on{};                        // cached key-on for edge detection
            std::array<op_state, operator_count> op{}; // modulator, carrier
        };

        void apply_instrument(channel_state& c, std::span<const std::uint8_t, 8> inst) noexcept;
        void refresh_channel_instrument(int ch_index) noexcept;
        void op_eg_tick(op_state& o) noexcept;
        [[nodiscard]] std::int32_t channel_sample(channel_state& c) noexcept;

        std::array<std::uint8_t, user_instrument_size> user_instrument_{};
        std::uint8_t rhythm_ctrl_{};
        std::uint8_t test_reg_{};
        std::uint8_t addr_latch_{};
        std::uint8_t audio_select_{};

        std::array<channel_state, channel_count> channels_{};

        std::uint32_t am_counter_{};
        std::uint32_t vib_counter_{};
        std::uint32_t eg_counter_{};

        std::int16_t last_sample_{};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 8> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
