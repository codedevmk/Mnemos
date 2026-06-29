#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "dac8.hpp"
#include "m90_game_manifests.hpp"
#include "rom_set.hpp"
#include "state.hpp"
#include "v30.hpp"
#include "ym2151.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m90 {

    inline constexpr std::uint32_t m90_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 384U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 284U;
    inline constexpr std::uint32_t frame_rate_x1000 = 55018U;
    inline constexpr std::uint32_t main_clock_hz = 14'318'181U;
    inline constexpr std::uint32_t sound_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t fm_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t dac_clock_hz = 3'579'545U;
    inline constexpr std::uint64_t main_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t sound_cycles_per_frame =
        (static_cast<std::uint64_t>(sound_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t fm_cycles_per_frame =
        (static_cast<std::uint64_t>(fm_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t dac_cycles_per_frame =
        (static_cast<std::uint64_t>(dac_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint32_t work_ram_base = 0xA0000U;
    inline constexpr std::size_t work_ram_size = 0x4000U;
    inline constexpr std::uint32_t sprite_ram_base = 0xC0000U;
    inline constexpr std::size_t sprite_ram_size = 0x1000U;
    inline constexpr std::uint32_t palette_ram_base = 0xC8000U;
    inline constexpr std::size_t palette_ram_size = 0x1000U;
    inline constexpr std::uint32_t vram_base = 0xD0000U;
    inline constexpr std::size_t vram_size = 0x8000U;
    inline constexpr std::uint32_t rowscroll_ram_base = 0xE0000U;
    inline constexpr std::size_t rowscroll_ram_size = 0x0800U;

    inline constexpr std::uint16_t sound_rom_base = 0x0000U;
    inline constexpr std::size_t sound_rom_mapped_size = 0xF000U;
    inline constexpr std::uint16_t sound_work_ram_base = 0xF000U;
    inline constexpr std::size_t sound_work_ram_size = 0x1000U;

    inline constexpr std::uint16_t port_in_p1 = 0x00U;
    inline constexpr std::uint16_t port_in_p2 = 0x01U;
    inline constexpr std::uint16_t port_in_system = 0x02U;
    inline constexpr std::uint16_t port_in_dsw_lo = 0x04U;
    inline constexpr std::uint16_t port_in_dsw_hi = 0x05U;
    inline constexpr std::uint16_t port_out_sound_latch = 0x00U;
    inline constexpr std::uint16_t port_out_control = 0x02U;
    inline constexpr std::uint16_t z80_port_ym2151_addr = 0x00U;
    inline constexpr std::uint16_t z80_port_ym2151_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x02U;
    inline constexpr std::uint16_t z80_port_latch_ack = 0x06U;
    inline constexpr std::uint16_t z80_port_sample_addr_lo = 0x80U;
    inline constexpr std::uint16_t z80_port_sample_addr_hi = 0x81U;
    inline constexpr std::uint16_t z80_port_dac = 0x82U;
    inline constexpr std::uint16_t z80_port_sample_read = 0x84U;
    inline constexpr std::uint8_t z80_rst_idle = 0xFFU;
    inline constexpr std::uint8_t z80_rst_latch = 0xDFU;
    inline constexpr std::uint8_t z80_rst_ym = 0xEFU;

    struct m90_board_params final {
        std::uint16_t dip_default{0xFFFFU};
        std::string_view rom_layout{"standard"};
    };

    [[nodiscard]] m90_board_params board_params_for(std::string_view set_name) noexcept;

    class m90_video final : public chips::ivideo {
      public:
        m90_video();

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

        void compose(std::span<const std::uint8_t> main_program,
                     std::span<const std::uint8_t> sound_program,
                     std::span<const std::uint8_t> samples, std::span<const std::uint8_t> graphics,
                     std::span<const std::uint8_t> vram, std::span<const std::uint8_t> rowscroll,
                     std::span<const std::uint8_t> palette,
                     std::span<const std::uint8_t> sprite_ram, std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m90_system final {
        chips::cpu::v30 main_cpu;
        chips::cpu::z80 sound_cpu;
        m90_video video;
        chips::audio::ym2151 fm;
        chips::audio::dac8 dac;
        topology::bus main_bus{20U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        m90_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, palette_ram_size> palette_ram{};
        std::array<std::uint8_t, vram_size> vram{};
        std::array<std::uint8_t, rowscroll_ram_size> rowscroll_ram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};

        std::uint8_t sound_latch{};
        std::uint8_t input_p1{0xFFU};
        std::uint8_t input_p2{0xFFU};
        std::uint8_t input_system{0xFFU};
        std::uint16_t dip_switches{0xFFFFU};
        std::uint8_t control_register{};
        bool sound_latch_irq{};
        std::uint32_t sample_address{};
        struct dac_write_event final {
            std::uint64_t sound_clock{};
            std::int16_t output{};
        };
        std::vector<dac_write_event> dac_write_events{};

        explicit m90_system(common::rom_set_image image, m90_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void update_sound_irq() noexcept;
        void record_dac_write(std::uint8_t value);
        void discard_dac_write_events_before(std::uint64_t sound_clock);
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m90_system> assemble_m90(common::rom_set_image image,
                                                           m90_board_params board_params = {});

} // namespace mnemos::manifests::irem_m90
