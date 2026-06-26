#pragma once

#include "beeper.hpp"
#include "bus.hpp"
#include "chip.hpp"
#include "m52_game_manifests.hpp"
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

namespace mnemos::manifests::irem_m52 {

    inline constexpr std::uint32_t m52_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 240U;
    inline constexpr std::uint32_t visible_height = 252U;
    inline constexpr std::uint32_t frame_lines = 256U;
    inline constexpr std::uint32_t frame_rate_x1000 = 56737U;
    inline constexpr std::uint32_t main_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint64_t main_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::size_t main_rom_size = 0x10000U;
    inline constexpr std::size_t sound_rom_size = 0x10000U;
    inline constexpr std::size_t tx_gfx_size = 0x2000U;
    inline constexpr std::size_t sprite_gfx_size = 0x1000U;
    inline constexpr std::size_t proms_size = 0x0540U;

    inline constexpr std::uint16_t protection_address = 0x8800U;
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

    inline constexpr std::uint8_t mpatrol_dsw1_default = 0xFFU;
    inline constexpr std::uint8_t mpatrol_dsw2_default = 0xFFU;

    struct m52_board_params final {
        std::string_view rom_layout{"standard"};
        std::uint8_t dsw1_default{mpatrol_dsw1_default};
        std::uint8_t dsw2_default{mpatrol_dsw2_default};
    };

    [[nodiscard]] m52_board_params board_params_for(std::string_view set_name) noexcept;

    class m52_video final : public chips::ivideo {
      public:
        m52_video();

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

        void
        compose(std::span<const std::uint8_t> main_program,
                std::span<const std::uint8_t> sound_program, std::span<const std::uint8_t> tx_gfx,
                std::span<const std::uint8_t> sprite_gfx, std::span<const std::uint8_t> proms,
                std::span<const std::uint8_t> video_ram, std::span<const std::uint8_t> color_ram,
                std::span<const std::uint8_t> sprite_ram, std::span<const std::uint8_t> work_ram,
                const std::array<std::uint8_t, 32>& scroll_regs,
                const std::array<std::uint8_t, 2>& bg_x, const std::array<std::uint8_t, 2>& bg_y,
                std::uint8_t bg_control, bool flip_screen, std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m52_system final {
        chips::cpu::z80 main_cpu;
        m52_video video;
        chips::audio::beeper audio_probe;
        topology::bus main_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        m52_board_params params;

        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, 32> scroll_regs{};
        std::array<std::uint8_t, 2> bg_x{};
        std::array<std::uint8_t, 2> bg_y{};

        std::uint8_t input0{0xFFU};
        std::uint8_t input1{0xFFU};
        std::uint8_t input2{0xFFU};
        std::uint8_t dsw1{mpatrol_dsw1_default};
        std::uint8_t dsw2{mpatrol_dsw2_default};
        std::uint8_t bg_control{};
        std::uint8_t sound_command{};
        std::uint8_t flip_latch{};
        bool flip_screen{};
        std::uint64_t sound_command_write_count{};
        std::uint64_t flip_write_count{};

        explicit m52_system(common::rom_set_image image, m52_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m52_system> assemble_m52(common::rom_set_image image,
                                                           m52_board_params board_params = {});

} // namespace mnemos::manifests::irem_m52
