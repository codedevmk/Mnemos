#include "msx_io_ports.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
    using mnemos::manifests::common::msx_cassette_motor_from_ppi;
    using mnemos::manifests::common::msx_cassette_output_high_from_ppi;
    using mnemos::manifests::common::msx_fdc_decode_drive_control;
    using mnemos::manifests::common::msx_joystick_port_bits;
    using mnemos::manifests::common::msx_mouse_port;
    using mnemos::manifests::common::msx_ppi_port_a_output;
    using mnemos::manifests::common::msx_ppi_port_b_output;
    using mnemos::manifests::common::msx_ppi_port_c_lower_output;
    using mnemos::manifests::common::msx_ppi_port_c_read;
    using mnemos::manifests::common::msx_ppi_port_c_upper_output;
    using mnemos::manifests::common::msx_psg_port_a_input;
    using mnemos::manifests::common::msx_psg_port_a_value;
    using mnemos::manifests::common::msx_psg_port_b_output;
    using mnemos::manifests::common::msx_psg_effective_port_b_latch;
    using mnemos::manifests::common::msx_initial_ram_mapper_pages;
    using mnemos::manifests::common::msx_ram_mapper_latch_value;
} // namespace

TEST_CASE("msx shared PPI helpers decode 8255 port directions",
          "[manifests][common][msx][io]") {
    CHECK(msx_ppi_port_a_output(0x82U));
    CHECK_FALSE(msx_ppi_port_b_output(0x82U));
    CHECK(msx_ppi_port_c_upper_output(0x82U));
    CHECK(msx_ppi_port_c_lower_output(0x82U));
    CHECK(msx_ppi_port_c_read(0x82U, 0x5AU) == 0x5AU);

    CHECK_FALSE(msx_ppi_port_a_output(0x9BU));
    CHECK_FALSE(msx_ppi_port_b_output(0x9BU));
    CHECK_FALSE(msx_ppi_port_c_upper_output(0x9BU));
    CHECK_FALSE(msx_ppi_port_c_lower_output(0x9BU));
    CHECK(msx_ppi_port_c_read(0x9BU, 0x00U) == 0xFFU);
}

TEST_CASE("msx shared PPI helpers derive cassette motor and output latches",
          "[manifests][common][msx][io]") {
    CHECK(msx_cassette_motor_from_ppi(0x82U, 0x20U));
    CHECK_FALSE(msx_cassette_motor_from_ppi(0x82U, 0x30U));
    CHECK(msx_cassette_output_high_from_ppi(0x82U, 0x20U));
    CHECK_FALSE(msx_cassette_output_high_from_ppi(0x82U, 0x10U));

    CHECK_FALSE(msx_cassette_motor_from_ppi(0x9BU, 0x00U));
    CHECK(msx_cassette_output_high_from_ppi(0x9BU, 0x00U));
}

TEST_CASE("msx shared joystick helpers compose active-low PSG port A",
          "[manifests][common][msx][io]") {
    const std::array<std::uint8_t, 2> ports{
        msx_joystick_port_bits(true, false, true, false, true, false),
        msx_joystick_port_bits(false, true, false, true, false, true),
    };

    CHECK(ports[0] == 0x2AU);
    CHECK(ports[1] == 0x15U);
    CHECK(msx_psg_port_a_value(ports, 0x0FU, true) == 0xEAU);
    CHECK(msx_psg_port_a_value(ports, 0x4FU, false) == 0x55U);
    CHECK(msx_psg_port_a_value({0x3FU, 0x3FU}, 0x00U, false) == 0x4FU);
    CHECK(msx_psg_port_a_value({0x3FU, 0x3FU}, 0x40U, false) == 0x4FU);
}

TEST_CASE("msx shared mouse port clocks high-first X/Y nibbles on pin 8",
          "[manifests][common][msx][io][mouse]") {
    msx_mouse_port mouse;
    mouse.attach_protocol_delta(0x12, -0x34, true, false);

    CHECK(mouse.read_port_bits(0x3FU) == 0x21U);
    mouse.set_pin8(true);
    CHECK(mouse.read_port_bits(0x3FU) == 0x22U);
    mouse.set_pin8(false);
    CHECK(mouse.read_port_bits(0x3FU) == 0x2CU);
    mouse.set_pin8(true);
    CHECK(mouse.read_port_bits(0x3FU) == 0x2CU);

    mouse.set_pin8(false);
    CHECK(mouse.read_port_bits(0x3FU) == 0x20U);

    mouse.detach();
    CHECK(mouse.read_port_bits(0x16U) == 0x16U);
}

TEST_CASE("msx shared PSG port A helper selects mouse data per joystick port",
          "[manifests][common][msx][io][mouse]") {
    std::array<std::uint8_t, 2> joystick_ports{0x3FU, 0x3FU};
    std::array<msx_mouse_port, 2> mouse_ports{};
    mouse_ports[0].attach_protocol_delta(0x45, 0x67, false, true);
    mouse_ports[1].attach_protocol_delta(0x23, 0x01, false, false);

    CHECK((msx_psg_port_a_value(joystick_ports, mouse_ports, 0x0FU, false) & 0x3FU) ==
          0x14U);
    mouse_ports[0].set_pin8(true);
    CHECK((msx_psg_port_a_value(joystick_ports, mouse_ports, 0x1FU, false) & 0x3FU) ==
          0x15U);

    CHECK((msx_psg_port_a_value(joystick_ports, mouse_ports, 0x4FU, false) & 0x3FU) ==
          0x32U);
}

TEST_CASE("msx shared PSG helpers decode MSX GPIO direction bits",
          "[manifests][common][msx][io]") {
    CHECK(msx_psg_port_a_input(0xBFU));
    CHECK_FALSE(msx_psg_port_a_input(0xFFU));
    CHECK(msx_psg_port_b_output(0xBFU));
    CHECK_FALSE(msx_psg_port_b_output(0x3FU));
    CHECK(msx_psg_effective_port_b_latch(0xBFU, 0x00U) == 0x00U);
    CHECK(msx_psg_effective_port_b_latch(0x3FU, 0x00U) == 0xFFU);
}

TEST_CASE("msx shared FDC control helper decodes drive and side aliases",
          "[manifests][common][msx][fdc]") {
    CHECK(msx_fdc_decode_drive_control(0x00U).drive == 0U);
    CHECK(msx_fdc_decode_drive_control(0x00U).side == 0U);
    CHECK(msx_fdc_decode_drive_control(0x02U).drive == 1U);
    CHECK(msx_fdc_decode_drive_control(0x03U).drive == 0U);
    CHECK(msx_fdc_decode_drive_control(0x04U).side == 1U);
    CHECK(msx_fdc_decode_drive_control(0x10U).side == 1U);
}

TEST_CASE("msx shared RAM mapper helpers expose BIOS-visible 3-2-1-0 page order",
          "[manifests][common][msx][io]") {
    CHECK(msx_initial_ram_mapper_pages(8U) ==
          std::array<std::uint8_t, 4>{3U, 2U, 1U, 0U});
    CHECK(msx_ram_mapper_latch_value(13U, 8U) == 5U);
    CHECK(msx_ram_mapper_latch_value(31U, 16U) == 15U);
    CHECK(msx_ram_mapper_latch_value(0xF7U, 0U) == 0xF7U);
}
