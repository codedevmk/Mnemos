#include "amiga_system.hpp"

#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    using mnemos::manifests::amiga::amiga_chipset;
    using mnemos::manifests::amiga::amiga_chipset_profile;
    using mnemos::manifests::amiga::amiga_config;
    using mnemos::manifests::amiga::amiga_encode_joystick;
    using mnemos::manifests::amiga::amiga_fast_ram_size_for_config;
    using mnemos::manifests::amiga::amiga_floppy_dd_size;
    using mnemos::manifests::amiga::amiga_floppy_drive_count;
    using mnemos::manifests::amiga::amiga_floppy_heads;
    using mnemos::manifests::amiga::amiga_floppy_sector_size;
    using mnemos::manifests::amiga::amiga_floppy_sectors_per_track;
    using mnemos::manifests::amiga::amiga_floppy_track_count;
    using mnemos::manifests::amiga::amiga_joy_fire;
    using mnemos::manifests::amiga::amiga_joy_middle_fire;
    using mnemos::manifests::amiga::amiga_joy_secondary_fire;
    using mnemos::manifests::amiga::amiga_joy_up;
    using mnemos::manifests::amiga::amiga_keyboard_accept_serial_ack_level;
    using mnemos::manifests::amiga::amiga_keyboard_ack_low_seen;
    using mnemos::manifests::amiga::amiga_keyboard_begin_serial_byte;
    using mnemos::manifests::amiga::amiga_keyboard_dequeue_code;
    using mnemos::manifests::amiga::amiga_keyboard_enqueue_key;
    using mnemos::manifests::amiga::amiga_keyboard_load_state;
    using mnemos::manifests::amiga::amiga_keyboard_press_caps_lock;
    using mnemos::manifests::amiga::amiga_keyboard_queue_capacity;
    using mnemos::manifests::amiga::amiga_keyboard_queue_state;
    using mnemos::manifests::amiga::amiga_keyboard_raw_key_count;
    using mnemos::manifests::amiga::amiga_keyboard_reset_warning_code;
    using mnemos::manifests::amiga::amiga_keyboard_save_state;
    using mnemos::manifests::amiga::amiga_keyboard_sdr;
    using mnemos::manifests::amiga::amiga_keyboard_serial_busy;
    using mnemos::manifests::amiga::amiga_model;
    using mnemos::manifests::amiga::amiga_model_profile;
    using mnemos::manifests::amiga::amiga_mouse_button_mask;
    using mnemos::manifests::amiga::amiga_pack_pot_target;
    using mnemos::manifests::amiga::amiga_pot_axis_value;
    using mnemos::manifests::amiga::amiga_pot_counter_value;
    using mnemos::manifests::amiga::amiga_pot_full_scale_scanlines;
    using mnemos::manifests::amiga::amiga_pot_reset_scanlines;
    using mnemos::manifests::amiga::amiga_sanitize_controller_mask;
    using mnemos::manifests::amiga::amiga_system;
    using mnemos::manifests::amiga::amiga_wrap_mouse_counter;
    using mnemos::manifests::amiga::assemble_amiga;
    using agnus = mnemos::chips::video::agnus;

    constexpr std::uint32_t pal_vblank_end_line = 24U;
    constexpr std::uint64_t pal_vblank_exit_ticks =
        static_cast<std::uint64_t>(agnus::color_clocks_per_line) * pal_vblank_end_line;

    [[nodiscard]] std::vector<std::uint8_t> tiny_kickstart() {
        std::vector<std::uint8_t> rom(amiga_system::kickstart_window_size, 0x00U);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x0000U, 0x0007F000U); // SSP inside chip RAM
        w32(0x0004U, amiga_system::kickstart_base + 0x0008U);
        w16(0x0008U, 0x46FCU); // MOVE.W #$2700,SR
        w16(0x000AU, 0x2700U);
        w16(0x000CU, 0x60FEU); // BRA.S *
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf() {
        std::vector<std::uint8_t> adf(amiga_system::floppy_dd_size, 0x00U);
        for (std::size_t i = 0; i < amiga_system::floppy_sector_size; ++i) {
            adf[i] = static_cast<std::uint8_t>(i);
        }
        return adf;
    }

    void select_df0(amiga_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU); // DDRB: disk control lines are outputs.
        sys.cia_b.write(0x01U, 0x75U); // /MTR=0, /SEL0=0, /SIDE=1, /STEP=1.
    }

    void select_df1(amiga_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU);
        sys.cia_b.write(0x01U, 0x6DU); // /MTR=0, /SEL1=0, /SIDE=1, /STEP=1.
    }

    [[nodiscard]] bool joy_up(std::uint16_t joy) {
        return (((joy >> 9U) ^ (joy >> 8U)) & 0x01U) != 0U;
    }

    [[nodiscard]] bool joy_down(std::uint16_t joy) { return (((joy >> 1U) ^ joy) & 0x01U) != 0U; }

    [[nodiscard]] bool joy_left(std::uint16_t joy) { return (joy & 0x0200U) != 0U; }

    [[nodiscard]] bool joy_right(std::uint16_t joy) { return (joy & 0x0002U) != 0U; }

    [[nodiscard]] std::uint8_t keyboard_sdr(std::uint8_t raw_code) noexcept {
        return amiga_keyboard_sdr(raw_code);
    }

    void write_chip_word(amiga_system& sys, std::uint32_t address, std::uint16_t value) {
        sys.bus.write8(address, static_cast<std::uint8_t>(value >> 8U));
        sys.bus.write8(address + 1U, static_cast<std::uint8_t>(value));
    }

    [[nodiscard]] constexpr std::uint16_t sprite_pos_word(std::uint32_t visible_y,
                                                          std::uint32_t visible_x) noexcept {
        const std::uint32_t beam_y = agnus::display_line_origin + visible_y;
        const std::uint32_t beam_x = agnus::sprite_hstart_origin + visible_x;
        return static_cast<std::uint16_t>(((beam_y & 0xFFU) << 8U) | ((beam_x >> 1U) & 0xFFU));
    }

    [[nodiscard]] constexpr std::uint16_t sprite_ctl_word(std::uint32_t visible_y,
                                                          std::uint32_t visible_x,
                                                          std::uint32_t height) noexcept {
        const std::uint32_t beam_y = agnus::display_line_origin + visible_y;
        const std::uint32_t beam_x = agnus::sprite_hstart_origin + visible_x;
        const std::uint32_t stop_y = beam_y + height;
        return static_cast<std::uint16_t>(
            ((stop_y & 0xFFU) << 8U) | ((beam_x & 0x01U) != 0U ? 0x0001U : 0x0000U) |
            ((stop_y & 0x100U) != 0U ? 0x0002U : 0x0000U) |
            ((beam_y & 0x100U) != 0U ? 0x0004U : 0x0000U));
    }

    void assign_first_zorro2_board(amiga_system& sys, std::uint8_t base_page = 0x20U) {
        REQUIRE(sys.zorro2_autoconfig_pending());
        sys.bus.write8(amiga_system::zorro2_autoconfig_base + 0x4AU,
                       static_cast<std::uint8_t>(base_page & 0x0FU));
        sys.bus.write8(amiga_system::zorro2_autoconfig_base + 0x48U, base_page);
    }

    void write_kickstart_word(amiga_system& sys, std::uint32_t offset, std::uint16_t value) {
        sys.kickstart_rom[offset] = static_cast<std::uint8_t>(value >> 8U);
        sys.kickstart_rom[offset + 1U] = static_cast<std::uint8_t>(value);
    }

    void write_kickstart_long(amiga_system& sys, std::uint32_t offset, std::uint32_t value) {
        sys.kickstart_rom[offset + 0U] = static_cast<std::uint8_t>(value >> 24U);
        sys.kickstart_rom[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
        sys.kickstart_rom[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
        sys.kickstart_rom[offset + 3U] = static_cast<std::uint8_t>(value);
    }

    void append_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value >> 8U));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
        append_be16(out, static_cast<std::uint16_t>(value >> 16U));
        append_be16(out, static_cast<std::uint16_t>(value));
    }

    [[nodiscard]] std::uint16_t read_chip_word(const amiga_system& sys,
                                               std::uint32_t address) noexcept {
        return static_cast<std::uint16_t>((sys.chip_ram[address] << 8U) |
                                          sys.chip_ram[address + 1U]);
    }

    [[nodiscard]] std::uint32_t read_track_long(const std::vector<std::uint8_t>& data,
                                                std::size_t offset) noexcept {
        return (static_cast<std::uint32_t>(data[offset + 0U]) << 24U) |
               (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
               (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
               static_cast<std::uint32_t>(data[offset + 3U]);
    }

    [[nodiscard]] std::uint32_t decode_mfm_odd_even(std::uint32_t odd,
                                                    std::uint32_t even) noexcept {
        constexpr std::uint32_t mask = 0x55555555U;
        return ((odd & mask) << 1U) | (even & mask);
    }

    [[nodiscard]] std::array<std::uint32_t, 2> encode_mfm_odd_even(
        std::uint32_t raw) noexcept {
        constexpr std::uint32_t mask = 0x55555555U;
        return {((raw >> 1U) & mask), (raw & mask)};
    }

    [[nodiscard]] std::uint16_t read_track_word(const std::vector<std::uint8_t>& data,
                                                std::size_t offset) noexcept {
        return static_cast<std::uint16_t>((data[offset + 0U] << 8U) | data[offset + 1U]);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    extended_adf_with_raw_side(std::span<const std::uint8_t> raw_track) {
        constexpr std::size_t track_count = amiga_system::floppy_track_count + 6U;
        std::vector<std::uint8_t> image{};
        image.insert(image.end(), {'U', 'A', 'E', '-', '1', 'A', 'D', 'F'});
        append_be16(image, 0U); // reserved flags
        append_be16(image, static_cast<std::uint16_t>(track_count));

        for (std::size_t track = 0U; track < track_count; ++track) {
            append_be16(image, 0U); // reserved
            if (track == 1U) {
                append_be16(image, 1U); // raw MFM track
                append_be32(image, static_cast<std::uint32_t>(raw_track.size()));
                append_be32(image, static_cast<std::uint32_t>(raw_track.size() * 8U));
            } else if (track < amiga_system::floppy_track_count) {
                append_be16(image, 0U); // normal AmigaDOS track
                append_be32(image,
                            static_cast<std::uint32_t>(amiga_system::floppy_sectors_per_track *
                                                       amiga_system::floppy_sector_size));
                append_be32(image,
                            static_cast<std::uint32_t>(amiga_system::floppy_sectors_per_track *
                                                       amiga_system::floppy_sector_size * 8U));
            } else {
                append_be16(image, 1U); // zero-length overtrack placeholder
                append_be32(image, 0U);
                append_be32(image, 0U);
            }
        }

        for (std::size_t track = 0U; track < amiga_system::floppy_track_count; ++track) {
            if (track == 1U) {
                image.insert(image.end(), raw_track.begin(), raw_track.end());
                continue;
            }
            for (std::size_t byte = 0U;
                 byte < amiga_system::floppy_sectors_per_track * amiga_system::floppy_sector_size;
                 ++byte) {
                image.push_back(static_cast<std::uint8_t>((track + byte) & 0xFFU));
            }
        }
        return image;
    }

    template <std::size_t N>
    [[nodiscard]] std::uint32_t mfm_checksum(const std::array<std::uint32_t, N>& words) noexcept {
        std::uint32_t checksum = 0U;
        for (std::uint32_t word : words) {
            checksum ^= word;
        }
        return checksum & 0x55555555U;
    }

    [[nodiscard]] bool decode_dma_amigados_sector(
        const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint8_t expected_track,
        std::uint8_t& sector,
        std::array<std::uint8_t, amiga_system::floppy_sector_size>& sector_data) noexcept {
        constexpr std::uint16_t sync = 0x4489U;
        constexpr std::size_t header_raw_longs = 5U;
        constexpr std::size_t header_encoded_longs = header_raw_longs * 2U;
        constexpr std::size_t checksum_bytes = 8U;
        constexpr std::size_t data_raw_longs = amiga_system::floppy_sector_size / 4U;
        constexpr std::size_t data_encoded_longs = data_raw_longs * 2U;
        constexpr std::size_t sector_bytes_after_first_sync =
            2U + header_encoded_longs * 4U + checksum_bytes * 2U + data_encoded_longs * 4U;

        if (offset + sector_bytes_after_first_sync > bytes.size() ||
            read_track_word(bytes, offset) != sync) {
            return false;
        }

        std::size_t cursor = offset + 2U;
        std::array<std::uint32_t, header_encoded_longs> header_encoded{};
        for (std::uint32_t& word : header_encoded) {
            word = read_track_long(bytes, cursor);
            cursor += 4U;
        }

        const std::uint32_t stored_header_checksum = decode_mfm_odd_even(
            read_track_long(bytes, cursor), read_track_long(bytes, cursor + 4U));
        cursor += checksum_bytes;
        if (stored_header_checksum != mfm_checksum(header_encoded)) {
            return false;
        }

        const std::uint32_t stored_data_checksum = decode_mfm_odd_even(
            read_track_long(bytes, cursor), read_track_long(bytes, cursor + 4U));
        cursor += checksum_bytes;

        const std::uint32_t info = decode_mfm_odd_even(header_encoded[0], header_encoded[1]);
        if ((info & 0xFF000000U) != 0xFF000000U ||
            static_cast<std::uint8_t>(info >> 16U) != expected_track) {
            return false;
        }
        sector = static_cast<std::uint8_t>(info >> 8U);
        const auto sectors_remaining = static_cast<std::uint8_t>(info);
        if (sector >= amiga_system::floppy_sectors_per_track ||
            sectors_remaining !=
                static_cast<std::uint8_t>(amiga_system::floppy_sectors_per_track - sector)) {
            return false;
        }

        std::array<std::uint32_t, data_encoded_longs> data_encoded{};
        for (std::uint32_t& word : data_encoded) {
            word = read_track_long(bytes, cursor);
            cursor += 4U;
        }
        if (stored_data_checksum != mfm_checksum(data_encoded)) {
            return false;
        }

        for (std::size_t i = 0U; i < data_raw_longs; ++i) {
            const std::uint32_t raw =
                decode_mfm_odd_even(data_encoded[i], data_encoded[i + data_raw_longs]);
            const std::size_t dst = i * 4U;
            sector_data[dst + 0U] = static_cast<std::uint8_t>(raw >> 24U);
            sector_data[dst + 1U] = static_cast<std::uint8_t>(raw >> 16U);
            sector_data[dst + 2U] = static_cast<std::uint8_t>(raw >> 8U);
            sector_data[dst + 3U] = static_cast<std::uint8_t>(raw);
        }
        return true;
    }

    void program_one_plane_display(amiga_system& sys) {
        sys.write_custom_word(0x180U, 0x000FU); // COLOR00 = blue backdrop
        sys.write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground
        sys.write_custom_word(0x100U, 0x1000U); // BPU = 1
        sys.write_custom_word(0x08EU, 0x2C81U);
        sys.write_custom_word(0x090U, 0xF4C1U);
        sys.write_custom_word(0x092U, 0x0038U);
        sys.write_custom_word(0x094U, 0x00D0U);
        sys.write_custom_word(0x0E0U, 0x0000U); // BPL1PTH
        sys.write_custom_word(0x0E2U, 0x0000U); // BPL1PTL
        sys.write_custom_word(0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                                 agnus::dmacon_dmaen |
                                                                 agnus::dmacon_bplen));
    }

    void run_frame(mnemos::runtime::scheduler& scheduler) { scheduler.run_frame(); }

    void run_scanlines(amiga_system& sys, std::uint32_t lines) {
        sys.agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * lines);
    }

    void reset_floppy_stream_phase(amiga_system::floppy_drive_state& drive, std::size_t offset = 0U,
                                   std::uint8_t bit_offset = 0U) {
        mnemos::manifests::amiga::amiga_reset_floppy_stream_phase(drive, offset, bit_offset);
    }

    [[nodiscard]] std::uint8_t next_expected_weak_bit(std::uint16_t& state) noexcept {
        if (state == 0U) {
            state = 0xACE1U;
        }
        const bool feedback = (state & 0x0001U) != 0U;
        state = static_cast<std::uint16_t>(state >> 1U);
        if (feedback) {
            state = static_cast<std::uint16_t>(state ^ 0xB400U);
        }
        if (state == 0U) {
            state = 0xACE1U;
        }
        return static_cast<std::uint8_t>(state & 0x0001U);
    }

    [[nodiscard]] std::uint8_t next_expected_weak_byte(std::uint16_t& state) noexcept {
        std::uint8_t value = 0U;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            value = static_cast<std::uint8_t>((value << 1U) | next_expected_weak_bit(state));
        }
        return value;
    }

    void run_blitter_to_idle(amiga_system& sys) {
        for (std::uint32_t cycle = 0U; cycle < 1'000'000U; ++cycle) {
            if ((sys.read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U) {
                return;
            }
            sys.agnus.tick(1U);
        }
        FAIL("Amiga500 blitter did not retire within the bounded test window");
    }

    void acknowledge_keyboard(amiga_system& sys) {
        sys.cia_a.write(0x04U, 0x01U); // Timer A latch = 1 for a short SP pulse.
        sys.cia_a.write(0x05U, 0x00U);
        sys.cia_a.write(0x0EU, 0x41U); // CRA: START | SPMODE output.
        sys.cia_a.write(0x0CU, 0x00U); // Drive KDAT/SP low.
        sys.cia_a.tick(12U);
        sys.cia_a.write(0x0EU, 0x00U); // Release KDAT/SP high.
        sys.service_keyboard_queue();
    }
} // namespace

TEST_CASE("amiga memory size constants share a binary size vocabulary",
          "[manifests][amiga][memory]") {
    CHECK(amiga_system::size_256k == 256U * 1024U);
    CHECK(amiga_system::size_512k == amiga_system::size_256k * 2U);
    CHECK(amiga_system::size_1m == amiga_system::size_512k * 2U);
    CHECK(amiga_system::size_2m == amiga_system::size_1m * 2U);
    CHECK(amiga_system::size_4m == amiga_system::size_2m * 2U);
    CHECK(amiga_system::size_8m == amiga_system::size_4m * 2U);

    CHECK(amiga_system::chip_ram_size == amiga_system::size_512k);
    CHECK(amiga_system::chip_ram_size_1m == amiga_system::size_1m);
    CHECK(amiga_system::fast_ram_size_512k == amiga_system::size_512k);
    CHECK(amiga_system::fast_ram_size_1m == amiga_system::size_1m);
    CHECK(amiga_system::fast_ram_size_2m == amiga_system::size_2m);
    CHECK(amiga_system::fast_ram_size_4m == amiga_system::size_4m);
    CHECK(amiga_system::fast_ram_max_size == amiga_system::size_8m);
    CHECK(amiga_system::kickstart_window_size == amiga_system::size_512k);
}

TEST_CASE("amiga model descriptors capture base chipset and expansion policy",
          "[manifests][amiga][models]") {
    const auto& a1000 = amiga_model_profile(amiga_model::amiga1000);
    CHECK(a1000.chipset == amiga_chipset::ocs);
    CHECK(a1000.chip_ram_size == amiga_system::size_256k);
    CHECK_FALSE(a1000.zorro2_expansion_bus);
    CHECK_FALSE(a1000.fast_ram_configurable);

    const auto& a500 = amiga_model_profile(amiga_model::amiga500);
    CHECK(a500.chipset == amiga_chipset::ocs);
    CHECK(a500.chip_ram_size == amiga_system::chip_ram_size);
    CHECK_FALSE(a500.zorro2_expansion_bus);
    CHECK_FALSE(a500.fast_ram_configurable);

    const auto& a500plus = amiga_model_profile(amiga_model::amiga500_plus);
    CHECK(a500plus.chipset == amiga_chipset::ecs_1m);
    CHECK(a500plus.chip_ram_size == amiga_system::chip_ram_size_1m);
    CHECK_FALSE(a500plus.fast_ram_configurable);

    const auto& a600 = amiga_model_profile(amiga_model::amiga600);
    CHECK(a600.chipset == amiga_chipset::ecs_1m);
    CHECK(a600.chip_ram_size == amiga_system::chip_ram_size_1m);
    CHECK_FALSE(a600.fast_ram_configurable);

    const auto& a2000 = amiga_model_profile(amiga_model::amiga2000);
    CHECK(a2000.chipset == amiga_chipset::ocs);
    CHECK(a2000.chip_ram_size == amiga_system::chip_ram_size);
    CHECK(a2000.zorro2_expansion_bus);
    CHECK(a2000.fast_ram_configurable);

    const auto& a2000_ecs = amiga_model_profile(amiga_model::amiga2000_ecs_1m);
    CHECK(a2000_ecs.chipset == amiga_chipset::ecs_1m);
    CHECK(a2000_ecs.chip_ram_size == amiga_system::chip_ram_size_1m);
    CHECK(a2000_ecs.zorro2_expansion_bus);
    CHECK(a2000_ecs.fast_ram_configurable);
}

TEST_CASE("amiga chipset descriptors own Copper address width policy",
          "[manifests][amiga][chipsets]") {
    CHECK(amiga_chipset_profile(amiga_chipset::ocs).copper_address_mask ==
          agnus::ocs_copper_address_mask);
    CHECK(amiga_chipset_profile(amiga_chipset::ecs_1m).copper_address_mask ==
          agnus::ecs_1m_copper_address_mask);
}

TEST_CASE("amiga model descriptors gate configurable Fast RAM",
          "[manifests][amiga][models][memory]") {
    const amiga_config a1000_fast{.model = amiga_model::amiga1000,
                                  .fast_ram_size = amiga_system::fast_ram_size_2m};
    CHECK(amiga_fast_ram_size_for_config(a1000_fast, amiga_system::fast_ram_max_size) == 0U);

    const amiga_config a500_fast{.model = amiga_model::amiga500,
                                 .fast_ram_size = amiga_system::fast_ram_size_2m};
    CHECK(amiga_fast_ram_size_for_config(a500_fast, amiga_system::fast_ram_max_size) == 0U);

    const amiga_config a2000_fast{.model = amiga_model::amiga2000,
                                  .fast_ram_size = amiga_system::fast_ram_size_2m};
    CHECK(amiga_fast_ram_size_for_config(a2000_fast, amiga_system::fast_ram_max_size) ==
          amiga_system::fast_ram_size_2m);

    const amiga_config a2000_oversized{.model = amiga_model::amiga2000_ecs_1m,
                                       .fast_ram_size = amiga_system::fast_ram_max_size + 512U};
    CHECK(amiga_fast_ram_size_for_config(a2000_oversized, amiga_system::fast_ram_max_size) ==
          amiga_system::fast_ram_max_size);
}

TEST_CASE("amiga floppy drive profile preserves public DD geometry", "[manifests][amiga][drives]") {
    CHECK(amiga_system::floppy_heads == amiga_floppy_heads);
    CHECK(amiga_system::floppy_track_count == amiga_floppy_track_count);
    CHECK(amiga_system::floppy_sectors_per_track == amiga_floppy_sectors_per_track);
    CHECK(amiga_system::floppy_sector_size == amiga_floppy_sector_size);
    CHECK(amiga_system::floppy_dd_size == amiga_floppy_dd_size);
    CHECK(amiga_system::floppy_drive_count == amiga_floppy_drive_count);
}

TEST_CASE("amiga floppy mounts UAE-1ADF extended images with raw MFM tracks",
          "[manifests][amiga500][disk]") {
    const std::array<std::uint8_t, 8> raw_track{
        0x44U, 0x89U, 0xAAU, 0x55U, 0x10U, 0x21U, 0x32U, 0x43U};
    const std::vector<std::uint8_t> extended = extended_adf_with_raw_side(raw_track);
    REQUIRE(amiga_system::supported_floppy_image(extended));

    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(extended));
    CHECK(sys->floppy_loaded());
    CHECK(sys->floppy_size() == amiga_system::floppy_dd_size);
    CHECK(sys->floppy_drives[0U].image[0U] == 0U);
    CHECK(sys->floppy_drives[0U].image[amiga_system::floppy_sectors_per_track *
                                      amiga_system::floppy_sector_size * 2U] == 2U);
    REQUIRE(sys->floppy_drives[0U].raw_track_cache[1U].size() == raw_track.size());
    CHECK(std::equal(raw_track.begin(), raw_track.end(),
                     sys->floppy_drives[0U].raw_track_cache[1U].begin()));

    select_df0(*sys);
    sys->cia_b.write(0x01U, 0x71U); // Keep DF0 selected and motor on, select side 1.
    CHECK(sys->floppy_side() == 1U);
    CHECK(sys->floppy_drives[0U].track_stream ==
          std::vector<std::uint8_t>(raw_track.begin(), raw_track.end()));
}

TEST_CASE("amiga input helpers preserve public controller semantics",
          "[manifests][amiga][devices]") {
    CHECK(amiga_system::joy_up == amiga_joy_up);
    CHECK(amiga_system::joy_fire == amiga_joy_fire);
    CHECK(amiga_system::joy_secondary_fire == amiga_joy_secondary_fire);
    CHECK(amiga_system::joy_middle_fire == amiga_joy_middle_fire);
    CHECK(amiga_sanitize_controller_mask(0xFFU) == 0x7FU);
    CHECK(amiga_encode_joystick(amiga_system::joy_up) == 0x0100U);
    CHECK(amiga_encode_joystick(amiga_system::joy_right) == 0x0003U);
    CHECK(amiga_wrap_mouse_counter(0xFEU, 3) == 0x01U);
    CHECK(amiga_wrap_mouse_counter(0x01U, -3) == 0xFEU);
    CHECK(amiga_mouse_button_mask(true, true, true) ==
          (amiga_system::joy_fire | amiga_system::joy_secondary_fire |
           amiga_system::joy_middle_fire));
    CHECK(amiga_pack_pot_target(3U, 5U) == 0x0503U);
    CHECK(amiga_pot_axis_value(0xFFU, amiga_pot_reset_scanlines) == 0U);
    CHECK(amiga_pot_axis_value(0xFFU, amiga_pot_reset_scanlines + amiga_pot_full_scale_scanlines +
                                          1U) == 0xFFU);
    CHECK(amiga_pot_counter_value(0x0503U, amiga_pot_reset_scanlines + 2U) == 0x0202U);
}

TEST_CASE("amiga500 boots through the Kickstart reset overlay", "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    const auto regs = sys->cpu.cpu_registers();
    CHECK(regs.a[7] == 0x0007F000U);
    CHECK(regs.pc == amiga_system::kickstart_base + 0x0008U);
    CHECK(sys->kickstart_overlay_active());

    // Writes under the overlay still land in chip RAM, but reads see Kickstart
    // until CIA-A PA0 is driven high.
    sys->bus.write8(0x000000U, 0x42U);
    CHECK(sys->bus.read8(0x000000U) == 0x00U); // ROM vector high byte

    // Kickstart clears OVL by making CIA-A PA0 an output and driving it low.
    sys->bus.write8(0x00BFE201U, 0x03U); // CIA-A DDRA bits 0/1 output
    sys->bus.write8(0x00BFE001U, 0x02U); // CIA-A PA0 low = overlay off
    CHECK_FALSE(sys->kickstart_overlay_active());
    CHECK(sys->bus.read8(0x000000U) == 0x42U);

    sys->bus.write8(0x00BFE001U, 0x03U); // CIA-A PA0 high = overlay on
    CHECK(sys->kickstart_overlay_active());
    CHECK(sys->bus.read8(0x000000U) == 0x00U);
}

TEST_CASE("amiga500 CPU RESET pulse warm-resets the board and reasserts the Kickstart overlay",
          "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x00BFE201U, 0x03U);
    sys->bus.write8(0x00BFE001U, 0x02U);
    REQUIRE_FALSE(sys->kickstart_overlay_active());

    sys->bus.write8(0x000004U, 0x00U);
    sys->bus.write8(0x000005U, 0x00U);
    sys->bus.write8(0x000006U, 0x06U);
    sys->bus.write8(0x000007U, 0x76U);
    CHECK(sys->bus.read8(0x000007U) == 0x76U);

    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_vertb));
    CHECK(sys->read_custom_word(0x01CU) != 0U);
    sys->write_custom_word(0x080U, 0x0001U);
    sys->write_custom_word(0x082U, 0x2340U);
    sys->write_custom_word(0x084U, 0x0002U);
    sys->write_custom_word(0x086U, 0x4680U);
    sys->write_custom_word(
        0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                           agnus::dmacon_copen | agnus::dmacon_dsken));
    sys->write_custom_word(0x024U, 0x8004U);
    sys->set_pot_position(0U, 4U, 8U);
    sys->write_custom_word(0x034U, 0x0001U);
    sys->frame_index = 7U;
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    REQUIRE(sys->floppy_loaded());
    REQUIRE(sys->selected_floppy_drive() == 0U);
    CHECK((sys->read_custom_word(0x002U) &
           (agnus::dmacon_dmaen | agnus::dmacon_copen | agnus::dmacon_dsken)) != 0U);

    sys->kickstart_rom[0x0004U] = 0x00U;
    sys->kickstart_rom[0x0005U] = 0xF8U;
    sys->kickstart_rom[0x0006U] = 0x12U;
    sys->kickstart_rom[0x0007U] = 0x34U;
    write_kickstart_word(*sys, 0x0008U, 0x4E70U); // RESET
    write_kickstart_word(*sys, 0x000AU, 0x4E71U); // NOP after RESET
    auto regs = sys->cpu.cpu_registers();
    regs.pc = amiga_system::kickstart_base + 0x0008U;
    regs.sr = static_cast<std::uint16_t>(mnemos::chips::cpu::m68000::sr_s |
                                         mnemos::chips::cpu::m68000::sr_ipm);
    sys->cpu.set_registers(regs);

    const int cycles = sys->cpu.step_instruction();
    CHECK(cycles == 132);
    CHECK(sys->kickstart_overlay_active());
    CHECK(sys->read_custom_word(0x01CU) == 0U);
    CHECK(sys->read_custom_word(0x002U) == 0U);
    CHECK(sys->read_custom_word(0x024U) == 0U);
    CHECK(sys->read_custom_word(0x012U) == 0xFFFFU);
    CHECK(sys->cop1lc == 0U);
    CHECK(sys->cop2lc == 0U);
    CHECK(sys->frame_index == 0U);
    CHECK(sys->floppy_loaded());
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK_FALSE(sys->floppy_motor_on);
    CHECK(sys->bus.read8(0x000004U) == 0x00U);
    CHECK(sys->bus.read8(0x000005U) == 0xF8U);
    CHECK(sys->bus.read8(0x000006U) == 0x12U);
    CHECK(sys->bus.read8(0x000007U) == 0x34U);

    regs = sys->cpu.cpu_registers();
    CHECK(regs.pc == amiga_system::kickstart_base + 0x000AU);
}

TEST_CASE("amiga500 joystick registers and CIA fire lines expose gamepad input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_joystick(1U, static_cast<std::uint8_t>(amiga_system::joy_up | amiga_system::joy_left |
                                                    amiga_system::joy_fire |
                                                    amiga_system::joy_secondary_fire));
    const std::uint16_t joy1 = sys->read_custom_word(0x00CU);
    CHECK(joy_up(joy1));
    CHECK_FALSE(joy_down(joy1));
    CHECK(joy_left(joy1));
    CHECK_FALSE(joy_right(joy1));
    CHECK((sys->cia_a.read(0x00U) & 0x80U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x4000U) == 0U);

    sys->set_joystick(0U,
                      static_cast<std::uint8_t>(amiga_system::joy_down | amiga_system::joy_right |
                                                amiga_system::joy_fire));
    const std::uint16_t joy0 = sys->read_custom_word(0x00AU);
    CHECK_FALSE(joy_up(joy0));
    CHECK(joy_down(joy0));
    CHECK_FALSE(joy_left(joy0));
    CHECK(joy_right(joy0));
    CHECK((sys->cia_a.read(0x00U) & 0x40U) == 0U);
}

TEST_CASE("amiga500 mouse counters and buttons expose controller port input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_mouse(0U, 3, -2, true, true, true);
    CHECK(sys->read_custom_word(0x00AU) == 0xFE03U);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) == 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0400U) == 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0100U) == 0U);

    sys->set_mouse(0U, -4, 5, false, false, false);
    CHECK(sys->read_custom_word(0x00AU) == 0x03FFU);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0400U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0100U) != 0U);
}

TEST_CASE("amiga500 POTGO starts RC-calibrated pot counters by raster line",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_pot_position(0U, 3U, 5U);
    sys->set_pot_position(1U, 10U, 2U);
    sys->write_custom_word(0x034U, 0x0001U); // POTGO START.

    CHECK(sys->read_custom_word(0x012U) == 0x0000U);
    CHECK(sys->read_custom_word(0x014U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 7U);

    CHECK(sys->read_custom_word(0x012U) == 0x0000U);
    CHECK(sys->read_custom_word(0x014U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 2U);

    CHECK(sys->read_custom_word(0x012U) == 0x0202U);
    CHECK(sys->read_custom_word(0x014U) == 0x0202U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 2U);

    CHECK(sys->read_custom_word(0x012U) == 0x0403U);
    CHECK(sys->read_custom_word(0x014U) == 0x0204U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 8U);

    CHECK(sys->read_custom_word(0x012U) == 0x0503U);
    CHECK(sys->read_custom_word(0x014U) == 0x020AU);

    sys->set_pot_position(0U, 0xFFU, 0xFFU);
    sys->write_custom_word(0x034U, 0x0001U); // POTGO START.
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 7U);
    CHECK(sys->read_custom_word(0x012U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    (agnus::scanlines_ntsc - 7U));

    CHECK(sys->read_custom_word(0x012U) == 0xFFFFU);
}

TEST_CASE("amiga500 keyboard events arrive through CIA-A serial input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->cia_a.write(0x0DU, 0x88U);                  // Enable CIA serial interrupt mask.
    REQUIRE(sys->enqueue_keyboard_key(0x44U, true)); // Return down.

    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x44U));
    sys->cia_a.tick(1U);
    CHECK(sys->cia_a.irq_asserted());
    CHECK((sys->cia_a.read(0x0DU) & 0x08U) != 0U);
}

TEST_CASE("amiga keyboard helpers preserve raw queue semantics",
          "[manifests][amiga][devices][keyboard]") {
    CHECK(amiga_system::keyboard_raw_key_count == amiga_keyboard_raw_key_count);
    CHECK(amiga_system::keyboard_queue_capacity == amiga_keyboard_queue_capacity);
    CHECK(amiga_system::keyboard_reset_warning_code == amiga_keyboard_reset_warning_code);
    CHECK(amiga_keyboard_sdr(0x44U) == 0x77U);
    CHECK(amiga_keyboard_sdr(0xCCU) == 0x66U);

    amiga_keyboard_queue_state keyboard{};
    REQUIRE(amiga_keyboard_enqueue_key(keyboard, 0xA0U, true));
    CHECK(keyboard.key_down[0x20U]);
    CHECK_FALSE(amiga_keyboard_enqueue_key(keyboard, 0x20U, true));

    std::uint8_t code = 0U;
    REQUIRE(amiga_keyboard_dequeue_code(keyboard, code));
    CHECK(code == 0x20U);
    REQUIRE(amiga_keyboard_enqueue_key(keyboard, 0x20U, false));
    REQUIRE(amiga_keyboard_dequeue_code(keyboard, code));
    CHECK(code == 0xA0U);

    REQUIRE(amiga_keyboard_press_caps_lock(keyboard));
    CHECK(keyboard.caps_lock_led);
    REQUIRE(amiga_keyboard_dequeue_code(keyboard, code));
    CHECK(code == 0x62U);

    amiga_keyboard_begin_serial_byte(keyboard);
    CHECK(amiga_keyboard_serial_busy(keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(keyboard));
    amiga_keyboard_accept_serial_ack_level(keyboard, true);
    CHECK(amiga_keyboard_serial_busy(keyboard));
    amiga_keyboard_accept_serial_ack_level(keyboard, false);
    CHECK(amiga_keyboard_ack_low_seen(keyboard));
    amiga_keyboard_accept_serial_ack_level(keyboard, true);
    CHECK_FALSE(amiga_keyboard_serial_busy(keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(keyboard));

    REQUIRE(amiga_keyboard_enqueue_key(keyboard, 0x22U, true));
    amiga_keyboard_begin_serial_byte(keyboard);
    amiga_keyboard_accept_serial_ack_level(keyboard, false);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    amiga_keyboard_save_state(keyboard, writer);

    amiga_keyboard_queue_state restored{};
    mnemos::chips::state_reader reader(blob);
    amiga_keyboard_load_state(restored, reader);
    REQUIRE(reader.ok());
    CHECK(restored.count == 1U);
    CHECK(restored.queue[restored.head] == 0x22U);
    CHECK(restored.key_down[0x22U]);
    CHECK(amiga_keyboard_serial_busy(restored));
    CHECK(amiga_keyboard_ack_low_seen(restored));
}

TEST_CASE("amiga500 keyboard queue waits for CIA serial acknowledgement",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x4CU, true));  // Cursor up down.
    REQUIRE(sys->enqueue_keyboard_key(0x4CU, false)); // Cursor up release.
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x4CU));

    CHECK((sys->cia_a.read(0x0DU) & 0x08U) != 0U);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x4CU));

    acknowledge_keyboard(*sys);
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xCCU));
}

TEST_CASE("amiga500 keyboard acknowledgement transmits the next queued byte",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_control_code(amiga_system::keyboard_powerup_stream_start_code));
    REQUIRE(sys->enqueue_keyboard_control_code(amiga_system::keyboard_powerup_stream_end_code));
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(sys->cia_a.read(0x0CU) ==
          keyboard_sdr(amiga_system::keyboard_powerup_stream_start_code));

    sys->write_cia_a_sp(false);
    sys->write_cia_a_sp(true);

    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) ==
          keyboard_sdr(amiga_system::keyboard_powerup_stream_end_code));
}

TEST_CASE("amiga500 keyboard matrix filters duplicate raw-key edges",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x20U, true)); // A down.
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));
    CHECK_FALSE(sys->enqueue_keyboard_key(0x20U, true));
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK_FALSE(sys->enqueue_keyboard_key(0x21U, false));
    CHECK(sys->keyboard_pending_count() == 0U);

    acknowledge_keyboard(*sys);
    REQUIRE(sys->enqueue_keyboard_key(0x20U, false)); // A release.
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xA0U));

    acknowledge_keyboard(*sys);
    CHECK_FALSE(sys->enqueue_keyboard_key(0x20U, false));
    CHECK(sys->keyboard_pending_count() == 0U);
}

TEST_CASE("amiga500 keyboard matrix ignores rejected queue-full edges",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x20U, true)); // In flight.
    for (std::uint8_t key = 0x21U; key < 0x21U + amiga_system::keyboard_queue_capacity; ++key) {
        REQUIRE(sys->enqueue_keyboard_key(key, true));
    }
    REQUIRE(sys->keyboard_pending_count() == amiga_system::keyboard_queue_capacity);

    CHECK_FALSE(sys->enqueue_keyboard_key(0x40U, true));
    acknowledge_keyboard(*sys);
    REQUIRE(sys->enqueue_keyboard_key(0x40U, true));
}

TEST_CASE("amiga500 keyboard matrix survives system save state",
          "[manifests][amiga500][input][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x20U, true)); // A down.
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    acknowledge_keyboard(*sys);
    REQUIRE(sys->enqueue_keyboard_key(0x20U, false));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xA0U));

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK_FALSE(sys->enqueue_keyboard_key(0x20U, true));

    acknowledge_keyboard(*sys);
    REQUIRE(sys->enqueue_keyboard_key(0x20U, false));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xA0U));
}

TEST_CASE("amiga500 keyboard save_state preserves in-flight serial byte",
          "[manifests][amiga500][input][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x20U, true)); // A down, transmitted immediately.
    REQUIRE(sys->enqueue_keyboard_key(0x21U, true)); // S down, queued behind A.
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    acknowledge_keyboard(*sys);
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x21U));

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    sys->write_cia_a_sp(true);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(sys->keyboard));

    sys->write_cia_a_sp(false);
    sys->write_cia_a_sp(true);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x21U));
}

TEST_CASE("amiga500 keyboard save_state preserves partial serial acknowledgement",
          "[manifests][amiga500][input][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x20U, true)); // A down, transmitted immediately.
    REQUIRE(sys->enqueue_keyboard_key(0x21U, true)); // S down, queued behind A.
    sys->write_cia_a_sp(false);
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->write_cia_a_sp(true);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x21U));

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK(amiga_keyboard_ack_low_seen(sys->keyboard));

    sys->write_cia_a_sp(true);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(amiga_keyboard_serial_busy(sys->keyboard));
    CHECK_FALSE(amiga_keyboard_ack_low_seen(sys->keyboard));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x21U));
}

TEST_CASE("amiga500 save_state restores CIA chip register state",
          "[manifests][amiga500][cia][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->cia_a.write(0x04U, 0x34U);
    sys->cia_a.write(0x05U, 0x12U);
    sys->cia_a.write(0x0DU, 0x81U); // Enable timer A interrupt mask.
    sys->cia_b.write(0x03U, 0xF0U);
    sys->cia_b.write(0x01U, 0xA5U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->cia_a.write(0x04U, 0x78U);
    sys->cia_a.write(0x05U, 0x56U);
    sys->cia_a.write(0x0DU, 0x01U); // Clear timer A interrupt mask.
    sys->cia_b.write(0x03U, 0x0FU);
    sys->cia_b.write(0x01U, 0x5AU);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->cia_a.read(0x04U) == 0x34U);
    CHECK(sys->cia_a.read(0x05U) == 0x12U);
    CHECK(sys->cia_a.read(0x0EU) == 0x00U);
    CHECK(sys->cia_b.read(0x03U) == 0xF0U);
    CHECK(sys->cia_b.port_b_output() == 0xA0U);
}

TEST_CASE("amiga500 keyboard control codes and caps-lock LED use raw serial codes",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_control_code(amiga_system::keyboard_reset_warning_code));
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(amiga_system::keyboard_reset_warning_code));

    acknowledge_keyboard(*sys);
    REQUIRE(sys->press_caps_lock());
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x62U));

    acknowledge_keyboard(*sys);
    REQUIRE(sys->press_caps_lock());
    CHECK_FALSE(sys->keyboard_caps_lock_led_on());
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xE2U));
}

TEST_CASE("amiga500 custom registers drive OCS bitplane rendering", "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);

    mnemos::runtime::scheduler scheduler({{&sys->agnus, 2U},
                                          {&sys->cpu, 1U},
                                          {&sys->paula, 2U},
                                          {&sys->cia_a, 10U},
                                          {&sys->cia_b, 10U}},
                                         &sys->agnus);
    run_frame(scheduler);

    const auto frame = sys->agnus.framebuffer();
    REQUIRE(frame.pixels != nullptr);
    CHECK(frame.width == agnus::visible_width);
    CHECK(frame.height == agnus::visible_height_pal);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x000000FFU);
    CHECK(sys->denise.read_color(1U) == 0x0F00U);
}

TEST_CASE("amiga500 BPLCON1 custom register delays the Agnus bitplane serializer",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x102U, 0x0011U); // Delay both playfield nibbles by one pixel.

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x000000FFU);
    CHECK(frame.pixels[1] == 0x00FF0000U);
}

TEST_CASE("amiga500 custom byte writes preserve the opposite register lane",
          "[manifests][amiga500][custom]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x100U, 0x1200U);
    sys->write_custom_byte(amiga_system::custom_base + 0x101U, 0x34U);
    CHECK(sys->read_custom_word(0x100U) == 0x1234U);
    sys->write_custom_byte(amiga_system::custom_base + 0x100U, 0x56U);
    CHECK(sys->read_custom_word(0x100U) == 0x5634U);

    sys->write_custom_word(0x084U, 0x0000U);
    sys->write_custom_byte(amiga_system::custom_base + 0x086U, 0x24U);
    sys->write_custom_byte(amiga_system::custom_base + 0x087U, 0x08U);
    CHECK(sys->cop2lc == 0x002408U);
    CHECK(sys->agnus.cop2lc() == 0x002408U);
}

TEST_CASE("amiga500 Copper location pointers are clipped to OCS chip address width",
          "[manifests][amiga500][custom]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x080U, 0x001FU); // COP1LCH
    sys->write_custom_word(0x082U, 0x1235U); // COP1LCL, low bit ignored.
    sys->write_custom_word(0x084U, 0x0019U); // COP2LCH
    sys->write_custom_word(0x086U, 0x5679U); // COP2LCL, low bit ignored.

    CHECK(sys->cop1lc == 0x00071234U);
    CHECK(sys->cop2lc == 0x00015678U);
    CHECK(sys->agnus.cop1lc() == 0x00071234U);
    CHECK(sys->agnus.cop2lc() == 0x00015678U);
}

TEST_CASE("amiga2000 Copper location pointers are clipped to OCS chip address width",
          "[manifests][amiga500][custom][amiga2000]") {
    const amiga_config config{.model = amiga_model::amiga2000};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);
    CHECK(sys->zorro2_boards.empty());
    CHECK_FALSE(sys->zorro2_autoconfig_pending());

    sys->write_custom_word(0x080U, 0x001FU); // COP1LCH
    sys->write_custom_word(0x082U, 0x1235U); // COP1LCL, low bit ignored.
    sys->write_custom_word(0x084U, 0x0019U); // COP2LCH
    sys->write_custom_word(0x086U, 0x5679U); // COP2LCL, low bit ignored.

    CHECK(sys->cop1lc == 0x00071234U);
    CHECK(sys->cop2lc == 0x00015678U);
    CHECK(sys->agnus.cop1lc() == 0x00071234U);
    CHECK(sys->agnus.cop2lc() == 0x00015678U);
}

TEST_CASE("amiga2000 ECS upgrade Copper location pointers use the ECS 1 MiB address width",
          "[manifests][amiga500][custom][amiga2000]") {
    const amiga_config config{.model = amiga_model::amiga2000_ecs_1m};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x080U, 0x001FU); // COP1LCH
    sys->write_custom_word(0x082U, 0x1235U); // COP1LCL, low bit ignored.
    sys->write_custom_word(0x084U, 0x0019U); // COP2LCH
    sys->write_custom_word(0x086U, 0x5679U); // COP2LCL, low bit ignored.

    CHECK(sys->cop1lc == 0x000F1234U);
    CHECK(sys->cop2lc == 0x00095678U);
    CHECK(sys->agnus.cop1lc() == 0x000F1234U);
    CHECK(sys->agnus.cop2lc() == 0x00095678U);
}

TEST_CASE("amiga2000 Fast RAM maps as CPU-visible expansion memory",
          "[manifests][amiga500][memory][amiga2000]") {
    const amiga_config config{.model = amiga_model::amiga2000,
                              .fast_ram_size = amiga_system::fast_ram_size_2m};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    REQUIRE(sys->chip_ram.size() == amiga_system::chip_ram_size);
    REQUIRE(sys->fast_ram.size() == amiga_system::fast_ram_size_2m);
    REQUIRE(sys->zorro2_boards.size() == 1U);
    CHECK(sys->zorro2_autoconfig_pending());
    CHECK(sys->bus.read8(amiga_system::fast_ram_base) == 0xFFU);
    CHECK(sys->bus.read8(amiga_system::zorro2_autoconfig_base + 0x00U) == 0xEEU);
    CHECK(sys->bus.read8(amiga_system::zorro2_autoconfig_base + 0x02U) == 0x66U);
    assign_first_zorro2_board(*sys);
    REQUIRE_FALSE(sys->zorro2_autoconfig_pending());
    REQUIRE(sys->zorro2_boards[0].configured);
    CHECK(sys->zorro2_boards[0].assigned_base == amiga_system::fast_ram_base);

    constexpr std::uint32_t fast_offset = 0x001234U;
    sys->bus.write8(amiga_system::fast_ram_base + fast_offset, 0xA6U);
    CHECK(sys->bus.read8(amiga_system::fast_ram_base + fast_offset) == 0xA6U);
    CHECK(sys->fast_ram[fast_offset] == 0xA6U);
    CHECK(sys->paula.chipram()[fast_offset] == 0x00U);
    CHECK(sys->cpu_bus_wait_cycles(amiga_system::fast_ram_base + fast_offset, false, false, 0U,
                                   0U) == 0U);
    CHECK(sys->bus.read8(amiga_system::fast_ram_base +
                         static_cast<std::uint32_t>(sys->fast_ram.size())) == 0xFFU);
}

TEST_CASE("amiga500plus Copper location pointers use the ECS 1 MiB address width",
          "[manifests][amiga500][custom][amiga500plus]") {
    const amiga_config config{.model = amiga_model::amiga500_plus};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x080U, 0x001FU); // COP1LCH
    sys->write_custom_word(0x082U, 0x1235U); // COP1LCL, low bit ignored.
    sys->write_custom_word(0x084U, 0x0019U); // COP2LCH
    sys->write_custom_word(0x086U, 0x5679U); // COP2LCL, low bit ignored.

    CHECK(sys->cop1lc == 0x000F1234U);
    CHECK(sys->cop2lc == 0x00095678U);
    CHECK(sys->agnus.cop1lc() == 0x000F1234U);
    CHECK(sys->agnus.cop2lc() == 0x00095678U);
}

TEST_CASE("amiga600 Copper location pointers use the ECS 1 MiB address width",
          "[manifests][amiga500][custom][amiga600]") {
    const amiga_config config{.model = amiga_model::amiga600};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x080U, 0x001FU); // COP1LCH
    sys->write_custom_word(0x082U, 0x1235U); // COP1LCL, low bit ignored.
    sys->write_custom_word(0x084U, 0x0019U); // COP2LCH
    sys->write_custom_word(0x086U, 0x5679U); // COP2LCL, low bit ignored.

    CHECK(sys->cop1lc == 0x000F1234U);
    CHECK(sys->cop2lc == 0x00095678U);
    CHECK(sys->agnus.cop1lc() == 0x000F1234U);
    CHECK(sys->agnus.cop2lc() == 0x00095678U);
}

TEST_CASE("amiga500 BPLCON0 HIRES custom register exposes 640-pixel OCS rows",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0000U, 0x8000U);
    write_chip_word(*sys, 39U * 2U, 0x0001U);
    sys->write_custom_word(0x180U, 0x000FU); // COLOR00 = blue backdrop
    sys->write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground
    sys->write_custom_word(0x100U, 0x9000U); // HIRES | BPU = 1
    sys->write_custom_word(0x08EU, 0x2C81U);
    sys->write_custom_word(0x090U, 0xF4C1U);
    sys->write_custom_word(0x092U, 0x003CU);
    sys->write_custom_word(0x094U, 0x00D4U);
    sys->write_custom_word(0x0E0U, 0x0000U); // BPL1PTH
    sys->write_custom_word(0x0E2U, 0x0000U); // BPL1PTL
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_bplen));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.effective_stride() == agnus::framebuffer_stride);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[agnus::visible_width_hires - 1U] == 0x00FF0000U);
}

TEST_CASE("amiga500plus ECS high-resolution DDF 3c-d4 advances by forty words",
          "[manifests][amiga500plus][video]") {
    const amiga_config config{.model = amiga_model::amiga500_plus};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0000U, 0x8000U);
    write_chip_word(*sys, 40U * 2U, 0x8000U);
    sys->write_custom_word(0x180U, 0x000FU); // COLOR00 = blue backdrop
    sys->write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground
    sys->write_custom_word(0x100U, 0x9000U); // HIRES | BPU = 1
    sys->write_custom_word(0x08EU, 0x2C81U);
    sys->write_custom_word(0x090U, 0xF4C1U);
    sys->write_custom_word(0x092U, 0x003CU);
    sys->write_custom_word(0x094U, 0x00D4U);
    sys->write_custom_word(0x0E0U, 0x0000U); // BPL1PTH
    sys->write_custom_word(0x0E2U, 0x0000U); // BPL1PTL
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_bplen));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[agnus::framebuffer_stride] == 0x00FF0000U);
}

TEST_CASE("amiga500plus routes ECS DIWHIGH custom writes into Agnus display clipping",
          "[manifests][amiga500plus][video]") {
    const amiga_config config{.model = amiga_model::amiga500_plus};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 41U * 2U, 0x8000U);
    sys->write_custom_word(0x180U, 0x0000U); // COLOR00 = black backdrop.
    sys->write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground.
    sys->write_custom_word(0x100U, 0x9000U); // HIRES | BPU = 1.
    sys->write_custom_word(0x08EU, 0x2C78U);
    sys->write_custom_word(0x090U, 0x010AU);
    sys->write_custom_word(0x1E4U, 0x0100U);
    sys->write_custom_word(0x092U, 0x0030U);
    sys->write_custom_word(0x094U, 0x00D8U);
    sys->write_custom_word(0x0E0U, 0x0000U); // BPL1PTH.
    sys->write_custom_word(0x0E2U, 0x0000U); // BPL1PTL.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_bplen));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[607U] == 0x00000000U);
    CHECK(frame.pixels[608U] == 0x00FF0000U);
}

TEST_CASE("amiga500 DMACON routes audio DMA to Paula", "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000100U, 0x10U);
    sys->bus.write8(0x000101U, 0xF0U);
    sys->write_custom_word(0x0A0U, 0x0000U); // AUD0LCH
    sys->write_custom_word(0x0A2U, 0x0100U); // AUD0LCL
    sys->write_custom_word(0x0A4U, 0x0001U); // AUD0LEN
    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_aud0en));

    REQUIRE(sys->paula.channel_active(0));
    std::array<std::int16_t, 4> samples{};
    sys->paula.generate(samples);
    CHECK(samples[0] != 0);
}

TEST_CASE("amiga500 Agnus cycle callback clocks Paula capture",
          "[manifests][amiga500][audio]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->paula.enable_audio_capture(true);
    sys->bus.write8(0x000100U, 0x10U);
    sys->bus.write8(0x000101U, 0xF0U);
    sys->write_custom_word(0x0A0U, 0x0000U); // AUD0LCH
    sys->write_custom_word(0x0A2U, 0x0100U); // AUD0LCL
    sys->write_custom_word(0x0A4U, 0x0001U); // AUD0LEN
    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_aud0en));

    sys->agnus.tick(8U);

    REQUIRE(sys->paula.pending_samples() >= 2U);
    std::array<std::int16_t, 16> samples{};
    const std::size_t pairs = sys->paula.drain_samples(samples.data(), samples.size() / 2U);
    REQUIRE(pairs > 0U);
    CHECK(std::any_of(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(pairs * 2U),
                      [](std::int16_t sample) { return sample != 0; }));
}

TEST_CASE("amiga500 Paula buffer wrap requests and acknowledges AUDx interrupts",
          "[manifests][amiga500][audio][interrupt]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t code_offset = 0x0100U;
    constexpr std::uint32_t level4_vector_offset = (24U + 4U) * 4U;
    constexpr std::uint32_t level4_handler = amiga_system::kickstart_base + 0x0200U;
    write_kickstart_word(*sys, code_offset, 0x4E71U); // NOP.
    write_kickstart_long(*sys, level4_vector_offset, level4_handler);
    write_kickstart_word(*sys, 0x0200U, 0x4E73U); // RTE if the handler is executed later.

    auto regs = sys->cpu.cpu_registers();
    regs.pc = amiga_system::kickstart_base + code_offset;
    regs.sr = mnemos::chips::cpu::m68000::sr_s;
    sys->cpu.set_registers(regs);

    sys->bus.write8(0x000100U, 0x10U);
    sys->bus.write8(0x000101U, 0xF0U);
    sys->write_custom_word(0x0A0U, 0x0000U); // AUD0LCH
    sys->write_custom_word(0x0A2U, 0x0100U); // AUD0LCL
    sys->write_custom_word(0x0A4U, 0x0001U); // AUD0LEN
    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(
        0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit | amiga_system::int_master));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_aud0en));

    std::array<std::int16_t, 12> samples{};
    sys->paula.generate(samples);

    REQUIRE((sys->paula.interrupts() & 0x01U) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_aud0) != 0U);

    sys->cpu.step_instruction();
    CHECK(sys->cpu.cpu_registers().pc == amiga_system::kickstart_base + code_offset + 2U);

    sys->write_custom_word(
        0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit | amiga_system::int_aud0));
    sys->cpu.step_instruction();
    CHECK(sys->cpu.cpu_registers().pc == level4_handler);

    sys->write_custom_word(0x09CU, amiga_system::int_aud0);
    CHECK((sys->paula.interrupts() & 0x01U) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_aud0) == 0U);
}

TEST_CASE("amiga500 AUDxDAT manual audio write reaches Paula and INTREQ",
          "[manifests][amiga500][audio][interrupt]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(0x0AAU, 0x10F0U); // AUD0DAT

    std::array<std::int16_t, 8> samples{};
    sys->paula.generate(samples);

    CHECK(samples[0] == 1024);
    CHECK(samples[1] == 0);
    CHECK(samples[2] == -1024);
    CHECK(samples[3] == 0);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_aud0) != 0U);
    CHECK((sys->paula.interrupts() & 0x01U) != 0U);

    sys->write_custom_word(0x09CU, amiga_system::int_aud0);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_aud0) == 0U);
    CHECK((sys->paula.interrupts() & 0x01U) == 0U);
}

TEST_CASE("amiga500 ADKCON audio attachment routes channel modulation into Paula",
          "[manifests][amiga500][audio][modulation]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000100U, 0x00U);
    sys->bus.write8(0x000101U, 0x10U); // AUD0 supplies volume 16 to AUD1.
    sys->bus.write8(0x000200U, 0x10U);
    sys->bus.write8(0x000201U, 0x10U); // AUD1 audible data.

    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0001U));
    CHECK((sys->read_custom_word(0x010U) & 0x0001U) != 0U);
    CHECK((sys->paula.volume_attachment_mask() & 0x01U) != 0U);

    sys->write_custom_word(0x0A0U, 0x0000U); // AUD0LCH
    sys->write_custom_word(0x0A2U, 0x0100U); // AUD0LCL
    sys->write_custom_word(0x0A4U, 0x0001U); // AUD0LEN
    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(0x0B0U, 0x0000U); // AUD1LCH
    sys->write_custom_word(0x0B2U, 0x0200U); // AUD1LCL
    sys->write_custom_word(0x0B4U, 0x0001U); // AUD1LEN
    sys->write_custom_word(0x0B6U, 0x0001U); // AUD1PER
    sys->write_custom_word(0x0B8U, 0x0040U); // AUD1VOL
    sys->write_custom_word(
        0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                           agnus::dmacon_aud0en | agnus::dmacon_aud1en));

    std::array<std::int16_t, 4> samples{};
    sys->paula.generate(samples);

    CHECK(samples[0] == 0);
    CHECK(samples[1] == 256);
    CHECK(sys->paula.read_reg(1, mnemos::chips::audio::paula::reg_vol) == 0x0010U);

    sys->write_custom_word(0x09EU, 0x0001U);
    CHECK((sys->paula.volume_attachment_mask() & 0x01U) == 0U);
}

TEST_CASE("amiga500 disk DMA streams a mounted ADF track into chip RAM",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->write_custom_word(0x020U, 0x0000U); // DSKPTH
    sys->write_custom_word(0x022U, 0x0200U); // DSKPTL
    sys->write_custom_word(0x07EU, 0x4489U); // DSKSYNC
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words_to_read = 8U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);

    CHECK(sys->chip_ram[0x0200U] == 0x00U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);

    run_scanlines(*sys, 2U);
    CHECK(sys->chip_ram[0x0200U] == 0xAAU);
    CHECK(sys->chip_ram[0x0201U] == 0xAAU);
    CHECK(sys->chip_ram[0x0202U] == 0xAAU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) != 0U);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());
    CHECK(sys->chip_ram[0x0200U] == 0xAAU);
    CHECK(sys->chip_ram[0x0201U] == 0xAAU);
    CHECK(sys->chip_ram[0x0202U] == 0xAAU);
    CHECK(sys->chip_ram[0x0203U] == 0xAAU);
    CHECK(sys->chip_ram[0x0204U] == 0x44U);
    CHECK(sys->chip_ram[0x0205U] == 0x89U);
    CHECK(sys->chip_ram[0x0206U] == 0x44U);
    CHECK(sys->chip_ram[0x0207U] == 0x89U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 mounted ADF sectors use AmigaDOS odd-even block layout",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    constexpr std::size_t gap_bytes = 4U;
    constexpr std::size_t sync_bytes = 4U;
    constexpr std::size_t header_raw_longs = 5U;
    constexpr std::size_t header_encoded_longs = header_raw_longs * 2U;
    constexpr std::size_t checksum_bytes = 8U;
    constexpr std::size_t data_raw_longs = amiga_system::floppy_sector_size / 4U;
    constexpr std::size_t data_encoded_longs = data_raw_longs * 2U;
    constexpr std::size_t data_offset =
        gap_bytes + sync_bytes + header_encoded_longs * 4U + checksum_bytes * 2U;
    constexpr std::size_t sector_bytes = data_offset + data_encoded_longs * 4U;
    constexpr std::size_t sector_slot_bytes = 0x440U;
    static_assert(sector_bytes == sector_slot_bytes);

    const auto& track = sys->floppy_drives[0].track_stream;
    REQUIRE(track.size() >= sector_bytes);
    CHECK(read_track_long(track, 0U) == 0xAAAAAAAAU);
    CHECK(read_track_long(track, gap_bytes) == 0x44894489U);
    CHECK(read_track_long(track, sector_slot_bytes) == 0xAAAAAAAAU);
    CHECK(read_track_long(track, sector_slot_bytes + gap_bytes) == 0x44894489U);

    const std::uint32_t info = decode_mfm_odd_even(
        read_track_long(track, gap_bytes + sync_bytes),
        read_track_long(track, gap_bytes + sync_bytes + 4U));
    CHECK(info == 0xFF00000BU);

    const std::uint32_t first_data_long = decode_mfm_odd_even(
        read_track_long(track, data_offset),
        read_track_long(track, data_offset + data_raw_longs * 4U));
    CHECK(first_data_long == 0x00010203U);
}

TEST_CASE("amiga500 Copper cannot arm disk DMA pointer or length registers",
          "[manifests][amiga500][disk][copper]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    constexpr std::uint32_t list = 0x0300U;
    constexpr std::uint16_t words_to_read = 8U;
    constexpr std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    write_chip_word(*sys, list + 0U, 0x0020U); // DSKPTH
    write_chip_word(*sys, list + 2U, 0x0000U);
    write_chip_word(*sys, list + 4U, 0x0022U); // DSKPTL
    write_chip_word(*sys, list + 6U, 0x0200U);
    write_chip_word(*sys, list + 8U, 0x007EU); // DSKSYNC
    write_chip_word(*sys, list + 10U, 0x4489U);
    write_chip_word(*sys, list + 12U, 0x0096U); // DMACON: enable disk DMA.
    write_chip_word(*sys, list + 14U,
                    static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                               agnus::dmacon_dsken));
    write_chip_word(*sys, list + 16U, 0x0024U); // First DSKLEN write arms DMA.
    write_chip_word(*sys, list + 18U, dsklen);
    write_chip_word(*sys, list + 20U, 0x0024U); // Second DSKLEN write starts DMA.
    write_chip_word(*sys, list + 22U, dsklen);
    write_chip_word(*sys, list + 24U, 0xFFFFU);
    write_chip_word(*sys, list + 26U, 0xFFFEU);

    sys->write_custom_word(0x02EU, 0x0002U); // CDANG does not open $000-$03e.
    sys->write_custom_word(0x080U, 0x0000U); // COP1LCH
    sys->write_custom_word(0x082U, static_cast<std::uint16_t>(list));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_copen));

    sys->agnus.tick(32U);
    CHECK(sys->read_custom_word(0x07EU) == 0x4489U);
    CHECK(sys->chip_ram[0x0200U] == 0x00U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);

    run_scanlines(*sys, 2U);
    CHECK(sys->chip_ram[0x0200U] == 0x00U);
    CHECK(sys->chip_ram[0x0201U] == 0x00U);
    CHECK(sys->chip_ram[0x0202U] == 0x00U);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());
    CHECK(sys->chip_ram[0x0203U] == 0x00U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);
}

TEST_CASE("amiga500 disk byte ready is paced across color clocks", "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 2U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0xA5U;
    drive.track_stream[1] = 0x5AU;
    reset_floppy_stream_phase(drive);

    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);
    sys->agnus.tick(agnus::color_clocks_per_line - 1U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    reset_floppy_stream_phase(sys->floppy_drives[0], 1U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    sys->agnus.tick(1U);
    const std::uint16_t first = sys->read_custom_word(0x01AU);
    CHECK((first & 0x8000U) != 0U);
    CHECK((first & 0x00FFU) == 0x00A5U);

    sys->agnus.tick(agnus::color_clocks_per_line - 1U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);
    sys->agnus.tick(1U);
    const std::uint16_t second = sys->read_custom_word(0x01AU);
    CHECK((second & 0x8000U) != 0U);
    CHECK((second & 0x00FFU) == 0x005AU);
}

TEST_CASE("amiga500 ADKCON WORDSYNC gates disk read DMA until DSKSYNC",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 8U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0x12U;
    drive.track_stream[1] = 0x34U;
    drive.track_stream[2] = 0x44U;
    drive.track_stream[3] = 0x89U;
    drive.track_stream[4] = 0xAAU;
    drive.track_stream[5] = 0xBBU;
    drive.track_stream[6] = 0xCCU;
    drive.track_stream[7] = 0xDDU;
    reset_floppy_stream_phase(drive);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0600U);
    sys->write_custom_word(0x07EU, 0x4489U);
    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0400U));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words_to_read = 2U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    REQUIRE(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 2U);
    CHECK(sys->disk_wordsync_waiting);
    CHECK(sys->chip_ram[0x0600U] == 0x00U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    reset_floppy_stream_phase(sys->floppy_drives[0]);
    sys->disk_wordsync_waiting = false;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 2U);
    CHECK_FALSE(sys->disk_wordsync_waiting);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dsksyn) != 0U);
    CHECK(sys->chip_ram[0x0600U] == 0x00U);

    run_scanlines(*sys, 4U);
    CHECK(sys->chip_ram[0x0600U] == 0xAAU);
    CHECK(sys->chip_ram[0x0601U] == 0xBBU);
    CHECK(sys->chip_ram[0x0602U] == 0xCCU);
    CHECK(sys->chip_ram[0x0603U] == 0xDDU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 WORDSYNC disk DMA finds DSKSYNC at bit granularity",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 8U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0x04U;
    drive.track_stream[1] = 0x48U;
    drive.track_stream[2] = 0x9AU;
    drive.track_stream[3] = 0xABU;
    drive.track_stream[4] = 0xBCU;
    drive.track_stream[5] = 0xCDU;
    drive.track_stream[6] = 0xD0U;
    reset_floppy_stream_phase(drive);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0640U);
    sys->write_custom_word(0x07EU, 0x4489U);
    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0400U));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words_to_read = 2U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    REQUIRE(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 2U);
    CHECK(sys->disk_wordsync_waiting);
    CHECK(sys->chip_ram[0x0640U] == 0x00U);

    run_scanlines(*sys, 1U);
    CHECK_FALSE(sys->disk_wordsync_waiting);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dsksyn) != 0U);
    CHECK(sys->chip_ram[0x0640U] == 0x00U);

    run_scanlines(*sys, 4U);
    CHECK(sys->chip_ram[0x0640U] == 0xAAU);
    CHECK(sys->chip_ram[0x0641U] == 0xBBU);
    CHECK(sys->chip_ram[0x0642U] == 0xCCU);
    CHECK(sys->chip_ram[0x0643U] == 0xDDU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 WORDSYNC disk DMA can start from a latched DSKSYN",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 8U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0x44U;
    drive.track_stream[1] = 0x89U;
    drive.track_stream[2] = 0xAAU;
    drive.track_stream[3] = 0xBBU;
    drive.track_stream[4] = 0xCCU;
    drive.track_stream[5] = 0xDDU;
    reset_floppy_stream_phase(drive);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0660U);
    sys->write_custom_word(0x07EU, 0x4489U);
    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0400U));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    run_scanlines(*sys, 2U);
    REQUIRE(sys->disk_sync_match);
    REQUIRE((sys->read_custom_word(0x01EU) & amiga_system::int_dsksyn) != 0U);

    constexpr std::uint16_t words_to_read = 2U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    CHECK_FALSE(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 4U);
    CHECK(sys->chip_ram[0x0660U] == 0xAAU);
    CHECK(sys->chip_ram[0x0661U] == 0xBBU);
    CHECK(sys->chip_ram[0x0662U] == 0xCCU);
    CHECK(sys->chip_ram[0x0663U] == 0xDDU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 DSKSYN interrupt requires ADKCON WORDSYNC",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0x44U;
    drive.track_stream[1] = 0x89U;
    reset_floppy_stream_phase(drive);

    run_scanlines(*sys, 2U);
    CHECK((sys->read_custom_word(0x01AU) & 0x1000U) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dsksyn) == 0U);

    reset_floppy_stream_phase(sys->floppy_drives[0]);
    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0400U));

    run_scanlines(*sys, 2U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dsksyn) != 0U);
}

TEST_CASE("amiga500 WORDSYNC disk DMA preserves decodable AmigaDOS sectors",
          "[manifests][amiga500][disk]") {
    auto adf = tiny_adf();
    for (std::uint8_t sector = 0U; sector < amiga_system::floppy_sectors_per_track; ++sector) {
        const std::size_t base = static_cast<std::size_t>(sector) * amiga_system::floppy_sector_size;
        for (std::size_t i = 0U; i < amiga_system::floppy_sector_size; ++i) {
            adf[base + i] = static_cast<std::uint8_t>((sector * 19U + i) & 0xFFU);
        }
    }

    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(adf));
    select_df0(*sys);

    constexpr std::uint32_t dma_base = 0x2000U;
    constexpr std::uint16_t words_to_read = 0x1CBEU;
    constexpr std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    constexpr std::size_t bytes_to_read = static_cast<std::size_t>(words_to_read) * 2U;

    sys->write_custom_word(0x020U, static_cast<std::uint16_t>(dma_base >> 16U));
    sys->write_custom_word(0x022U, static_cast<std::uint16_t>(dma_base));
    sys->write_custom_word(0x07EU, 0x4489U);
    sys->write_custom_word(0x09EU, static_cast<std::uint16_t>(amiga_system::setclr_bit | 0x0400U));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    REQUIRE(sys->disk_wordsync_waiting);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution() * 2U);
    REQUIRE_FALSE(sys->disk_wordsync_waiting);
    REQUIRE((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);

    std::vector<std::uint8_t> dma(bytes_to_read);
    std::copy(sys->chip_ram.begin() + dma_base, sys->chip_ram.begin() + dma_base + bytes_to_read,
              dma.begin());

    std::array<bool, amiga_system::floppy_sectors_per_track> decoded{};
    std::size_t decoded_count = 0U;
    std::array<std::uint8_t, amiga_system::floppy_sector_size> sector_data{};
    for (std::size_t offset = 0U; offset + 2U < dma.size(); ++offset) {
        if (read_track_word(dma, offset) != 0x4489U) {
            continue;
        }

        std::uint8_t sector = 0U;
        if (!decode_dma_amigados_sector(dma, offset, 0U, sector, sector_data)) {
            continue;
        }
        if (decoded[sector]) {
            continue;
        }

        const std::size_t adf_base =
            static_cast<std::size_t>(sector) * amiga_system::floppy_sector_size;
        CHECK(std::equal(sector_data.begin(), sector_data.end(), adf.begin() + adf_base));
        decoded[sector] = true;
        ++decoded_count;
    }

    CHECK(decoded_count == amiga_system::floppy_sectors_per_track);
    CHECK(std::all_of(decoded.begin(), decoded.end(), [](bool found) { return found; }));
}

TEST_CASE("amiga500 disk write DMA patches and saves the raw track stream",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    sys->chip_ram[0x0400U] = 0xDEU;
    sys->chip_ram[0x0401U] = 0xADU;
    sys->chip_ram[0x0402U] = 0xBEU;
    sys->chip_ram[0x0403U] = 0xEFU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0400U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 2U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    const std::uint16_t dskbytr = sys->read_custom_word(0x01AU);
    CHECK((dskbytr & 0x6000U) == 0x6000U);

    run_scanlines(*sys, 2U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    REQUIRE(!sys->floppy_drives[0].track_stream.empty());
    sys->floppy_drives[0].track_stream[0] = 0x00U;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    sys->write_custom_word(0x09CU, amiga_system::int_dskblk);
    reset_floppy_stream_phase(sys->floppy_drives[0]);
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0500U);
    const std::uint16_t read_dsklen = static_cast<std::uint16_t>(0x8000U | words);
    sys->write_custom_word(0x024U, read_dsklen);
    sys->write_custom_word(0x024U, read_dsklen);

    run_scanlines(*sys, 2U);
    CHECK(sys->chip_ram[0x0500U] == 0xDEU);
    CHECK(sys->chip_ram[0x0501U] == 0xADU);
    CHECK(sys->chip_ram[0x0502U] == 0xBEU);
    CHECK(sys->chip_ram[0x0503U] == 0xEFU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk read DMA samples raw tracks at sub-byte phase",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0xABU;
    drive.track_stream[1] = 0xCDU;
    drive.track_stream[2] = 0xEFU;
    drive.track_stream[3] = 0x01U;
    reset_floppy_stream_phase(drive, 0U, 4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    reset_floppy_stream_phase(drive, 2U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->floppy_drives[0].stream_offset == 0U);
    CHECK(sys->floppy_drives[0].stream_bit_offset == 4U);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0700U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t read_dsklen = static_cast<std::uint16_t>(0x8000U | words);
    sys->write_custom_word(0x024U, read_dsklen);
    sys->write_custom_word(0x024U, read_dsklen);

    run_scanlines(*sys, 2U);
    CHECK(sys->chip_ram[0x0700U] == 0xBCU);
    CHECK(sys->chip_ram[0x0701U] == 0xDEU);
    CHECK(sys->floppy_drives[0].stream_offset == 2U);
    CHECK(sys->floppy_drives[0].stream_bit_offset == 4U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 floppy side changes preserve rotational bit phase",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(16U, 0x00U);
    drive.track_stream_track_index = 0U;
    reset_floppy_stream_phase(drive, 4U, 0U);

    drive.raw_track_cache[1].assign(8U, 0x00U);
    drive.raw_track_cache[1][2] = 0xDEU;
    drive.raw_track_cache[1][3] = 0xF0U;

    sys->cia_b.write(0x01U, 0x71U); // Keep DF0 selected and motor on, select side 1.

    CHECK(sys->floppy_side() == 1U);
    CHECK(drive.track_stream_track_index == 1U);
    CHECK(drive.stream_offset == 2U);
    CHECK(drive.stream_bit_offset == 0U);
    CHECK(sys->next_floppy_byte() == 0xDEU);
    CHECK(drive.stream_offset == 3U);
    CHECK(drive.stream_bit_offset == 0U);
}

TEST_CASE("amiga500 floppy side changes preserve in-flight raw byte shifter",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(16U, 0x00U);
    drive.track_stream[0] = 0xA0U;
    drive.track_stream_track_index = 0U;
    reset_floppy_stream_phase(drive);

    for (std::size_t bit = 0U; bit < 4U; ++bit) {
        CHECK_FALSE(sys->shift_floppy_read_bit());
    }
    CHECK(drive.stream_offset == 0U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK(drive.stream_read_shift == 0x0AU);
    CHECK(drive.stream_read_bit_count == 4U);

    drive.raw_track_cache[1].assign(16U, 0x00U);
    drive.raw_track_cache[1][0] = 0x0CU;
    sys->cia_b.write(0x01U, 0x71U); // Keep DF0 selected and motor on, select side 1.

    CHECK(sys->floppy_side() == 1U);
    CHECK(drive.track_stream_track_index == 1U);
    CHECK(drive.stream_offset == 0U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK(drive.stream_read_shift == 0x0AU);
    CHECK(drive.stream_read_bit_count == 4U);

    for (std::size_t bit = 0U; bit < 3U; ++bit) {
        CHECK_FALSE(sys->shift_floppy_read_bit());
    }
    REQUIRE(sys->shift_floppy_read_bit());
    const std::uint16_t dskbytr = sys->read_custom_word(0x01AU);
    CHECK((dskbytr & 0x8000U) != 0U);
    CHECK((dskbytr & 0x00FFU) == 0xACU);
}

TEST_CASE("amiga500 floppy track steps preserve in-flight raw byte shifter",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(16U, 0x00U);
    drive.track_stream[0] = 0xA0U;
    drive.track_stream_track_index = 0U;
    reset_floppy_stream_phase(drive);

    for (std::size_t bit = 0U; bit < 4U; ++bit) {
        CHECK_FALSE(sys->shift_floppy_read_bit());
    }

    constexpr std::size_t cylinder_one_side_zero_track = 2U;
    drive.raw_track_cache[cylinder_one_side_zero_track].assign(16U, 0x00U);
    drive.raw_track_cache[cylinder_one_side_zero_track][0] = 0x0CU;
    sys->cia_b.write(0x01U, 0x74U); // Step inward while DF0 stays selected and motor-on.

    CHECK(sys->floppy_cylinder() == 1U);
    CHECK(drive.track_stream_track_index == cylinder_one_side_zero_track);
    CHECK(drive.stream_offset == 0U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK(drive.stream_read_shift == 0x0AU);
    CHECK(drive.stream_read_bit_count == 4U);

    for (std::size_t bit = 0U; bit < 3U; ++bit) {
        CHECK_FALSE(sys->shift_floppy_read_bit());
    }
    REQUIRE(sys->shift_floppy_read_bit());
    const std::uint16_t dskbytr = sys->read_custom_word(0x01AU);
    CHECK((dskbytr & 0x8000U) != 0U);
    CHECK((dskbytr & 0x00FFU) == 0xACU);
}

TEST_CASE("amiga500 weak raw-track masks sample unstable bits deterministically",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[1] = 0xA5U;
    drive.weak_bit_stream.assign(drive.track_stream.size(), 0x00U);
    drive.weak_bit_stream[0] = 0xFFU;
    drive.weak_bit_stream[2] = 0xFFU;
    drive.weak_bit_lfsr = 0xACE1U;
    reset_floppy_stream_phase(drive);

    std::uint16_t expected_lfsr = drive.weak_bit_lfsr;
    const std::uint8_t first_weak_byte = next_expected_weak_byte(expected_lfsr);
    CHECK(sys->next_floppy_byte() == first_weak_byte);
    CHECK(drive.weak_bit_lfsr == expected_lfsr);

    const std::uint16_t stable_lfsr = drive.weak_bit_lfsr;
    CHECK(sys->next_floppy_byte() == 0xA5U);
    CHECK(drive.weak_bit_lfsr == stable_lfsr);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    drive.weak_bit_lfsr = 0x1234U;
    drive.weak_bit_stream.clear();
    reset_floppy_stream_phase(drive);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    auto& loaded_drive = sys->floppy_drives[0];
    REQUIRE(loaded_drive.weak_bit_stream.size() == loaded_drive.track_stream.size());
    CHECK(loaded_drive.stream_offset == 2U);
    CHECK(loaded_drive.stream_bit_offset == 0U);
    CHECK(loaded_drive.weak_bit_lfsr == stable_lfsr);

    expected_lfsr = stable_lfsr;
    const std::uint8_t second_weak_byte = next_expected_weak_byte(expected_lfsr);
    CHECK(sys->next_floppy_byte() == second_weak_byte);
    CHECK(loaded_drive.weak_bit_lfsr == expected_lfsr);
}

TEST_CASE("amiga500 load state ignores stale active weak raw-track masks",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    auto& drive = sys->floppy_drives[0];
    REQUIRE(drive.track_stream.size() > 2U);
    const std::uint8_t clean_first_byte = drive.track_stream[0];
    const std::uint8_t clean_second_byte = drive.track_stream[1];
    REQUIRE(clean_first_byte != 0xE7U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    drive.track_stream.assign(drive.track_stream.size(), 0xE7U);
    drive.weak_bit_stream.assign(drive.track_stream.size(), 0xFFU);
    drive.track_stream_track_index = 0U;
    drive.track_stream_dirty = false;
    drive.raw_track_cache[0].clear();
    drive.weak_bit_cache[0].clear();

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    auto& loaded_drive = sys->floppy_drives[0];
    REQUIRE_FALSE(loaded_drive.track_stream.empty());
    CHECK(loaded_drive.track_stream[0] == clean_first_byte);
    CHECK(loaded_drive.track_stream[1] == clean_second_byte);
    CHECK(loaded_drive.weak_bit_stream.empty());
    CHECK(loaded_drive.raw_track_cache[0].empty());
    CHECK(loaded_drive.weak_bit_cache[0].empty());

    loaded_drive.cylinder_pos = 1U;
    sys->update_floppy_track_stream(0U);
    loaded_drive.cylinder_pos = 0U;
    sys->update_floppy_track_stream(0U);
    REQUIRE(loaded_drive.track_stream.size() > 2U);
    CHECK(loaded_drive.track_stream[0] == clean_first_byte);
    CHECK(loaded_drive.track_stream[1] == clean_second_byte);
    CHECK(loaded_drive.weak_bit_stream.empty());
    CHECK(loaded_drive.raw_track_cache[0].empty());
    CHECK(loaded_drive.weak_bit_cache[0].empty());
}

TEST_CASE("amiga500 disk write DMA patches raw tracks at sub-byte phase",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0xAAU);
    reset_floppy_stream_phase(drive, 0U, 4U);

    sys->chip_ram[0x0740U] = 0xDEU;
    sys->chip_ram[0x0741U] = 0xADU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0740U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    run_scanlines(*sys, 2U);
    CHECK(drive.track_stream[0] == 0xADU);
    CHECK(drive.track_stream[1] == 0xEAU);
    CHECK(drive.track_stream[2] == 0xDAU);
    CHECK(drive.stream_offset == 2U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk write DMA clears weak raw-track bits",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.weak_bit_stream.assign(drive.track_stream.size(), 0xFFU);
    reset_floppy_stream_phase(drive);

    sys->chip_ram[0x0760U] = 0xC3U;
    sys->chip_ram[0x0761U] = 0x5AU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0760U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    run_scanlines(*sys, 2U);
    CHECK(drive.track_stream[0] == 0xC3U);
    CHECK(drive.track_stream[1] == 0x5AU);
    CHECK(drive.weak_bit_stream[0] == 0x00U);
    CHECK(drive.weak_bit_stream[1] == 0x00U);
    CHECK(drive.weak_bit_stream[2] == 0xFFU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    drive.weak_bit_stream[0] = 0xFFU;
    drive.weak_bit_stream[1] = 0xFFU;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    auto& loaded_drive = sys->floppy_drives[0];
    REQUIRE(loaded_drive.weak_bit_stream.size() == loaded_drive.track_stream.size());
    CHECK(loaded_drive.weak_bit_stream[0] == 0x00U);
    CHECK(loaded_drive.weak_bit_stream[1] == 0x00U);
    CHECK(loaded_drive.weak_bit_stream[2] == 0xFFU);
}

TEST_CASE("amiga500 disk read DMA resumes within a raw bitcell byte after save state",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    const std::uint64_t clocks_per_revolution =
        static_cast<std::uint64_t>(lines_per_revolution) * agnus::color_clocks_per_line;
    REQUIRE((clocks_per_revolution % 8U) == 0U);
    const auto one_bit_per_clock_track_bytes = static_cast<std::size_t>(clocks_per_revolution / 8U);
    REQUIRE(one_bit_per_clock_track_bytes > 2U);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(one_bit_per_clock_track_bytes, 0x00U);
    drive.track_stream[0] = 0xA5U;
    drive.track_stream[1] = 0x5AU;
    reset_floppy_stream_phase(drive);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0790U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t read_dsklen = static_cast<std::uint16_t>(0x8000U | words);
    sys->write_custom_word(0x024U, read_dsklen);
    sys->write_custom_word(0x024U, read_dsklen);

    sys->agnus.tick(4U);
    CHECK(sys->chip_ram[0x0790U] == 0x00U);
    CHECK(drive.stream_offset == 0U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK(drive.stream_read_shift == 0x0AU);
    CHECK(drive.stream_read_bit_count == 4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    drive.stream_offset = 1U;
    drive.stream_bit_offset = 0U;
    drive.stream_read_shift = 0U;
    drive.stream_read_bit_count = 0U;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->floppy_drives[0].stream_offset == 0U);
    CHECK(sys->floppy_drives[0].stream_bit_offset == 4U);
    CHECK(sys->floppy_drives[0].stream_read_shift == 0x0AU);
    CHECK(sys->floppy_drives[0].stream_read_bit_count == 4U);

    sys->agnus.tick(4U);
    CHECK(sys->chip_ram[0x0790U] == 0xA5U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);

    sys->agnus.tick(8U);
    CHECK(sys->chip_ram[0x0791U] == 0x5AU);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk write DMA resumes within a raw bitcell byte after save state",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    const std::uint64_t clocks_per_revolution =
        static_cast<std::uint64_t>(lines_per_revolution) * agnus::color_clocks_per_line;
    REQUIRE((clocks_per_revolution % 8U) == 0U);
    const auto one_bit_per_clock_track_bytes = static_cast<std::size_t>(clocks_per_revolution / 8U);
    REQUIRE(one_bit_per_clock_track_bytes > 2U);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(one_bit_per_clock_track_bytes, 0x00U);
    reset_floppy_stream_phase(drive);

    sys->chip_ram[0x07A0U] = 0xA5U;
    sys->chip_ram[0x07A1U] = 0x5AU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x07A0U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    sys->agnus.tick(4U);
    CHECK(drive.track_stream[0] == 0xA0U);
    CHECK(drive.stream_offset == 0U);
    CHECK(drive.stream_bit_offset == 4U);
    CHECK(drive.stream_write_latch == 0xA5U);
    CHECK(drive.stream_write_bits_remaining == 4U);
    CHECK(sys->disk_dma_bytes_remaining == 2U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    drive.track_stream[0] = 0x00U;
    drive.stream_offset = 1U;
    drive.stream_bit_offset = 0U;
    drive.stream_write_latch = 0U;
    drive.stream_write_shift = 0U;
    drive.stream_write_bits_remaining = 0U;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    auto& loaded_drive = sys->floppy_drives[0];
    CHECK(loaded_drive.track_stream[0] == 0xA0U);
    CHECK(loaded_drive.stream_offset == 0U);
    CHECK(loaded_drive.stream_bit_offset == 4U);
    CHECK(loaded_drive.stream_write_latch == 0xA5U);
    CHECK(loaded_drive.stream_write_bits_remaining == 4U);

    sys->agnus.tick(4U);
    CHECK(loaded_drive.track_stream[0] == 0xA5U);
    CHECK(sys->disk_dma_bytes_remaining == 1U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) == 0U);

    sys->agnus.tick(8U);
    CHECK(loaded_drive.track_stream[1] == 0x5AU);
    CHECK(loaded_drive.stream_offset == 2U);
    CHECK(loaded_drive.stream_bit_offset == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk write DMA keeps custom raw tracks across movement and save state",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 4U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x11U);
    drive.weak_bit_stream.assign(lines_per_revolution, 0x00U);
    drive.weak_bit_stream[2] = 0xF0U;
    reset_floppy_stream_phase(drive);

    sys->chip_ram[0x0780U] = 0xCAU;
    sys->chip_ram[0x0781U] = 0xFEU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0780U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 1U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    run_scanlines(*sys, 2U);
    REQUIRE(drive.track_stream.size() == lines_per_revolution);
    CHECK(drive.track_stream[0] == 0xCAU);
    CHECK(drive.track_stream[1] == 0xFEU);
    CHECK(drive.weak_bit_stream[0] == 0x00U);
    CHECK(drive.weak_bit_stream[1] == 0x00U);
    CHECK(drive.weak_bit_stream[2] == 0xF0U);

    drive.cylinder_pos = 1U;
    sys->update_floppy_track_stream(0U);
    REQUIRE_FALSE(drive.track_stream.empty());
    CHECK(drive.track_stream[0] != 0xCAU);
    CHECK(drive.weak_bit_stream.empty());

    drive.cylinder_pos = 0U;
    sys->update_floppy_track_stream(0U);
    REQUIRE(drive.track_stream.size() == lines_per_revolution);
    REQUIRE(drive.weak_bit_stream.size() == lines_per_revolution);
    CHECK(drive.track_stream[0] == 0xCAU);
    CHECK(drive.track_stream[1] == 0xFEU);
    CHECK(drive.weak_bit_stream[0] == 0x00U);
    CHECK(drive.weak_bit_stream[1] == 0x00U);
    CHECK(drive.weak_bit_stream[2] == 0xF0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    drive.raw_track_cache[0].clear();
    drive.weak_bit_cache[0].clear();
    drive.track_stream[0] = 0x00U;
    drive.weak_bit_stream.clear();

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    auto& loaded_drive = sys->floppy_drives[0];
    loaded_drive.cylinder_pos = 1U;
    sys->update_floppy_track_stream(0U);
    loaded_drive.cylinder_pos = 0U;
    sys->update_floppy_track_stream(0U);
    REQUIRE(loaded_drive.track_stream.size() == lines_per_revolution);
    REQUIRE(loaded_drive.weak_bit_stream.size() == lines_per_revolution);
    CHECK(loaded_drive.track_stream[0] == 0xCAU);
    CHECK(loaded_drive.track_stream[1] == 0xFEU);
    CHECK(loaded_drive.weak_bit_stream[0] == 0x00U);
    CHECK(loaded_drive.weak_bit_stream[1] == 0x00U);
    CHECK(loaded_drive.weak_bit_stream[2] == 0xF0U);
}

TEST_CASE("amiga500 disk write DMA decodes AmigaDOS sectors back into the ADF image",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    std::vector<std::uint8_t> patched = tiny_adf();
    patched[0x0000U] = 0xA5U;
    patched[0x01FFU] = 0x5AU;
    auto reference = assemble_amiga(tiny_kickstart());
    REQUIRE(reference != nullptr);
    REQUIRE(reference->mount_floppy(patched));

    constexpr std::size_t raw_sector_slot_bytes = 0x440U;
    REQUIRE(reference->floppy_drives[0].track_stream.size() >= raw_sector_slot_bytes);
    std::copy_n(reference->floppy_drives[0].track_stream.begin(), raw_sector_slot_bytes,
                sys->chip_ram.begin() + 0x0800U);

    reset_floppy_stream_phase(sys->floppy_drives[0]);
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0800U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = static_cast<std::uint16_t>(raw_sector_slot_bytes / 2U);
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
    REQUIRE(sys->floppy_drives[0].image.size() == amiga_system::floppy_dd_size);
    CHECK(sys->floppy_drives[0].image[0x0000U] == 0xA5U);
    CHECK(sys->floppy_drives[0].image[0x01FFU] == 0x5AU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    REQUIRE(!blob.empty());

    REQUIRE(sys->mount_floppy(tiny_adf()));
    CHECK(sys->floppy_drives[0].image[0x0000U] != 0xA5U);
    CHECK(sys->floppy_drives[0].image[0x01FFU] != 0x5AU);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(sys->floppy_drives[0].image.size() == amiga_system::floppy_dd_size);
    CHECK(sys->floppy_drives[0].image[0x0000U] == 0xA5U);
    CHECK(sys->floppy_drives[0].image[0x01FFU] == 0x5AU);
}

TEST_CASE("amiga500 paced disk DMA survives system save state", "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0300U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));
    constexpr std::uint16_t words_to_read = 8U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    run_scanlines(*sys, 1U);
    CHECK(sys->chip_ram[0x0300U] == 0xAAU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    std::fill(sys->chip_ram.begin() + 0x0300U, sys->chip_ram.begin() + 0x0310U,
              std::uint8_t{0x00U});
    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    CHECK(sys->chip_ram[0x0300U] == 0xAAU);
    CHECK(sys->chip_ram[0x0301U] == 0xAAU);
    CHECK(sys->chip_ram[0x0302U] == 0xAAU);
    CHECK(sys->chip_ram[0x0303U] == 0xAAU);
    CHECK(sys->chip_ram[0x0304U] == 0x44U);
    CHECK(sys->chip_ram[0x0305U] == 0x89U);
    CHECK(sys->chip_ram[0x0306U] == 0x44U);
    CHECK(sys->chip_ram[0x0307U] == 0x89U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 CIAB disk control steps the mounted ADF drive", "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    select_df0(*sys);
    CHECK(sys->floppy_cylinder() == 0U);
    CHECK(sys->floppy_side() == 0U);

    sys->cia_b.write(0x01U, 0x74U); // falling /STEP with DIR=0 moves inward.
    CHECK(sys->floppy_cylinder() == 1U);

    sys->cia_b.write(0x01U, 0x77U); // /STEP high, DIR=1.
    sys->cia_b.write(0x01U, 0x76U); // falling /STEP with DIR=1 moves outward.
    CHECK(sys->floppy_cylinder() == 0U);

    sys->cia_b.write(0x01U, 0x71U); // /SIDE=0 selects side 1.
    CHECK(sys->floppy_side() == 1U);
}

TEST_CASE("amiga500 CIAB disk control only follows PRB lines driven by DDRB",
          "[manifests][amiga500][disk][cia]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    sys->cia_b.write(0x01U, 0x75U);
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK_FALSE(sys->floppy_motor_on);

    sys->cia_b.write(0x03U, 0xFFU);
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK(sys->floppy_motor_on);
}

TEST_CASE("amiga500 CIAB disk motor state latches when a drive is selected",
          "[manifests][amiga500][disk][cia][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    sys->cia_b.write(0x01U, 0xFFU);
    sys->cia_b.write(0x03U, 0xFFU);
    sys->cia_b.write(0x01U, 0x7FU); // /MTR=0 with no drive selected.
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK_FALSE(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);

    sys->cia_b.write(0x01U, 0x77U); // DF0 samples /MTR=0 on select assertion.
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK(sys->floppy_drives[0].motor_on);
    CHECK(sys->floppy_motor_on);
    CHECK((sys->cia_a.read(0x00U) & 0x20U) == 0U);

    sys->cia_b.write(0x01U, 0xFFU); // Deselecting all drives does not relatch /MTR=1.
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->cia_b.write(0x01U, 0xF7U); // DF0 samples /MTR=1 on the next select assertion.
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK_FALSE(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);
    CHECK((sys->cia_a.read(0x00U) & 0x20U) != 0U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);

    sys->cia_b.write(0x01U, 0xFFU);
    sys->cia_b.write(0x01U, 0xF7U);
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK_FALSE(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);
    CHECK((sys->cia_a.read(0x00U) & 0x20U) != 0U);

    sys->cia_b.write(0x01U, 0xFFU);
    sys->cia_b.write(0x01U, 0x77U);
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK(sys->floppy_drives[0].motor_on);
    CHECK(sys->floppy_motor_on);
    CHECK((sys->cia_a.read(0x00U) & 0x20U) == 0U);
}

TEST_CASE("amiga500 motor-latched floppy keeps rotating while deselected",
          "[manifests][amiga500][disk][cia]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 2U);

    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0xA5U;
    drive.track_stream[1] = 0x5AU;
    drive.raw_track_cache[0] = drive.track_stream;
    reset_floppy_stream_phase(drive);

    sys->cia_b.write(0x01U, 0xFFU); // Deselect DF0; its latched motor stays on.
    CHECK(sys->selected_floppy_drive() == amiga_system::no_floppy_drive);
    CHECK(sys->floppy_drives[0].motor_on);
    CHECK_FALSE(sys->floppy_motor_on);

    run_scanlines(*sys, 1U);
    CHECK(drive.stream_offset == 1U);
    CHECK(drive.stream_bit_offset == 0U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);

    sys->cia_b.write(0x01U, 0x77U); // Re-select DF0 and keep its motor latched on.
    run_scanlines(*sys, 1U);

    const std::uint16_t dskbytr = sys->read_custom_word(0x01AU);
    CHECK((dskbytr & 0x8000U) != 0U);
    CHECK((dskbytr & 0x00FFU) == 0x005AU);
}

TEST_CASE("amiga500 CIAB disk select lines address independent DF0-DF3 state",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(0U, tiny_adf()));
    REQUIRE(sys->mount_floppy(1U, tiny_adf()));

    select_df1(*sys);
    CHECK(sys->selected_floppy_drive() == 1U);
    CHECK(sys->floppy_loaded(1U));
    CHECK(sys->floppy_cylinder(1U) == 0U);

    sys->cia_b.write(0x01U, 0x6CU); // Falling inward /STEP on DF1.
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK(sys->floppy_cylinder(1U) == 1U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);

    select_df0(*sys);
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) == 0U);

    sys->unmount_floppy(1U);
    select_df1(*sys);
    CHECK_FALSE(sys->floppy_loaded(1U));
    CHECK((sys->cia_a.read(0x00U) & 0x20U) != 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
}

TEST_CASE("amiga500 absent external floppy drives leave CIAA disk sense lines high",
          "[manifests][amiga500][disk][cia]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    select_df1(*sys);
    CHECK(sys->selected_floppy_drive() == 1U);
    CHECK_FALSE(sys->floppy_drives[1].connected);
    CHECK((sys->cia_a.read(0x00U) & 0x3CU) == 0x3CU);

    REQUIRE(sys->mount_floppy(1U, tiny_adf()));
    CHECK(sys->floppy_drives[1].connected);
    CHECK((sys->cia_a.read(0x00U) & 0x3CU) == 0x00U);

    sys->unmount_floppy(1U);
    CHECK(sys->floppy_drives[1].connected);
    CHECK((sys->cia_a.read(0x00U) & 0x3CU) == 0x38U);
}

TEST_CASE("amiga500 multidrive selection survives system save state",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(0U, tiny_adf()));
    REQUIRE(sys->mount_floppy(1U, tiny_adf()));

    select_df1(*sys);
    sys->cia_b.write(0x01U, 0x6CU);
    CHECK(sys->floppy_cylinder(1U) == 1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    select_df0(*sys);
    sys->cia_b.write(0x01U, 0x74U);
    CHECK(sys->floppy_cylinder(0U) == 1U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->selected_floppy_drive() == 1U);
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK(sys->floppy_cylinder(1U) == 1U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);
}

TEST_CASE("amiga500 CIAA disk change latch clears after a selected step",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    select_df0(*sys);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    REQUIRE(sys->mount_floppy(tiny_adf()));
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) == 0U);

    sys->cia_b.write(0x01U, 0x77U);
    sys->cia_b.write(0x01U, 0x76U); // outward /STEP at track 0 clears /CHNG without moving.
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
    CHECK(sys->floppy_cylinder() == 0U);

    sys->unmount_floppy();
    select_df0(*sys);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);
}

TEST_CASE("amiga500 Kickstart-style DF0 probe releases disk change after step",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    sys->cia_b.write(0x03U, 0xFFU);
    sys->cia_b.write(0x01U, 0xF7U); // DF0 selected, motor off, STEP high.
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    sys->cia_b.write(0x01U, 0xF6U); // Falling outward STEP at track 0 clears /CHNG.
    sys->cia_b.write(0x01U, 0xF7U);
    sys->cia_b.write(0x01U, 0xFFU);
    sys->cia_b.write(0x01U, 0xF5U);

    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
    CHECK(sys->floppy_cylinder() == 0U);
}

TEST_CASE("amiga500 simultaneous DF0 select and step releases disk change",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    sys->cia_b.write(0x03U, 0xFFU);
    sys->cia_b.write(0x01U, 0xF7U);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    sys->cia_b.write(0x01U, 0xFFU);
    sys->cia_b.write(0x01U, 0xF4U); // Select DF0 and assert inward /STEP in one write.
    sys->cia_b.write(0x01U, 0xF5U);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
    CHECK(sys->floppy_cylinder() == 1U);
}

TEST_CASE("amiga500 selected disk rotation pulses CIAB FLAG at index",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->cia_b.write(0x0DU, 0x90U); // Enable FLAG interrupt mask.
    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 1U);

    run_scanlines(*sys, lines_per_revolution - 1U);
    sys->cia_b.tick(1U);
    CHECK_FALSE(sys->cia_b.irq_asserted());
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) == 0U);

    run_scanlines(*sys, 1U);
    sys->cia_b.tick(1U);
    CHECK(sys->cia_b.irq_asserted());
    CHECK((sys->cia_b.read(0x0DU) & 0x90U) == 0x90U);

    run_scanlines(*sys, lines_per_revolution - 1U);
    sys->cia_b.tick(1U);
    CHECK_FALSE(sys->cia_b.irq_asserted());
}

TEST_CASE("amiga500 floppy index phase survives system save state", "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 1U);
    run_scanlines(*sys, lines_per_revolution - 1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    run_scanlines(*sys, 1U);
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) != 0U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    run_scanlines(*sys, 1U);
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) != 0U);
}

TEST_CASE("amiga500 vblank requests a level-3 interrupt source", "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_vertb));
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    sys->agnus.active_height());
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_vertb) == 0U);
    CHECK(sys->frame_index == 0U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    (agnus::scanlines_pal - sys->agnus.active_height()));

    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_vertb) != 0U);
    CHECK(sys->frame_index == 1U);
}

TEST_CASE("amiga500 custom MMIO keeps word writes atomic and byte writes lane-local",
          "[manifests][amiga500][interrupt]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t intena_addr = amiga_system::custom_base + 0x09AU;
    constexpr std::uint16_t enabled = amiga_system::int_master | amiga_system::int_exter |
                                      amiga_system::int_ports | amiga_system::int_soft |
                                      amiga_system::int_vertb;

    sys->bus.write16_be(intena_addr,
                        static_cast<std::uint16_t>(amiga_system::setclr_bit | enabled));
    CHECK(sys->read_custom_word(0x01CU) == enabled);

    sys->custom_high_latch = 0x4000U;
    sys->bus.write8(intena_addr + 1U, static_cast<std::uint8_t>(amiga_system::int_vertb));
    CHECK((sys->read_custom_word(0x01CU) & amiga_system::int_master) != 0U);
    CHECK((sys->read_custom_word(0x01CU) & amiga_system::int_vertb) == 0U);

    sys->bus.write8(intena_addr, 0x40U);
    CHECK((sys->read_custom_word(0x01CU) & amiga_system::int_master) == 0U);
    sys->bus.write8(intena_addr, 0xC0U);
    CHECK((sys->read_custom_word(0x01CU) & amiga_system::int_master) != 0U);
}

TEST_CASE("amiga500 visible INTREQ exposes CIA lines and INTENA gates their CPU level",
          "[manifests][amiga500][cia][interrupt]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t code_offset = 0x0100U;
    constexpr std::uint32_t level2_vector_offset = (24U + 2U) * 4U;
    constexpr std::uint32_t level2_handler = amiga_system::kickstart_base + 0x0200U;
    write_kickstart_word(*sys, code_offset, 0x4E71U); // NOP.
    write_kickstart_long(*sys, level2_vector_offset, level2_handler);
    write_kickstart_word(*sys, 0x0200U, 0x4E73U); // RTE if the handler is executed later.

    auto regs = sys->cpu.cpu_registers();
    regs.pc = amiga_system::kickstart_base + code_offset;
    regs.sr = mnemos::chips::cpu::m68000::sr_s;
    sys->cpu.set_registers(regs);

    sys->cia_a.write(0x0DU, 0x90U); // Enable FLAG mask.
    sys->cia_a.flag_edge();
    sys->cia_a.tick(1U);
    REQUIRE(sys->cia_a.irq_asserted());
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_ports) != 0U);

    sys->write_custom_word(
        0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit | amiga_system::int_master));
    sys->cpu.step_instruction();
    CHECK(sys->cpu.cpu_registers().pc == amiga_system::kickstart_base + code_offset + 2U);

    sys->write_custom_word(
        0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit | amiga_system::int_ports));
    sys->cpu.step_instruction();
    CHECK(sys->cpu.cpu_registers().pc == level2_handler);

    static_cast<void>(sys->cia_a.read(0x0DU));
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_ports) == 0U);

    sys->cia_b.write(0x0DU, 0x90U);
    sys->cia_b.flag_edge();
    sys->cia_b.tick(1U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_exter) != 0U);
}

TEST_CASE("amiga500 CIA-A Timer B one-shot reaches the custom PORTS interrupt",
          "[manifests][amiga500][cia][interrupt]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    mnemos::runtime::scheduler scheduler({{&sys->agnus, 2U},
                                          {&sys->cpu, 1U},
                                          {&sys->paula, 2U},
                                          {&sys->cia_a, 10U},
                                          {&sys->cia_b, 10U}},
                                         &sys->agnus);

    constexpr std::uint32_t ciaa = amiga_system::cia_a_base;
    sys->bus.write8(ciaa + 0x0D01U, 0x82U); // ICR: enable Timer B mask.
    sys->bus.write8(ciaa + 0x0F01U, 0x08U); // CRB: one-shot, START clear.
    sys->bus.write8(ciaa + 0x0601U, 0xFFU);
    sys->bus.write8(ciaa + 0x0701U, 0xFFU); // 8520 high-byte write starts one-shot.
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_ports));

    constexpr std::uint64_t one_shot_master_cycles = 65537ULL * 10ULL;
    scheduler.run_master_cycles(one_shot_master_cycles - 10ULL);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_ports) == 0U);

    scheduler.run_master_cycles(20ULL);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_ports) != 0U);
    CHECK((sys->bus.read8(ciaa + 0x0D01U) & 0x82U) == 0x82U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_ports) == 0U);
}

TEST_CASE("amiga500 Copper MOVEs update board-owned custom registers",
          "[manifests][amiga500][copper]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t list = 0x0100U;
    write_chip_word(*sys, list + 0U, 0x0180U); // COLOR00 = green backdrop.
    write_chip_word(*sys, list + 2U, 0x00F0U);
    write_chip_word(*sys, list + 4U, 0x009CU); // INTREQ = COPER.
    write_chip_word(*sys, list + 6U,
                    static_cast<std::uint16_t>(amiga_system::setclr_bit | amiga_system::int_coper));
    write_chip_word(*sys, list + 8U, 0xFFFFU);
    write_chip_word(*sys, list + 10U, 0xFFFEU);

    sys->write_custom_word(0x080U, 0x0000U); // COP1LCH
    sys->write_custom_word(0x082U, static_cast<std::uint16_t>(list));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_coper));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_copen));

    sys->agnus.tick(pal_vblank_exit_ticks + 8U);
    CHECK(sys->read_custom_word(0x180U) == 0x00F0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_coper) != 0U);

    sys->agnus.tick(
        (static_cast<std::uint64_t>(agnus::color_clocks_per_line) * agnus::scanlines_pal) -
        (pal_vblank_exit_ticks + sys->agnus.beam_clock()));
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("amiga500 Copper can program blitter registers with CDANG",
          "[manifests][amiga500][copper][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x1234U);

    constexpr std::uint32_t list = 0x0300U;
    write_chip_word(*sys, list + 0U, 0x0050U); // BLTAPTH
    write_chip_word(*sys, list + 2U, 0x0000U);
    write_chip_word(*sys, list + 4U, 0x0052U); // BLTAPTL
    write_chip_word(*sys, list + 6U, 0x0100U);
    write_chip_word(*sys, list + 8U, 0x0054U); // BLTDPTH
    write_chip_word(*sys, list + 10U, 0x0000U);
    write_chip_word(*sys, list + 12U, 0x0056U); // BLTDPTL
    write_chip_word(*sys, list + 14U, 0x0200U);
    write_chip_word(*sys, list + 16U, 0x0040U); // BLTCON0: USEA|USED, D=A.
    write_chip_word(*sys, list + 18U, 0x09F0U);
    write_chip_word(*sys, list + 20U, 0x0096U); // DMACON: enable blitter DMA.
    write_chip_word(*sys, list + 22U,
                    static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                               agnus::dmacon_blten));
    write_chip_word(*sys, list + 24U, 0x0058U); // BLTSIZE: one row, one word.
    write_chip_word(*sys, list + 26U, 0x0041U);
    write_chip_word(*sys, list + 28U, 0xFFFFU);
    write_chip_word(*sys, list + 30U, 0xFFFEU);

    sys->write_custom_word(0x02EU, 0x0002U); // CDANG opens $040-$07e blitter registers.
    sys->write_custom_word(0x080U, 0x0000U); // COP1LCH
    sys->write_custom_word(0x082U, static_cast<std::uint16_t>(list));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_copen));

    sys->agnus.tick(pal_vblank_exit_ticks + 40U);
    CHECK(sys->read_custom_word(0x040U) == 0x09F0U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x1234U);
    CHECK(sys->read_custom_word(0x076U) == 0x1234U);
}

TEST_CASE("amiga500 Copper BFD waits hold behind the live board blitter",
          "[manifests][amiga500][copper][blitter][timing]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t list = 0x0300U;
    write_chip_word(*sys, list + 0U, 0x0001U); // WAIT beam 0:0, BFD clear.
    write_chip_word(*sys, list + 2U, 0x0000U);
    write_chip_word(*sys, list + 4U, 0x0180U); // COLOR00 = red after the wait releases.
    write_chip_word(*sys, list + 6U, 0x0F00U);
    write_chip_word(*sys, list + 8U, 0xFFFFU);
    write_chip_word(*sys, list + 10U, 0xFFFEU);

    sys->write_custom_word(0x080U, 0x0000U);
    sys->write_custom_word(0x082U, static_cast<std::uint16_t>(list));
    sys->write_custom_word(
        0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                           agnus::dmacon_copen | agnus::dmacon_blten));
    sys->blitter_cycles_remaining = 2U;
    sys->agnus.set_blitter_busy(true);
    sys->write_custom_word(0x088U, 0x0000U); // COPJMP1.

    REQUIRE(sys->agnus.copper_pc() == list);
    sys->agnus.tick(1U);
    CHECK(sys->agnus.copper_pc() == list);
    CHECK(sys->read_custom_word(0x180U) == 0U);
    CHECK(sys->blitter_cycles_remaining == 1U);

    sys->agnus.tick(1U);
    CHECK(sys->agnus.copper_pc() == list);
    CHECK(sys->read_custom_word(0x180U) == 0U);
    CHECK(sys->blitter_cycles_remaining == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);

    sys->agnus.tick(1U);
    CHECK(sys->agnus.copper_pc() == list + 4U);
    CHECK(sys->read_custom_word(0x180U) == 0U);

    sys->agnus.tick(6U);
    CHECK(sys->read_custom_word(0x180U) == 0x0F00U);
}

TEST_CASE("amiga500 blitter copies A channel words to D through BLTSIZE",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x1234U);
    sys->write_custom_word(0x050U, 0x0000U); // BLTAPTH
    sys->write_custom_word(0x052U, 0x0100U); // BLTAPTL
    sys->write_custom_word(0x054U, 0x0000U); // BLTDPTH
    sys->write_custom_word(0x056U, 0x0200U); // BLTDPTL
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_blit));

    sys->write_custom_word(0x058U, 0x0041U); // one row, one word.

    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) == 0U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x1234U);
    CHECK(sys->read_custom_word(0x076U) == 0x1234U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) != 0U);
}

TEST_CASE("amiga500 blitter keeps BBUSY and BLIT IRQ timing across save state",
          "[manifests][amiga500][blitter][state]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x5678U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_blit));

    sys->write_custom_word(0x058U, 0x0041U);
    REQUIRE((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    sys->agnus.tick(1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->blitter_cycles_remaining = 0U;
    sys->agnus.set_blitter_busy(false);
    sys->write_custom_word(0x09CU, static_cast<std::uint16_t>(amiga_system::int_blit));

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) == 0U);

    sys->agnus.tick(1U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) != 0U);
    CHECK(read_chip_word(*sys, 0x0200U) == 0x5678U);
}

TEST_CASE("amiga500 display DMA stalls blitter busy countdown",
          "[manifests][amiga500][blitter][timing]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x100U, 0xC000U); // HIRES | BPU = 4.
    sys->write_custom_word(0x08EU, 0x2C81U);
    sys->write_custom_word(0x090U, 0xF4C1U);
    sys->write_custom_word(0x092U, 0x003CU);
    sys->write_custom_word(0x094U, 0x00D4U);
    sys->write_custom_word(
        0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                           agnus::dmacon_bplen | agnus::dmacon_blten));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                              amiga_system::int_master |
                                                              amiga_system::int_blit));

    constexpr std::uint32_t blocked_clock = 0x50U;
    constexpr std::uint32_t display_fetch_end = 0x3CU + 40U * 4U;
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                        agnus::display_line_origin +
                    blocked_clock);

    sys->blitter_cycles_remaining = 1U;
    sys->agnus.set_blitter_busy(true);

    sys->agnus.tick(1U);
    CHECK(sys->blitter_cycles_remaining == 1U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) == 0U);

    sys->agnus.tick(display_fetch_end - blocked_clock);
    CHECK(sys->blitter_cycles_remaining == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) != 0U);
}

TEST_CASE("amiga500 BLTPRI stalls CPU chip-RAM bus cycles while blitter DMA is busy",
          "[manifests][amiga500][blitter][timing]") {
    auto run_move_with_blitter_priority = [](bool blitter_priority) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0100U;
        constexpr std::uint32_t data_address = 0x0200U;
        write_kickstart_word(*sys, code_offset, 0x3010U); // MOVE.W (A0),D0.
        write_chip_word(*sys, data_address, 0x1234U);
        sys->overlay_active = false;

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        sys->write_custom_word(
            0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                               agnus::dmacon_blten |
                                               (blitter_priority ? agnus::dmacon_bltpri : 0U)));
        sys->write_custom_word(0x040U, 0x0100U); // D channel only.
        sys->write_custom_word(0x058U, static_cast<std::uint16_t>((3U << 6U) | 4U));
        const auto expected_wait = static_cast<std::uint32_t>(sys->blitter_cycles_remaining);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0x1234U);
        return std::array<std::uint32_t, 3>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles, expected_wait};
    };

    const auto normal = run_move_with_blitter_priority(false);
    const auto priority = run_move_with_blitter_priority(true);

    CHECK(normal[1] == 6U);
    CHECK(priority[1] == priority[2]);
    CHECK(priority[0] == normal[0] + (priority[2] - normal[1]));
}

TEST_CASE("amiga500 BLTPRI waits behind display-owned DMA slots",
          "[manifests][amiga500][blitter][timing]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->overlay_active = false;
    sys->write_custom_word(0x100U, 0x6000U); // Six low-resolution bitplanes.
    sys->write_custom_word(0x08EU, 0x2C81U);
    sys->write_custom_word(0x090U, 0xF4C1U);
    sys->write_custom_word(0x092U, 0x0038U);
    sys->write_custom_word(0x094U, 0x00D0U);

    constexpr std::uint32_t blocked_clock = 0x39U; // DDFSTRT+1: first CPU-open slot steal.
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                        agnus::display_line_origin +
                    blocked_clock);

    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_bplen |
                                                      agnus::dmacon_blten | agnus::dmacon_bltpri));
    sys->write_custom_word(0x040U, 0x0100U); // D channel only.
    sys->write_custom_word(0x058U, static_cast<std::uint16_t>((1U << 6U) | 8U));

    REQUIRE(sys->blitter_cycles_remaining == 8U);
    CHECK(sys->agnus.display_dma_cpu_wait_cycles(0U) == 4U);
    CHECK(sys->cpu_bus_wait_cycles(0x0200U, false, false, 0U, 0U) == 12U);
}

TEST_CASE("amiga500 BLTPRI charges one lockout across CPU longword chip-RAM transfers",
          "[manifests][amiga500][blitter][timing]") {
    auto run_move_long_with_blitter_priority = [](bool blitter_priority) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0110U;
        constexpr std::uint32_t data_address = 0x0220U;
        write_kickstart_word(*sys, code_offset, 0x2010U); // MOVE.L (A0),D0.
        write_chip_word(*sys, data_address, 0xDEADU);
        write_chip_word(*sys, data_address + 2U, 0xBEEFU);
        sys->overlay_active = false;

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        sys->write_custom_word(
            0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                               agnus::dmacon_blten |
                                               (blitter_priority ? agnus::dmacon_bltpri : 0U)));
        sys->write_custom_word(0x040U, 0x0100U); // D channel only.
        sys->write_custom_word(0x058U, static_cast<std::uint16_t>((5U << 6U) | 4U));
        const auto expected_wait = static_cast<std::uint32_t>(sys->blitter_cycles_remaining);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0xDEADBEEFU);
        return std::array<std::uint32_t, 3>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles, expected_wait};
    };

    const auto normal = run_move_long_with_blitter_priority(false);
    const auto priority = run_move_long_with_blitter_priority(true);

    CHECK(normal[1] == 6U);
    CHECK(priority[1] == priority[2]);
    CHECK(priority[0] == normal[0] + (priority[2] - normal[1]));
}

TEST_CASE("amiga500 high-resolution display DMA stalls CPU chip-RAM bus cycles during fetch",
          "[manifests][amiga500][video][timing]") {
    auto run_move_during_display_fetch = [](std::uint16_t bplcon0, bool blitter_busy = false,
                                            std::uint16_t bplcon1 = 0U) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0120U;
        constexpr std::uint32_t data_address = 0x0240U;
        constexpr std::uint32_t fetch_clock = 0x50U;
        write_kickstart_word(*sys, code_offset, 0x3010U); // MOVE.W (A0),D0.
        write_chip_word(*sys, data_address, 0xCAFEU);
        sys->overlay_active = false;

        sys->write_custom_word(0x100U, bplcon0);
        sys->write_custom_word(0x102U, bplcon1);
        sys->write_custom_word(0x08EU, 0x2C81U);
        sys->write_custom_word(0x090U, 0xF4C1U);
        sys->write_custom_word(0x092U, 0x003CU);
        sys->write_custom_word(0x094U, 0x00D4U);
        sys->write_custom_word(0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                                  agnus::dmacon_dmaen |
                                                                  agnus::dmacon_bplen));
        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            agnus::display_line_origin +
                        fetch_clock);
        if (blitter_busy) {
            sys->write_custom_word(
                0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_blten));
            sys->write_custom_word(0x040U, 0x0100U); // D channel only.
            sys->write_custom_word(0x058U, static_cast<std::uint16_t>((5U << 6U) | 4U));
        }

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0xCAFEU);
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    constexpr std::uint32_t ddf_start = 0x3CU;
    constexpr std::uint32_t words_per_hires_line = 40U;
    constexpr std::uint32_t clocks_per_hires_word = 4U;
    constexpr std::uint32_t operand_fetch_delay = 2U;
    constexpr std::uint32_t fetch_clock = 0x50U;
    constexpr std::uint32_t expected_wait =
        ((ddf_start + words_per_hires_line * clocks_per_hires_word) -
         (fetch_clock + operand_fetch_delay)) *
        2U;

    const auto one_plane = run_move_during_display_fetch(0x9000U);   // HIRES | BPU = 1.
    const auto four_planes = run_move_during_display_fetch(0xC000U); // HIRES | BPU = 4.
    const auto one_plane_blitter = run_move_during_display_fetch(0x9000U, true);
    const auto four_planes_blitter = run_move_during_display_fetch(0xC000U, true);
    const auto four_planes_pf1_scroll = run_move_during_display_fetch(0xC000U, false, 0x0001U);
    const auto four_planes_both_scroll = run_move_during_display_fetch(0xC000U, false, 0x0011U);

    CHECK(one_plane[1] == 0U);
    CHECK(four_planes[1] == expected_wait);
    CHECK(one_plane_blitter[1] == 6U);
    CHECK(four_planes_blitter[1] == expected_wait);
    CHECK(four_planes_pf1_scroll[1] == expected_wait);
    CHECK(four_planes_both_scroll[1] == expected_wait);
    CHECK(four_planes[0] == one_plane[0] + expected_wait);
    CHECK(one_plane_blitter[0] == one_plane[0] + 6U);
    CHECK(four_planes_blitter[0] == four_planes[0]);
    CHECK(four_planes_pf1_scroll[0] == four_planes[0]);
    CHECK(four_planes_both_scroll[0] == four_planes[0]);
}

TEST_CASE("amiga500 high-resolution display DMA charges one lockout across longword transfers",
          "[manifests][amiga500][video][timing]") {
    auto run_move_long_during_display_fetch = [](std::uint16_t bplcon0) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0140U;
        constexpr std::uint32_t data_address = 0x0280U;
        constexpr std::uint32_t fetch_clock = 0x50U;
        write_kickstart_word(*sys, code_offset, 0x2010U); // MOVE.L (A0),D0.
        write_chip_word(*sys, data_address, 0xCAFEU);
        write_chip_word(*sys, data_address + 2U, 0x1234U);
        sys->overlay_active = false;

        sys->write_custom_word(0x100U, bplcon0);
        sys->write_custom_word(0x08EU, 0x2C81U);
        sys->write_custom_word(0x090U, 0xF4C1U);
        sys->write_custom_word(0x092U, 0x003CU);
        sys->write_custom_word(0x094U, 0x00D4U);
        sys->write_custom_word(0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                                  agnus::dmacon_dmaen |
                                                                  agnus::dmacon_bplen));
        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            agnus::display_line_origin +
                        fetch_clock);

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0xCAFE1234U);
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    constexpr std::uint32_t ddf_start = 0x3CU;
    constexpr std::uint32_t words_per_hires_line = 40U;
    constexpr std::uint32_t clocks_per_hires_word = 4U;
    constexpr std::uint32_t operand_fetch_delay = 2U;
    constexpr std::uint32_t fetch_clock = 0x50U;
    constexpr std::uint32_t expected_wait =
        ((ddf_start + words_per_hires_line * clocks_per_hires_word) -
         (fetch_clock + operand_fetch_delay)) *
        2U;

    const auto one_plane = run_move_long_during_display_fetch(0x9000U);   // HIRES | BPU = 1.
    const auto four_planes = run_move_long_during_display_fetch(0xC000U); // HIRES | BPU = 4.

    CHECK(one_plane[1] == 0U);
    CHECK(four_planes[1] == expected_wait);
    CHECK(four_planes[0] == one_plane[0] + expected_wait);
}

TEST_CASE("amiga500 display DMA wait is already residual after prior instruction waits",
          "[manifests][amiga500][video][timing]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t data_address = 0x0280U;
    constexpr std::uint32_t fetch_clock = 0x50U;
    constexpr std::uint32_t instruction_cycles_before_access = 10U;
    constexpr std::uint32_t prior_external_wait = 6U;
    sys->overlay_active = false;

    sys->write_custom_word(0x100U, 0xC000U); // HIRES | BPU = 4.
    sys->write_custom_word(0x08EU, 0x2C81U);
    sys->write_custom_word(0x090U, 0xF4C1U);
    sys->write_custom_word(0x092U, 0x003CU);
    sys->write_custom_word(0x094U, 0x00D4U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_bplen));
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                        agnus::display_line_origin +
                    fetch_clock);

    constexpr std::uint32_t ddf_start = 0x3CU;
    constexpr std::uint32_t words_per_hires_line = 40U;
    constexpr std::uint32_t clocks_per_hires_word = 4U;
    constexpr std::uint32_t projected_clock = fetch_clock + instruction_cycles_before_access / 2U;
    constexpr std::uint32_t expected_wait =
        ((ddf_start + words_per_hires_line * clocks_per_hires_word) - projected_clock) * 2U;

    CHECK(sys->cpu_bus_wait_cycles(data_address, false, false, instruction_cycles_before_access,
                                   prior_external_wait) == expected_wait);
}

TEST_CASE("amiga500 six-plane low-resolution display DMA steals CPU-open slots",
          "[manifests][amiga500][video][timing]") {
    auto run_move_during_display_fetch = [](std::uint16_t bplcon0, std::uint16_t opcode,
                                            std::uint32_t fetch_clock, bool blitter_busy = false,
                                            std::uint16_t bplcon1 = 0U) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0160U;
        constexpr std::uint32_t data_address = 0x02C0U;
        write_kickstart_word(*sys, code_offset, opcode);
        write_chip_word(*sys, data_address, 0x1357U);
        write_chip_word(*sys, data_address + 2U, 0x9BDFU);
        sys->overlay_active = false;

        sys->write_custom_word(0x100U, bplcon0);
        sys->write_custom_word(0x102U, bplcon1);
        sys->write_custom_word(0x08EU, 0x2C81U);
        sys->write_custom_word(0x090U, 0xF4C1U);
        sys->write_custom_word(0x092U, 0x0038U);
        sys->write_custom_word(0x094U, 0x00D0U);
        sys->write_custom_word(0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                                  agnus::dmacon_dmaen |
                                                                  agnus::dmacon_bplen));
        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            agnus::display_line_origin +
                        fetch_clock);
        if (blitter_busy) {
            sys->write_custom_word(
                0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_blten));
            sys->write_custom_word(0x040U, 0x0100U); // D channel only.
            sys->write_custom_word(0x058U, static_cast<std::uint16_t>((5U << 6U) | 4U));
        }

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK((opcode == 0x2010U ? sys->cpu.cpu_registers().d[0] == 0x13579BDFU
                                 : sys->cpu.cpu_registers().d[0] == 0x1357U));
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    const auto four_planes = run_move_during_display_fetch(0x4000U, 0x3010U, 0x37U);
    const auto four_planes_longword = run_move_during_display_fetch(0x4000U, 0x2010U, 0x37U);
    const auto five_planes = run_move_during_display_fetch(0x5000U, 0x3010U, 0x3BU);
    const auto five_planes_second_clock = run_move_during_display_fetch(0x5000U, 0x3010U, 0x3CU);
    const auto six_planes = run_move_during_display_fetch(0x6000U, 0x3010U, 0x37U);
    const auto six_planes_second_clock = run_move_during_display_fetch(0x6000U, 0x3010U, 0x38U);
    const auto six_planes_longword = run_move_during_display_fetch(0x6000U, 0x2010U, 0x37U);
    const auto six_planes_blitter = run_move_during_display_fetch(0x6000U, 0x3010U, 0x37U, true);
    const auto six_planes_tail_no_scroll = run_move_during_display_fetch(0x6000U, 0x3010U, 0xD7U);
    const auto six_planes_tail_pf1_scroll =
        run_move_during_display_fetch(0x6000U, 0x3010U, 0xD7U, false, 0x0001U);
    const auto six_planes_tail_both_scroll =
        run_move_during_display_fetch(0x6000U, 0x3010U, 0xD7U, false, 0x0011U);

    CHECK(four_planes[1] == 0U);
    CHECK(four_planes_longword[1] == 0U);
    CHECK(five_planes[1] == 4U);
    CHECK(five_planes_second_clock[1] == 2U);
    CHECK(six_planes[1] == 4U);
    CHECK(six_planes_second_clock[1] == 2U);
    CHECK(six_planes_longword[1] == 8U);
    CHECK(six_planes_blitter[1] == 6U);
    CHECK(six_planes_tail_no_scroll[1] == 0U);
    CHECK(six_planes_tail_pf1_scroll[1] == 0U);
    CHECK(six_planes_tail_both_scroll[1] == 0U);
    CHECK(five_planes[0] == four_planes[0] + 4U);
    CHECK(five_planes_second_clock[0] == four_planes[0] + 2U);
    CHECK(six_planes[0] == four_planes[0] + 4U);
    CHECK(six_planes_second_clock[0] == four_planes[0] + 2U);
    CHECK(six_planes_longword[0] == four_planes_longword[0] + 8U);
    CHECK(six_planes_blitter[0] == four_planes[0] + 6U);
    CHECK(six_planes_tail_pf1_scroll[0] == six_planes_tail_no_scroll[0]);
    CHECK(six_planes_tail_both_scroll[0] == six_planes_tail_no_scroll[0]);
}

TEST_CASE("amiga500 sprite DMA stalls CPU chip-RAM bus cycles during sprite slots",
          "[manifests][amiga500][video][timing]") {
    auto run_move_during_sprite_slot = [](bool sprite_dma_enabled, std::uint32_t fetch_clock,
                                          std::uint32_t visible_line = 0U) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x01A0U;
        constexpr std::uint32_t data_address = 0x0340U;
        constexpr std::uint32_t sprite_base = 0x0400U;
        write_kickstart_word(*sys, code_offset, 0x3010U); // MOVE.W (A0),D0.
        write_chip_word(*sys, data_address, 0x2468U);
        sys->overlay_active = false;

        write_chip_word(*sys, sprite_base + 0U, 0x2C40U); // SPR0POS: line 0, x 0.
        write_chip_word(*sys, sprite_base + 2U, 0x2E00U); // Two visible lines.
        write_chip_word(*sys, sprite_base + 4U, 0x8000U);
        write_chip_word(*sys, sprite_base + 6U, 0x0000U);
        write_chip_word(*sys, sprite_base + 8U, 0x0000U);
        write_chip_word(*sys, sprite_base + 10U, 0x0000U);
        sys->write_custom_word(0x120U, 0x0000U);
        sys->write_custom_word(0x122U, static_cast<std::uint16_t>(sprite_base));
        sys->write_custom_word(
            0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit | agnus::dmacon_dmaen |
                                               (sprite_dma_enabled ? agnus::dmacon_spren : 0U)));

        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            (agnus::display_line_origin + visible_line) +
                        fetch_clock);

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0x2468U);
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    constexpr std::uint32_t first_sprite_slot_probe = 0x16U;
    constexpr std::uint32_t second_sprite_slot_probe = 0x17U;
    const auto disabled = run_move_during_sprite_slot(false, first_sprite_slot_probe);
    const auto first_slot = run_move_during_sprite_slot(true, first_sprite_slot_probe);
    const auto second_slot = run_move_during_sprite_slot(true, second_sprite_slot_probe);
    const auto stop_line_disabled = run_move_during_sprite_slot(false, first_sprite_slot_probe, 2U);
    const auto stop_line = run_move_during_sprite_slot(true, first_sprite_slot_probe, 2U);

    CHECK(disabled[1] == 0U);
    CHECK(first_slot[1] == 4U);
    CHECK(second_slot[1] == 2U);
    CHECK(stop_line_disabled[1] == 0U);
    CHECK(stop_line[1] == 4U);
    CHECK(first_slot[0] == disabled[0] + 4U);
    CHECK(second_slot[0] == disabled[0] + 2U);
    CHECK(stop_line[0] == stop_line_disabled[0] + 4U);
}

TEST_CASE("amiga500 three-plane high-resolution display DMA steals CPU-open slots",
          "[manifests][amiga500][video][timing]") {
    auto run_move_during_display_fetch = [](std::uint16_t bplcon0, std::uint16_t opcode,
                                            std::uint32_t fetch_clock = 0x3CU) {
        auto sys = assemble_amiga(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0180U;
        constexpr std::uint32_t data_address = 0x02E0U;
        write_kickstart_word(*sys, code_offset, opcode);
        write_chip_word(*sys, data_address, 0x2468U);
        write_chip_word(*sys, data_address + 2U, 0xACE0U);
        sys->overlay_active = false;

        sys->write_custom_word(0x100U, bplcon0);
        sys->write_custom_word(0x08EU, 0x2C81U);
        sys->write_custom_word(0x090U, 0xF4C1U);
        sys->write_custom_word(0x092U, 0x003CU);
        sys->write_custom_word(0x094U, 0x00D4U);
        sys->write_custom_word(0x096U, static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                                  agnus::dmacon_dmaen |
                                                                  agnus::dmacon_bplen));
        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            agnus::display_line_origin +
                        fetch_clock);

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK((opcode == 0x2010U ? sys->cpu.cpu_registers().d[0] == 0x2468ACE0U
                                 : sys->cpu.cpu_registers().d[0] == 0x2468U));
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    const auto two_planes = run_move_during_display_fetch(0xA000U, 0x3010U);
    const auto two_planes_longword = run_move_during_display_fetch(0xA000U, 0x2010U);
    const auto three_planes = run_move_during_display_fetch(0xB000U, 0x3010U);
    const auto three_planes_second_clock = run_move_during_display_fetch(0xB000U, 0x3010U, 0x3DU);
    const auto three_planes_longword = run_move_during_display_fetch(0xB000U, 0x2010U);

    CHECK(two_planes[1] == 0U);
    CHECK(two_planes_longword[1] == 0U);
    CHECK(three_planes[1] == 4U);
    CHECK(three_planes_second_clock[1] == 2U);
    CHECK(three_planes_longword[1] == 8U);
    CHECK(three_planes[0] == two_planes[0] + 4U);
    CHECK(three_planes_second_clock[0] == two_planes[0] + 2U);
    CHECK(three_planes_longword[0] == two_planes_longword[0] + 8U);
}

TEST_CASE("amiga500 blitter applies first-word masks and minterms",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0xFFFFU); // A mask source, restricted by BLTAFWM.
    write_chip_word(*sys, 0x0110U, 0xAAAAU); // B foreground.
    write_chip_word(*sys, 0x0120U, 0x5555U); // C/D background.
    sys->write_custom_word(0x044U, 0x00FFU); // BLTAFWM
    sys->write_custom_word(0x046U, 0xFFFFU); // BLTALWM
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x04CU, 0x0000U);
    sys->write_custom_word(0x04EU, 0x0110U);
    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0120U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0120U);
    sys->write_custom_word(0x040U, 0x0FCAU); // USEA|USEB|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0120U) == 0x55AAU);
    CHECK(sys->paula.chipram()[0x0120U] == 0x55U);
    CHECK(sys->paula.chipram()[0x0121U] == 0xAAU);
}

TEST_CASE("amiga500 blitter decodes Kickstart split-half MFM sectors",
          "[manifests][amiga500][blitter][disk]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t source = 0x0400U;
    constexpr std::uint32_t destination = 0x1000U;
    constexpr std::size_t bytes = amiga_system::floppy_sector_size;
    constexpr std::size_t raw_longs = bytes / 4U;

    std::array<std::uint8_t, bytes> expected{};
    expected[0] = 'D';
    expected[1] = 'O';
    expected[2] = 'S';
    expected[3] = 0x00U;
    for (std::size_t i = 4U; i < expected.size(); ++i) {
        expected[i] = static_cast<std::uint8_t>((i * 37U + 11U) & 0xFFU);
    }

    for (std::size_t i = 0U; i < raw_longs; ++i) {
        const std::size_t src = i * 4U;
        const auto src_address = static_cast<std::uint32_t>(src);
        const std::uint32_t raw = (static_cast<std::uint32_t>(expected[src + 0U]) << 24U) |
                                  (static_cast<std::uint32_t>(expected[src + 1U]) << 16U) |
                                  (static_cast<std::uint32_t>(expected[src + 2U]) << 8U) |
                                  static_cast<std::uint32_t>(expected[src + 3U]);
        const auto encoded = encode_mfm_odd_even(raw);
        write_chip_word(*sys, source + src_address + 0U,
                        static_cast<std::uint16_t>(encoded[0] >> 16U));
        write_chip_word(*sys, source + src_address + 2U,
                        static_cast<std::uint16_t>(encoded[0]));
        write_chip_word(*sys, source + static_cast<std::uint32_t>(bytes) + src_address + 0U,
                        static_cast<std::uint16_t>(encoded[1] >> 16U));
        write_chip_word(*sys, source + static_cast<std::uint32_t>(bytes) + src_address + 2U,
                        static_cast<std::uint16_t>(encoded[1]));
    }

    sys->write_custom_word(0x044U, 0xFFFFU);
    sys->write_custom_word(0x046U, 0xFFFFU);
    sys->write_custom_word(0x060U, 0x0000U);
    sys->write_custom_word(0x062U, 0x0000U);
    sys->write_custom_word(0x064U, 0x0000U);
    sys->write_custom_word(0x066U, 0x0000U);
    sys->write_custom_word(0x070U, 0x5555U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, static_cast<std::uint16_t>(source + bytes - 1U));
    sys->write_custom_word(0x04CU, 0x0000U);
    sys->write_custom_word(0x04EU, static_cast<std::uint16_t>(source + bytes * 2U - 1U));
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, static_cast<std::uint16_t>(destination + bytes - 1U));
    sys->write_custom_word(0x040U, 0x1DD8U);
    sys->write_custom_word(0x042U, 0x0002U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0808U);
    run_blitter_to_idle(*sys);

    CHECK(std::equal(expected.begin(), expected.end(), sys->chip_ram.begin() + destination));
    CHECK(std::equal(expected.begin(), expected.end(), sys->paula.chipram().begin() + destination));
}

TEST_CASE("amiga500 blitter performs inclusive area fill from the right edge",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x2418U);
    sys->write_custom_word(0x044U, 0xFFFFU); // Fill mode keeps A first/last masks open.
    sys->write_custom_word(0x046U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x000AU); // IFE|DESC: inclusive fill scans right-to-left.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x3C18U);
    CHECK(sys->read_custom_word(0x076U) == 0x3C18U);
}

TEST_CASE("amiga500 blitter performs exclusive area fill from the right edge",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x2418U);
    sys->write_custom_word(0x044U, 0xFFFFU);
    sys->write_custom_word(0x046U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x0012U); // EFE|DESC: exclusive fill scans right-to-left.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x1C08U);
    CHECK(sys->read_custom_word(0x076U) == 0x1C08U);
}

TEST_CASE("amiga500 blitter area fill honors fill carry input", "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x0000U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x0016U); // EFE|FCI|DESC.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xFFFFU);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
}

TEST_CASE("amiga500 blitter masks latched A when A DMA is disabled",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x0000U);
    write_chip_word(*sys, 0x0102U, 0x0000U);
    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0100U);
    sys->write_custom_word(0x044U, 0x00FFU);
    sys->write_custom_word(0x046U, 0xF000U);
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x074U, 0xFFFFU);
    sys->write_custom_word(0x040U, 0x03CAU); // USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0000U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0042U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0100U) == 0x00FFU);
    CHECK(read_chip_word(*sys, 0x0102U) == 0xF000U);
}

TEST_CASE("amiga500 blitter line mode draws a shallow octant line",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U); // BLTCPTH
    sys->write_custom_word(0x04AU, 0x0200U); // BLTCPTL
    sys->write_custom_word(0x054U, 0x0000U); // BLTDPTH
    sys->write_custom_word(0x056U, 0x0200U); // BLTDPTL
    sys->write_custom_word(0x050U, 0x0000U); // BLTAPTH
    sys->write_custom_word(0x052U, 0xFFFEU); // 4 * dy - 2 * dx = -2.
    sys->write_custom_word(0x064U, 0xFFF4U); // BLTAMOD = 4 * (dy - dx).
    sys->write_custom_word(0x062U, 0x0008U); // BLTBMOD = 4 * dy.
    sys->write_custom_word(0x060U, 0x0020U); // BLTCMOD = row stride.
    sys->write_custom_word(0x066U, 0x0020U); // BLTDMOD = row stride.
    sys->write_custom_word(0x072U, 0xFFFFU); // Solid texture.
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0051U); // SIGN|SUD|LINE: shallow right/down.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0182U); // width=2, length=6 dots.
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xC000U);
    CHECK(read_chip_word(*sys, 0x0220U) == 0x3000U);
    CHECK(read_chip_word(*sys, 0x0240U) == 0x0C00U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga_system::int_blit) != 0U);
}

TEST_CASE("amiga500 blitter line mode steps destination rows with C modulo",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U); // BLTCPTH
    sys->write_custom_word(0x04AU, 0x0200U); // BLTCPTL
    sys->write_custom_word(0x054U, 0x0000U); // BLTDPTH
    sys->write_custom_word(0x056U, 0x0200U); // BLTDPTL
    sys->write_custom_word(0x050U, 0x0000U); // BLTAPTH
    sys->write_custom_word(0x052U, 0xFFFEU);
    sys->write_custom_word(0x064U, 0xFFF4U);
    sys->write_custom_word(0x062U, 0x0008U);
    sys->write_custom_word(0x060U, 0x0020U);
    sys->write_custom_word(0x066U, 0x0000U); // Kickstart leaves BLTDMOD unused in line mode.
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x040U, 0x0BCAU);
    sys->write_custom_word(0x042U, 0x0051U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0182U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xC000U);
    CHECK(read_chip_word(*sys, 0x0220U) == 0x3000U);
    CHECK(read_chip_word(*sys, 0x0240U) == 0x0C00U);
}

TEST_CASE("amiga500 blitter line mode draws a steep octant line",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFEU);
    sys->write_custom_word(0x064U, 0xFFF4U);
    sys->write_custom_word(0x062U, 0x0008U);
    sys->write_custom_word(0x060U, 0x0020U);
    sys->write_custom_word(0x066U, 0x0020U);
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0041U); // SIGN|LINE: steep right/down.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0182U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x8000U);
    CHECK(read_chip_word(*sys, 0x0220U) == 0x8000U);
    CHECK(read_chip_word(*sys, 0x0240U) == 0x4000U);
    CHECK(read_chip_word(*sys, 0x0260U) == 0x4000U);
    CHECK(read_chip_word(*sys, 0x0280U) == 0x2000U);
    CHECK(read_chip_word(*sys, 0x02A0U) == 0x2000U);
}

TEST_CASE("amiga500 blitter line mode applies texture start and SING",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFAU); // 4 * 0 - 2 * 3 = -6.
    sys->write_custom_word(0x064U, 0xFFF4U);
    sys->write_custom_word(0x062U, 0x0000U);
    sys->write_custom_word(0x060U, 0x0020U);
    sys->write_custom_word(0x066U, 0x0020U);
    sys->write_custom_word(0x072U, 0x4001U); // Texture bits 0 and 14 are set.
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0051U); // SIGN|SUD|LINE.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0102U); // Horizontal line, four dots.
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xA000U);

    write_chip_word(*sys, 0x0200U, 0x0000U);
    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFAU);
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x042U, 0x0053U); // SIGN|SUD|SING|LINE.

    sys->write_custom_word(0x058U, 0x0102U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x8000U);
}

TEST_CASE("amiga500 blitter zero result updates DMACONR BZERO", "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0200U, 0xFFFFU);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x0100U); // USED, minterm 0 => clear destination.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x0000U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) != 0U);
}

TEST_CASE("amiga500 custom sprite registers render through Agnus", "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x1A2U, 0x0F00U); // COLOR17 = red
    sys->write_custom_word(0x1A4U, 0x00F0U); // COLOR18 = green
    sys->write_custom_word(0x140U, sprite_pos_word(0U, 0U));
    sys->write_custom_word(0x142U, sprite_ctl_word(0U, 0U, 1U));
    sys->write_custom_word(0x146U, 0x4000U); // SPR0DATB: pixel 1 high bit.
    sys->write_custom_word(0x144U, 0x8000U); // SPR0DATA: pixel 0 low bit, arm.

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x0000FF00U);
}

TEST_CASE("amiga500 byte writes to custom sprite registers preserve the companion byte",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x1A2U, 0x0F00U); // COLOR17 = red.
    const std::uint16_t pos = sprite_pos_word(0U, 0U);
    const std::uint16_t ctl = sprite_ctl_word(0U, 0U, 1U);
    sys->write_custom_byte(amiga_system::custom_base + 0x140U, static_cast<std::uint8_t>(pos >> 8U));
    sys->write_custom_byte(amiga_system::custom_base + 0x141U, static_cast<std::uint8_t>(pos));
    sys->write_custom_byte(amiga_system::custom_base + 0x142U, static_cast<std::uint8_t>(ctl >> 8U));
    sys->write_custom_byte(amiga_system::custom_base + 0x143U, static_cast<std::uint8_t>(ctl));
    sys->write_custom_byte(amiga_system::custom_base + 0x144U, 0x80U);
    sys->write_custom_byte(amiga_system::custom_base + 0x145U, 0x00U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);

    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("amiga500 sprites stay low-resolution on high-resolution custom display",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x100U, 0x8000U); // HIRES with no bitplanes.
    sys->write_custom_word(0x1A2U, 0x0F00U); // COLOR17 = red.
    sys->write_custom_word(0x1A4U, 0x00F0U); // COLOR18 = green.
    sys->write_custom_word(0x140U, sprite_pos_word(0U, 1U));
    sys->write_custom_word(0x142U, sprite_ctl_word(0U, 1U, 1U));
    sys->write_custom_word(0x146U, 0x4000U); // SPR0DATB: second sprite pixel.
    sys->write_custom_word(0x144U, 0x8000U); // SPR0DATA: first sprite pixel, arm.

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.pixels[0] == 0x00000000U);
    CHECK(frame.pixels[1] == 0x00000000U);
    CHECK(frame.pixels[2] == 0x00FF0000U);
    CHECK(frame.pixels[3] == 0x00FF0000U);
    CHECK(frame.pixels[4] == 0x0000FF00U);
    CHECK(frame.pixels[5] == 0x0000FF00U);
}

TEST_CASE("amiga500 sprite DMA pointers advance until custom registers rewrite them",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t sprite_base = 0x0400U;
    write_chip_word(*sys, sprite_base + 0U, sprite_pos_word(0U, 0U));
    write_chip_word(*sys, sprite_base + 2U, sprite_ctl_word(0U, 0U, 1U));
    write_chip_word(*sys, sprite_base + 4U, 0x8000U);
    write_chip_word(*sys, sprite_base + 6U, 0x0000U);
    write_chip_word(*sys, sprite_base + 8U, 0x0000U);
    write_chip_word(*sys, sprite_base + 10U, 0x0000U);

    sys->write_custom_word(0x1A2U, 0x0F00U);                                 // COLOR17 = red.
    sys->write_custom_word(0x120U, 0x0000U);                                 // SPR0PTH.
    sys->write_custom_word(0x122U, static_cast<std::uint16_t>(sprite_base)); // SPR0PTL.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_spren));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00FF0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00000000U);

    sys->write_custom_word(0x120U, 0x0000U);
    sys->write_custom_word(0x122U, static_cast<std::uint16_t>(sprite_base));
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("amiga500 sprite DMA descriptors carry attached-pair palette state",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t sprite0_base = 0x0400U;
    constexpr std::uint32_t sprite1_base = 0x0500U;
    write_chip_word(*sys, sprite0_base + 0U, sprite_pos_word(0U, 0U));
    write_chip_word(*sys, sprite0_base + 2U, sprite_ctl_word(0U, 0U, 1U));
    write_chip_word(*sys, sprite0_base + 4U, 0x8000U); // Low attached bits = 1.
    write_chip_word(*sys, sprite0_base + 6U, 0x0000U);
    write_chip_word(*sys, sprite0_base + 8U, 0x0000U);
    write_chip_word(*sys, sprite0_base + 10U, 0x0000U);

    write_chip_word(*sys, sprite1_base + 0U, sprite_pos_word(0U, 0U));
    write_chip_word(*sys, sprite1_base + 2U,
                    static_cast<std::uint16_t>(sprite_ctl_word(0U, 0U, 1U) | 0x0080U));
    write_chip_word(*sys, sprite1_base + 4U, 0x0000U);
    write_chip_word(*sys, sprite1_base + 6U, 0x8000U); // High attached bits = 2.
    write_chip_word(*sys, sprite1_base + 8U, 0x0000U);
    write_chip_word(*sys, sprite1_base + 10U, 0x0000U);

    sys->write_custom_word(0x1A2U, 0x0F00U); // COLOR17 = red if not attached.
    sys->write_custom_word(0x1B2U, 0x000FU); // COLOR25 = blue if attached value 9.
    sys->write_custom_word(0x120U, 0x0000U); // SPR0PTH.
    sys->write_custom_word(0x122U, static_cast<std::uint16_t>(sprite0_base));
    sys->write_custom_word(0x124U, 0x0000U); // SPR1PTH.
    sys->write_custom_word(0x126U, static_cast<std::uint16_t>(sprite1_base));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_spren));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);

    CHECK(sys->agnus.framebuffer().pixels[0] == 0x000000FFU);
}

TEST_CASE("amiga500 bitplane DMA pointers advance until custom registers rewrite them",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0000U, 0x8000U);
    program_one_plane_display(*sys);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00FF0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x000000FFU);

    sys->write_custom_word(0x0E0U, 0x0000U); // BPL1PTH.
    sys->write_custom_word(0x0E2U, 0x0000U); // BPL1PTL.
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("amiga500 BPLCON2 custom priority places sprites ahead of a playfield",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000.
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x1A2U, 0x00F0U); // COLOR17 = green sprite.
    sys->write_custom_word(0x104U, 0x0020U); // PF2 priority slot 4: behind sprites.
    sys->write_custom_word(0x140U, sprite_pos_word(0U, 0U));
    sys->write_custom_word(0x142U, sprite_ctl_word(0U, 0U, 1U));
    sys->write_custom_word(0x144U, 0x8000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x0000FF00U);
}

TEST_CASE("amiga500 CLXCON and CLXDAT expose OCS collision latches",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000.
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x098U, 0x0041U); // Include BPL1 and require BPL1=1.
    sys->write_custom_word(0x140U, sprite_pos_word(0U, 0U)); // SPR0 over first playfield pixel.
    sys->write_custom_word(0x142U, sprite_ctl_word(0U, 0U, 1U));
    sys->write_custom_word(0x144U, 0x8000U);
    sys->write_custom_word(0x150U, sprite_pos_word(0U, 0U)); // SPR2 overlaps SPR0.
    sys->write_custom_word(0x152U, sprite_ctl_word(0U, 0U, 1U));
    sys->write_custom_word(0x154U, 0x8000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const std::uint16_t first = sys->read_custom_word(0x00EU);

    CHECK((first & 0x0002U) != 0U);
    CHECK((first & 0x0004U) != 0U);
    CHECK((first & 0x0200U) != 0U);
    CHECK(sys->read_custom_word(0x00EU) == 0U);
}

TEST_CASE("amiga500 save_state restores overlay and chip RAM", "[manifests][amiga500]") {
    auto sys = assemble_amiga(tiny_kickstart());
    REQUIRE(sys != nullptr);
    sys->bus.write8(0x000123U, 0x5AU);
    sys->bus.write8(0x00BFE201U, 0x03U);
    sys->bus.write8(0x00BFE001U, 0x02U);
    sys->write_custom_word(0x040U, 0x09F0U);
    sys->write_custom_word(0x044U, 0x00FFU);
    sys->write_custom_word(0x050U, 0x0001U);
    sys->write_custom_word(0x052U, 0x2340U);
    sys->write_custom_word(0x064U, 0xFFFEU);
    sys->set_pot_position(0U, 6U, 8U);
    sys->write_custom_word(0x034U, 0x0001U);
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 10U);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->cia_b.write(0x01U, 0x74U);
    REQUIRE(sys->press_caps_lock());
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->bus.write8(0x000123U, 0x00U);
    sys->overlay_active = true;
    sys->write_custom_word(0x040U, 0x0000U);
    sys->write_custom_word(0x044U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0000U);
    sys->write_custom_word(0x064U, 0x0000U);
    sys->set_pot_position(0U, 1U, 1U);
    sys->write_custom_word(0x034U, 0x0001U);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    sys->keyboard.caps_lock_led = false;
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK_FALSE(sys->kickstart_overlay_active());
    CHECK(sys->bus.read8(0x000123U) == 0x5AU);
    CHECK(sys->read_custom_word(0x040U) == 0x09F0U);
    CHECK(sys->read_custom_word(0x044U) == 0x00FFU);
    CHECK(sys->read_custom_word(0x050U) == 0x0001U);
    CHECK(sys->read_custom_word(0x052U) == 0x2340U);
    CHECK(sys->read_custom_word(0x064U) == 0xFFFEU);
    CHECK(sys->read_custom_word(0x012U) == 0x0303U);
    CHECK(sys->floppy_cylinder() == 1U);
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
}

TEST_CASE("amiga2000 save_state restores configured Fast RAM",
          "[manifests][amiga500][memory][amiga2000]") {
    const amiga_config config{.model = amiga_model::amiga2000,
                              .fast_ram_size = amiga_system::fast_ram_size_2m};
    auto sys = assemble_amiga(tiny_kickstart(), config);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fast_ram.size() == amiga_system::fast_ram_size_2m);
    assign_first_zorro2_board(*sys);

    constexpr std::uint32_t fast_offset = 0x010123U;
    sys->bus.write8(amiga_system::fast_ram_base + fast_offset, 0x6DU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->bus.write8(amiga_system::fast_ram_base + fast_offset, 0x00U);
    REQUIRE(sys->fast_ram[fast_offset] == 0x00U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(sys->zorro2_boards.size() == 1U);
    CHECK(sys->zorro2_boards[0].configured);
    CHECK(sys->zorro2_boards[0].assigned_base == amiga_system::fast_ram_base);
    CHECK(sys->bus.read8(amiga_system::fast_ram_base + fast_offset) == 0x6DU);
    CHECK(sys->fast_ram[fast_offset] == 0x6DU);
}
