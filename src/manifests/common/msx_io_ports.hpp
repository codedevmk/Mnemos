#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::common {

    struct msx_fdc_drive_control final {
        std::uint8_t drive{};
        std::uint8_t side{};
    };

    class msx_mouse_port final {
      public:
        void attach_protocol_delta(std::int16_t delta_x, std::int16_t delta_y, bool left_button,
                                   bool right_button) noexcept;
        void detach() noexcept;
        [[nodiscard]] bool attached() const noexcept { return attached_; }

        void set_pin8(bool high) noexcept;
        [[nodiscard]] bool pin8_high() const noexcept { return pin8_high_; }
        [[nodiscard]] std::uint8_t phase() const noexcept { return phase_; }
        [[nodiscard]] std::uint8_t protocol_delta_x() const noexcept { return delta_x_; }
        [[nodiscard]] std::uint8_t protocol_delta_y() const noexcept { return delta_y_; }
        [[nodiscard]] bool left_button() const noexcept { return left_button_; }
        [[nodiscard]] bool right_button() const noexcept { return right_button_; }
        [[nodiscard]] std::uint8_t read_port_bits(std::uint8_t joystick_bits) const noexcept;
        void restore(bool attached, bool pin8_high, std::uint8_t phase, std::uint8_t delta_x,
                     std::uint8_t delta_y, bool left_button, bool right_button) noexcept;

      private:
        void rebuild_nibbles() noexcept;
        void clear_consumed_delta() noexcept;

        bool attached_{};
        bool pin8_high_{};
        std::uint8_t phase_{};
        std::uint8_t delta_x_{};
        std::uint8_t delta_y_{};
        std::array<std::uint8_t, 4> nibbles_{};
        bool left_button_{};
        bool right_button_{};
    };

    [[nodiscard]] bool msx_ppi_port_a_output(std::uint8_t control) noexcept;
    [[nodiscard]] bool msx_ppi_port_b_output(std::uint8_t control) noexcept;
    [[nodiscard]] bool msx_ppi_port_c_upper_output(std::uint8_t control) noexcept;
    [[nodiscard]] bool msx_ppi_port_c_lower_output(std::uint8_t control) noexcept;
    [[nodiscard]] std::uint8_t msx_ppi_port_c_read(std::uint8_t control,
                                                   std::uint8_t latch) noexcept;

    [[nodiscard]] bool msx_cassette_motor_from_ppi(std::uint8_t control,
                                                   std::uint8_t latch) noexcept;
    [[nodiscard]] bool msx_cassette_output_high_from_ppi(std::uint8_t control,
                                                         std::uint8_t latch) noexcept;

    [[nodiscard]] std::uint8_t msx_joystick_port_bits(bool up, bool down, bool left, bool right,
                                                      bool trigger_a,
                                                      bool trigger_b) noexcept;
    [[nodiscard]] std::uint8_t
    msx_psg_port_a_value(const std::array<std::uint8_t, 2>& joystick_ports,
                         std::uint8_t port_b_latch,
                         bool cassette_input_high) noexcept;
    [[nodiscard]] std::uint8_t
    msx_psg_port_a_value(const std::array<std::uint8_t, 2>& joystick_ports,
                         const std::array<msx_mouse_port, 2>& mouse_ports,
                         std::uint8_t port_b_latch,
                         bool cassette_input_high) noexcept;
    [[nodiscard]] bool msx_psg_port_a_input(std::uint8_t mixer) noexcept;
    [[nodiscard]] bool msx_psg_port_b_output(std::uint8_t mixer) noexcept;
    [[nodiscard]] std::uint8_t
    msx_psg_effective_port_b_latch(std::uint8_t mixer, std::uint8_t port_b_latch) noexcept;

    [[nodiscard]] msx_fdc_drive_control
    msx_fdc_decode_drive_control(std::uint8_t value) noexcept;

    [[nodiscard]] std::uint8_t
    msx_ram_mapper_latch_value(std::uint8_t value, std::size_t segment_count) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 4>
    msx_initial_ram_mapper_pages(std::size_t segment_count) noexcept;

} // namespace mnemos::manifests::common
