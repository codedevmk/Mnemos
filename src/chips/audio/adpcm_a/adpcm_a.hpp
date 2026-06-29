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

    // Yamaha ADPCM-A: the 6-channel fixed-rate ADPCM sample player embedded in
    // the OPN-family rhythm/voice path (YM2608 OPNA, YM2610 OPNB). Each channel
    // streams 4-bit ADPCM nibbles from a shared external sample ROM, decoding to
    // 12-bit PCM via the standard ADPCM step-index predictor. There is no
    // resampler: every channel plays at the chip's single fixed rate
    // (~18.5 kHz). Per channel there is a 24-bit ROM start/end address (the
    // registers hold the high 16 bits; the low 8 are implied zero), a 6-bit
    // pan + 5-bit individual level, and a key-on/off bit. A global 6-bit Total
    // Level (TL) scales the master mix. A key-on jumps the channel to its
    // start address; playback stops at the end address.
    //
    // The external sample ROM is host-set (set_sample_rom / sample_rom), exactly
    // like the RF5C68's wave RAM -- the chip owns the synthesis core and borrows
    // the ROM span. This keeps the chip unit-testable without bus/system context
    // while remaining faithful to the hardware's external-ROM topology.
    //
    // Ported from the Emu reference (chips/adpcm_a); clean-room per the YM2610
    // ADPCM-A datasheet. The reference shipped only the register surface +
    // channel decode; the ADPCM step decoder and mixer are implemented here from
    // the datasheet's predictor (step-size table + index-adjust table).
    class adpcm_a final : public iaudio_synth {
      public:
        static constexpr int channel_count = 6;
        // The external sample ROM addressing is 24-bit; the largest YM2610
        // ADPCM-A ROM is 16 MiB. The host may set any size up to this.
        static constexpr std::size_t max_rom_size = 16U * 1024U * 1024U;

        // Register block offsets (the OPNB bank-B $00..$2D window).
        // Channel-specific blocks span `channel_count` consecutive offsets
        // from their base.
        enum reg : std::uint8_t {
            reg_key = 0x00,          // key on/off; bit7=DUMP(off), bits5:0 = channel mask
            reg_tl = 0x01,           // master Total Level (bits5:0 gain)
            reg_ch_pan_level = 0x08, // $08..$0D: bit7=L, bit6=R, bits4:0 = level
            reg_ch_start_lo = 0x10,  // $10..$15: start address low byte
            reg_ch_start_hi = 0x18,  // $18..$1D: start address high byte
            reg_ch_end_lo = 0x20,    // $20..$25: end address low byte
            reg_ch_end_hi = 0x28,    // $28..$2D: end address high byte
        };
        static constexpr std::uint8_t key_dump_mask = 0x80U; // bit7 = OFF mode
        static constexpr std::uint8_t reg_count = 0x30U;

        adpcm_a() {
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

        // Register access. `index` is the 6-bit local offset ($00..$2D); writes
        // to reserved offsets are ignored, reads of them return open bus.
        [[nodiscard]] std::uint8_t read_reg(std::uint8_t index) const noexcept;
        void write_reg(std::uint8_t index, std::uint8_t value) noexcept;

        struct channel_status final {
            bool pan_l{};
            bool pan_r{};
            std::uint8_t level{};
            std::uint32_t start_byte{};
            std::uint32_t end_byte{};
            bool active{};
            std::uint32_t current_byte{};
            bool high_nibble{};
            std::int32_t accumulator{};
            std::int32_t step_index{};
            std::uint32_t end_events{};
            std::uint32_t rom_underruns{};
        };
        [[nodiscard]] channel_status status(std::uint8_t channel_index) const noexcept;
        [[nodiscard]] std::uint8_t active_channel_mask() const noexcept;

        // Host-set external sample ROM (the ADPCM nibble stream). The chip
        // borrows the span; the host owns the storage. Like RF5C68 wave RAM,
        // this is the bulk load surface (and the test setup surface).
        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        // Key-on a channel: jump it to its start address and arm the decoder.
        void key_on(std::uint8_t channel_index) noexcept;
        // Key-off (DUMP) a channel: stop it immediately.
        void key_off(std::uint8_t channel_index) noexcept;

        // Generate one fixed-rate stereo sample, updating last_left()/right()
        // and advancing every active channel by one ADPCM nibble.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even).
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors rf5c68/ym2612): when enabled, tick()
        // queues one interleaved (L,R) stereo frame per native sample step.
        // Counts are STEREO FRAMES (pairs), matching the player's add_source()
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
        struct channel {
            bool pan_l{};
            bool pan_r{};
            std::uint8_t level{};       // 5-bit individual attenuation index
            std::uint16_t start_addr{}; // high 16 bits of the 24-bit ROM start
            std::uint16_t end_addr{};   // high 16 bits of the 24-bit ROM end
            bool active{};
            // ADPCM decoder runtime state.
            std::uint32_t cur_addr{};   // current 24-bit ROM byte address (full res)
            bool high_nibble{};         // which 4-bit nibble of cur byte is next
            std::int32_t accumulator{}; // 12-bit signed running PCM value
            std::int32_t step_index{};  // index into the ADPCM step-size table
            std::uint32_t end_events{}; // times playback crossed END
            std::uint32_t rom_underruns{}; // times playback ran past the attached ROM
        };

        // Surfaces each active channel's ROM region as a decoded PCM sample_view
        // (the audio analogue of the VDPs' asset_source). Buffers are rebuilt per
        // call and borrowed under the tick lifetime rule; registered via
        // with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(adpcm_a& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            adpcm_a* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        // Decode the channel's register block into its decoded fields.
        void decode_channel(std::uint8_t channel_index) noexcept;
        // Decode one ADPCM nibble for a channel, advancing its accumulator and
        // step index and returning the new 12-bit signed PCM value.
        [[nodiscard]] std::int32_t decode_nibble(channel& c, std::uint8_t nibble) noexcept;
        // Read the next nibble (4 bits) for a channel from the sample ROM and
        // advance its address; returns the nibble value (0..15).
        [[nodiscard]] std::uint8_t next_nibble(channel& c) noexcept;

        std::array<std::uint8_t, reg_count> regs_{};
        std::array<channel, channel_count> channels_{};
        std::uint8_t tl_{};       // 6-bit master Total Level gain
        std::uint8_t key_mask_{}; // last $00 write

        std::span<const std::uint8_t> rom_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 63> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
