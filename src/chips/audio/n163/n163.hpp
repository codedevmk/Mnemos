#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // Namco 163 expansion sound: 1-8 wavetable channels that share a single 128-byte
    // internal RAM. The low half ($00-$3F) holds 4-bit waveform samples (two per
    // byte, low nibble first); the high half ($40-$7F) holds the per-channel control
    // registers, eight bytes per channel, allocated from the top down so unused
    // channels' register space can hold extra wave data. $7F bits 6-4 give the active
    // channel count minus one.
    //
    // Each channel owns an 18-bit frequency, a 24-bit phase accumulator (kept in the
    // shared RAM), a wave length, a wave start address and a 4-bit volume. The chip
    // services exactly one active channel every 15 CPU cycles, cycling through the
    // active set -- so with N channels enabled each updates every 15*N cycles, which
    // is why enabling fewer channels raises their effective sample rate. The serial
    // single-pin output is modelled as the average of the active channels' held
    // outputs (the analog low-pass of the multiplexed DAC).
    //
    // The host programmes it through two ports the cartridge maps: an address port
    // ($F800: bits 0-6 select the RAM byte, bit 7 enables post-access auto-increment)
    // and a data port ($4800, read/write) onto the selected byte. The chip is mono;
    // like the other synth chips it duplicates the mono mix onto both stereo lanes,
    // and set_clock_divider() picks how many CPU cycles make one captured sample.
    //
    // Clean-room from the public Namco 163 audio documentation; no emulator source.
    class n163 final : public iaudio_synth {
      public:
        static constexpr int default_clock_divider = 37; // ~48.4 kHz at 1.79 MHz

        n163() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Sound-RAM ports. The cartridge forwards the $F800 address-port write and the
        // $4800 data-port read/write here.
        void write_address(std::uint8_t value) noexcept {
            addr_ = static_cast<std::uint8_t>(value & 0x7FU);
            autoinc_ = (value & 0x80U) != 0U;
        }
        void write_data(std::uint8_t value) noexcept {
            ram_[addr_] = value;
            advance_addr();
        }
        [[nodiscard]] std::uint8_t read_data() noexcept {
            const std::uint8_t v = ram_[addr_];
            advance_addr();
            return v;
        }
        // The cartridge's $E000 bit 6 mutes the sound circuit when set.
        void set_enabled(bool on) noexcept { enabled_ = on; }
        [[nodiscard]] bool enabled() const noexcept { return enabled_; }

        [[nodiscard]] std::int16_t last_left() const noexcept { return last_left_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_right_; }

        // Real-time capture sink (mirrors the other synth chips); counts are STEREO
        // FRAMES (pairs).
        void enable_audio_capture(bool on) noexcept { audio_capture_ = on; }
        [[nodiscard]] bool audio_capture_enabled() const noexcept { return audio_capture_; }
        [[nodiscard]] std::size_t pending_samples() const noexcept {
            return sample_queue_.size() / 2U;
        }
        std::size_t drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept;
        void set_clock_divider(int divider) noexcept {
            clock_divider_ = divider > 0 ? divider : default_clock_divider;
        }

        // Test/introspection accessors.
        [[nodiscard]] std::uint8_t ram(std::size_t i) const noexcept {
            return i < ram_.size() ? ram_[i] : 0U;
        }
        [[nodiscard]] std::size_t active_channels() const noexcept {
            return static_cast<std::size_t>((ram_[0x7FU] >> 4U) & 0x07U) + 1U;
        }
        [[nodiscard]] std::int32_t channel_output(std::size_t ch) const noexcept {
            return ch < chan_out_.size() ? chan_out_[ch] : 0;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        static constexpr int k_service_cycles = 15; // CPU cycles per channel update
        static constexpr int k_output_gain = 160;   // average-channel level -> int16

        void advance_addr() noexcept {
            if (autoinc_) {
                addr_ = static_cast<std::uint8_t>((addr_ + 1U) & 0x7FU);
            }
        }
        void service_channel(std::size_t ch) noexcept;

        std::array<std::uint8_t, 128> ram_{}; // shared wave + channel-register RAM
        std::uint8_t addr_{};                 // 7-bit RAM pointer
        bool autoinc_{};                      // post-access auto-increment
        bool enabled_{true};                  // $E000 bit 6 (0 = sound on)

        int cycle_div_{};                        // CPU-cycle countup to the 15-cycle slot
        std::size_t service_idx_{};              // round-robin position in the active set
        std::array<std::int32_t, 8> chan_out_{}; // last output per physical channel

        std::int16_t last_left_{};
        std::int16_t last_right_{};
        double dc_{}; // DC-blocker running average

        int clock_divider_{default_clock_divider};
        int sample_prescaler_{};
        bool audio_capture_{};
        std::vector<std::int16_t> sample_queue_{};

        std::array<register_descriptor, 5> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
