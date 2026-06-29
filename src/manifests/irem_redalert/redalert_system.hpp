#pragma once

#include "beeper.hpp"
#include "chip.hpp"
#include "ibus.hpp"
#include "introspection_adapters.hpp"
#include "m6510.hpp"
#include "redalert_game_manifests.hpp"
#include "rom_set.hpp"
#include "state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::manifests::irem_redalert {

    inline constexpr std::uint32_t redalert_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 256U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60000U;
    inline constexpr std::uint32_t cpu_clock_hz = 781'250U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t ram_base = 0x0000U;
    inline constexpr std::size_t ram_size = 0x2000U;
    inline constexpr std::uint16_t bitmap_ram_base = 0x2000U;
    inline constexpr std::size_t bitmap_ram_size = 0x2000U;
    inline constexpr std::size_t bitmap_color_ram_size = 0x0400U;
    inline constexpr std::uint16_t char_ram_base = 0x4000U;
    inline constexpr std::size_t char_ram_size = 0x1000U;
    inline constexpr std::uint16_t program_rom_base = 0x5000U;
    inline constexpr std::uint16_t program_rom_limit = 0xC000U;
    inline constexpr std::uint16_t vector_mirror_base = 0xF000U;
    inline constexpr std::uint16_t vector_mirror_source = 0x8000U;

    inline constexpr std::uint16_t io_decode_mask = 0xF070U;
    inline constexpr std::uint16_t dip_switch_address = 0xC000U;
    inline constexpr std::uint16_t key1_address = 0xC010U;
    inline constexpr std::uint16_t key2_address = 0xC020U;
    inline constexpr std::uint16_t audio_command_address = 0xC030U;
    inline constexpr std::uint16_t video_control_address = 0xC040U;
    inline constexpr std::uint16_t bitmap_color_address = 0xC050U;
    inline constexpr std::uint16_t interrupt_clear_address = 0xC070U;

    inline constexpr std::uint8_t ww3_dip_default = 0x10U;

    inline constexpr std::uint8_t key1_start1_bit = 0x01U;
    inline constexpr std::uint8_t key1_start2_bit = 0x02U;
    inline constexpr std::uint8_t key1_button1_bit = 0x04U;
    inline constexpr std::uint8_t key1_up_bit = 0x08U;
    inline constexpr std::uint8_t key1_down_bit = 0x10U;
    inline constexpr std::uint8_t key1_left_bit = 0x20U;
    inline constexpr std::uint8_t key1_right_bit = 0x40U;
    inline constexpr std::uint8_t key2_sound_status_bit = 0x01U;
    inline constexpr std::uint8_t key2_button1_bit = 0x04U;
    inline constexpr std::uint8_t key2_up_bit = 0x08U;
    inline constexpr std::uint8_t key2_down_bit = 0x10U;
    inline constexpr std::uint8_t key2_left_bit = 0x20U;
    inline constexpr std::uint8_t key2_right_bit = 0x40U;
    inline constexpr std::uint8_t coin1_bit = 0x01U;
    inline constexpr std::uint8_t coin2_bit = 0x02U;
    inline constexpr std::uint8_t service_bit = 0x04U;
    inline constexpr std::uint8_t video_flip_bit = 0x04U;
    inline constexpr std::uint8_t audio_nmi_low_bit = 0x80U;

    struct redalert_board_params final {
        std::uint32_t cpu_clock_hz{mnemos::manifests::irem_redalert::cpu_clock_hz};
        std::string_view rom_layout{"redalert_ww3_m27mb"};
        std::uint8_t dip_default{ww3_dip_default};
        std::uint8_t video_control_xor{video_flip_bit};
    };

    [[nodiscard]] redalert_board_params board_params_for(std::string_view set_name) noexcept;

    struct redalert_system;

    class redalert_cpu_bus final : public chips::ibus {
      public:
        void attach(redalert_system& system) noexcept { system_ = &system; }
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

      private:
        redalert_system* system_{};
    };

    class redalert_video final : public chips::ivideo {
      public:
        redalert_video();

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
        void compose(std::span<const std::uint8_t> bitmap_ram,
                     std::span<const std::uint8_t> bitmap_color_ram,
                     std::span<const std::uint8_t> char_ram,
                     std::span<const std::uint8_t> proms,
                     std::uint8_t video_control,
                     std::uint8_t control_xor,
                     std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct redalert_system final {
        redalert_cpu_bus main_bus;
        chips::cpu::m6510 main_cpu;
        redalert_video video;
        chips::audio::beeper speaker;

        common::rom_set_image roms;
        redalert_board_params params;

        std::array<std::uint8_t, ram_size> ram{};
        std::array<std::uint8_t, bitmap_ram_size> bitmap_ram{};
        std::array<std::uint8_t, bitmap_color_ram_size> bitmap_color_ram{};
        std::array<std::uint8_t, char_ram_size> char_ram{};

        std::uint8_t key1{};
        std::uint8_t key2{};
        std::uint8_t coin_inputs{};
        std::uint8_t dip_switches{ww3_dip_default};
        std::uint8_t audio_command{};
        std::uint8_t video_control{};
        std::uint8_t bitmap_color{};
        bool sound_handshake{};
        bool speaker_output_high{};
        std::uint64_t audio_command_write_count{};
        std::uint64_t speaker_output_edge_count{};
        std::uint64_t video_control_write_count{};
        std::uint64_t bitmap_color_write_count{};
        std::uint64_t interrupt_ack_count{};

        explicit redalert_system(common::rom_set_image image,
                                 redalert_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t coins) noexcept;
        void write_audio_command(std::uint8_t value) noexcept;
        [[nodiscard]] bool flip_screen() const noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<redalert_system>
    assemble_redalert(common::rom_set_image image, redalert_board_params board_params = {});

} // namespace mnemos::manifests::irem_redalert
