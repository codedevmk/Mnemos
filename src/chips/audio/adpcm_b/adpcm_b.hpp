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

    // Yamaha ADPCM-B -- the single streaming/variable-rate ADPCM channel found in
    // the OPN-family chips (the YM2610 / OPNB ADPCM-B block). One voice streams
    // 4-bit packed delta-ADPCM nibbles out of an external sample ROM, decoding
    // through the classic 49-step delta machine: each nibble carries a 3-bit
    // magnitude plus a sign, the running step size adapts via a fixed +-table,
    // and the accumulator is the 16-bit signed PCM output. Playback runs from a
    // programmable START address to an END address with optional repeat; the
    // DELTA-N register pair sets a caller-programmable resampling rate, so the
    // same waveform plays back at any pitch. A 6-bit total-level (TL) attenuator
    // and an L/R pan pair shape the mono output into a stereo frame.
    //
    // START/END hold the high 16 bits of a 24-bit byte address; the low 8 bits
    // are implied zero, matching the external-ROM topology used by YM2610
    // boards. The host supplies the ROM span, keeping the decode core
    // deterministic and unit-testable without a full bus.
    //
    // Ported from the Emu reference (chips/adpcm_b); clean-room per the YM2610
    // ADPCM-B datasheet. The Emu reference landed the register surface only; the
    // delta-decode step machine + streaming address generator are the datasheet
    // ADPCM-B algorithm, implemented here in integer math.
    class adpcm_b final : public iaudio_synth {
      public:
        static constexpr std::size_t reg_count = 0x10U;
        // The YM2610 Delta-T external ROM address space is 24-bit; the host may
        // set any ROM size up to this bound.
        static constexpr std::size_t max_rom_size = 16U * 1024U * 1024U;

        // Register offsets ($00..$0D).
        enum reg : std::uint8_t {
            reg_ctrl = 0x00,     // start / rec / mem / repeat / SP-off / reset
            reg_pan_tl = 0x01,   // L/R pan enable + 6-bit total level
            reg_start_lo = 0x02, // start address low
            reg_start_hi = 0x03, // start address high
            reg_end_lo = 0x04,   // end address low
            reg_end_hi = 0x05,   // end address high
            reg_presc_lo = 0x06, // prescale low (unused by the decode core)
            reg_presc_hi = 0x07, // prescale high (unused by the decode core)
            reg_data = 0x08,     // CPU-driven data byte (passthrough scaffold)
            reg_delta_lo = 0x09, // DELTA-N rate low
            reg_delta_hi = 0x0A, // DELTA-N rate high
            reg_status = 0x0B,   // EOS / BUSY flag (read-only)
            reg_limit_lo = 0x0C, // limit address low
            reg_limit_hi = 0x0D, // limit address high
        };

        // CTRL bits ($00).
        static constexpr std::uint8_t ctrl_reset = 0x01U;
        static constexpr std::uint8_t ctrl_sp_off = 0x08U;
        static constexpr std::uint8_t ctrl_repeat = 0x10U;
        static constexpr std::uint8_t ctrl_mem = 0x20U;
        static constexpr std::uint8_t ctrl_rec = 0x40U;
        static constexpr std::uint8_t ctrl_start = 0x80U;

        // PAN_TL bits ($01).
        static constexpr std::uint8_t pan_right = 0x40U;
        static constexpr std::uint8_t pan_left = 0x80U;
        static constexpr std::uint8_t tl_mask = 0x3FU;

        // STATUS bits ($0B, read-only): EOS latches when playback hits END.
        static constexpr std::uint8_t status_busy = 0x01U;
        static constexpr std::uint8_t status_eos = 0x02U;

        // Initial running step size at key-on (the datasheet's minimum step).
        static constexpr std::int32_t step_init = 127;

        adpcm_b() {
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

        // Register access. `index` is the low 4 bits ($00..$0D).
        [[nodiscard]] std::uint8_t read_reg(std::uint8_t index) const noexcept;
        void write_reg(std::uint8_t index, std::uint8_t value) noexcept;

        // Host-set external sample ROM. The chip borrows the span; ROM storage
        // belongs to the board/system and is reattached after save-state load.
        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        // Key-on: jump the streaming address to START, clear the decoder, mark
        // the voice playing. CTRL.START does this implicitly.
        void key_on() noexcept;

        // Generate one mono native sample, duplicated to both lanes via pan/TL,
        // updating last_left()/last_right() and advancing the decode stream.
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
        // Surfaces the active sample region (START..END) as a decoded PCM
        // sample_view, the audio analogue of the VDPs' asset_source; registered
        // with the introspection builder via with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(adpcm_b& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            adpcm_b* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        void decode_ctrl(std::uint8_t value) noexcept;
        // Fetch the next 4-bit nibble at the current stream cursor (high nibble
        // first within each byte), advancing the nibble cursor.
        [[nodiscard]] std::uint8_t next_nibble() noexcept;
        // Advance the decoder by one ADPCM nibble: returns the new 16-bit PCM
        // accumulator. Handles END/repeat/EOS.
        void advance_nibble() noexcept;

        std::array<std::uint8_t, reg_count> regs_{};

        // Decoded control state.
        bool active_{};
        bool repeat_{};
        bool sp_off_{};
        bool memory_mode_{};
        bool pan_l_{};
        bool pan_r_{};
        std::uint8_t tl_{};
        std::uint16_t start_addr_{};
        std::uint16_t end_addr_{};
        std::uint16_t limit_addr_{};
        std::uint16_t delta_n_{};
        std::uint8_t status_{};

        // Decoder runtime state. `nibble_cursor_` counts nibbles from ROM base;
        // the integer byte address is cursor>>1, high nibble = even index.
        std::uint32_t nibble_cursor_{};
        std::int32_t accumulator_{};   // running 16-bit signed PCM accumulator
        std::int32_t step_{step_init}; // running step size, adapted per nibble
        std::uint32_t phase_{};        // 16.16 resample phase; carry advances a nibble
        std::uint32_t end_events_{};   // times playback reached END
        std::uint32_t loop_events_{};  // times repeat returned to START
        std::uint32_t rom_underruns_{}; // times playback addressed past attached ROM

        std::span<const std::uint8_t> rom_{};

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 15> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
