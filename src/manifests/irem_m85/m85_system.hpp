#pragma once

#include "m81_system.hpp"
#include "rom_set.hpp"
#include "state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m85 {

    inline constexpr std::size_t main_rom_size = 0x100000U;
    inline constexpr std::size_t plds_size = 0x0600U;
    inline constexpr std::uint32_t m85_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = irem_m81::visible_width;
    inline constexpr std::uint32_t visible_height = irem_m81::visible_height;
    inline constexpr std::uint32_t frame_lines = irem_m81::frame_lines;
    inline constexpr std::uint32_t frame_rate_x1000 = irem_m81::frame_rate_x1000;
    inline constexpr std::uint32_t main_clock_hz = irem_m81::main_clock_hz;
    inline constexpr std::uint32_t sound_clock_hz = irem_m81::sound_clock_hz;
    inline constexpr std::uint64_t main_cycles_per_frame = irem_m81::main_cycles_per_frame;
    inline constexpr std::uint64_t sound_cycles_per_frame = irem_m81::sound_cycles_per_frame;

    struct m85_board_params final {
        std::uint16_t dip_default{0xFFFFU};
        std::string_view rom_layout{"m85_program_pair"};
        chips::cpu::v30::model main_cpu_model{chips::cpu::v30::model::v30};
    };

    [[nodiscard]] m85_board_params board_params_for(std::string_view set_name) noexcept;

    // First executable M85 slice: Pound for Pound uses the shared
    // M81-compatible V30/Z80/YM2151/DAC board core while preserving an M85
    // manifest/save identity and M85-owned media routing.
    struct m85_system final {
        irem_m81::m81_system board;
        m85_board_params params;

        chips::cpu::v30& main_cpu;
        chips::cpu::z80& sound_cpu;
        irem_m81::m81_video& video;
        chips::audio::ym2151& fm;
        chips::audio::dac8& dac;
        chips::bus_controller::pic_8259& pic;

        std::array<std::uint8_t, irem_m81::work_ram_size>& work_ram;
        std::array<std::uint8_t, irem_m81::sprite_ram_size>& sprite_ram;
        std::array<std::uint8_t, irem_m81::palette_ram_size>& palette_ram;
        std::array<std::uint8_t, irem_m81::vram_size>& vram;
        std::array<std::uint8_t, irem_m81::rowscroll_ram_size>& rowscroll_ram;
        std::array<std::uint8_t, irem_m81::sound_work_ram_size>& sound_ram;

        std::uint8_t& input_system;
        std::uint16_t& dip_switches;
        std::vector<irem_m81::m81_system::dac_write_event>& dac_write_events;

        explicit m85_system(common::rom_set_image image, m85_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void discard_dac_write_events_before(std::uint64_t sound_clock);
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m85_system> assemble_m85(common::rom_set_image image,
                                                           m85_board_params board_params = {});

} // namespace mnemos::manifests::irem_m85
