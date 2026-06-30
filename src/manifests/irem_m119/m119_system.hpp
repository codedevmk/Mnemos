#pragma once

#include "bus.hpp"
#include "m119_game_manifests.hpp"
#include "rom_set.hpp"
#include "sh3.hpp"
#include "state.hpp"
#include "upd94244.hpp"
#include "ymz280b.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m119 {

    inline constexpr std::uint32_t m119_system_state_version = 2U;

    inline constexpr std::uint32_t visible_width = chips::video::upd94244::visible_width;
    inline constexpr std::uint32_t visible_height = chips::video::upd94244::visible_height;
    inline constexpr std::uint32_t frame_rate_x1000 = chips::video::upd94244::frame_rate_x1000;
    inline constexpr std::uint32_t main_clock_hz = 60'000'000U;
    inline constexpr std::uint32_t ymz_clock_hz = chips::audio::ymz280b::default_input_clock_hz;
    inline constexpr std::uint32_t ymz_capture_divider = 1U;
    inline constexpr std::uint32_t audio_rate_hz =
        ymz_clock_hz / chips::audio::ymz280b::clocks_per_sample / ymz_capture_divider;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t first_pass_cpu_cycles_per_frame = 20'000U;
    inline constexpr std::uint64_t ymz_cycles_per_frame =
        (static_cast<std::uint64_t>(ymz_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint32_t main_rom_base = 0x00000000U;
    inline constexpr std::uint32_t work_ram_base = 0x00080000U;
    inline constexpr std::size_t work_ram_size = 0x20000U;
    inline constexpr std::uint32_t video_ram_base = 0x00100000U;
    inline constexpr std::size_t video_ram_size = chips::video::upd94244::vram_size;
    inline constexpr std::uint32_t nvram_base = 0x00120000U;
    inline constexpr std::size_t nvram_size = 0x2000U;
    inline constexpr std::uint32_t mmio_base = 0x10000000U;
    inline constexpr std::uint32_t mmio_size = 0x100U;

    inline constexpr std::uint32_t mmio_input = 0x00U;
    inline constexpr std::uint32_t mmio_control = 0x01U;
    inline constexpr std::uint32_t mmio_vdp_base = 0x10U;
    inline constexpr std::uint32_t mmio_vdp_register_stride = 4U;
    inline constexpr std::uint32_t mmio_vdp_register_count = 8U;
    inline constexpr std::uint32_t mmio_vdp_end =
        mmio_vdp_base + (mmio_vdp_register_count * mmio_vdp_register_stride) - 1U;
    inline constexpr std::uint32_t mmio_ymz_base = 0x40U;
    inline constexpr std::uint32_t mmio_ymz_end = 0xFFU;

    inline constexpr std::uint8_t input_default = 0xFFU;
    inline constexpr std::uint8_t input_coin1_bit = 0x01U;
    inline constexpr std::uint8_t input_service_bit = 0x02U;
    inline constexpr std::uint8_t input_start1_bit = 0x04U;
    inline constexpr std::uint8_t input_button1_bit = 0x10U;
    inline constexpr std::uint8_t input_button2_bit = 0x20U;

    struct m119_board_params final {
        std::uint32_t main_clock_hz{mnemos::manifests::irem_m119::main_clock_hz};
        std::uint32_t ymz_clock_hz{mnemos::manifests::irem_m119::ymz_clock_hz};
        std::string_view rom_layout{"m119_scumimon_sh7708_upd94244_ymz280b_first_pass"};
    };

    [[nodiscard]] m119_board_params board_params_for(std::string_view set_name) noexcept;

    struct m119_system final {
        topology::bus main_bus{32U, topology::endianness::big};
        chips::cpu::sh3 main_cpu;
        chips::video::upd94244 video;
        chips::audio::ymz280b ymz;

        common::rom_set_image roms;
        m119_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, nvram_size> nvram{};

        std::uint8_t input_latch{input_default};
        std::uint8_t control_latch{};
        std::uint64_t frames_run{};
        std::uint64_t mmio_read_count{};
        std::uint64_t mmio_write_count{};
        std::uint64_t vdp_register_write_count{};
        std::uint64_t ymz_register_write_count{};
        std::uint64_t ymz_key_on_count{};

        explicit m119_system(common::rom_set_image image, m119_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t input) noexcept { input_latch = input; }
        [[nodiscard]] std::uint8_t read_mmio(std::uint32_t offset) noexcept;
        void write_mmio(std::uint32_t offset, std::uint8_t value) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        void prime_audio_preview() noexcept;
    };

    [[nodiscard]] std::unique_ptr<m119_system> assemble_m119(common::rom_set_image image,
                                                             m119_board_params board_params = {});

} // namespace mnemos::manifests::irem_m119
