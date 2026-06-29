#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"
#include "irem_ga20.hpp"
#include "m102_game_manifests.hpp"
#include "rom_set.hpp"
#include "state.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m102 {

    inline constexpr std::uint32_t m102_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 320U;
    inline constexpr std::uint32_t visible_height = 240U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60000U;
    inline constexpr std::uint32_t cpu_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t ga20_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t ga20_capture_divider = 18U;
    inline constexpr std::uint32_t audio_rate_hz =
        ga20_clock_hz / chips::audio::irem_ga20::clocks_per_sample / ga20_capture_divider;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t ga20_cycles_per_frame =
        (static_cast<std::uint64_t>(ga20_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t fixed_rom_base = 0x0000U;
    inline constexpr std::size_t fixed_rom_size = 0x8000U;
    inline constexpr std::uint16_t bank_rom_base = 0x8000U;
    inline constexpr std::size_t bank_rom_size = 0x4000U;
    inline constexpr std::uint16_t video_ram_base = 0xC000U;
    inline constexpr std::size_t video_ram_size = 0x1000U;
    inline constexpr std::uint16_t medal_ram_base = 0xD000U;
    inline constexpr std::size_t medal_ram_size = 0x1000U;
    inline constexpr std::uint16_t work_ram_base = 0xE000U;
    inline constexpr std::size_t work_ram_size = 0x2000U;

    inline constexpr std::uint8_t input0_default = 0xFFU;
    inline constexpr std::uint8_t input1_default = 0xFFU;
    inline constexpr std::uint8_t dsw1_default = 0xFFU;
    inline constexpr std::uint8_t dsw2_default = 0xFFU;

    inline constexpr std::uint8_t input_coin1_bit = 0x01U;
    inline constexpr std::uint8_t input_service_bit = 0x02U;
    inline constexpr std::uint8_t input_start1_bit = 0x04U;
    inline constexpr std::uint8_t input_start2_bit = 0x08U;
    inline constexpr std::uint8_t input_button1_bit = 0x10U;
    inline constexpr std::uint8_t input_button2_bit = 0x20U;
    inline constexpr std::uint8_t input_button3_bit = 0x40U;

    inline constexpr std::uint8_t port_ga20_base = 0x00U;
    inline constexpr std::uint8_t port_ga20_end = 0x1FU;
    inline constexpr std::uint8_t port_input0 = 0x20U;
    inline constexpr std::uint8_t port_input1 = 0x21U;
    inline constexpr std::uint8_t port_dsw1 = 0x22U;
    inline constexpr std::uint8_t port_dsw2 = 0x23U;
    inline constexpr std::uint8_t port_bank = 0x40U;
    inline constexpr std::uint8_t port_output = 0x50U;

    struct m102_board_params final {
        std::uint32_t cpu_clock_hz{mnemos::manifests::irem_m102::cpu_clock_hz};
        std::uint32_t ga20_clock_hz{mnemos::manifests::irem_m102::ga20_clock_hz};
        std::string_view rom_layout{"m102_hclimber_z80_ga20_first_pass"};
        std::uint8_t dsw1_default{mnemos::manifests::irem_m102::dsw1_default};
        std::uint8_t dsw2_default{mnemos::manifests::irem_m102::dsw2_default};
    };

    [[nodiscard]] m102_board_params board_params_for(std::string_view set_name) noexcept;

    class m102_video final : public chips::ivideo {
      public:
        m102_video();

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
        void compose(std::span<const std::uint8_t> main_rom,
                     std::span<const std::uint8_t> ga20_rom,
                     std::span<const std::uint8_t> work_ram,
                     std::span<const std::uint8_t> video_ram,
                     std::span<const std::uint8_t> medal_ram,
                     std::uint8_t bank_select,
                     std::uint8_t output_latch);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m102_system final {
        topology::bus main_bus{16U, topology::endianness::little};
        chips::cpu::z80 main_cpu;
        m102_video video;
        chips::audio::irem_ga20 ga20;

        common::rom_set_image roms;
        m102_board_params params;

        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, medal_ram_size> medal_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};

        std::uint8_t input0{input0_default};
        std::uint8_t input1{input1_default};
        std::uint8_t dsw1{dsw1_default};
        std::uint8_t dsw2{dsw2_default};
        std::uint8_t bank_select{};
        std::uint8_t output_latch{};
        std::uint64_t bank_switch_count{};
        std::uint64_t output_write_count{};
        std::uint64_t ga20_register_write_count{};
        std::uint64_t ga20_key_on_count{};

        explicit m102_system(common::rom_set_image image, m102_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t in0, std::uint8_t in1) noexcept;
        [[nodiscard]] std::uint8_t read_io_port(std::uint16_t port) const noexcept;
        void write_io_port(std::uint16_t port, std::uint8_t value) noexcept;
        void set_bank(std::uint8_t value) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        void retarget_bank_window() noexcept;
    };

    [[nodiscard]] std::unique_ptr<m102_system> assemble_m102(common::rom_set_image image,
                                                             m102_board_params board_params = {});

} // namespace mnemos::manifests::irem_m102
