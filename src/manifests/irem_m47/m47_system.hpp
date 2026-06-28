#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "m47_game_manifests.hpp"
#include "rom_set.hpp"
#include "ssg.hpp"
#include "state.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m47 {

    inline constexpr std::uint32_t m47_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 256U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 256U;
    inline constexpr std::uint32_t frame_rate_x1000 = 56737U;
    inline constexpr std::uint32_t main_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t sound_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint32_t ssg_clock_divider = main_clock_hz / audio_rate_hz;
    inline constexpr std::uint64_t main_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t sound_cycles_per_frame =
        (static_cast<std::uint64_t>(sound_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::size_t main_rom_size = 0x8000U;
    inline constexpr std::size_t sound_rom_size = 0x2000U;
    inline constexpr std::size_t samples_size = 0x2000U;
    inline constexpr std::size_t tile_gfx_size = 0x02000U;
    inline constexpr std::size_t sprite_gfx_size = 0x04000U;
    inline constexpr std::size_t proms_size = 0x0220U;
    inline constexpr std::uint16_t sound_rom_base = 0x0000U;
    inline constexpr std::size_t sound_rom_mapped_size = 0x2000U;
    inline constexpr std::uint16_t sound_work_ram_base = 0xE000U;
    inline constexpr std::size_t sound_work_ram_size = 0x0800U;

    inline constexpr std::uint16_t video_ram_base = 0x8000U;
    inline constexpr std::size_t video_ram_size = 0x0400U;
    inline constexpr std::uint16_t color_ram_base = 0x8400U;
    inline constexpr std::size_t color_ram_size = 0x0400U;
    inline constexpr std::uint16_t sprite_ram_base = 0xC800U;
    inline constexpr std::size_t sprite_ram_size = 0x0100U;
    inline constexpr std::uint16_t input0_address = 0xD000U;
    inline constexpr std::uint16_t input1_address = 0xD001U;
    inline constexpr std::uint16_t input2_address = 0xD002U;
    inline constexpr std::uint16_t dsw1_address = 0xD003U;
    inline constexpr std::uint16_t dsw2_address = 0xD004U;
    inline constexpr std::uint16_t work_ram_base = 0xE000U;
    inline constexpr std::size_t work_ram_size = 0x0800U;

    inline constexpr std::uint16_t z80_port_ay0_address = 0x00U;
    inline constexpr std::uint16_t z80_port_ay0_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;
    inline constexpr std::uint16_t z80_port_ay1_address = 0x04U;
    inline constexpr std::uint16_t z80_port_ay1_data = 0x05U;
    inline constexpr std::uint16_t z80_port_latch_ack = 0x06U;
    inline constexpr std::uint8_t z80_rst_idle = 0xFFU;
    inline constexpr std::uint8_t z80_rst_latch = 0xDFU;

    inline constexpr std::uint8_t m47_dsw1_default = 0xFFU;
    inline constexpr std::uint8_t m47_dsw2_default = 0xFFU;

    struct m47_board_params final {
        std::string_view rom_layout{"m47"};
        std::uint8_t dsw1_default{m47_dsw1_default};
        std::uint8_t dsw2_default{m47_dsw2_default};
    };

    [[nodiscard]] m47_board_params board_params_for(std::string_view set_name) noexcept;

    class m47_video final : public chips::ivideo {
      public:
        m47_video();

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

        void compose(std::span<const std::uint8_t> tile_gfx,
                     std::span<const std::uint8_t> sprite_gfx,
                     std::span<const std::uint8_t> proms,
                     std::span<const std::uint8_t> video_ram,
                     std::span<const std::uint8_t> color_ram,
                     std::span<const std::uint8_t> sprite_ram,
                     const std::array<std::uint8_t, 32>& scroll_regs,
                     bool flip_screen,
                     std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m47_system final {
        chips::cpu::z80 main_cpu;
        chips::cpu::z80 sound_cpu;
        m47_video video;
        chips::audio::ssg ay0;
        chips::audio::ssg ay1;
        topology::bus main_bus{16U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        m47_board_params params;

        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};
        std::array<std::uint8_t, 32> scroll_regs{};

        std::uint8_t input0{0xFFU};
        std::uint8_t input1{0xFFU};
        std::uint8_t input2{0xFFU};
        std::uint8_t dsw1{m47_dsw1_default};
        std::uint8_t dsw2{m47_dsw2_default};
        std::uint8_t sound_command{};
        std::uint8_t sound_ay0_address{};
        std::uint8_t sound_ay1_address{};
        std::uint8_t flip_latch{};
        bool flip_screen{};
        bool sound_latch_irq{};
        std::uint64_t sound_command_write_count{};
        std::uint64_t sound_latch_ack_count{};
        std::uint64_t sound_cpu_psg_write_count{};
        std::uint64_t flip_write_count{};

        explicit m47_system(common::rom_set_image image, m47_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void latch_sound_command(std::uint8_t value) noexcept;
        void update_sound_irq() noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m47_system> assemble_m47(common::rom_set_image image,
                                                           m47_board_params board_params = {});

} // namespace mnemos::manifests::irem_m47
