#pragma once

#include "audio_views.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::chips::audio {

    // Commodore Paula (MOS 8364) audio subsystem -- the four-channel DMA/manual
    // 8-bit signed PCM sound engine of the Amiga custom chip set. Each DMA
    // channel fetches 16-bit words from chip RAM (two signed 8-bit samples per
    // word, high byte first then low byte); CPU writes to AUDxDAT can also feed
    // one word while DMA is off. Per-channel registers latch the DMA start pointer
    // (AUDxLC), word length (AUDxLEN), sample period in color clocks (AUDxPER),
    // and volume (AUDxVOL, clamped 0..64). ADKCON can attach channel N as a
    // volume and/or period modulator for channel N+1; attached channels still run
    // their DMA stream and interrupts, but their DAC output is disabled. DMA is
    // gated by a master enable plus a per-channel enable bit (DMACON). When a
    // channel exhausts its LEN words it reloads from LC/LEN and raises the
    // matching AUDxINT. Channels pan to fixed stereo outputs: left = ch0 + ch3,
    // right = ch1 + ch2.
    //
    // The Emu reference fetches words through a host callback into chip RAM. To
    // keep this core standalone and unit-testable (the rf5c68 / okim6295 idiom),
    // the host instead sets a chip-RAM span via chipram(); audio DMA reads words
    // from it directly. The Paula interrupt controller + priority encoder is system
    // glue, not part of the audio synth, so it is omitted here; a buffer-wrap still
    // latches the per-channel interrupt request as observable state.
    //
    // Ported from the Emu reference (chips/paula); clean-room per the Commodore
    // Paula (MOS 8364) datasheet.
    class paula final : public iaudio_synth {
      public:
        static constexpr int channel_count = 4;
        static constexpr std::uint16_t volume_max = 64U;
        // Default chip-RAM the DMA channels fetch from. The pointer is 20-bit and
        // word-aligned; the board sets the active backing size for OCS/ECS models.
        static constexpr std::size_t chipram_size = 512U * 1024U;
        static constexpr std::uint32_t pointer_mask = 0x001FFFFEU; // 20-bit, word-aligned

        // Per-channel register selectors for write_reg(channel, reg, value).
        enum reg : std::uint8_t {
            reg_lch = 0x00, // AUDxLC high word (top 5 bits of the 20-bit pointer)
            reg_lcl = 0x01, // AUDxLC low word  (low 16 bits, LSB forced off)
            reg_len = 0x02, // AUDxLEN word count
            reg_per = 0x03, // AUDxPER sample period in color clocks
            reg_vol = 0x04, // AUDxVOL 0..64 (clamped)
            reg_dat = 0x05, // AUDxDAT manual-mode data word
        };

        paula() {
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

        // Per-channel register writes. `channel` is 0..3; out-of-range is dropped.
        // The LCH/LCL pair latches the 20-bit word-aligned chip-RAM pointer (top 5
        // bits in the high word, low 16 in the low word, LSB dropped).
        void write_reg(int channel, std::uint8_t index, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t read_reg(int channel, std::uint8_t index) const noexcept;

        // DMACON image: master enable + a 4-bit per-channel enable mask (bit N =
        // channel N). A channel arms (IDLE->READY) on the inactive->active edge,
        // reloading live pointer/length from LC/LEN; on active->inactive it parks
        // to IDLE, preserving the latched registers for a clean restart.
        void set_dma(bool master_enable, std::uint8_t channel_enable_mask) noexcept;
        [[nodiscard]] bool channel_active(int channel) const noexcept;

        // ADKCON audio attachment masks. Bit N attaches channel N to N+1 for
        // volume and/or period modulation; bit 3 has no target and only silences
        // channel 3 when set.
        void set_audio_attachment(std::uint8_t volume_mask, std::uint8_t period_mask) noexcept;
        [[nodiscard]] std::uint8_t volume_attachment_mask() const noexcept {
            return volume_attach_mask_;
        }
        [[nodiscard]] std::uint8_t period_attachment_mask() const noexcept {
            return period_attach_mask_;
        }

        // Host-set chip-RAM the audio DMA fetches words from (the rf5c68 waveram
        // analogue). Words are big-endian byte pairs at word-aligned addresses.
        [[nodiscard]] std::span<std::uint8_t> chipram() noexcept { return chipram_; }
        [[nodiscard]] std::span<const std::uint8_t> chipram() const noexcept { return chipram_; }
        void resize_chipram(std::size_t size);

        // Per-channel signed-16 output: signed 8-bit sample * volume (0..64);
        // 0 while idle or priming.
        [[nodiscard]] std::int16_t channel_output(int channel) const noexcept;

        // Pending AUDxINT request bits latched on buffer wrap (bit N = channel N).
        // Reading does not clear; clear_interrupts() acks them. This stands in for
        // the omitted interrupt controller so wrap events stay observable.
        [[nodiscard]] std::uint8_t interrupts() const noexcept { return audio_int_; }
        void clear_interrupts(std::uint8_t mask) noexcept {
            audio_int_ = static_cast<std::uint8_t>(audio_int_ & ~mask);
        }
        void set_interrupt_callback(std::function<void(std::uint8_t)> callback) noexcept {
            interrupt_callback_ = std::move(callback);
        }

        // Generate one native stereo sample (advance one color-clock per channel)
        // and latch last_left()/last_right(). Mirrors rf5c68::step().
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
        enum class channel_state : std::uint8_t {
            idle = 0,  // DMA disabled, channel parked
            ready,     // DMA just enabled; awaiting first fetch
            play_high, // playing the high byte of the current word
            play_low,  // playing the low byte of the current word
            manual_ready,
            manual_high,
            manual_low,
        };

        // Per-DMA-channel synth state. Named `voice` (not `channel`) so the type
        // never collides with the `int channel` selector parameters on the public
        // API -- mirrors the rf5c68 `voice` idiom.
        struct voice {
            // Latched register values -- what the guest wrote.
            std::uint32_t lc{};     // AUDxLC 20-bit chip-RAM word pointer
            std::uint16_t len{};    // AUDxLEN word count
            std::uint16_t period{}; // AUDxPER sample period in color clocks
            std::uint16_t volume{}; // AUDxVOL 0..64 (clamped on write)
            std::uint16_t dat{};    // AUDxDAT manual-mode data word

            // Live DMA fetch state, refreshed from lc/len on the DMA arm edge.
            std::uint32_t live_pointer{};
            std::uint16_t words_remaining{};
            channel_state state{channel_state::idle};
            bool dma_enable{};

            // Playback micro-state populated by the step engine.
            std::uint16_t current_word{};  // word currently being sampled
            std::int32_t period_counter{}; // color-clocks left in the current sample
            std::int16_t last_sample{};    // signed 8-bit * volume output latch
            bool modulation_volume_next{true};
        };

        // Surfaces each channel's chip-RAM region as a PCM sample_view (the audio
        // analogue of the VDPs' asset_source). Decoded buffers are rebuilt per call
        // and borrowed under the contract's tick lifetime rule; registered with the
        // introspection builder via with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(paula& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            paula* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        [[nodiscard]] std::uint16_t fetch_word(std::uint32_t addr) const noexcept;
        void latch_sample(voice& ch, std::int8_t sample) noexcept;
        [[nodiscard]] bool channel_attached(int channel_index) const noexcept;
        void apply_modulation_word(int channel_index, std::uint16_t value) noexcept;
        // Advance one channel by a sub-step; returns true while still active.
        bool advance_channel(int channel_index, voice& ch) noexcept;

        std::array<voice, channel_count> channels_{};
        bool dma_master_{};
        std::uint8_t volume_attach_mask_{};
        std::uint8_t period_attach_mask_{};
        std::uint8_t audio_int_{};
        // Heap-backed: 512 KB as an in-object array would overflow the default
        // stack when two chips are live at once (e.g. a save/load round-trip).
        std::vector<std::uint8_t> chipram_ = std::vector<std::uint8_t>(chipram_size, 0U);

        std::int16_t last_left_{};
        std::int16_t last_right_{};

        static constexpr int default_clock_divider = 1;
        int clock_divider_{default_clock_divider};
        int prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};
        std::function<void(std::uint8_t)> interrupt_callback_{};

        std::array<register_descriptor, 6> register_view_{};
        audio_source_impl audio_{*this};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
