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
        static constexpr std::size_t register_trace_capacity = 128U;
        static constexpr std::size_t adpcm_register_fields = 10U;
        static constexpr std::size_t register_trace_fields = 4U;
        static constexpr std::size_t register_histogram_fields = 3U;
        static constexpr std::size_t register_view_count =
            15U + adpcm_voice_count * adpcm_register_fields +
            256U * register_histogram_fields +
            register_trace_capacity * register_trace_fields;
        // 60 MHz / 2 / 1248 -- the DSP's native stereo output rate.
        static constexpr std::uint32_t native_sample_rate = 24038U;

        struct register_trace_entry final {
            std::uint32_t sequence{};
            std::uint8_t reg{};
            std::uint16_t data{};
            std::uint16_t pc{};
        };

        qsound();

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
        void write_port_with_pc(std::uint8_t offset, std::uint8_t value,
                                std::uint16_t writer_pc) noexcept;
        [[nodiscard]] std::uint8_t read_status() const noexcept { return ready_; }

        // Advance every active voice one step and recompute the stereo mix (stored
        // as last_left()/last_right()).
        void step() noexcept;
        [[nodiscard]] std::int16_t last_left() const noexcept { return last_l_; }
        [[nodiscard]] std::int16_t last_right() const noexcept { return last_r_; }

        // Fill interleaved (L,R) pairs (size must be even), stepping once per pair.
        void generate(std::span<std::int16_t> buf_lr) noexcept;

        [[nodiscard]] std::uint32_t port_write_count() const noexcept {
            return port_write_count_;
        }
        [[nodiscard]] std::uint32_t register_write_count() const noexcept {
            return register_write_count_;
        }
        [[nodiscard]] std::uint32_t nonzero_pcm_volume_write_count() const noexcept {
            return nonzero_pcm_volume_write_count_;
        }
        [[nodiscard]] std::uint32_t nonzero_adpcm_volume_write_count() const noexcept {
            return nonzero_adpcm_volume_write_count_;
        }
        [[nodiscard]] std::uint32_t adpcm_trigger_count() const noexcept {
            return adpcm_trigger_count_;
        }
        [[nodiscard]] std::uint8_t last_register() const noexcept { return last_register_; }
        [[nodiscard]] std::uint16_t last_register_data() const noexcept {
            return last_register_data_;
        }
        [[nodiscard]] std::uint16_t last_register_pc() const noexcept { return last_register_pc_; }
        [[nodiscard]] std::uint32_t register_write_histogram(std::uint8_t reg) const noexcept {
            return register_write_histogram_[reg];
        }
        [[nodiscard]] std::uint32_t register_trace_count() const noexcept {
            return register_trace_count_;
        }
        [[nodiscard]] register_trace_entry register_trace(std::uint32_t sequence) const noexcept {
            return register_trace_[sequence % register_trace_.size()];
        }

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

        void write_register(std::uint8_t reg, std::uint16_t data,
                            std::uint16_t writer_pc) noexcept;
        void initialize_trace_register_names() noexcept;
        [[nodiscard]] std::uint8_t read_sample_u8(std::uint32_t rom_addr) const noexcept;
        [[nodiscard]] std::int16_t read_sample(std::uint16_t bank,
                                               std::uint16_t addr) const noexcept;
        [[nodiscard]] std::uint8_t read_adpcm_nibble(const adpcm_voice& voice,
                                                     std::uint32_t nibble) const noexcept;
        [[nodiscard]] std::int16_t step_adpcm(adpcm_voice& voice,
                                              std::uint32_t nibble_phase) noexcept;
        void reset_echo_state() noexcept;
        [[nodiscard]] std::uint16_t echo_delay_length() const noexcept;
        void advance_echo_state(std::int64_t input) noexcept;

        std::array<voice, voice_count> voices_{};
        std::array<adpcm_voice, adpcm_voice_count> adpcm_{};
        std::span<const std::uint8_t> rom_{};

        std::uint16_t data_latch_{};
        std::uint8_t ready_{ready_flag};
        std::uint32_t port_write_count_{};
        std::uint32_t register_write_count_{};
        std::array<std::uint32_t, 256> register_write_histogram_{};
        std::array<std::uint16_t, 256> register_last_data_{};
        std::array<std::uint16_t, 256> register_last_pc_{};
        std::uint32_t nonzero_pcm_volume_write_count_{};
        std::uint8_t last_nonzero_pcm_volume_reg_{};
        std::uint16_t last_nonzero_pcm_volume_data_{};
        std::uint16_t last_nonzero_pcm_volume_pc_{};
        std::uint32_t nonzero_adpcm_volume_write_count_{};
        std::uint8_t last_nonzero_adpcm_volume_voice_{};
        std::uint16_t last_nonzero_adpcm_volume_data_{};
        std::uint16_t last_nonzero_adpcm_volume_pc_{};
        std::uint32_t adpcm_trigger_count_{};
        std::uint8_t last_adpcm_trigger_voice_{};
        std::uint16_t last_adpcm_trigger_flag_{};
        std::uint16_t last_adpcm_trigger_pc_{};
        std::uint8_t last_register_{};
        std::uint16_t last_register_data_{};
        std::uint16_t last_register_pc_{};
        std::uint32_t register_trace_count_{};
        std::array<register_trace_entry, register_trace_capacity> register_trace_{};
        std::array<std::array<char, 16>, 256U * register_histogram_fields>
            histogram_register_names_{};
        std::array<std::array<char, 16>, register_trace_capacity * register_trace_fields>
            trace_register_names_{};
        std::uint32_t adpcm_phase_{};
        std::array<std::int16_t, echo_delay_capacity> echo_delay_{};
        std::uint16_t echo_end_pos_{};
        std::int16_t echo_feedback_{};
        std::uint16_t echo_delay_pos_{};
        std::int16_t echo_last_sample_{};
        std::int16_t last_l_{};
        std::int16_t last_r_{};

        std::array<register_descriptor, register_view_count> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::audio
