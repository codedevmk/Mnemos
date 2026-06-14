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

    // Ricoh RF5C164 (RF5C68 family) PCM sound chip -- the Sega CD / Mega-CD PCM
    // synthesiser. Eight voices play 8-bit sign-magnitude samples from a shared
    // 64 KB wave RAM; each voice has an envelope (8-bit volume), pan, a 16-bit
    // frequency divider, a loop-start address, and a start address. On the Sega
    // CD the chip is mapped into the sub-CPU bus as a 9-register window plus a
    // bank-selected 4 KB wave-RAM window. A 0xFF sample byte is the loop/stop
    // sentinel. Native output is ~32.55 kHz stereo (per the Mega-CD manual).
    //
    // Ported from the Emu reference (chips/rf5c68); clean-room per the Mega-CD
    // Hardware Manual + RF5C68 datasheet. See NOTES.md.
    class rf5c68 final : public iaudio_synth {
      public:
        static constexpr int voice_count = 8;
        static constexpr std::size_t waveram_size = 64U * 1024U;
        static constexpr std::size_t window_size = 0x1000U; // 4 KB wave-RAM window

        // Chip register offsets ($00..$08).
        enum reg : std::uint8_t {
            reg_env = 0x00,  // current voice: envelope (8-bit volume)
            reg_pan = 0x01,  // current voice: high nibble right vol, low nibble left
            reg_fdl = 0x02,  // current voice: frequency divider low
            reg_fdh = 0x03,  // current voice: frequency divider high
            reg_lsl = 0x04,  // current voice: loop-start low
            reg_lsh = 0x05,  // current voice: loop-start high
            reg_st = 0x06,   // current voice: start-address high (low byte is 0)
            reg_ctrl = 0x07, // global: enable + waveram bank + current-voice select
            reg_chan = 0x08, // global: per-voice mute bitmap (bit=1 means muted)
        };
        static constexpr std::uint8_t ctrl_enable = 0x80U;
        static constexpr std::uint8_t ctrl_mod = 0x40U; // bits 3:0 = channel (1) / bank (0)
        static constexpr std::uint8_t ctrl_bank_mask = 0x0FU;

        rf5c68() {
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

        // Chip-register access. `index` is the low 4 bits ($00..$08).
        [[nodiscard]] std::uint8_t read_reg(std::uint8_t index) const noexcept;
        void write_reg(std::uint8_t index, std::uint8_t value) noexcept;

        // Wave-RAM window access. `window_offset` is 0..0xFFF; the CTRL bank
        // index selects which 4 KB of the 64 KB wave RAM is exposed.
        [[nodiscard]] std::uint8_t read_waveram(std::uint16_t window_offset) const noexcept;
        void write_waveram(std::uint16_t window_offset, std::uint8_t value) noexcept;

        // Bulk wave-RAM view (CDC PCM-DMA loads in the Sega CD, plus tests).
        [[nodiscard]] std::span<std::uint8_t> waveram() noexcept { return waveram_; }
        [[nodiscard]] std::span<const std::uint8_t> waveram() const noexcept { return waveram_; }

        // Key-on: jump the voice's address accumulator to its start address
        // (ST << 8) and mark it active.
        void key_on(std::uint8_t voice_index) noexcept;

        // Generate one stereo sample, updating last_left()/last_right() and
        // advancing every active, unmuted voice.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even).
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors ym2612): when enabled, tick() queues
        // one interleaved (L,R) stereo frame per native sample step. The counts
        // below are in STEREO FRAMES (pairs) -- matching ym2612 and the player's
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
        struct voice {
            std::uint8_t envelope{};
            std::uint8_t pan{};
            std::uint16_t freq_divider{};
            std::uint16_t loop_start{};
            std::uint8_t start_high{};
            bool muted{true};
            std::uint16_t sample_pos{};
            std::uint16_t sample_frac{};
            bool active{};
        };

        // Surfaces each voice's wave-RAM region as a PCM sample_view (the audio
        // analogue of the VDPs' asset_source). Decoded buffers are rebuilt per
        // call and borrowed under the contract's tick lifetime rule; registered
        // with the introspection builder via with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(rf5c68& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view>
            samples() const override;

          private:
            rf5c68* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        void apply_voice_write(std::uint8_t index, std::uint8_t value) noexcept;

        std::array<std::uint8_t, 9> regs_{};
        std::array<voice, voice_count> voices_{};
        std::uint8_t selected_voice_{};
        bool enabled_{};
        std::uint8_t bank_index_{};
        std::uint8_t channel_mute_{0xFFU};
        std::array<std::uint8_t, waveram_size> waveram_{};

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
