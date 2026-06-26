#pragma once

#include "bus.hpp"
#include "chip.hpp"
#include "irem_ga20.hpp"
#include "m92_game_manifests.hpp"
#include "rom_set.hpp"
#include "state.hpp"
#include "v30.hpp"
#include "ym2151.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_m92 {

    inline constexpr std::uint32_t m92_system_state_version = 3U;

    inline constexpr std::uint32_t visible_width = 320U;
    inline constexpr std::uint32_t visible_height = 240U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60011U;
    inline constexpr std::uint32_t main_clock_hz = 9'000'000U;
    inline constexpr std::uint32_t sound_cpu_clock_hz = 14'318'181U;
    inline constexpr std::uint32_t fm_clock_hz = 3'579'545U;
    inline constexpr std::uint32_t pcm_clock_hz = 3'579'545U;
    inline constexpr std::uint64_t main_cycles_per_frame =
        (static_cast<std::uint64_t>(main_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t sound_cycles_per_frame =
        (static_cast<std::uint64_t>(sound_cpu_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t fm_cycles_per_frame =
        (static_cast<std::uint64_t>(fm_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint64_t pcm_cycles_per_frame =
        (static_cast<std::uint64_t>(pcm_clock_hz) * 1000U) / frame_rate_x1000;
    inline constexpr std::uint32_t pcm_capture_divider =
        chips::audio::ym2151::clocks_per_sample / chips::audio::irem_ga20::clocks_per_sample;
    static_assert(chips::audio::ym2151::clocks_per_sample %
                      chips::audio::irem_ga20::clocks_per_sample ==
                  0U);

    inline constexpr std::uint32_t vram_base = 0x80000U;
    inline constexpr std::size_t vram_size = 0x10000U;
    inline constexpr std::uint32_t work_ram_base = 0xE0000U;
    inline constexpr std::size_t work_ram_size = 0x10000U;
    inline constexpr std::uint32_t sprite_ram_base = 0xF8000U;
    inline constexpr std::size_t sprite_ram_size = 0x0800U;
    inline constexpr std::uint32_t palette_ram_base = 0xF8800U;
    inline constexpr std::size_t palette_ram_size = 0x0800U;
    inline constexpr std::uint32_t sound_work_ram_base = 0xA0000U;
    inline constexpr std::size_t sound_work_ram_size = 0x4000U;
    inline constexpr std::uint32_t sound_ga20_base = 0xA8000U;
    inline constexpr std::uint32_t sound_ym2151_base = 0xA8040U;
    inline constexpr std::uint32_t sound_latch_addr = 0xA8044U;
    inline constexpr std::uint32_t sound_reply_addr = 0xA8046U;

    inline constexpr std::uint16_t port_in_p1 = 0x00U;
    inline constexpr std::uint16_t port_in_p2 = 0x01U;
    inline constexpr std::uint16_t port_in_system = 0x02U;
    inline constexpr std::uint16_t port_in_dsw_lo = 0x04U;
    inline constexpr std::uint16_t port_in_dsw_hi = 0x05U;
    inline constexpr std::uint16_t port_out_sound_latch = 0x00U;
    inline constexpr std::uint16_t port_out_control = 0x02U;
    inline constexpr std::uint16_t sound_port_latch = 0x00U;
    inline constexpr std::uint16_t sound_port_reply = 0x02U;
    inline constexpr std::uint16_t sound_port_ym2151_addr = 0x10U;
    inline constexpr std::uint16_t sound_port_ym2151_data = 0x11U;
    inline constexpr std::uint16_t sound_port_ga20_base = 0x20U;
    inline constexpr std::uint16_t sound_port_ga20_limit =
        sound_port_ga20_base + chips::audio::irem_ga20::register_count;
    inline constexpr std::uint8_t sound_irq_vector_ym2151 = 24U;        // V35 INTP0
    inline constexpr std::uint8_t sound_irq_vector_command_latch = 25U; // V35 INTP1

    struct m92_board_params final {
        std::uint16_t dip_default{0xFFFFU};
        std::string_view rom_layout{"standard"};
    };

    [[nodiscard]] m92_board_params board_params_for(std::string_view set_name) noexcept;

    class m92_video final : public chips::ivideo {
      public:
        m92_video();

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

        void compose(std::span<const std::uint8_t> tiles, std::span<const std::uint8_t> sprites,
                     std::span<const std::uint8_t> plds, std::span<const std::uint8_t> samples,
                     std::span<const std::uint8_t> vram, std::span<const std::uint8_t> palette,
                     std::span<const std::uint8_t> sprite_ram, std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m92_system final {
        chips::cpu::v30 main_cpu;
        chips::cpu::v30 sound_cpu;
        m92_video video;
        chips::audio::ym2151 fm;
        chips::audio::irem_ga20 pcm;
        topology::bus main_bus{20U, topology::endianness::little};
        topology::bus sound_bus{20U, topology::endianness::little};

        common::rom_set_image roms;
        m92_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, palette_ram_size> palette_ram{};
        std::array<std::uint8_t, vram_size> vram{};
        std::array<std::uint8_t, sound_work_ram_size> sound_ram{};

        std::uint8_t input_p1{0xFFU};
        std::uint8_t input_p2{0xFFU};
        std::uint8_t input_system{0xFFU};
        std::uint16_t dip_switches{0xFFFFU};
        std::uint8_t sound_latch{0xFFU};
        std::uint8_t sound_reply{0xFFU};
        std::uint8_t control_register{};
        std::uint8_t ym_address{};
        bool sound_latch_pending{};
        bool sound_reply_pending{};

        explicit m92_system(common::rom_set_image image, m92_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void update_sound_irq() noexcept;
        void write_sound_latch(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_sound_latch() noexcept;
        void acknowledge_sound_latch() noexcept;
        void write_sound_reply(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_sound_reply() noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m92_system> assemble_m92(common::rom_set_image image,
                                                           m92_board_params board_params = {});

} // namespace mnemos::manifests::irem_m92
