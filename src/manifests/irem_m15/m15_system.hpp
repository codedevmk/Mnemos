#pragma once

#include "beeper.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"
#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "rom_set.hpp"
#include "state.hpp"

namespace mnemos::manifests::irem_m15 {

    inline constexpr std::size_t main_rom_size = 0x2000U;
    inline constexpr std::size_t program_rom_size = 0x0400U;
    inline constexpr std::uint32_t m15_system_state_version = 1U;

    inline constexpr std::uint32_t visible_width = 224U;
    inline constexpr std::uint32_t visible_height = 256U;
    inline constexpr std::uint32_t frame_rate_x1000 = 60000U;
    inline constexpr std::uint32_t frame_lines = 262U;
    inline constexpr std::uint32_t cpu_clock_hz = 1'996'800U;
    inline constexpr std::uint32_t audio_rate_hz = 48'000U;
    inline constexpr std::uint64_t cpu_cycles_per_frame =
        (static_cast<std::uint64_t>(cpu_clock_hz) * 1000U) / frame_rate_x1000;

    inline constexpr std::uint16_t work_ram_base = 0x2000U;
    inline constexpr std::size_t work_ram_size = 0x0400U;
    inline constexpr std::uint16_t video_ram_base = 0x2400U;
    inline constexpr std::size_t video_ram_size = 0x0400U;
    inline constexpr std::uint16_t color_ram_base = 0x2800U;
    inline constexpr std::size_t color_ram_size = 0x0400U;

    inline constexpr std::uint16_t port_in_p1 = 0x00U;
    inline constexpr std::uint16_t port_in_p2 = 0x01U;
    inline constexpr std::uint16_t port_in_system = 0x02U;
    inline constexpr std::uint16_t port_in_dip = 0x03U;
    inline constexpr std::uint16_t port_out_speaker = 0x00U;
    inline constexpr std::uint16_t port_out_control = 0x01U;

    struct m15_board_params final {
        std::uint32_t cpu_clock_hz{1'996'800U};
        std::string_view rom_layout{"standard"};
        std::uint8_t dip_default{0xFFU};
    };

    [[nodiscard]] m15_board_params board_params_for(std::string_view set_name) noexcept;

    class m15_i8080_cpu final : public chips::icpu {
      public:
        using read_fn = std::function<std::uint8_t(std::uint16_t address)>;
        using write_fn = std::function<void(std::uint16_t address, std::uint8_t value)>;
        using port_in_fn = std::function<std::uint8_t(std::uint16_t port)>;
        using port_out_fn = std::function<void(std::uint16_t port, std::uint8_t value)>;

        m15_i8080_cpu();

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(chips::reset_kind kind) override;
        void save_state(chips::state_writer& writer) const override;
        void load_state(chips::state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }
        void attach_bus(chips::ibus& bus) noexcept override;

        void set_memory(read_fn read, write_fn write) noexcept;
        void set_ports(port_in_fn input, port_out_fn output) noexcept;
        [[nodiscard]] std::span<const chips::register_descriptor> register_snapshot() noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_cycles_; }
        [[nodiscard]] std::uint16_t pc() const noexcept { return pc_; }
        [[nodiscard]] std::uint32_t unsupported_opcode_count() const noexcept {
            return unsupported_opcodes_;
        }

      private:
        [[nodiscard]] std::uint8_t read8(std::uint16_t address) const;
        void write8(std::uint16_t address, std::uint8_t value);
        [[nodiscard]] std::uint8_t fetch8();
        [[nodiscard]] std::uint16_t fetch16();
        [[nodiscard]] int step_instruction();
        void set_zero_sign_flags(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t reg_at(std::uint8_t index) const noexcept;
        void set_reg_at(std::uint8_t index, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t hl() const noexcept {
            return static_cast<std::uint16_t>((h_ << 8U) | l_);
        }
        void set_hl(std::uint16_t value) noexcept {
            h_ = static_cast<std::uint8_t>(value >> 8U);
            l_ = static_cast<std::uint8_t>(value);
        }

        read_fn read_{};
        write_fn write_{};
        port_in_fn port_in_{};
        port_out_fn port_out_{};
        std::function<void(std::uint32_t)> trace_callback_{};
        instrumentation::introspection_builder introspection_{};
        std::array<chips::register_descriptor, 12> register_view_{};

        std::uint8_t a_{};
        std::uint8_t b_{};
        std::uint8_t c_{};
        std::uint8_t d_{};
        std::uint8_t e_{};
        std::uint8_t h_{};
        std::uint8_t l_{};
        std::uint8_t flags_{};
        std::uint16_t pc_{};
        std::uint16_t sp_{0x2400U};
        std::uint64_t elapsed_cycles_{};
        std::uint32_t unsupported_opcodes_{};
        bool halted_{};
    };

    class m15_video final : public chips::ivideo {
      public:
        m15_video();

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
        void compose(std::span<const std::uint8_t> program_rom,
                     std::span<const std::uint8_t> video_ram,
                     std::span<const std::uint8_t> color_ram,
                     std::span<const std::uint8_t> work_ram, std::uint8_t control,
                     std::string_view rom_layout);

      private:
        std::vector<std::uint32_t> pixels_;
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};
        instrumentation::ichip_introspection introspection_{};
    };

    struct m15_system final {
        m15_i8080_cpu main_cpu;
        m15_video video;
        chips::audio::beeper speaker;

        common::rom_set_image roms;
        m15_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, video_ram_size> video_ram{};
        std::array<std::uint8_t, color_ram_size> color_ram{};

        std::uint8_t input_p1{0xFFU};
        std::uint8_t input_p2{0xFFU};
        std::uint8_t input_system{0xFFU};
        std::uint8_t dip_switches{0xFFU};
        std::uint8_t control_register{};
        std::uint8_t speaker_latch{};

        explicit m15_system(common::rom_set_image image, m15_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<m15_system> assemble_m15(common::rom_set_image image,
                                                           m15_board_params board_params = {});

} // namespace mnemos::manifests::irem_m15
