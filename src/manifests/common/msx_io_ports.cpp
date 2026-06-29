#include "msx_io_ports.hpp"

#include <cstddef>

namespace mnemos::manifests::common {

    namespace {
        [[nodiscard]] std::uint8_t clamp_mouse_delta(std::int16_t value) noexcept {
            if (value < -128) {
                return 0x80U;
            }
            if (value > 127) {
                return 0x7FU;
            }
            return static_cast<std::uint8_t>(static_cast<std::int8_t>(value));
        }
    } // namespace

    void msx_mouse_port::attach_protocol_delta(std::int16_t delta_x, std::int16_t delta_y,
                                               bool left_button, bool right_button) noexcept {
        attached_ = true;
        phase_ = 0U;
        delta_x_ = clamp_mouse_delta(delta_x);
        delta_y_ = clamp_mouse_delta(delta_y);
        left_button_ = left_button;
        right_button_ = right_button;
        rebuild_nibbles();
    }

    void msx_mouse_port::detach() noexcept {
        attached_ = false;
        phase_ = 0U;
        delta_x_ = 0U;
        delta_y_ = 0U;
        nibbles_ = {};
        left_button_ = false;
        right_button_ = false;
    }

    void msx_mouse_port::set_pin8(bool high) noexcept {
        if (pin8_high_ == high) {
            return;
        }
        pin8_high_ = high;
        if (!attached_) {
            return;
        }

        phase_ = static_cast<std::uint8_t>((phase_ + 1U) & 0x03U);
        if (phase_ == 0U) {
            clear_consumed_delta();
        }
    }

    std::uint8_t msx_mouse_port::read_port_bits(std::uint8_t joystick_bits) const noexcept {
        if (!attached_) {
            return static_cast<std::uint8_t>(joystick_bits & 0x3FU);
        }

        std::uint8_t triggers = 0x30U;
        if (left_button_) {
            triggers = static_cast<std::uint8_t>(triggers & ~0x10U);
        }
        if (right_button_) {
            triggers = static_cast<std::uint8_t>(triggers & ~0x20U);
        }
        return static_cast<std::uint8_t>((nibbles_[phase_ & 0x03U] & 0x0FU) | triggers);
    }

    void msx_mouse_port::restore(bool attached, bool pin8_high, std::uint8_t phase,
                                 std::uint8_t delta_x, std::uint8_t delta_y, bool left_button,
                                 bool right_button) noexcept {
        attached_ = attached;
        pin8_high_ = pin8_high;
        phase_ = static_cast<std::uint8_t>(phase & 0x03U);
        delta_x_ = delta_x;
        delta_y_ = delta_y;
        left_button_ = left_button;
        right_button_ = right_button;
        rebuild_nibbles();
    }

    void msx_mouse_port::rebuild_nibbles() noexcept {
        nibbles_[0] = static_cast<std::uint8_t>((delta_x_ >> 4U) & 0x0FU);
        nibbles_[1] = static_cast<std::uint8_t>(delta_x_ & 0x0FU);
        nibbles_[2] = static_cast<std::uint8_t>((delta_y_ >> 4U) & 0x0FU);
        nibbles_[3] = static_cast<std::uint8_t>(delta_y_ & 0x0FU);
    }

    void msx_mouse_port::clear_consumed_delta() noexcept {
        delta_x_ = 0U;
        delta_y_ = 0U;
        rebuild_nibbles();
    }

    bool msx_ppi_port_a_output(std::uint8_t control) noexcept { return (control & 0x10U) == 0U; }

    bool msx_ppi_port_b_output(std::uint8_t control) noexcept { return (control & 0x02U) == 0U; }

    bool msx_ppi_port_c_upper_output(std::uint8_t control) noexcept {
        return (control & 0x08U) == 0U;
    }

    bool msx_ppi_port_c_lower_output(std::uint8_t control) noexcept {
        return (control & 0x01U) == 0U;
    }

    std::uint8_t msx_ppi_port_c_read(std::uint8_t control, std::uint8_t latch) noexcept {
        const auto lower = static_cast<std::uint8_t>(
            msx_ppi_port_c_lower_output(control) ? (latch & 0x0FU) : 0x0FU);
        const auto upper = static_cast<std::uint8_t>(
            msx_ppi_port_c_upper_output(control) ? (latch & 0xF0U) : 0xF0U);
        return static_cast<std::uint8_t>(upper | lower);
    }

    bool msx_cassette_motor_from_ppi(std::uint8_t control, std::uint8_t latch) noexcept {
        return msx_ppi_port_c_upper_output(control) && (latch & 0x10U) == 0U;
    }

    bool msx_cassette_output_high_from_ppi(std::uint8_t control, std::uint8_t latch) noexcept {
        return !msx_ppi_port_c_upper_output(control) || (latch & 0x20U) != 0U;
    }

    std::uint8_t msx_joystick_port_bits(bool up, bool down, bool left, bool right, bool trigger_a,
                                        bool trigger_b) noexcept {
        std::uint8_t bits = 0x3FU;
        if (up) {
            bits = static_cast<std::uint8_t>(bits & ~0x01U);
        }
        if (down) {
            bits = static_cast<std::uint8_t>(bits & ~0x02U);
        }
        if (left) {
            bits = static_cast<std::uint8_t>(bits & ~0x04U);
        }
        if (right) {
            bits = static_cast<std::uint8_t>(bits & ~0x08U);
        }
        if (trigger_a) {
            bits = static_cast<std::uint8_t>(bits & ~0x10U);
        }
        if (trigger_b) {
            bits = static_cast<std::uint8_t>(bits & ~0x20U);
        }
        return bits;
    }

    std::uint8_t msx_psg_port_a_value(const std::array<std::uint8_t, 2>& joystick_ports,
                                      std::uint8_t port_b_latch,
                                      bool cassette_input_high) noexcept {
        const std::array<msx_mouse_port, 2> mouse_ports{};
        return msx_psg_port_a_value(joystick_ports, mouse_ports, port_b_latch,
                                    cassette_input_high);
    }

    std::uint8_t msx_psg_port_a_value(const std::array<std::uint8_t, 2>& joystick_ports,
                                      const std::array<msx_mouse_port, 2>& mouse_ports,
                                      std::uint8_t port_b_latch,
                                      bool cassette_input_high) noexcept {
        const bool select_port2 = (port_b_latch & 0x40U) != 0U;
        const auto port = static_cast<std::size_t>(select_port2 ? 1U : 0U);
        const auto cassette =
            static_cast<std::uint8_t>(cassette_input_high ? 0x80U : std::uint8_t{0U});
        const std::uint8_t trigger_a_enable = select_port2 ? 0x04U : 0x01U;
        const std::uint8_t trigger_b_enable = select_port2 ? 0x08U : 0x02U;

        std::uint8_t joystick = mouse_ports[port].read_port_bits(joystick_ports[port]);
        if ((port_b_latch & trigger_a_enable) == 0U) {
            joystick = static_cast<std::uint8_t>(joystick & ~0x10U);
        }
        if ((port_b_latch & trigger_b_enable) == 0U) {
            joystick = static_cast<std::uint8_t>(joystick & ~0x20U);
        }
        return static_cast<std::uint8_t>(joystick | 0x40U | cassette);
    }

    bool msx_psg_port_a_input(std::uint8_t mixer) noexcept {
        return (mixer & 0x40U) == 0U;
    }

    bool msx_psg_port_b_output(std::uint8_t mixer) noexcept {
        return (mixer & 0x80U) != 0U;
    }

    std::uint8_t msx_psg_effective_port_b_latch(std::uint8_t mixer,
                                                std::uint8_t port_b_latch) noexcept {
        return msx_psg_port_b_output(mixer) ? port_b_latch : 0xFFU;
    }

    msx_fdc_drive_control msx_fdc_decode_drive_control(std::uint8_t value) noexcept {
        std::uint8_t drive = (value & 0x02U) != 0U ? 1U : 0U;
        if ((value & 0x01U) != 0U) {
            drive = 0U;
        }

        const std::uint8_t side = (value & 0x14U) != 0U ? 1U : 0U;
        return {.drive = drive, .side = side};
    }

    std::uint8_t msx_ram_mapper_latch_value(std::uint8_t value,
                                            std::size_t segment_count) noexcept {
        if (segment_count == 0U) {
            return value;
        }

        std::size_t mask = 1U;
        while (mask + 1U < segment_count && mask < 0xFFU) {
            mask = (mask << 1U) | 1U;
        }
        return static_cast<std::uint8_t>(value & static_cast<std::uint8_t>(mask));
    }

    std::array<std::uint8_t, 4> msx_initial_ram_mapper_pages(
        std::size_t segment_count) noexcept {
        return {msx_ram_mapper_latch_value(3U, segment_count),
                msx_ram_mapper_latch_value(2U, segment_count),
                msx_ram_mapper_latch_value(1U, segment_count),
                msx_ram_mapper_latch_value(0U, segment_count)};
    }

} // namespace mnemos::manifests::common
