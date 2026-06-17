#pragma once

#include "audio_views.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::chips::audio {

    // Yamaha SCSP (YMF292) -- the Saturn's 32-slot PCM sound generator. Each of
    // the 32 voices ("slots") plays 8- or 16-bit signed samples from a shared
    // 512 KB external wave RAM, with an independent ADSR envelope, total-level
    // attenuation, pitch/amplitude LFO, OCT/FNS pitch and four loop modes
    // (none / forward / reverse / alternating). The host mixes the 32 slots
    // down to a single ~44.1 kHz stereo pair via per-slot direct-send level
    // (DISDL, 6 dB/step) and pan (DIPAN, 3 dB/step). On the Saturn the chip is
    // driven by a sound-CPU bus as 32 sixteen-register slot blocks ($000..$3FF)
    // plus a common-control area; key-on is edge-triggered through KYONEX.
    //
    // This port keeps the slot synthesis core (envelope, LFO, pitch, looping,
    // sample fetch) and the direct stereo mixdown. The on-chip FH1 DSP effect
    // path, the slot-to-slot FM modulation forward references, the sound-stack
    // (SOUS) input mix and the timer/IRQ block are system-level concerns and
    // are not modelled here -- see NOTES below.
    //
    // Ported from the Emu reference (chips/scsp); clean-room per the Yamaha SCSP
    // (YMF292) datasheet.
    class scsp final : public iaudio_synth {
      public:
        static constexpr int slot_count = 32;
        static constexpr std::size_t waveram_size = 512U * 1024U;
        // 16 sixteen-bit registers per slot => 32 bytes ($20) of register space.
        static constexpr std::size_t slot_reg_stride = 0x20U;
        static constexpr std::size_t slot_reg_window = slot_count * slot_reg_stride; // $400

        scsp() {
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

        // Slot-register window access. `offset` is a byte offset into the 32-slot
        // register block ($000..$3FF); registers are big-endian 16-bit pairs. A
        // write to slot byte $00 with the KYONEX bit set rescans every slot and
        // keys on / releases voices per their KYONB bit (the edge-trigger path
        // real software uses).
        [[nodiscard]] std::uint8_t read_reg(std::uint16_t offset) const noexcept;
        void write_reg(std::uint16_t offset, std::uint8_t value) noexcept;

        // Wave RAM is SCSP-attached DRAM the host DMAs sample data into. Exposed
        // as a span for the host loader and for tests, mirroring the PCM family.
        [[nodiscard]] std::span<std::uint8_t> waveram() noexcept { return *waveram_; }
        [[nodiscard]] std::span<const std::uint8_t> waveram() const noexcept { return *waveram_; }

        // Generate one native stereo sample, updating last_left()/last_right():
        // advances each active slot's ADSR + position, then accumulates the
        // stereo mixdown.
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Fill `buf_lr` with interleaved L,R samples (size must be even).
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        // Real-time capture sink (mirrors the PCM family): when enabled, tick()
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
        // Decoded slot state -- mirrors the 16 slot registers so the mixer inner
        // loop does not re-parse them every sample. Field names follow the SCSP
        // register mnemonics (SA/LSA/LEA/OCT-FNS/ADSR/LFO/DISDL/DIPAN...).
        struct slot {
            // Decoded register fields.
            std::uint32_t start_addr{};     // SA (20-bit waveform start)
            std::uint16_t loop_start{};     // LSA
            std::uint16_t loop_end{};       // LEA
            std::uint16_t pitch{};          // OCT:4 | FNS:11
            std::uint8_t loop_control{};    // LPCTL: 0 none / 1 fwd / 2 rev / 3 alt
            std::uint8_t sample_format{};   // PCM8B (1 = 8-bit signed, 0 = 16-bit)
            std::uint8_t source_control{};  // SSCTL: 0 wave RAM / 1 noise / 2-3 zero
            std::uint8_t source_bit_ctrl{}; // SBCTL: optional sample-bit invert
            std::uint8_t eg_hold{};         // EGHOLD
            std::uint8_t total_level{};     // TL (7-bit attenuation)
            std::uint8_t attack_rate{};
            std::uint8_t decay1_rate{};
            std::uint8_t decay2_rate{};
            std::uint8_t release_rate{};
            std::uint8_t decay_level{};    // DL (5-bit)
            std::uint8_t key_rate_scale{}; // KRS (4-bit)
            std::uint8_t loop_link{};      // LPSLNK
            std::uint8_t lfo_reset{};
            std::uint8_t lfo_frequency{};
            std::uint8_t lfo_pitch_wave{};
            std::uint8_t lfo_pitch_sens{};
            std::uint8_t lfo_amp_wave{};
            std::uint8_t lfo_amp_sens{};
            std::uint8_t direct_send_level{}; // DISDL (3-bit)
            std::uint8_t direct_pan{};        // DIPAN (5-bit)
            std::uint8_t effect_send_level{}; // EFSDL
            std::uint8_t effect_pan{};        // EFPAN
            std::uint8_t input_select{};      // ISEL
            std::uint8_t input_mix_level{};   // IMXL
            std::uint8_t reverse_dir{};       // SDIR -- bypass EG/TL/ALFO
            std::uint8_t loop_inhibit{};      // STWINH

            // Runtime playback state.
            bool active{};
            std::uint8_t env_state{};  // 0 att / 1 dec1 / 2 dec2 / 3 rel
            std::uint16_t env_level{}; // 10-bit
            std::uint16_t env_sample_counter{};
            std::uint32_t sample_pos_frac{}; // Q16.16
            std::uint32_t sample_pos{};
            std::int8_t sample_dir{1}; // +1 / -1 for LPCTL alt
            std::uint32_t lfo_phase{};
        };

        // Surfaces each slot's referenced wave-RAM region as a PCM sample_view
        // (the audio analogue of the VDPs' asset_source). Decoded buffers are
        // rebuilt per call and borrowed under the contract's tick lifetime rule;
        // registered with the introspection builder via with_audio().
        class audio_source_impl final : public instrumentation::audio_source {
          public:
            explicit audio_source_impl(scsp& owner) noexcept : owner_(&owner) {}
            [[nodiscard]] std::span<const instrumentation::sample_view> samples() const override;

          private:
            scsp* owner_;
            mutable std::vector<std::int16_t> pcm_{};
            mutable std::vector<std::string> names_{};
            mutable std::vector<instrumentation::sample_view> samples_{};
        };

        // Pure synthesis core ported from the reference. The voice_* helpers
        // mutate one slot in place; the fetch + mix helpers read wave RAM.
        void voice_tick(slot& v) const noexcept;
        void voice_step_pos(slot& v) const noexcept;
        [[nodiscard]] std::int16_t fetch_sample(const slot& v) const noexcept;
        void decode_slot(int index) noexcept;
        void key_on_kyonex() noexcept;

        std::array<std::uint8_t, slot_reg_window> regs_{};
        std::array<slot, slot_count> slots_{};

        // The 512 KB external wave RAM lives on the heap: a stack-resident member
        // this large overflows the default stack when two chips are instantiated
        // together (e.g. a save/load round-trip). The fixed-size array keeps the
        // contiguous span exposure and the power-of-two address masking intact.
        using waveram_buffer = std::array<std::uint8_t, waveram_size>;
        std::unique_ptr<waveram_buffer> waveram_{std::make_unique<waveram_buffer>()};

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
