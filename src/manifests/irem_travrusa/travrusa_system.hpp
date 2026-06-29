#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "msm5205.hpp"
#include "rom_set.hpp"
#include "ssg.hpp"
#include "state.hpp"
#include "travrusa_game_manifests.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_travrusa {

    inline constexpr std::uint32_t travrusa_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 240U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 282U;
    inline constexpr std::uint32_t frame_rate_x1000 = 56737U;
    inline constexpr std::uint32_t main_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t sound_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint32_t ssg_clock_divider = main_clock_hz / audio_rate_hz;
    inline constexpr std::uint64_t main_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t sound_cycles_per_frame =
        (static_cast<std::uint64_t>(sound_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::size_t main_rom_size = 0x12000U;
    inline constexpr std::size_t sound_rom_size = 0x10000U;
    inline constexpr std::size_t tiles_size = 0x6000U;
    inline constexpr std::size_t sprites_size = 0x6000U;
    inline constexpr std::size_t proms_size = 0x0320U;
    inline constexpr std::uint16_t sound_rom_base = 0x0000U;
    inline constexpr std::size_t sound_rom_mapped_size = 0xF000U;
    inline constexpr std::uint16_t sound_work_ram_base = 0xF000U;
    inline constexpr std::size_t sound_work_ram_size = 0x1000U;

    inline constexpr std::uint16_t main_rom_mapped_size = 0x8000U;
    inline constexpr std::uint16_t video_ram_base = 0x8000U;
    inline constexpr std::size_t video_ram_size = 0x1000U;
    inline constexpr std::uint16_t scroll_x_low_address = 0x9000U;
    inline constexpr std::uint16_t scroll_x_high_address = 0xA000U;
    inline constexpr std::uint16_t sprite_ram_base = 0xC800U;
    inline constexpr std::size_t sprite_ram_size = 0x0200U;
    inline constexpr std::uint16_t input_system_address = 0xD000U;
    inline constexpr std::uint16_t input_p1_address = 0xD001U;
    inline constexpr std::uint16_t input_p2_address = 0xD002U;
    inline constexpr std::uint16_t dsw1_address = 0xD003U;
    inline constexpr std::uint16_t dsw2_address = 0xD004U;
    inline constexpr std::uint16_t work_ram_base = 0xE000U;
    inline constexpr std::size_t work_ram_size = 0x1000U;

    inline constexpr std::uint16_t z80_port_ay0_address = 0x00U;
    inline constexpr std::uint16_t z80_port_ay0_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;
    inline constexpr std::uint16_t z80_port_ay1_address = 0x04U;
    inline constexpr std::uint16_t z80_port_ay1_data = 0x05U;
    inline constexpr std::uint16_t z80_port_latch_ack = 0x06U;
    inline constexpr std::uint16_t z80_port_msm_data = 0x08U;
    inline constexpr std::uint16_t z80_port_msm_control = 0x09U;
    inline constexpr std::uint8_t z80_rst_idle = 0xFFU;
    inline constexpr std::uint8_t z80_rst_latch = 0xDFU;

    inline constexpr std::uint8_t travrusa_dsw1_default = 0xFFU;
    inline constexpr std::uint8_t travrusa_dsw2_default = 0xFFU;

    struct travrusa_board_params final {
        std::string_view rom_layout{"travrusa"};
        std::uint8_t dsw1_default{travrusa_dsw1_default};
        std::uint8_t dsw2_default{travrusa_dsw2_default};
    };

    [[nodiscard]] travrusa_board_params board_params_for(std::string_view set_name) noexcept;

    class travrusa_video final : public chips::ivideo {
      public:
        travrusa_video();

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

        void compose(std::span<const std::uint8_t> tiles,
                     std::span<const std::uint8_t> sprites,
                     std::span<const std::uint8_t> proms,
                     std::span<const std::uint8_t> video_ram,
                     std::span<const std::uint8_t> sprite_ram,
                     std::uint16_t scroll_x,
                     bool flip_screen,
                     std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct travrusa_system final {
        chips::cpu::z80 main_cpu;
        chips::cpu::z80 sound_cpu;
        travrusa_video video;
        chips::audio::ssg ay0;
        chips::audio::ssg ay1;
        chips::audio::msm5205 msm;
        topology::bus main_bus{16U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        travrusa_board_params params;

        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};

        std::uint8_t input_system{0xFFU};
        std::uint8_t input_p1{0xFFU};
        std::uint8_t input_p2{0xFFU};
        std::uint8_t dsw1{travrusa_dsw1_default};
        std::uint8_t dsw2{travrusa_dsw2_default};
        std::uint8_t scroll_x_low{};
        std::uint8_t scroll_x_high{};
        std::uint8_t sound_command{};
        std::uint8_t sound_ay0_address{};
        std::uint8_t sound_ay1_address{};
        std::uint8_t flip_latch{};
        bool flip_screen{};
        bool sound_latch_irq{};
        std::uint64_t sound_command_write_count{};
        std::uint64_t sound_latch_ack_count{};
        std::uint64_t sound_cpu_msm_write_count{};
        std::uint64_t flip_write_count{};
        std::uint64_t scroll_write_count{};

        explicit travrusa_system(common::rom_set_image image,
                                 travrusa_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        [[nodiscard]] std::uint16_t scroll_x() const noexcept;
        void latch_sound_command(std::uint8_t value) noexcept;
        void update_sound_irq() noexcept;
        void sound_cpu_write_msm(std::uint8_t value) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<travrusa_system>
    assemble_travrusa(common::rom_set_image image, travrusa_board_params board_params = {});

} // namespace mnemos::manifests::irem_travrusa
