#pragma once

#include "beeper.hpp"
#include "chip.hpp"
#include "ibus.hpp"
#include "i8080.hpp"
#include "introspection_adapters.hpp"
#include "m10_rom_layout.hpp"
#include "rom_set.hpp"
#include "state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m10 {

    inline constexpr std::uint32_t m10_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 224U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60000U;
    inline constexpr std::uint32_t cpu_clock_hz = 1'000'000U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t scratch_ram_base = 0x0000U;
    inline constexpr std::size_t scratch_ram_size = 0x0400U;
    inline constexpr std::uint16_t program_rom_base = 0x1000U;
    inline constexpr std::uint16_t program_rom_limit = 0x3000U;
    inline constexpr std::uint16_t video_ram_base = 0x4000U;
    inline constexpr std::size_t video_ram_size = 0x0400U;
    inline constexpr std::uint16_t color_ram_base = 0x4800U;
    inline constexpr std::size_t color_ram_size = 0x0400U;
    inline constexpr std::uint16_t work_ram_base = 0x5000U;
    inline constexpr std::size_t work_ram_size = 0x0400U;
    inline constexpr std::uint16_t vector_rom_base = 0xFC00U;

    inline constexpr std::uint16_t input_p1_address = 0x6000U;
    inline constexpr std::uint16_t input_p2_address = 0x6001U;
    inline constexpr std::uint16_t input_system_address = 0x6002U;
    inline constexpr std::uint16_t dip_switch_address = 0x6003U;
    inline constexpr std::uint16_t sound_latch_address = 0x6004U;
    inline constexpr std::uint16_t control_register_address = 0x6005U;

    inline constexpr std::uint8_t m10_dip_default = 0xFFU;

    inline constexpr std::uint8_t panel_up_bit = 0x01U;
    inline constexpr std::uint8_t panel_down_bit = 0x02U;
    inline constexpr std::uint8_t panel_left_bit = 0x04U;
    inline constexpr std::uint8_t panel_right_bit = 0x08U;
    inline constexpr std::uint8_t panel_button1_bit = 0x10U;
    inline constexpr std::uint8_t panel_button2_bit = 0x20U;
    inline constexpr std::uint8_t start1_bit = 0x01U;
    inline constexpr std::uint8_t start2_bit = 0x02U;
    inline constexpr std::uint8_t coin1_bit = 0x04U;
    inline constexpr std::uint8_t service_bit = 0x08U;
    inline constexpr std::uint8_t control_flip_bit = 0x01U;
    inline constexpr std::uint8_t sound_speaker_bit = 0x01U;

    struct m10_board_params final {
        std::uint32_t cpu_clock_hz{mnemos::manifests::irem_m10::cpu_clock_hz};
        std::string_view rom_layout{"m10_early_8085"};
        std::uint8_t dip_default{m10_dip_default};
    };

    [[nodiscard]] m10_board_params board_params_for(std::string_view set_name) noexcept;

    struct m10_system;

    class m10_cpu_bus final : public chips::ibus {
      public:
        void attach(m10_system& system) noexcept { system_ = &system; }
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

      private:
        m10_system* system_{};
    };

    class m10_video final : public chips::ivideo {
      public:
        m10_video();

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
                     std::span<const std::uint8_t> gfx_rom,
                     std::span<const std::uint8_t> program_rom,
                     bool flip_screen,
                     std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m10_system final {
        m10_cpu_bus main_bus;
        chips::cpu::i8080 main_cpu;
        m10_video video;
        chips::audio::beeper speaker;

        common::rom_set_image roms;
        m10_board_params params;

        std::array<std::uint8_t, scratch_ram_size> scratch_ram{};
        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};

        std::uint8_t input_p1{};
        std::uint8_t input_p2{};
        std::uint8_t input_system{};
        std::uint8_t dip_switches{m10_dip_default};
        std::uint8_t sound_latch{};
        std::uint8_t control_register{};
        bool flip_screen{};
        bool speaker_output_high{};
        std::uint64_t sound_latch_write_count{};
        std::uint64_t speaker_output_edge_count{};
        std::uint64_t control_write_count{};

        explicit m10_system(common::rom_set_image image, m10_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        [[nodiscard]] std::uint8_t read_io_port(std::uint16_t port) const noexcept;
        void write_io_port(std::uint16_t port, std::uint8_t value) noexcept;
        void write_sound_latch(std::uint8_t value) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m10_system> assemble_m10(common::rom_set_image image,
                                                           m10_board_params board_params = {});

} // namespace mnemos::manifests::irem_m10
