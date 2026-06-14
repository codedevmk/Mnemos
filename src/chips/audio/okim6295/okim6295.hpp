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

    // OKI MSM6295 -- a 4-channel ADPCM sample player widely used as the sampled-
    // audio voice on late-1980s/early-1990s arcade boards alongside an FM synth.
    // Each channel decodes Dialogic/OKI 4-bit ADPCM (a 12-bit signed predictor)
    // from an external sample ROM. The ROM begins with a phrase table: 8 bytes
    // per phrase, the first three a 24-bit big-endian start address and the next
    // three the end address. A 2-byte command selects a phrase, a channel mask,
    // and one of 16 attenuation levels; a single byte with bit 7 clear stops the
    // masked channels. The host reads a status byte whose low four bits report
    // per-channel busy. Output is mono; the board mixes it with the FM voice.
    //
    // The pin-7 input selects the internal prescaler (input/132 or input/165) and
    // hence the native sample rate. Ported from the Emu reference (chips/msm6295);
    // clean-room per the OKI MSM6295 datasheet.
    class okim6295 final : public iaudio_synth {
      public:
        static constexpr int channel_count = 4;
        static constexpr int max_step_index = 48;
        // The two pin-7 prescaler ratios (input clocks per native sample step).
        static constexpr int divider_pin7_high = 132;
        static constexpr int divider_pin7_low = 165;
        static constexpr std::uint32_t default_input_clock_hz = 1'000'000U;

        okim6295() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_reg_writes([this](instrumentation::reg_write_trace::callback cb) {
                    reg_write_callback_ = std::move(cb);
                })
                .with_audio(&audio_);
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // The external sample ROM (phrase table at offset 0). Borrowed, not
        // owned: the board keeps the backing storage alive for the chip's life.
        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        // Host command port: a play command is two bytes (0x80|phrase, then
        // channel-mask<<4 | volume-code); a single byte with bit 7 clear stops
        // the channels selected by bits 3..6.
        void write_command(std::uint8_t data) noexcept;
        // Host status port: high nibble reads back as 1s, low four bits are the
        // per-channel busy flags.
        [[nodiscard]] std::uint8_t read_status() const noexcept;

        // Decode one mono sample, advancing every active channel; also stored as
        // last_sample().
        std::int16_t step() noexcept;
        [[nodiscard]] std::int16_t last_sample() const noexcept { return last_sample_; }

        // Fill interleaved (L,R) pairs (size must be even); the mono output is
        // duplicated to both lanes.
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Pin-7 select: high = input/132, low = input/165. The board must tick()
        // the chip at the OKI input clock for the native rate to come out right.
        void set_pin7(bool high) noexcept {
            pin7_high_ = high;
            clock_divider_ = high ? divider_pin7_high : divider_pin7_low;
        }
        [[nodiscard]] bool pin7_high() const noexcept { return pin7_high_; }
        // OKI input clock in Hz (board-set); native sample rate = clock/divider.
        void set_input_clock(std::uint32_t hz) noexcept {
            input_clock_hz_ = hz != 0U ? hz : default_input_clock_hz;
        }
        [[nodiscard]] std::uint32_t native_sample_rate() const noexcept {
            return clock_divider_ > 0 ? input_clock_hz_ / static_cast<std::uint32_t>(clock_divider_)
                                      : 0U;
        }

        // Real-time capture sink (mirrors rf5c68/ym2612): when enabled, tick()
        // queues one (L,R) stereo frame per native sample step. Counts below are
        // in STEREO FRAMES (pairs), matching the player's add_source() contract.
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        // Raw prescaler override (cycles per native step); set_pin7 is the
        // hardware path. Mainly for tests and non-board harnesses.
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        struct channel {
            bool active{};
            std::uint32_t current_addr{};
            std::uint32_t end_addr{};
            bool nibble_high{true};   // high nibble of the current byte plays first
            std::int32_t predictor{}; // 12-bit signed accumulator, [-2048, 2047]
            int step_index{};         // 0..48 into the step ladder
            std::uint8_t volume_code{};
            int volume{}; // attenuation factor out of 256
        };

        // Trigger one channel from a phrase's start/end addresses; a channel
        // already sounding ignores the retrigger (hardware busy gating).
        void start_channel(channel& ch, std::uint32_t start_addr, std::uint32_t end_addr,
                           std::uint8_t volume_code) noexcept;

        // Surfaces the ROM's valid phrase-table entries as decoded PCM waveforms
        // (the audio analogue of rf5c68's voice export). Decoded buffers are
        // rebuilt per call and borrowed under the contract's tick lifetime rule.
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(okim6295& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            okim6295* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        std::array<channel, channel_count> channels_{};
        std::span<const std::uint8_t> rom_{};

        bool command_pending_{};
        std::uint8_t pending_phrase_{};

        std::int16_t last_sample_{};

        bool pin7_high_{true};
        std::uint32_t input_clock_hz_{default_input_clock_hz};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{divider_pin7_high};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        instrumentation::reg_write_trace::callback reg_write_callback_{};

        std::array<register_descriptor, 8> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
