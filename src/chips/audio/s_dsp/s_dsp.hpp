#pragma once

#include "audio_views.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnemos::chips::audio {

    // Sony S-DSP -- the SNES audio DSP (the digital half of the S-SMP package).
    // Eight voices play BRR-compressed (4-bit ADPCM, 16 samples per 9-byte block)
    // samples from a host-provided 64 KB audio RAM. Each voice has a 14-bit pitch
    // (Gaussian-interpolated), an ADSR-or-GAIN amplitude envelope, signed L/R
    // volumes, and optional noise and pitch-modulation routing. The mix is scaled
    // by a master L/R volume and summed with a 32 kHz FIR echo path. The CPU side
    // reaches the chip through a register-select / data pair ($F2/$F3); the DSP
    // exposes a flat 128-byte register file (8 x 16-byte voice blocks plus sparse
    // globals). A sample directory at page DIR*256 maps each voice's source number
    // (SRCN) to a {start, loop} address pair in audio RAM. Native output is 32 kHz
    // stereo.
    //
    // Ported from the Emu reference (chips/s_dsp); clean-room per the Sony S-DSP
    // datasheet. The Emu source shipped the register surface + KON/KOFF latch only
    // (its own comments list decode/envelope/echo as follow-ups); the synthesis
    // core here is the documented next stage, implemented integer-exact per the
    // datasheet and kept standalone over the host-set audio-RAM span (the rf5c68
    // wave-RAM idiom). The hardware's 4-point Gaussian pitch filter is rendered
    // as a deterministic linear interpolation (a documented simplification).
    class s_dsp final : public iaudio_synth {
      public:
        static constexpr int voice_count = 8;
        static constexpr std::size_t reg_count = 128;
        static constexpr std::size_t aram_size = 64U * 1024U;
        static constexpr int fir_taps = 8;

        // Per-voice register offsets within the voice's 16-byte block.
        enum vreg : std::uint8_t {
            vreg_vol_l = 0x00, // signed left volume
            vreg_vol_r = 0x01, // signed right volume
            vreg_p_l = 0x02,   // pitch low byte
            vreg_p_h = 0x03,   // pitch high byte (14-bit pitch across P_L/P_H)
            vreg_srcn = 0x04,  // source number (sample-directory index)
            vreg_adsr1 = 0x05, // attack/decay
            vreg_adsr2 = 0x06, // sustain/release (bit7 selects ADSR vs GAIN)
            vreg_gain = 0x07,  // GAIN envelope (used when ADSR2 bit7 = 0)
            vreg_envx = 0x08,  // current envelope level (read-only)
            vreg_outx = 0x09,  // current waveform sample (read-only)
        };

        // Global register indices (sparse).
        enum greg : std::uint8_t {
            reg_mvol_l = 0x0C, // master volume left
            reg_mvol_r = 0x1C, // master volume right
            reg_evol_l = 0x2C, // echo volume left
            reg_evol_r = 0x3C, // echo volume right
            reg_kon = 0x4C,    // key-on bitmask
            reg_koff = 0x5C,   // key-off bitmask
            reg_flg = 0x6C,    // reset / mute / echo-disable / noise clock
            reg_endx = 0x7C,   // voice-end flags (read-only)
            reg_efb = 0x0D,    // echo feedback (signed)
            reg_pmon = 0x2D,   // pitch-modulation enable mask
            reg_non = 0x3D,    // noise enable mask
            reg_eon = 0x4D,    // echo enable mask
            reg_dir = 0x5D,    // sample-directory base page (x256)
            reg_esa = 0x6D,    // echo buffer start page (x256)
            reg_edl = 0x7D,    // echo delay (low nibble x 2 KB)
        };

        // FLG bit layout.
        static constexpr std::uint8_t flg_noise_clk = 0x1FU; // bits 0..4
        static constexpr std::uint8_t flg_echo_disable = 0x20U;
        static constexpr std::uint8_t flg_mute = 0x40U;
        static constexpr std::uint8_t flg_reset = 0x80U;

        s_dsp() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_audio(&audio_);
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // DSP register file access. `addr` is the 7-bit register index ($00..$7F);
        // this is the data port the host CPU drives after selecting `addr`.
        [[nodiscard]] std::uint8_t read_reg(std::uint8_t addr) const noexcept;
        void write_reg(std::uint8_t addr, std::uint8_t value) noexcept;

        // Host-provided audio RAM (the SPC700's 64 KB ARAM): the chip reads the
        // sample directory and BRR sample data from here. The host loads samples
        // into this span, exactly as the rf5c68 host loads wave RAM.
        [[nodiscard]] std::span<std::uint8_t> aram() noexcept { return aram_; }
        [[nodiscard]] std::span<const std::uint8_t> aram() const noexcept { return aram_; }

        // Apply the KON / KOFF latches: arm/restart key-on voices and release
        // key-off voices. The native step() calls this on the frame boundary; it
        // is exposed so tests can drive the transition deterministically. Returns
        // the mask of voices that transitioned inactive -> active.
        std::uint8_t commit_kon_koff() noexcept;

        // Generate one native stereo sample, updating last_left()/last_right() and
        // advancing every active voice.
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
        // Envelope generator state machine phases (ADSR + GAIN share this).
        enum class env_phase : std::uint8_t { release, attack, decay, sustain };

        struct voice {
            std::int8_t vol_l{};
            std::int8_t vol_r{};
            std::uint16_t pitch{}; // 14-bit
            std::uint8_t srcn{};
            std::uint8_t adsr1{};
            std::uint8_t adsr2{};
            std::uint8_t gain{};
            std::uint8_t envx{}; // read-only envelope snapshot (0..127)
            std::uint8_t outx{}; // read-only sample snapshot (signed, top 8 bits)
            bool use_adsr{};
            bool active{};

            // BRR decode + interpolation runtime.
            std::uint16_t brr_addr{};           // ARAM address of the current BRR block header
            std::uint8_t brr_offset{};          // byte 1..8 within the current block
            std::uint16_t interp_pos{};         // 12-bit pitch accumulator (top 4 bits = tap)
            bool brr_end{};                     // last decoded block had the END flag
            std::array<std::int16_t, 4> ring{}; // last 4 decoded samples (Gaussian taps)
            std::int32_t brr_prev0{};           // BRR filter history s[n-1]
            std::int32_t brr_prev1{};           // BRR filter history s[n-2]
            bool started{};                     // a block has been primed since key-on

            // Envelope generator.
            env_phase phase{env_phase::release};
            std::int32_t env_level{}; // 0..0x7FF (11-bit internal)
        };

        // Surfaces each voice's BRR region as a decoded PCM sample_view (the audio
        // analogue of the VDPs' asset_source). Buffers are rebuilt per call and
        // borrowed under the contract's tick lifetime rule; registered with the
        // introspection builder via with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(s_dsp& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            s_dsp* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        void decode_voice(int v) noexcept;
        void decode_globals() noexcept;
        // Roll a voice to the next 9-byte BRR block, honouring its END/LOOP flags
        // (sets ENDX bit `vi`, loops to the directory loop address, or stops).
        void brr_advance_block(voice& v, int vi) noexcept;
        // Decode the next BRR nibble for voice `vi` into a signed 16-bit sample,
        // advancing the block as needed.
        std::int16_t brr_decode_next(voice& v, int vi) noexcept;
        void env_run(voice& v) noexcept;
        [[nodiscard]] std::uint16_t dir_entry(std::uint8_t srcn, bool loop) const noexcept;

        std::array<std::uint8_t, reg_count> regs_{};
        std::array<voice, voice_count> voices_{};

        std::int8_t mvol_l_{};
        std::int8_t mvol_r_{};
        std::int8_t evol_l_{};
        std::int8_t evol_r_{};
        std::int8_t efb_{};
        std::uint8_t pmon_{};
        std::uint8_t non_{};
        std::uint8_t eon_{};
        std::uint8_t dir_page_{};
        std::uint8_t esa_page_{};
        std::uint8_t edl_{};
        std::uint8_t flg_{};
        std::uint8_t endx_{};
        std::uint8_t kon_pending_{};
        std::uint8_t koff_pending_{};
        std::array<std::int8_t, fir_taps> fir_{};

        std::array<std::uint8_t, aram_size> aram_{};

        std::int16_t noise_{}; // LFSR output sample (signed 15-bit)
        std::uint16_t noise_lfsr_{0x4000U};
        std::uint16_t noise_counter_{};

        // Shared free-running tick driving the envelope / noise rate dividers, so
        // the per-voice cadence is deterministic and rate-correct.
        std::uint32_t env_tick_{};

        // Echo delay line: stereo int16 pairs, sized to the maximum EDL (15 * 2 KB
        // -> 7680 stereo frames). Indexed modulo the active EDL length.
        static constexpr std::size_t echo_max_frames = 15U * 2048U / 4U;
        std::array<std::int16_t, echo_max_frames * 2U> echo_buffer_{};
        std::size_t echo_pos_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 9> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
