#pragma once

#include "beeper.hpp"
#include "chip.hpp"
#include "ibus.hpp"
#include "introspection_adapters.hpp"
#include "m6510.hpp"
#include "rom_set.hpp"
#include "state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m15 {

    inline constexpr std::size_t main_rom_size = 0x10000U;
    inline constexpr std::size_t program_rom_size = 0x0400U;
    inline constexpr std::uint32_t m15_system_state_version = 3U;

    inline constexpr std::uint32_t visible_width = 224U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60176U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t cpu_clock_hz = 733'125U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t scratch_ram_base = 0x0000U;
    inline constexpr std::size_t scratch_ram_size = 0x0300U;
    inline constexpr std::uint16_t program_rom_base = 0x1000U;
    inline constexpr std::uint16_t program_rom_limit = 0x3400U;
    inline constexpr std::uint16_t video_ram_base = 0x4000U;
    inline constexpr std::size_t video_ram_size = 0x0400U;
    inline constexpr std::uint16_t color_ram_base = 0x4800U;
    inline constexpr std::size_t color_ram_size = 0x0400U;
    inline constexpr std::uint16_t chargen_ram_base = 0x5000U;
    inline constexpr std::size_t chargen_ram_size = 0x0800U;
    inline constexpr std::uint16_t vector_rom_base = 0xFC00U;

    inline constexpr std::uint16_t input_p2_address = 0xA000U;
    inline constexpr std::uint16_t sound_latch_address = 0xA100U;
    inline constexpr std::uint16_t dip_switch_address = 0xA200U;
    inline constexpr std::uint16_t input_p1_address = 0xA300U;
    inline constexpr std::uint16_t control_register_address = 0xA400U;

    inline constexpr std::uint8_t headoni_dip_default = 0x11U;

    inline constexpr std::uint8_t p1_start1_bit = 0x01U;
    inline constexpr std::uint8_t p1_start2_bit = 0x02U;
    inline constexpr std::uint8_t panel_button1_bit = 0x04U;
    inline constexpr std::uint8_t panel_up_bit = 0x08U;
    inline constexpr std::uint8_t panel_down_bit = 0x10U;
    inline constexpr std::uint8_t panel_left_bit = 0x20U;
    inline constexpr std::uint8_t panel_right_bit = 0x40U;
    inline constexpr std::uint8_t coin1_bit = 0x01U;
    inline constexpr std::uint8_t control_flip_active_low_bit = 0x04U;

    struct m15_board_params final {
        std::uint32_t cpu_clock_hz{mnemos::manifests::irem_m15::cpu_clock_hz};
        std::string_view rom_layout{"m15_headon_6502"};
        std::uint8_t dip_default{headoni_dip_default};
    };

    [[nodiscard]] m15_board_params board_params_for(std::string_view set_name) noexcept;

    struct m15_system;

    class m15_cpu_bus final : public chips::ibus {
      public:
        void attach(m15_system& system) noexcept { system_ = &system; }
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

      private:
        m15_system* system_{};
    };

    class m15_video final : public chips::ivideo {
      public:
        m15_video();

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(chips::reset_kind kind) override;
        void save_state(chips::state_writer& writer) const override;
        void load_state(chips::state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] chips::frame_buffer_view framebuffer() const noexcept override;
        void compose(std::span<const std::uint8_t> video_ram,
                     std::span<const std::uint8_t> color_ram,
                     std::span<const std::uint8_t> chargen_ram,
                     bool flip_screen);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m15_system final {
        m15_cpu_bus main_bus;
        chips::cpu::m6510 main_cpu;
        m15_video video;
        chips::audio::beeper speaker;

        common::rom_set_image roms;
        m15_board_params params;

        std::array<std::uint8_t, scratch_ram_size> scratch_ram{};
        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};
        std::array<std::uint8_t, chargen_ram_size> chargen_ram{};

        std::uint8_t input_p1{};
        std::uint8_t input_p2{};
        std::uint8_t input_system{};
        std::uint8_t dip_switches{headoni_dip_default};
        std::uint8_t control_register{};
        bool flip_screen{};
        std::uint8_t speaker_latch{};

        explicit m15_system(common::rom_set_image image, m15_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m15_system> assemble_m15(common::rom_set_image image,
                                                           m15_board_params board_params = {});

} // namespace mnemos::manifests::irem_m15
