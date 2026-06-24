#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::audio {

    // QSound (Capcom DL-1425) -- the 16-voice stereo PCM + 3-voice ADPCM mixer on
    // QSound CPS1 boards and CPS2. Modelled at the behavioural level (HLE), not
    // the DSP16 instruction set the real part runs: each PCM voice streams 8-bit
    // samples from an external ROM with a 12.4 fixed-point phase, and the ADPCM
    // lane decodes the CPS2-triggered mono effects mixed equally into both lanes.
    // The sound CPU programs the voices through a 3-port window (data hi / data lo
    // / a register-select that commits). Ported from the Emu CPS2 QSound HLE.
    class qsound final : public iaudio_synth {
      public:
        static constexpr int voice_count = 16;
        static constexpr int adpcm_voice_count = 3;
        static constexpr std::uint16_t default_pan = 0x0110U;
        static constexpr int mix_shift = 2;
        static constexpr std::uint8_t ready_flag = 0x80U;
        static constexpr std::int16_t adpcm_min_step_size = 1;
        static constexpr std::int16_t adpcm_max_step_size = 2000;
        static constexpr std::uint16_t echo_delay_base = 0x0554U;
        static constexpr std::size_t echo_delay_capacity = 1024U;
        // 60 MHz / 2 / 1248 -- the DSP's native stereo output rate.
        static constexpr std::uint32_t native_sample_rate = 24038U;

        qsound() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // The external QSound sample ROM (8-bit PCM, addressed as bank:addr).
        // Borrowed: the board keeps the backing storage alive for the chip's life.
        void set_sample_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> sample_rom() const noexcept { return rom_; }

        // Sound-CPU port window: offset&3 == 0 latches the data high byte, == 1 the
        // low byte, == 2 commits (register = value, data = the latched word) and
        // re-arms the ready flag.
        void write_port(std::uint8_t offset, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept { return ready_; }

        // Advance every active voice one step and recompute the stereo mix (stored
        // as last_left()/last_right()).
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_l_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_r_; }

        // Fill interleaved (L,R) pairs (size must be even), stepping once per pair.
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // One PCM voice. All fields are the raw 16-bit values the DSP registers
        // hold; `addr`/`phase` form a 12.4 fixed-point position into the bank.
        struct voice final {
            std::uint16_t bank{0x8000U};
            std::uint16_t addr{};
            std::uint16_t rate{};
            std::uint16_t phase{};
            std::uint16_t loop_len{};
            std::uint16_t end_addr{};
            std::uint16_t volume{};
            std::uint16_t pan{default_pan};
            std::uint16_t echo{};
        };

        struct adpcm_voice final {
            std::uint16_t start_addr{};
            std::uint16_t end_addr{};
            std::uint16_t bank{0x8000U};
            std::uint16_t volume{};
            std::uint16_t play_volume{};
            std::uint16_t flag{};
            std::uint16_t cur_addr{};
            std::int16_t step_size{};
            std::int16_t cur_vol{};
            std::int16_t last_sample{};
        };

        void write_register(std::uint8_t reg, std::uint16_t data) noexcept;
        [[nodiscard]] std::uint8_t read_sample_u8(std::uint32_t rom_addr) const noexcept;
        [[nodiscard]] std::int16_t read_sample(std::uint16_t bank,
                                               std::uint16_t addr) const noexcept;
        [[nodiscard]] std::uint8_t read_adpcm_nibble(const adpcm_voice& voice,
                                                     std::uint32_t nibble) const noexcept;
        [[nodiscard]] std::int16_t step_adpcm(adpcm_voice& voice,
                                              std::uint32_t nibble_phase) noexcept;
        void reset_echo_state() noexcept;
        [[nodiscard]] std::uint16_t echo_delay_length() const noexcept;
        [[nodiscard]] std::int16_t apply_echo(std::int64_t input) noexcept;

        std::array<voice, voice_count> voices_{};
        std::array<adpcm_voice, adpcm_voice_count> adpcm_{};
        std::span<const std::uint8_t> rom_{};

        std::uint16_t data_latch_{};
        std::uint8_t ready_{ready_flag};
        std::uint32_t adpcm_phase_{};
        std::array<std::int16_t, echo_delay_capacity> echo_delay_{};
        std::uint16_t echo_end_pos_{};
        std::int16_t echo_feedback_{};
        std::uint16_t echo_delay_pos_{};
        std::int16_t echo_last_sample_{};
        std::int16_t last_l_{};
        std::int16_t last_r_{};

        std::array<register_descriptor, 6> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
