#pragma once

#include "beeper.hpp"
#include "bus.hpp"
#include "chip.hpp"
#include "ibus.hpp"
#include "introspection_adapters.hpp"
#include "m6803.hpp"
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

namespace mnemos::manifests::irem_m62 {

    inline constexpr std::uint32_t m62_system_state_version = 3U;

    inline constexpr std::size_t main_rom_size = 0x10000U;
    inline constexpr std::size_t sound_rom_size = 0x10000U;
    inline constexpr std::size_t gfx_rom_size = 0x6000U;

    inline constexpr std::uint32_t visible_width = 256U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60000U;
    inline constexpr std::uint32_t cpu_clock_hz = 3'072'000U;
    inline constexpr std::uint32_t sound_clock_hz = 894'886U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint32_t ssg_clock_divider = sound_clock_hz / audio_rate_hz;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t sound_cycles_per_frame =
        (static_cast<std::uint64_t>(sound_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t sound_rom_base = 0x8000U;
    inline constexpr std::size_t sound_rom_mapped_size = 0x8000U;
    inline constexpr std::uint16_t sound_work_ram_base = 0x0080U;
    inline constexpr std::size_t sound_work_ram_size = 0x0080U;
    inline constexpr std::uint16_t sound_io_base = 0x0000U;
    inline constexpr std::uint16_t sound_io_size = 0x0008U;
    inline constexpr std::uint16_t m6803_io_ay0_address = 0x00U;
    inline constexpr std::uint16_t m6803_io_ay0_data = 0x01U;
    inline constexpr std::uint16_t m6803_io_latch = 0x02U;
    inline constexpr std::uint16_t m6803_io_ay1_address = 0x04U;
    inline constexpr std::uint16_t m6803_io_ay1_data = 0x05U;
    inline constexpr std::uint16_t m6803_io_latch_ack = 0x06U;

    inline constexpr std::uint16_t video_ram_base = 0x8000U;
    inline constexpr std::size_t video_ram_size = 0x0800U;
    inline constexpr std::uint16_t color_ram_base = 0x8800U;
    inline constexpr std::size_t color_ram_size = 0x0800U;
    inline constexpr std::uint16_t scratch_ram_base = 0xC000U;
    inline constexpr std::size_t scratch_ram_size = 0x0800U;
    inline constexpr std::uint16_t work_ram_base = 0xD000U;
    inline constexpr std::size_t work_ram_size = 0x0800U;
    inline constexpr std::uint16_t program_rom_base = 0x0000U;
    inline constexpr std::uint16_t program_rom_limit = 0x8000U;

    inline constexpr std::uint16_t input_p1_address = 0xA000U;
    inline constexpr std::uint16_t input_p2_address = 0xA001U;
    inline constexpr std::uint16_t input_system_address = 0xA002U;
    inline constexpr std::uint16_t dip_switch_address = 0xA003U;
    inline constexpr std::uint16_t sound_latch_address = 0xA004U;
    inline constexpr std::uint16_t control_register_address = 0xA005U;

    inline constexpr std::uint8_t ldrun_dip_default = 0xFFU;

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

    struct m62_board_params final {
        std::uint32_t cpu_clock_hz{mnemos::manifests::irem_m62::cpu_clock_hz};
        std::string_view rom_layout{"m62_ldrun_z80_raw_media"};
        std::uint8_t dip_default{ldrun_dip_default};
    };

    [[nodiscard]] m62_board_params board_params_for(std::string_view set_name) noexcept;

    struct m62_system;

    class m62_cpu_bus final : public chips::ibus {
      public:
        void attach(m62_system& system) noexcept { system_ = &system; }
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

      private:
        m62_system* system_{};
    };

    class m62_video final : public chips::ivideo {
      public:
        m62_video();

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

    struct m62_system final {
        m62_cpu_bus main_bus;
        chips::cpu::z80 main_cpu;
        chips::cpu::m6803 sound_cpu;
        m62_video video;
        chips::audio::ssg ay0;
        chips::audio::ssg ay1;
        chips::audio::beeper speaker;
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        m62_board_params params;

        std::array<std::uint8_t, scratch_ram_size> scratch_ram{};
        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};
        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};

        std::uint8_t input_p1{};
        std::uint8_t input_p2{};
        std::uint8_t input_system{};
        std::uint8_t dip_switches{ldrun_dip_default};
        std::uint8_t sound_latch{};
        std::uint8_t sound_ay0_address{};
        std::uint8_t sound_ay1_address{};
        std::uint8_t control_register{};
        bool flip_screen{};
        bool speaker_output_high{};
        bool sound_cpu_enabled{};
        bool sound_latch_irq{};
        std::uint64_t sound_latch_write_count{};
        std::uint64_t sound_latch_ack_count{};
        std::uint64_t sound_cpu_psg_write_count{};
        std::uint64_t speaker_output_edge_count{};
        std::uint64_t control_write_count{};

        explicit m62_system(common::rom_set_image image, m62_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        [[nodiscard]] std::uint8_t read_io_port(std::uint16_t port) const noexcept;
        void write_io_port(std::uint16_t port, std::uint8_t value) noexcept;
        void write_sound_latch(std::uint8_t value) noexcept;
        void update_sound_irq() noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m62_system> assemble_m62(common::rom_set_image image,
                                                           m62_board_params board_params = {});

} // namespace mnemos::manifests::irem_m62
