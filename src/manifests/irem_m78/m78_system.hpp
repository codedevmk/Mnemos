#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "dac8.hpp"
#include "m78_game_manifests.hpp"
#include "rom_set.hpp"
#include "state.hpp"
#include "ym2151.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::irem_m78 {

    inline constexpr std::uint32_t m78_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 512U;
    inline constexpr std::uint32_t visible_height = 384U;
    inline constexpr std::uint32_t frame_lines = 384U;
    inline constexpr std::uint32_t frame_rate_x1000 = 55000U;
    inline constexpr std::uint32_t main_clock_hz = 3'579'545U;
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

    inline constexpr std::size_t main_rom_mapped_size = 0x8000U;
    inline constexpr std::uint16_t work_ram_base = 0xE000U;
    inline constexpr std::size_t work_ram_size = 0x0800U;
    inline constexpr std::uint16_t sound_rom_base = 0x0000U;
    inline constexpr std::size_t sound_rom_mapped_size = 0x4000U;
    inline constexpr std::uint16_t sound_work_ram_base = 0xFD00U;
    inline constexpr std::size_t sound_work_ram_size = 0x0300U;
    inline constexpr std::size_t tilemap_ram_size = 0x1000U;
    inline constexpr std::size_t vregs_size = 0x1000U;
    inline constexpr std::size_t layer_control_size = 0x1000U;

    inline constexpr std::uint16_t port_dsw1 = 0x2000U;
    inline constexpr std::uint16_t port_in1 = 0x3000U;
    inline constexpr std::uint16_t port_in0 = 0x5000U;
    inline constexpr std::uint16_t port_layer1_tile = 0x8000U;
    inline constexpr std::uint16_t port_layer1_attr = 0x9000U;
    inline constexpr std::uint16_t port_layer1_color = 0xA000U;
    inline constexpr std::uint16_t port_vregs = 0xB000U;
    inline constexpr std::uint16_t port_layer0_tile = 0xC000U;
    inline constexpr std::uint16_t port_layer0_attr = 0xD000U;
    inline constexpr std::uint16_t port_layer0_color = 0xE000U;
    inline constexpr std::uint16_t port_layer_control = 0xF000U;

    inline constexpr std::uint16_t z80_port_ym2151_addr = 0x00U;
    inline constexpr std::uint16_t z80_port_ym2151_data = 0x01U;
    inline constexpr std::uint16_t z80_port_latch = 0x80U;
    inline constexpr std::uint16_t z80_port_sample_addr_lo = 0x80U;
    inline constexpr std::uint16_t z80_port_sample_addr_hi = 0x81U;
    inline constexpr std::uint16_t z80_port_dac = 0x82U;
    inline constexpr std::uint16_t z80_port_latch_ack = 0x83U;
    inline constexpr std::uint16_t z80_port_sample_read = 0x84U;
    inline constexpr std::uint8_t z80_rst_idle = 0xFFU;
    inline constexpr std::uint8_t z80_rst_latch = 0xDFU;
    inline constexpr std::uint8_t z80_rst_ym = 0xEFU;

    inline constexpr std::uint8_t m78_in0_default = 0xDBU;
    inline constexpr std::uint8_t m78_in1_default = 0x20U;
    inline constexpr std::uint8_t m78_dsw1_default = 0x3FU;

    class m78_video final : public chips::ivideo {
      public:
        m78_video();

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

        void compose(std::span<const std::uint8_t> tiles0,
                     std::span<const std::uint8_t> tiles1,
                     std::span<const std::uint8_t> proms,
                     std::span<const std::uint8_t> layer0_tile,
                     std::span<const std::uint8_t> layer0_attr,
                     std::span<const std::uint8_t> layer0_color,
                     std::span<const std::uint8_t> layer1_tile,
                     std::span<const std::uint8_t> layer1_attr,
                     std::span<const std::uint8_t> layer1_color,
                     std::span<const std::uint8_t> vregs,
                     std::span<const std::uint8_t> layer_control);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m78_system final {
        chips::cpu::z80 main_cpu;
        chips::cpu::z80 sound_cpu;
        m78_video video;
        chips::audio::ym2151 fm;
        chips::audio::dac8 dac;
        topology::bus main_bus{16U, topology::endianness::little};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};
        std::array<std::array<std::uint8_t, tilemap_ram_size>, 2> tile_ram{};
        std::array<std::array<std::uint8_t, tilemap_ram_size>, 2> attr_ram{};
        std::array<std::array<std::uint8_t, tilemap_ram_size>, 2> color_ram{};
        std::array<std::uint8_t, vregs_size> vregs{};
        std::array<std::uint8_t, layer_control_size> layer_control{};

        std::uint8_t input0{m78_in0_default};
        std::uint8_t input1{m78_in1_default};
        std::uint8_t dsw1{m78_dsw1_default};
        std::uint8_t sound_latch{};
        bool sound_latch_irq{};
        std::uint32_t sample_address{};
        struct dac_write_event final {
            std::uint64_t sound_clock{};
            std::int16_t output{};
        };
        std::vector<dac_write_event> dac_write_events{};
        std::uint64_t sound_command_write_count{};
        std::uint64_t sound_latch_ack_count{};
        std::uint64_t ym2151_write_count{};
        std::uint64_t dac_write_count{};
        std::uint64_t vreg_write_count{};
        std::uint64_t layer_control_write_count{};

        explicit m78_system(common::rom_set_image image);

        void run_frame();
        void set_inputs(std::uint8_t in0, std::uint8_t in1) noexcept;
        void latch_sound_command(std::uint8_t value) noexcept;
        void update_sound_irq() noexcept;
        void record_dac_write(std::uint8_t value);
        void discard_dac_write_events_before(std::uint64_t sound_clock);
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        [[nodiscard]] std::uint8_t main_port_read(std::uint16_t port) const noexcept;
        void main_port_write(std::uint16_t port, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t sound_port_read(std::uint16_t port) noexcept;
        void sound_port_write(std::uint16_t port, std::uint8_t value);
    };

    [[nodiscard]] std::unique_ptr<m78_system> assemble_m78(common::rom_set_image image);

} // namespace mnemos::manifests::irem_m78
