#include "v9938.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        constexpr std::uint32_t k_state_version = 4U;
        constexpr std::uint8_t k_status_frame_irq = 0x80U;
        constexpr std::uint8_t k_status_scanline_irq = 0x01U;
        constexpr std::uint8_t k_status_command_execute = 0x01U;
        constexpr std::uint8_t k_status_border_found = 0x10U;
        constexpr std::uint8_t k_status_horizontal_retrace = 0x20U;
        constexpr std::uint8_t k_status_vertical_retrace = 0x40U;
        constexpr std::uint8_t k_status_transfer_ready = 0x80U;
        constexpr std::uint8_t k_status_display_field = 0x02U;
        constexpr std::uint8_t k_status2_fixed_bits = 0x0CU;
        constexpr std::uint8_t k_register8_black_white = 0x01U;
        constexpr std::uint8_t k_register8_sprite_disable = 0x02U;
        constexpr std::uint8_t k_register8_colour0_opaque = 0x20U;
        constexpr std::uint8_t k_register9_interlace = 0x08U;
        constexpr std::uint8_t k_register9_even_odd_page = 0x04U;
        constexpr std::uint8_t k_register9_pal_refresh = 0x02U;
        constexpr std::uint8_t k_register45_source_expansion = 0x20U;
        constexpr std::uint8_t k_register45_destination_expansion = 0x40U;
        constexpr int k_horizontal_retrace_cycles = 32;

        [[nodiscard]] constexpr std::uint64_t
        master_cycles_to_v9938_ticks(std::uint64_t cycles) noexcept {
            return (cycles + 5U) / 6U;
        }

        [[nodiscard]] constexpr std::uint64_t
        rect_command_ticks(std::uint64_t pixels, std::uint64_t rows, std::uint64_t per_pixel_master,
                           std::uint64_t per_line_master) noexcept {
            if (pixels == 0U || rows == 0U) {
                return 0U;
            }
            const std::uint64_t line_cost = rows > 1U ? (rows - 1U) * per_line_master : 0U;
            return master_cycles_to_v9938_ticks(pixels * per_pixel_master + line_cost);
        }

        [[nodiscard]] constexpr std::uint8_t expand3(std::uint16_t value) noexcept {
            return static_cast<std::uint8_t>(((value & 0x07U) * 255U) / 7U);
        }

        [[nodiscard]] bool
        display_enabled(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (r[1] & 0x40U) != 0U;
        }

        [[nodiscard]] bool
        frame_irq_enabled(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (r[1] & 0x20U) != 0U;
        }

        [[nodiscard]] bool
        scanline_irq_enabled(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (r[0] & 0x10U) != 0U;
        }

        [[nodiscard]] bool
        sprite_16x16(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (r[1] & 0x02U) != 0U;
        }

        [[nodiscard]] bool
        sprite_magnified(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (r[1] & 0x01U) != 0U;
        }

        [[nodiscard]] std::uint32_t
        name_base_tms(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return static_cast<std::uint32_t>(r[2] & 0x7FU) << 10U;
        }

        [[nodiscard]] std::uint32_t
        name_base_text_ii(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return static_cast<std::uint32_t>(r[2] & 0x7CU) << 10U;
        }

        [[nodiscard]] std::uint32_t
        blink_base_text_ii(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (static_cast<std::uint32_t>(r[10] & 0x07U) << 14U) |
                   (static_cast<std::uint32_t>(r[3] & 0xF8U) << 6U);
        }

        [[nodiscard]] std::uint32_t
        color_base_g1(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (static_cast<std::uint32_t>(r[10] & 0x07U) << 14U) |
                   (static_cast<std::uint32_t>(r[3]) << 6U);
        }

        [[nodiscard]] std::uint32_t
        pattern_base_g1(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return static_cast<std::uint32_t>(r[4] & 0x3FU) << 11U;
        }

        [[nodiscard]] std::uint32_t
        color_base_g2(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (static_cast<std::uint32_t>(r[10] & 0x07U) << 14U) |
                   (static_cast<std::uint32_t>(r[3] & 0x80U) << 6U);
        }

        [[nodiscard]] std::uint32_t
        pattern_base_g2(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return static_cast<std::uint32_t>(r[4] & 0x3CU) << 11U;
        }

        [[nodiscard]] std::uint32_t
        graphics_ii_pattern_address(const std::array<std::uint8_t, v9938::register_count>& r,
                                    std::uint8_t pattern, int page, int fine_y) noexcept {
            const auto char_index =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) | pattern);
            const auto offset =
                static_cast<std::uint16_t>((char_index << 3U) | static_cast<std::uint16_t>(fine_y));
            const auto mask = static_cast<std::uint16_t>(((r[4] & 0x03U) << 11U) | 0x07FFU);
            return pattern_base_g2(r) | static_cast<std::uint32_t>(offset & mask);
        }

        [[nodiscard]] std::uint32_t
        graphics_ii_color_address(const std::array<std::uint8_t, v9938::register_count>& r,
                                  std::uint8_t pattern, int page, int fine_y) noexcept {
            const auto char_index =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) | pattern);
            const auto offset =
                static_cast<std::uint16_t>((char_index << 3U) | static_cast<std::uint16_t>(fine_y));
            const auto mask = static_cast<std::uint16_t>(((r[3] & 0x7FU) << 6U) | 0x003FU);
            return color_base_g2(r) | static_cast<std::uint32_t>(offset & mask);
        }

        [[nodiscard]] std::uint32_t
        sprite_attr_base(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (static_cast<std::uint32_t>(r[11] & 0x03U) << 15U) |
                   (static_cast<std::uint32_t>(r[5]) << 7U);
        }

        [[nodiscard]] std::uint32_t
        sprite_pattern_base(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return static_cast<std::uint32_t>(r[6] & 0x3FU) << 11U;
        }

        [[nodiscard]] std::uint32_t
        sprite_attr_base_mode2(const std::array<std::uint8_t, v9938::register_count>& r) noexcept {
            return (static_cast<std::uint32_t>(r[11] & 0x03U) << 15U) |
                   (static_cast<std::uint32_t>(r[5]) << 7U);
        }

        [[nodiscard]] int signed_sprite_y(std::uint8_t y) noexcept {
            int sy = static_cast<int>(y) + 1;
            if (sy >= 225) {
                sy -= 256;
            }
            return sy;
        }

        [[nodiscard]] std::uint16_t high_speed_x_mask(v9938::display_mode mode) noexcept {
            switch (mode) {
            case v9938::display_mode::graphics_iv:
            case v9938::display_mode::graphics_vi:
                return 0xFFFEU;
            case v9938::display_mode::graphics_v:
                return 0xFFFCU;
            default:
                return 0xFFFFU;
            }
        }

        [[nodiscard]] int display_adjust_delta(std::uint8_t value) noexcept {
            value = static_cast<std::uint8_t>(value & 0x0FU);
            return value <= 7U ? -static_cast<int>(value) : 16 - static_cast<int>(value);
        }
    } // namespace

    chip_metadata v9938::metadata() const noexcept {
        return {.manufacturer = "Yamaha",
                .part_number = "V9938",
                .family = "MSX-VIDEO",
                .klass = chip_class::video,
                .revision = 1U};
    }

    void v9938::set_pal(bool pal) noexcept {
        pal_mode_ = pal;
        total_scanlines_ = pal ? scanlines_pal : scanlines_ntsc;
    }

    void v9938::sync_palette_entry(std::size_t index) noexcept {
        if (index >= palette_.size()) {
            return;
        }
        const std::uint16_t packed = palette_[index];
        palette_bytes_[index * 2U] = static_cast<std::uint8_t>(packed & 0x00FFU);
        palette_bytes_[index * 2U + 1U] = static_cast<std::uint8_t>(packed >> 8U);
    }

    void v9938::sync_palette_bytes() noexcept {
        for (std::size_t index = 0; index < palette_.size(); ++index) {
            sync_palette_entry(index);
        }
    }

    v9938::display_mode v9938::mode() const noexcept {
        const bool m1 = (reg_[1] & 0x10U) != 0U;
        const bool m2 = (reg_[1] & 0x08U) != 0U;
        const bool m3 = (reg_[0] & 0x02U) != 0U;
        const bool m4 = (reg_[0] & 0x04U) != 0U;
        const bool m5 = (reg_[0] & 0x08U) != 0U;

        if (m1 && !m2 && !m3 && !m4 && !m5) {
            return display_mode::text_i;
        }
        if (m1 && !m2 && !m3 && m4 && !m5) {
            return display_mode::text_ii;
        }
        if (!m1 && m2 && !m3 && !m4 && !m5) {
            return display_mode::multicolor;
        }
        if (!m1 && !m2 && m3 && !m4 && !m5) {
            return display_mode::graphics_ii;
        }
        if (!m1 && !m2 && !m3 && m4 && !m5) {
            return display_mode::graphics_iii;
        }
        if (!m1 && !m2 && m3 && m4 && !m5) {
            return display_mode::graphics_iv;
        }
        if (!m1 && !m2 && !m3 && !m4 && m5) {
            return display_mode::graphics_v;
        }
        if (!m1 && !m2 && m3 && !m4 && m5) {
            return display_mode::graphics_vi;
        }
        if (!m1 && !m2 && m3 && m4 && m5) {
            return display_mode::graphics_vii;
        }
        return display_mode::graphics_i;
    }

    int v9938::visible_width() const noexcept {
        const display_mode m = mode();
        return (m == display_mode::text_ii || m == display_mode::graphics_v ||
                m == display_mode::graphics_vi)
                   ? max_width
                   : display_width_256;
    }

    int v9938::visible_height() const noexcept {
        const int height = field_visible_height();
        return interlace_enabled() ? height * 2 : height;
    }

    bool v9938::interlace_enabled() const noexcept {
        return (reg_[9] & k_register9_interlace) != 0U;
    }

    int v9938::field_visible_height() const noexcept {
        const display_mode m = mode();
        const bool supports_212_lines =
            m == display_mode::text_ii || m == display_mode::graphics_iv ||
            m == display_mode::graphics_v || m == display_mode::graphics_vi ||
            m == display_mode::graphics_vii;
        return supports_212_lines && (reg_[9] & 0x80U) != 0U ? display_height_212
                                                             : display_height_192;
    }

    int v9938::display_line_to_field_line(int line) const noexcept {
        return interlace_enabled() ? (line >> 1) : line;
    }

    bool v9938::display_line_uses_second_field(int line) const noexcept {
        return interlace_enabled() ? ((line & 1) != 0) : second_display_field();
    }

    std::uint32_t v9938::palette_rgb(std::uint8_t colour) const noexcept {
        const std::uint16_t packed = palette_[static_cast<std::size_t>(colour & 0x0FU)];
        const std::uint32_t r = expand3(static_cast<std::uint16_t>(packed >> 6U));
        const std::uint32_t g = expand3(static_cast<std::uint16_t>(packed >> 3U));
        const std::uint32_t b = expand3(packed);
        return (r << 16U) | (g << 8U) | b;
    }

    std::uint32_t v9938::paletted_display_rgb(std::uint8_t colour) const noexcept {
        const std::uint8_t index = static_cast<std::uint8_t>(colour & 0x0FU);
        const bool colour_zero_transparent = (reg_[8] & k_register8_colour0_opaque) == 0U;
        return index == 0U && colour_zero_transparent ? backdrop_rgb()
                                                      : display_rgb(palette_rgb(index));
    }

    std::uint32_t v9938::backdrop_rgb() const noexcept {
        return display_rgb(mode() == display_mode::graphics_vii
                               ? fixed_rgb(reg_[7])
                               : palette_rgb(static_cast<std::uint8_t>(reg_[7] & 0x0FU)));
    }

    std::uint32_t v9938::fixed_rgb(std::uint8_t colour) const noexcept {
        const std::uint32_t r = ((colour >> 2U) & 0x07U) * 255U / 7U;
        const std::uint32_t g = ((colour >> 5U) & 0x07U) * 255U / 7U;
        const std::uint32_t b = (colour & 0x03U) * 255U / 3U;
        return (r << 16U) | (g << 8U) | b;
    }

    std::uint32_t v9938::display_rgb(std::uint32_t rgb) const noexcept {
        rgb &= 0x00FFFFFFU;
        if ((reg_[8] & k_register8_black_white) == 0U) {
            return rgb;
        }

        const std::uint32_t r = (rgb >> 16U) & 0xFFU;
        const std::uint32_t g = (rgb >> 8U) & 0xFFU;
        const std::uint32_t b = rgb & 0xFFU;
        const std::uint32_t luma8 = (77U * r + 150U * g + 29U * b + 128U) >> 8U;
        const std::uint32_t luma5 = (luma8 * 31U + 127U) / 255U;
        const std::uint32_t channel = (luma5 * 255U + 15U) / 31U;
        return (channel << 16U) | (channel << 8U) | channel;
    }

    std::uint8_t v9938::memory_at(std::uint32_t address, bool expansion) const noexcept {
        return expansion ? expanded_vram_[address & (expanded_vram_size - 1U)]
                         : vram_[address & (vram_size - 1U)];
    }

    void v9938::write_memory(std::uint32_t address, bool expansion, std::uint8_t value) noexcept {
        if (expansion) {
            expanded_vram_[address & (expanded_vram_size - 1U)] = value;
            return;
        }
        vram_[address & (vram_size - 1U)] = value;
    }

    bool v9938::cpu_access_uses_expansion() const noexcept {
        return (reg_[45] & k_register45_destination_expansion) != 0U;
    }

    bool v9938::command_source_uses_expansion() const noexcept {
        return (reg_[45] & k_register45_source_expansion) != 0U;
    }

    bool v9938::command_destination_uses_expansion() const noexcept {
        return (reg_[45] & k_register45_destination_expansion) != 0U;
    }

    std::uint32_t v9938::vram_address() const noexcept {
        return ((static_cast<std::uint32_t>(reg_[14] & 0x07U) << 14U) | addr_low_) &
               (vram_size - 1U);
    }

    bool v9938::vram_access_auto_increments_base() const noexcept {
        switch (mode()) {
        case display_mode::graphics_i:
        case display_mode::text_i:
        case display_mode::multicolor:
        case display_mode::graphics_ii:
            return false;
        default:
            return true;
        }
    }

    void v9938::set_vram_address(std::uint32_t address) noexcept {
        address &= vram_size - 1U;
        addr_low_ = static_cast<std::uint16_t>(address & 0x3FFFU);
        reg_[14] = static_cast<std::uint8_t>((reg_[14] & 0xF8U) | ((address >> 14U) & 0x07U));
    }

    void v9938::increment_vram_address() noexcept {
        const std::uint16_t next_low = static_cast<std::uint16_t>((addr_low_ + 1U) & 0x3FFFU);
        const bool carry = next_low == 0U;
        addr_low_ = next_low;
        if (carry && vram_access_auto_increments_base()) {
            reg_[14] = static_cast<std::uint8_t>((reg_[14] & 0xF8U) | ((reg_[14] + 1U) & 0x07U));
        }
    }

    void v9938::write_register(std::uint8_t reg, std::uint8_t value) noexcept {
        const std::size_t idx = static_cast<std::size_t>(reg & 0x3FU);
        if (idx == 17U) {
            reg_[17] = static_cast<std::uint8_t>(value & 0xBFU);
            return;
        }
        reg_[idx] = value;
        if (idx == 14U) {
            reg_[14] &= 0x07U;
        } else if (idx == 15U || idx == 16U) {
            reg_[idx] &= 0x0FU;
        } else if (idx == 9U) {
            set_pal((reg_[9] & k_register9_pal_refresh) != 0U);
        } else if (idx == 0U || idx == 1U) {
            update_irq();
        } else if (idx == 44U && command_stream_ != command_stream_kind::none &&
                   command_stream_ != command_stream_kind::lmcm) {
            consume_cpu_command_data(value);
        } else if (idx == 46U) {
            execute_command(value);
        }
    }

    void v9938::ctrl_write(std::uint8_t value) noexcept {
        if (!cmd_pending_) {
            cmd_first_ = value;
            cmd_pending_ = true;
            addr_low_ = static_cast<std::uint16_t>((addr_low_ & 0x3F00U) | value);
            return;
        }

        cmd_pending_ = false;
        if ((value & 0x80U) != 0U) {
            write_register(static_cast<std::uint8_t>(value & 0x3FU), cmd_first_);
            return;
        }

        addr_low_ = static_cast<std::uint16_t>(((value & 0x3FU) << 8U) | cmd_first_);
        code_ = static_cast<std::uint8_t>((value >> 6U) & 0x01U);
        if (code_ == 0U) {
            read_buffer_ = memory_at(vram_address(), cpu_access_uses_expansion());
            increment_vram_address();
        }
    }

    std::uint8_t v9938::data_read() noexcept {
        cmd_pending_ = false;
        palette_second_ = false;
        const std::uint8_t result = read_buffer_;
        read_buffer_ = memory_at(vram_address(), cpu_access_uses_expansion());
        increment_vram_address();
        return result;
    }

    void v9938::data_write(std::uint8_t value) noexcept {
        cmd_pending_ = false;
        palette_second_ = false;
        if (command_stream_ != command_stream_kind::none &&
            command_stream_ != command_stream_kind::lmcm) {
            consume_cpu_command_data(value);
            return;
        }
        write_memory(vram_address(), cpu_access_uses_expansion(), value);
        read_buffer_ = value;
        increment_vram_address();
    }

    std::uint8_t v9938::compose_status_register(std::uint8_t index) const noexcept {
        switch (index) {
        case 2U:
            return compose_status2();
        case 4U:
            return static_cast<std::uint8_t>(0xFEU | (status_[4] & 0x01U));
        case 6U:
            return static_cast<std::uint8_t>(
                0xFCU | (second_display_field() ? k_status_display_field : 0U) |
                (status_[6] & 0x01U));
        case 9U:
            return static_cast<std::uint8_t>(0xFEU | (status_[9] & 0x01U));
        default:
            return status_[index];
        }
    }

    std::uint8_t v9938::compose_status2() const noexcept {
        std::uint8_t result = static_cast<std::uint8_t>(status_[2] | k_status2_fixed_bits);
        if (second_display_field()) {
            result |= k_status_display_field;
        }
        if (vertical_retrace_active()) {
            result |= k_status_vertical_retrace;
        }
        if (horizontal_retrace_active()) {
            result |= k_status_horizontal_retrace;
        }
        return result;
    }

    bool v9938::vertical_retrace_active() const noexcept {
        return scanline_ >= field_visible_height();
    }

    bool v9938::horizontal_retrace_active() const noexcept {
        return scanline_cycle_ >= cycles_per_line - k_horizontal_retrace_cycles;
    }

    std::uint8_t v9938::status_read() noexcept {
        cmd_pending_ = false;
        palette_second_ = false;
        const std::uint8_t selected = static_cast<std::uint8_t>(reg_[15] & 0x0FU);
        if (selected >= status_register_count) {
            return 0xFFU;
        }
        const std::uint8_t result = compose_status_register(selected);
        if (selected == 7U && command_stream_ == command_stream_kind::lmcm &&
            (status_[2] & k_status_transfer_ready) != 0U) {
            advance_command_stream(1U);
            if (command_stream_ == command_stream_kind::lmcm) {
                arm_transfer_delay(estimate_cpu_transfer_delay_cycles(command_stream_));
            }
            return result;
        }
        if (selected == 0U) {
            status_[0] &= static_cast<std::uint8_t>(~k_status_frame_irq);
            update_irq();
        } else if (selected == 1U) {
            status_[1] &= static_cast<std::uint8_t>(~k_status_scanline_irq);
            update_irq();
        } else if (selected == 5U) {
            status_[3] = 0U;
            status_[4] = 0U;
            status_[5] = 0U;
            status_[6] = 0U;
        }
        return result;
    }

    void v9938::palette_write(std::uint8_t value) noexcept {
        cmd_pending_ = false;
        const std::size_t index = static_cast<std::size_t>(reg_[16] & 0x0FU);
        if (!palette_second_) {
            palette_first_ = value;
            palette_second_ = true;
            return;
        }
        palette_second_ = false;
        const auto r = static_cast<std::uint16_t>((palette_first_ >> 4U) & 0x07U);
        const auto b = static_cast<std::uint16_t>(palette_first_ & 0x07U);
        const auto g = static_cast<std::uint16_t>(value & 0x07U);
        palette_[index] = static_cast<std::uint16_t>((r << 6U) | (g << 3U) | b);
        sync_palette_entry(index);
        reg_[16] = static_cast<std::uint8_t>((reg_[16] + 1U) & 0x0FU);
    }

    void v9938::indirect_reg_write(std::uint8_t value) noexcept {
        cmd_pending_ = false;
        palette_second_ = false;
        const std::uint8_t target = static_cast<std::uint8_t>(reg_[17] & 0x3FU);
        if (target != 17U) {
            write_register(target, value);
        }
        if ((reg_[17] & 0x80U) == 0U) {
            reg_[17] = static_cast<std::uint8_t>((reg_[17] & 0x80U) | ((target + 1U) & 0x3FU));
        }
    }

    std::uint8_t v9938::mmio_read(std::uint16_t offset) {
        switch (offset & 0x03U) {
        case 0U:
            return data_read();
        case 1U:
            return status_read();
        default:
            cmd_pending_ = false;
            palette_second_ = false;
            return 0xFFU;
        }
    }

    void v9938::mmio_write(std::uint16_t offset, std::uint8_t value) {
        switch (offset & 0x03U) {
        case 0U:
            data_write(value);
            break;
        case 1U:
            ctrl_write(value);
            break;
        case 2U:
            palette_write(value);
            break;
        case 3U:
            indirect_reg_write(value);
            break;
        }
    }

    void v9938::stop_command() noexcept {
        command_stream_ = command_stream_kind::none;
        stream_x_ = 0U;
        stream_y_ = 0U;
        stream_nx_ = 0U;
        stream_ny_ = 0U;
        stream_col_ = 0U;
        stream_row_ = 0U;
        stream_x_step_ = 1;
        stream_y_step_ = 1;
        stream_op_ = 0U;
        stream_high_speed_ = false;
        stream_source_expansion_ = false;
        stream_dest_expansion_ = false;
        stream_mode_ = display_mode::graphics_i;
        command_busy_cycles_ = 0U;
        stream_ready_delay_cycles_ = 0U;
        status_[2] &=
            static_cast<std::uint8_t>(~(k_status_command_execute | k_status_transfer_ready));
    }

    void v9938::arm_command_busy(std::uint64_t cycles) noexcept {
        command_busy_cycles_ = apply_command_access_pressure(cycles);
        status_[2] &= static_cast<std::uint8_t>(~k_status_transfer_ready);
        if (command_busy_cycles_ == 0U) {
            status_[2] &= static_cast<std::uint8_t>(~k_status_command_execute);
            return;
        }
        status_[2] |= k_status_command_execute;
    }

    void v9938::arm_transfer_delay(std::uint64_t cycles) noexcept {
        stream_ready_delay_cycles_ = apply_command_access_pressure(cycles);
        if (stream_ready_delay_cycles_ == 0U) {
            update_command_stream_status();
            return;
        }
        status_[2] |= k_status_command_execute;
        status_[2] &= static_cast<std::uint8_t>(~k_status_transfer_ready);
    }

    void v9938::advance_command_timers(std::uint64_t cycles) noexcept {
        if (command_busy_cycles_ != 0U) {
            if (cycles >= command_busy_cycles_) {
                command_busy_cycles_ = 0U;
                if (command_stream_ == command_stream_kind::none) {
                    status_[2] &= static_cast<std::uint8_t>(~k_status_command_execute);
                }
            } else {
                command_busy_cycles_ -= cycles;
            }
        }

        if (stream_ready_delay_cycles_ == 0U) {
            return;
        }
        if (cycles >= stream_ready_delay_cycles_) {
            stream_ready_delay_cycles_ = 0U;
            if (command_stream_ == command_stream_kind::lmcm) {
                prepare_vram_to_cpu_data();
            } else {
                update_command_stream_status();
            }
        } else {
            stream_ready_delay_cycles_ -= cycles;
        }
    }

    std::uint64_t v9938::apply_command_access_pressure(std::uint64_t cycles) const noexcept {
        if (cycles == 0U) {
            return 0U;
        }

        constexpr std::uint64_t screen_off_slots = 154U;
        constexpr std::uint64_t sprites_off_slots = 88U;
        constexpr std::uint64_t sprites_on_slots = 31U;

        const bool screen_enabled = display_enabled(reg_);
        const bool sprites_inhibited = (reg_[8] & k_register8_sprite_disable) != 0U;
        const std::uint64_t slots =
            !screen_enabled ? screen_off_slots
                            : (sprites_inhibited ? sprites_off_slots : sprites_on_slots);
        return (cycles * screen_off_slots + slots - 1U) / slots;
    }

    std::uint8_t v9938::high_speed_pixels_per_byte(display_mode mode) const noexcept {
        switch (mode) {
        case display_mode::graphics_v:
            return 4U;
        case display_mode::graphics_iv:
        case display_mode::graphics_vi:
            return 2U;
        default:
            return 1U;
        }
    }

    std::uint8_t v9938::high_speed_colour_from_byte(std::uint8_t value, int x,
                                                    display_mode mode) const noexcept {
        switch (mode) {
        case display_mode::graphics_v:
            return static_cast<std::uint8_t>((value >> ((3 - (x & 3)) * 2)) & 0x03U);
        case display_mode::graphics_iv:
        case display_mode::graphics_vi:
            return (x & 1) == 0 ? static_cast<std::uint8_t>(value >> 4U)
                                : static_cast<std::uint8_t>(value & 0x0FU);
        default:
            return value;
        }
    }

    std::uint16_t v9938::command_screen_width() const noexcept {
        switch (mode()) {
        case display_mode::graphics_v:
        case display_mode::graphics_vi:
            return static_cast<std::uint16_t>(max_width);
        case display_mode::graphics_iv:
        case display_mode::graphics_vii:
            return static_cast<std::uint16_t>(display_width_256);
        default:
            return 0U;
        }
    }

    void v9938::update_command_stream_status() noexcept {
        if (command_stream_ == command_stream_kind::none || stream_row_ >= stream_ny_ ||
            stream_nx_ == 0U || stream_ny_ == 0U) {
            if (command_stream_ != command_stream_kind::none) {
                apply_command_register_postconditions(reg_[46]);
            }
            stop_command();
            return;
        }

        status_[2] |= k_status_command_execute;
        if (stream_ready_delay_cycles_ == 0U) {
            status_[2] |= k_status_transfer_ready;
        } else {
            status_[2] &= static_cast<std::uint8_t>(~k_status_transfer_ready);
        }
    }

    void v9938::advance_command_stream(std::uint16_t pixels) noexcept {
        if (command_stream_ == command_stream_kind::none) {
            return;
        }

        stream_col_ = static_cast<std::uint16_t>(stream_col_ + pixels);
        while (stream_col_ >= stream_nx_ && stream_row_ < stream_ny_) {
            stream_col_ = static_cast<std::uint16_t>(stream_col_ - stream_nx_);
            ++stream_row_;
        }
        update_command_stream_status();
    }

    std::uint8_t v9938::read_high_speed_byte(std::uint16_t x, std::uint16_t y,
                                             bool expansion) const noexcept {
        std::uint32_t address = 0U;
        std::uint8_t shift = 0U;
        std::uint8_t mask = 0U;
        if (!command_pixel_address(x, y, address, shift, mask)) {
            return 0U;
        }
        return memory_at(address, expansion);
    }

    void v9938::write_high_speed_byte(std::uint16_t x, std::uint16_t y, std::uint8_t value,
                                      display_mode mode, bool expansion) noexcept {
        const std::uint8_t pixels = high_speed_pixels_per_byte(mode);
        for (std::uint8_t i = 0; i < pixels; ++i) {
            const auto px = static_cast<std::uint16_t>(x + i);
            write_command_pixel(px, y, high_speed_colour_from_byte(value, px, mode), expansion);
        }
    }

    void v9938::write_high_speed_byte(std::uint16_t x, std::uint16_t y,
                                      std::uint8_t value) noexcept {
        write_high_speed_byte(x, y, value, stream_mode_, stream_dest_expansion_);
    }

    void v9938::consume_cpu_command_data(std::uint8_t value) noexcept {
        if (command_stream_ == command_stream_kind::none ||
            command_stream_ == command_stream_kind::lmcm ||
            (status_[2] & k_status_transfer_ready) == 0U) {
            return;
        }

        const int x = static_cast<int>(stream_x_) + static_cast<int>(stream_col_) * stream_x_step_;
        const int y = static_cast<int>(stream_y_) + static_cast<int>(stream_row_) * stream_y_step_;
        if (x >= 0 && y >= 0) {
            if (command_stream_ == command_stream_kind::hmmc) {
                write_high_speed_byte(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                      value);
                advance_command_stream(high_speed_pixels_per_byte(stream_mode_));
            } else {
                write_command_pixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                    value, stream_dest_expansion_, stream_op_);
                advance_command_stream(1U);
            }
        } else {
            advance_command_stream(command_stream_ == command_stream_kind::hmmc
                                       ? high_speed_pixels_per_byte(stream_mode_)
                                       : 1U);
        }
        if (command_stream_ != command_stream_kind::none) {
            arm_transfer_delay(estimate_cpu_transfer_delay_cycles(command_stream_));
        }
    }

    void v9938::prepare_vram_to_cpu_data() noexcept {
        if (command_stream_ != command_stream_kind::lmcm) {
            return;
        }
        const int x = static_cast<int>(stream_x_) + static_cast<int>(stream_col_) * stream_x_step_;
        const int y = static_cast<int>(stream_y_) + static_cast<int>(stream_row_) * stream_y_step_;
        status_[7] =
            (x >= 0 && y >= 0)
                ? read_command_pixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                     stream_source_expansion_)
                : 0U;
        update_command_stream_status();
    }

    void v9938::start_cpu_to_vram_command(command_stream_kind kind, std::uint8_t op) noexcept {
        stop_command();
        stream_mode_ = mode();
        stream_high_speed_ = kind == command_stream_kind::hmmc;
        stream_x_ = command_x(36, 37);
        stream_y_ = command_y(38, 39);
        stream_nx_ = command_x(40, 41);
        stream_ny_ = command_y(42, 43);
        if (stream_high_speed_) {
            const std::uint16_t x_mask = high_speed_x_mask(stream_mode_);
            stream_x_ = static_cast<std::uint16_t>(stream_x_ & x_mask);
            stream_nx_ = static_cast<std::uint16_t>(stream_nx_ & x_mask);
        }
        stream_x_step_ = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        stream_y_step_ = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        stream_op_ = op;
        stream_source_expansion_ = false;
        stream_dest_expansion_ = command_destination_uses_expansion();
        stream_ready_delay_cycles_ = 0U;
        command_stream_ = kind;
        update_command_stream_status();
        consume_cpu_command_data(reg_[44]);
    }

    void v9938::start_vram_to_cpu_command() noexcept {
        stop_command();
        stream_mode_ = mode();
        stream_high_speed_ = false;
        stream_x_ = command_x(32, 33);
        stream_y_ = command_y(34, 35);
        stream_nx_ = command_x(40, 41);
        stream_ny_ = command_y(42, 43);
        stream_x_step_ = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        stream_y_step_ = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        stream_source_expansion_ = command_source_uses_expansion();
        stream_dest_expansion_ = false;
        stream_ready_delay_cycles_ = 0U;
        command_stream_ = command_stream_kind::lmcm;
        arm_transfer_delay(estimate_cpu_transfer_delay_cycles(command_stream_));
    }

    std::uint64_t v9938::estimate_command_busy_cycles(std::uint8_t command) const noexcept {
        switch (command & 0xF0U) {
        case 0xE0U: {
            const display_mode active_mode = mode();
            const std::uint16_t x_mask = high_speed_x_mask(active_mode);
            const std::uint16_t x_origin = static_cast<std::uint16_t>(command_x(36, 37) & x_mask);
            const std::uint16_t ny = command_y(42, 43);
            const std::uint16_t screen_width = command_screen_width();
            if (ny == 0U || screen_width == 0U || x_origin >= screen_width) {
                return 0U;
            }
            const int x_step = (reg_[45] & 0x04U) != 0U ? -1 : 1;
            const std::uint16_t width =
                x_step > 0 ? static_cast<std::uint16_t>(screen_width - x_origin)
                           : static_cast<std::uint16_t>(x_origin +
                                                        high_speed_pixels_per_byte(active_mode));
            return rect_command_ticks(static_cast<std::uint64_t>(width) * ny, ny, 64U, 0U);
        }
        case 0xC0U: {
            const std::uint16_t nx =
                static_cast<std::uint16_t>(command_x(40, 41) & high_speed_x_mask(mode()));
            const std::uint16_t ny = command_y(42, 43);
            return rect_command_ticks(static_cast<std::uint64_t>(nx) * ny, ny, 48U, 56U);
        }
        case 0xD0U: {
            const std::uint16_t nx =
                static_cast<std::uint16_t>(command_x(40, 41) & high_speed_x_mask(mode()));
            const std::uint16_t ny = command_y(42, 43);
            return rect_command_ticks(static_cast<std::uint64_t>(nx) * ny, ny, 88U, 64U);
        }
        case 0x80U: {
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t ny = command_y(42, 43);
            return rect_command_ticks(static_cast<std::uint64_t>(nx) * ny, ny, 96U, 64U);
        }
        case 0x90U: {
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t ny = command_y(42, 43);
            return rect_command_ticks(static_cast<std::uint64_t>(nx) * ny, ny, 120U, 64U);
        }
        case 0x70U: {
            const std::uint64_t major = command_x(40, 41);
            const std::uint64_t minor = command_y(42, 43);
            return master_cycles_to_v9938_ticks((major + 1U) * 112U + minor * 32U);
        }
        case 0x60U: {
            const std::uint16_t sx = command_x(32, 33);
            const std::uint16_t width = command_screen_width();
            if (width == 0U || sx >= width) {
                return 0U;
            }
            const std::uint64_t pixels = (reg_[45] & 0x04U) != 0U
                                             ? static_cast<std::uint64_t>(sx) + 1U
                                             : static_cast<std::uint64_t>(width - sx);
            return master_cycles_to_v9938_ticks(pixels * 48U);
        }
        case 0x50U:
            return master_cycles_to_v9938_ticks(96U);
        case 0x40U:
            return master_cycles_to_v9938_ticks(64U);
        default:
            return 0U;
        }
    }

    std::uint64_t
    v9938::estimate_cpu_transfer_delay_cycles(command_stream_kind kind) const noexcept {
        switch (kind) {
        case command_stream_kind::hmmc:
            return master_cycles_to_v9938_ticks(48U);
        case command_stream_kind::lmmc:
            return master_cycles_to_v9938_ticks(96U);
        case command_stream_kind::lmcm:
            return master_cycles_to_v9938_ticks(48U);
        case command_stream_kind::none:
            return 0U;
        }
        return 0U;
    }

    std::uint16_t v9938::command_x(int low_reg, int high_reg) const noexcept {
        return static_cast<std::uint16_t>(
            reg_[static_cast<std::size_t>(low_reg)] |
            ((reg_[static_cast<std::size_t>(high_reg)] & 0x01U) << 8U));
    }

    std::uint16_t v9938::command_y(int low_reg, int high_reg) const noexcept {
        return static_cast<std::uint16_t>(
            reg_[static_cast<std::size_t>(low_reg)] |
            ((reg_[static_cast<std::size_t>(high_reg)] & 0x03U) << 8U));
    }

    void v9938::set_command_x(int low_reg, int high_reg, std::uint16_t value) noexcept {
        value = static_cast<std::uint16_t>(value & 0x01FFU);
        reg_[static_cast<std::size_t>(low_reg)] = static_cast<std::uint8_t>(value & 0xFFU);
        auto& high = reg_[static_cast<std::size_t>(high_reg)];
        high = static_cast<std::uint8_t>((high & 0xFEU) | ((value >> 8U) & 0x01U));
    }

    void v9938::set_command_y(int low_reg, int high_reg, std::uint16_t value) noexcept {
        value = static_cast<std::uint16_t>(value & 0x03FFU);
        reg_[static_cast<std::size_t>(low_reg)] = static_cast<std::uint8_t>(value & 0xFFU);
        auto& high = reg_[static_cast<std::size_t>(high_reg)];
        high = static_cast<std::uint8_t>((high & 0xFCU) | ((value >> 8U) & 0x03U));
    }

    std::uint16_t v9938::command_screen_height() const noexcept {
        switch (mode()) {
        case display_mode::graphics_iv:
        case display_mode::graphics_v:
            return 1024U;
        case display_mode::graphics_vi:
        case display_mode::graphics_vii:
            return 512U;
        default:
            return 0U;
        }
    }

    std::uint16_t v9938::executed_vertical_rows(std::uint16_t y, std::uint16_t rows,
                                                int y_step) const noexcept {
        const std::uint16_t height = command_screen_height();
        if (rows == 0U || height == 0U) {
            return 0U;
        }
        if (y_step >= 0) {
            if (y >= height) {
                return 0U;
            }
            return std::min(rows, static_cast<std::uint16_t>(height - y));
        }
        return std::min(rows, static_cast<std::uint16_t>(y + 1U));
    }

    std::uint16_t v9938::executed_vertical_rows(std::uint16_t y0, std::uint16_t y1,
                                                std::uint16_t rows, int y_step) const noexcept {
        return std::min(executed_vertical_rows(y0, rows, y_step),
                        executed_vertical_rows(y1, rows, y_step));
    }

    void v9938::apply_command_register_postconditions(std::uint8_t command) noexcept {
        const std::uint8_t family = static_cast<std::uint8_t>(command & 0xF0U);
        const std::uint16_t ny = command_y(42, 43);
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;

        const auto advance_y = [y_step](std::uint16_t y, std::uint16_t rows) noexcept {
            return static_cast<std::uint16_t>(static_cast<int>(y) +
                                              static_cast<int>(rows) * y_step);
        };
        const auto set_nyb = [this, ny](std::uint16_t rows) noexcept {
            set_command_y(42, 43, static_cast<std::uint16_t>(ny - rows));
        };
        const auto clear_command_high = [this]() noexcept {
            reg_[46] = static_cast<std::uint8_t>(reg_[46] & 0x0FU);
        };

        switch (family) {
        case 0xF0U: { // HMMC
            const std::uint16_t nx =
                static_cast<std::uint16_t>(command_x(40, 41) & high_speed_x_mask(mode()));
            const std::uint16_t rows =
                nx == 0U ? 0U : executed_vertical_rows(command_y(38, 39), ny, y_step);
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0xE0U: { // YMMM
            const std::uint16_t x_origin =
                static_cast<std::uint16_t>(command_x(36, 37) & high_speed_x_mask(mode()));
            const std::uint16_t width = command_screen_width();
            const std::uint16_t rows =
                width == 0U || x_origin >= width
                    ? 0U
                    : executed_vertical_rows(command_y(34, 35), command_y(38, 39), ny, y_step);
            set_command_y(34, 35, advance_y(command_y(34, 35), rows));
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0xD0U: { // HMMM
            const std::uint16_t nx =
                static_cast<std::uint16_t>(command_x(40, 41) & high_speed_x_mask(mode()));
            const std::uint16_t rows =
                nx == 0U ? 0U
                         : executed_vertical_rows(command_y(34, 35), command_y(38, 39), ny, y_step);
            set_command_y(34, 35, advance_y(command_y(34, 35), rows));
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0xC0U: { // HMMV
            const std::uint16_t nx =
                static_cast<std::uint16_t>(command_x(40, 41) & high_speed_x_mask(mode()));
            const std::uint16_t rows =
                nx == 0U ? 0U : executed_vertical_rows(command_y(38, 39), ny, y_step);
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0xB0U: { // LMMC
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t rows =
                nx == 0U ? 0U : executed_vertical_rows(command_y(38, 39), ny, y_step);
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0xA0U: { // LMCM
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t rows =
                nx == 0U ? 0U : executed_vertical_rows(command_y(34, 35), ny, y_step);
            set_command_y(34, 35, advance_y(command_y(34, 35), rows));
            set_nyb(rows);
            reg_[44] = status_[7];
            clear_command_high();
            break;
        }
        case 0x90U: { // LMMM
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t rows =
                nx == 0U ? 0U
                         : executed_vertical_rows(command_y(34, 35), command_y(38, 39), ny, y_step);
            set_command_y(34, 35, advance_y(command_y(34, 35), rows));
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0x80U: { // LMMV
            const std::uint16_t nx = command_x(40, 41);
            const std::uint16_t rows =
                nx == 0U ? 0U : executed_vertical_rows(command_y(38, 39), ny, y_step);
            set_command_y(38, 39, advance_y(command_y(38, 39), rows));
            set_nyb(rows);
            clear_command_high();
            break;
        }
        case 0x70U: { // LINE
            const int y = command_y(38, 39);
            const int major = command_x(40, 41);
            const int minor = command_y(42, 43);
            const bool major_is_y = (reg_[45] & 0x01U) != 0U;
            const int vertical_delta = major_is_y ? major + 1 : minor;

            set_command_y(38, 39,
                          static_cast<std::uint16_t>(y + vertical_delta * y_step));
            clear_command_high();
            break;
        }
        case 0x60U: // SRCH
        case 0x50U: // PSET
            clear_command_high();
            break;
        case 0x40U: // POINT
            reg_[44] = status_[7];
            clear_command_high();
            break;
        default:
            break;
        }
    }

    bool v9938::command_pixel_address(std::uint16_t x, std::uint16_t y, std::uint32_t& address,
                                      std::uint8_t& shift, std::uint8_t& mask) const noexcept {
        switch (mode()) {
        case display_mode::graphics_iv:
            if (x >= static_cast<std::uint16_t>(display_width_256) || y >= 1024U) {
                return false;
            }
            address = (static_cast<std::uint32_t>(y) * 128U) + (x >> 1U);
            shift = (x & 1U) == 0U ? 4U : 0U;
            mask = 0x0FU;
            return true;
        case display_mode::graphics_v:
            if (x >= static_cast<std::uint16_t>(max_width) || y >= 1024U) {
                return false;
            }
            address = (static_cast<std::uint32_t>(y) * 128U) + (x >> 2U);
            shift = static_cast<std::uint8_t>((3U - (x & 3U)) * 2U);
            mask = 0x03U;
            return true;
        case display_mode::graphics_vi:
            if (x >= static_cast<std::uint16_t>(max_width) || y >= 512U) {
                return false;
            }
            address = (static_cast<std::uint32_t>(y) * 256U) + (x >> 1U);
            shift = (x & 1U) == 0U ? 4U : 0U;
            mask = 0x0FU;
            return true;
        case display_mode::graphics_vii:
            if (x >= static_cast<std::uint16_t>(display_width_256) || y >= 512U) {
                return false;
            }
            address = (static_cast<std::uint32_t>(y) * 256U) + x;
            shift = 0U;
            mask = 0xFFU;
            return true;
        default:
            return false;
        }
    }

    std::uint8_t v9938::read_command_pixel(std::uint16_t x, std::uint16_t y,
                                           bool expansion) const noexcept {
        std::uint32_t address = 0U;
        std::uint8_t shift = 0U;
        std::uint8_t mask = 0U;
        if (!command_pixel_address(x, y, address, shift, mask)) {
            return 0U;
        }
        return static_cast<std::uint8_t>((memory_at(address, expansion) >> shift) & mask);
    }

    std::uint8_t v9938::apply_logical(std::uint8_t source, std::uint8_t dest, std::uint8_t op,
                                      std::uint8_t mask) const noexcept {
        source &= mask;
        dest &= mask;
        if ((op & 0x08U) != 0U && source == 0U) {
            return dest;
        }

        switch (op & 0x07U) {
        case 0U:
            return source;
        case 1U:
            return static_cast<std::uint8_t>(source & dest);
        case 2U:
            return static_cast<std::uint8_t>(source | dest);
        case 3U:
            return static_cast<std::uint8_t>(source ^ dest);
        case 4U:
            return static_cast<std::uint8_t>((~source) & mask);
        default:
            return source;
        }
    }

    void v9938::write_command_pixel(std::uint16_t x, std::uint16_t y, std::uint8_t colour,
                                    bool expansion, std::uint8_t logical_op) noexcept {
        std::uint32_t address = 0U;
        std::uint8_t shift = 0U;
        std::uint8_t mask = 0U;
        if (!command_pixel_address(x, y, address, shift, mask)) {
            return;
        }

        std::uint8_t packed = memory_at(address, expansion);
        const std::uint8_t dest = static_cast<std::uint8_t>((packed >> shift) & mask);
        const std::uint8_t result = apply_logical(colour, dest, logical_op, mask);
        const std::uint8_t shifted_mask = static_cast<std::uint8_t>(mask << shift);
        const std::uint8_t preserve_mask = static_cast<std::uint8_t>(~shifted_mask);
        packed = static_cast<std::uint8_t>((packed & preserve_mask) | ((result & mask) << shift));
        write_memory(address, expansion, packed);
    }

    void v9938::execute_hmmv() noexcept {
        const display_mode active_mode = mode();
        const std::uint16_t x_mask = high_speed_x_mask(active_mode);
        const std::uint16_t dx = static_cast<std::uint16_t>(command_x(36, 37) & x_mask);
        const std::uint16_t dy = command_y(38, 39);
        const std::uint16_t nx = static_cast<std::uint16_t>(command_x(40, 41) & x_mask);
        const std::uint16_t ny = command_y(42, 43);
        if (nx == 0U || ny == 0U) {
            return;
        }

        const std::uint8_t pixels_per_byte = high_speed_pixels_per_byte(active_mode);
        const std::uint16_t byte_count = static_cast<std::uint16_t>(nx / pixels_per_byte);
        if (byte_count == 0U) {
            return;
        }
        const bool dest_expansion = command_destination_uses_expansion();
        const int x_step =
            (reg_[45] & 0x04U) != 0U ? -static_cast<int>(pixels_per_byte)
                                     : static_cast<int>(pixels_per_byte);
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        for (std::uint16_t row = 0; row < ny; ++row) {
            const int y = static_cast<int>(dy) + static_cast<int>(row) * y_step;
            if (y < 0) {
                continue;
            }
            for (std::uint16_t col = 0; col < byte_count; ++col) {
                const int x = static_cast<int>(dx) + static_cast<int>(col) * x_step;
                if (x < 0) {
                    continue;
                }
                write_high_speed_byte(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                      reg_[44], active_mode, dest_expansion);
            }
        }
    }

    void v9938::execute_hmmm() noexcept {
        const display_mode active_mode = mode();
        const std::uint16_t x_mask = high_speed_x_mask(active_mode);
        const std::uint16_t sx = static_cast<std::uint16_t>(command_x(32, 33) & x_mask);
        const std::uint16_t sy = command_y(34, 35);
        const std::uint16_t dx = static_cast<std::uint16_t>(command_x(36, 37) & x_mask);
        const std::uint16_t dy = command_y(38, 39);
        const std::uint16_t nx = static_cast<std::uint16_t>(command_x(40, 41) & x_mask);
        const std::uint16_t ny = command_y(42, 43);
        if (nx == 0U || ny == 0U) {
            return;
        }

        const std::uint8_t pixels_per_byte = high_speed_pixels_per_byte(active_mode);
        const std::uint16_t byte_count = static_cast<std::uint16_t>(nx / pixels_per_byte);
        if (byte_count == 0U) {
            return;
        }
        const bool source_expansion = command_source_uses_expansion();
        const bool dest_expansion = command_destination_uses_expansion();
        const int x_step =
            (reg_[45] & 0x04U) != 0U ? -static_cast<int>(pixels_per_byte)
                                     : static_cast<int>(pixels_per_byte);
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        for (std::uint16_t row = 0; row < ny; ++row) {
            const int src_y = static_cast<int>(sy) + static_cast<int>(row) * y_step;
            const int dst_y = static_cast<int>(dy) + static_cast<int>(row) * y_step;
            if (dst_y < 0) {
                continue;
            }
            for (std::uint16_t col = 0; col < byte_count; ++col) {
                const int src_x = static_cast<int>(sx) + static_cast<int>(col) * x_step;
                const int dst_x = static_cast<int>(dx) + static_cast<int>(col) * x_step;
                if (dst_x < 0) {
                    continue;
                }
                std::uint8_t colour = 0U;
                if (src_x >= 0 && src_y >= 0) {
                    colour = read_high_speed_byte(static_cast<std::uint16_t>(src_x),
                                                  static_cast<std::uint16_t>(src_y),
                                                  source_expansion);
                }
                write_high_speed_byte(static_cast<std::uint16_t>(dst_x),
                                      static_cast<std::uint16_t>(dst_y), colour, active_mode,
                                      dest_expansion);
            }
        }
    }

    void v9938::execute_hmmc() noexcept {
        start_cpu_to_vram_command(command_stream_kind::hmmc, 0U);
    }

    void v9938::execute_ymmm() noexcept {
        const display_mode active_mode = mode();
        const std::uint16_t x_mask = high_speed_x_mask(active_mode);
        const std::uint16_t x_origin = static_cast<std::uint16_t>(command_x(36, 37) & x_mask);
        const std::uint16_t sy = command_y(34, 35);
        const std::uint16_t dy = command_y(38, 39);
        const std::uint16_t ny = command_y(42, 43);
        const std::uint16_t screen_width = command_screen_width();
        if (ny == 0U || screen_width == 0U || x_origin >= screen_width) {
            return;
        }

        const std::uint8_t pixels_per_byte = high_speed_pixels_per_byte(active_mode);
        const int x_step =
            (reg_[45] & 0x04U) != 0U ? -static_cast<int>(pixels_per_byte)
                                     : static_cast<int>(pixels_per_byte);
        const std::uint16_t width =
            x_step > 0 ? static_cast<std::uint16_t>(screen_width - x_origin)
                       : static_cast<std::uint16_t>(x_origin + pixels_per_byte);
        const std::uint16_t byte_count = static_cast<std::uint16_t>(width / pixels_per_byte);
        if (byte_count == 0U) {
            return;
        }
        const bool source_expansion = command_source_uses_expansion();
        const bool dest_expansion = command_destination_uses_expansion();
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        for (std::uint16_t row = 0; row < ny; ++row) {
            const int src_y = static_cast<int>(sy) + static_cast<int>(row) * y_step;
            const int dst_y = static_cast<int>(dy) + static_cast<int>(row) * y_step;
            if (src_y < 0 || dst_y < 0) {
                continue;
            }
            for (std::uint16_t col = 0; col < byte_count; ++col) {
                const int x = static_cast<int>(x_origin) + static_cast<int>(col) * x_step;
                if (x < 0) {
                    continue;
                }
                write_high_speed_byte(static_cast<std::uint16_t>(x),
                                      static_cast<std::uint16_t>(dst_y),
                                      read_high_speed_byte(static_cast<std::uint16_t>(x),
                                                           static_cast<std::uint16_t>(src_y),
                                                           source_expansion),
                                      active_mode, dest_expansion);
            }
        }
    }

    void v9938::execute_lmmc(std::uint8_t op) noexcept {
        start_cpu_to_vram_command(command_stream_kind::lmmc, op);
    }

    void v9938::execute_lmcm() noexcept { start_vram_to_cpu_command(); }

    void v9938::execute_lmmv(std::uint8_t op) noexcept {
        const std::uint16_t dx = command_x(36, 37);
        const std::uint16_t dy = command_y(38, 39);
        const std::uint16_t nx = command_x(40, 41);
        const std::uint16_t ny = command_y(42, 43);
        if (nx == 0U || ny == 0U) {
            return;
        }

        const int x_step = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        const bool dest_expansion = command_destination_uses_expansion();
        for (std::uint16_t row = 0; row < ny; ++row) {
            const int y = static_cast<int>(dy) + static_cast<int>(row) * y_step;
            if (y < 0) {
                continue;
            }
            for (std::uint16_t col = 0; col < nx; ++col) {
                const int x = static_cast<int>(dx) + static_cast<int>(col) * x_step;
                if (x < 0) {
                    continue;
                }
                write_command_pixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                    reg_[44], dest_expansion, op);
            }
        }
    }

    void v9938::execute_lmmm(std::uint8_t op) noexcept {
        const std::uint16_t sx = command_x(32, 33);
        const std::uint16_t sy = command_y(34, 35);
        const std::uint16_t dx = command_x(36, 37);
        const std::uint16_t dy = command_y(38, 39);
        const std::uint16_t nx = command_x(40, 41);
        const std::uint16_t ny = command_y(42, 43);
        if (nx == 0U || ny == 0U) {
            return;
        }

        const int x_step = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        const bool source_expansion = command_source_uses_expansion();
        const bool dest_expansion = command_destination_uses_expansion();
        for (std::uint16_t row = 0; row < ny; ++row) {
            const int src_y = static_cast<int>(sy) + static_cast<int>(row) * y_step;
            const int dst_y = static_cast<int>(dy) + static_cast<int>(row) * y_step;
            if (dst_y < 0) {
                continue;
            }
            for (std::uint16_t col = 0; col < nx; ++col) {
                const int src_x = static_cast<int>(sx) + static_cast<int>(col) * x_step;
                const int dst_x = static_cast<int>(dx) + static_cast<int>(col) * x_step;
                if (dst_x < 0) {
                    continue;
                }
                std::uint8_t colour = 0U;
                if (src_x >= 0 && src_y >= 0) {
                    colour = read_command_pixel(static_cast<std::uint16_t>(src_x),
                                                static_cast<std::uint16_t>(src_y),
                                                source_expansion);
                }
                write_command_pixel(static_cast<std::uint16_t>(dst_x),
                                    static_cast<std::uint16_t>(dst_y), colour, dest_expansion, op);
            }
        }
    }

    void v9938::execute_line(std::uint8_t op) noexcept {
        int x = command_x(36, 37);
        int y = command_y(38, 39);
        const int major = command_x(40, 41);
        const int minor = command_y(42, 43);
        const int x_step = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        const int y_step = (reg_[45] & 0x08U) != 0U ? -1 : 1;
        const bool major_is_y = (reg_[45] & 0x01U) != 0U;
        const bool dest_expansion = command_destination_uses_expansion();
        int error = major / 2;

        for (int i = 0; i <= major; ++i) {
            if (x >= 0 && y >= 0) {
                write_command_pixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                    reg_[44], dest_expansion, op);
            }

            if (major_is_y) {
                y += y_step;
                error -= minor;
                if (error < 0) {
                    x += x_step;
                    error += major;
                }
            } else {
                x += x_step;
                error -= minor;
                if (error < 0) {
                    y += y_step;
                    error += major;
                }
            }
        }
    }

    void v9938::execute_srch() noexcept {
        const std::uint16_t sx = command_x(32, 33);
        const std::uint16_t sy = command_y(34, 35);
        const std::uint16_t width = command_screen_width();
        if (width == 0U || sx >= width) {
            status_[2] &= static_cast<std::uint8_t>(~k_status_border_found);
            return;
        }

        std::uint32_t dummy_address = 0U;
        std::uint8_t dummy_shift = 0U;
        std::uint8_t pixel_mask = 0U;
        if (!command_pixel_address(sx, sy, dummy_address, dummy_shift, pixel_mask)) {
            status_[2] &= static_cast<std::uint8_t>(~k_status_border_found);
            return;
        }
        const std::uint8_t border = static_cast<std::uint8_t>(reg_[44] & pixel_mask);
        const bool stop_on_border_colour = (reg_[45] & 0x02U) == 0U;
        const int x_step = (reg_[45] & 0x04U) != 0U ? -1 : 1;
        const bool source_expansion = command_source_uses_expansion();
        for (int x = sx; x >= 0 && x < width; x += x_step) {
            const std::uint8_t colour =
                read_command_pixel(static_cast<std::uint16_t>(x), sy, source_expansion);
            const bool matches =
                stop_on_border_colour ? colour == border : colour != border;
            if (!matches) {
                continue;
            }

            status_[2] |= k_status_border_found;
            status_[8] = static_cast<std::uint8_t>(x & 0xFF);
            status_[9] = static_cast<std::uint8_t>(0xFEU | ((x >> 8) & 0x01));
            return;
        }

        status_[2] &= static_cast<std::uint8_t>(~k_status_border_found);
    }

    void v9938::execute_pset(std::uint8_t op) noexcept {
        write_command_pixel(command_x(36, 37), command_y(38, 39), reg_[44],
                            command_destination_uses_expansion(), op);
    }

    void v9938::execute_point() noexcept {
        status_[7] =
            read_command_pixel(command_x(32, 33), command_y(34, 35),
                               command_source_uses_expansion());
    }

    void v9938::execute_command(std::uint8_t command) noexcept {
        const std::uint64_t busy_cycles = estimate_command_busy_cycles(command);
        stop_command();
        status_[2] &= static_cast<std::uint8_t>(~k_status_border_found);
        status_[2] |= k_status_command_execute;

        switch (command & 0xF0U) {
        case 0xF0U:
            execute_hmmc();
            break;
        case 0xE0U:
            execute_ymmm();
            break;
        case 0xC0U:
            execute_hmmv();
            break;
        case 0xD0U:
            execute_hmmm();
            break;
        case 0xB0U:
            execute_lmmc(static_cast<std::uint8_t>(command & 0x0FU));
            break;
        case 0xA0U:
            execute_lmcm();
            break;
        case 0x80U:
            execute_lmmv(static_cast<std::uint8_t>(command & 0x0FU));
            break;
        case 0x90U:
            execute_lmmm(static_cast<std::uint8_t>(command & 0x0FU));
            break;
        case 0x70U:
            execute_line(static_cast<std::uint8_t>(command & 0x0FU));
            break;
        case 0x60U:
            execute_srch();
            break;
        case 0x50U:
            execute_pset(static_cast<std::uint8_t>(command & 0x0FU));
            break;
        case 0x40U:
            execute_point();
            break;
        case 0x00U:
            stop_command();
            return;
        default:
            break;
        }

        if (command_stream_ == command_stream_kind::none) {
            apply_command_register_postconditions(command);
            arm_command_busy(busy_cycles);
        }
    }

    void v9938::render_graphics_i_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const std::uint32_t nt = name_base_tms(reg_);
        const std::uint32_t pt = pattern_base_g1(reg_);
        const std::uint32_t ct = color_base_g1(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
            const std::uint8_t bits = vram_at(
                pt + static_cast<std::uint32_t>(pattern) * 8U +
                static_cast<std::uint32_t>(fine_y));
            const std::uint8_t colours =
                vram_at(ct + static_cast<std::uint32_t>(pattern >> 3U));
            const std::uint8_t fg = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t bg = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 8; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[col * 8 + px] = paletted_display_rgb(on ? fg : bg);
            }
        }
    }

    void v9938::render_graphics_ii_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const int page = line >> 6;
        const std::uint32_t nt = name_base_tms(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
            const std::uint8_t bits =
                vram_at(graphics_ii_pattern_address(reg_, pattern, page, fine_y));
            const std::uint8_t colours =
                vram_at(graphics_ii_color_address(reg_, pattern, page, fine_y));
            const std::uint8_t fg = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t bg = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 8; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[col * 8 + px] = paletted_display_rgb(on ? fg : bg);
            }
        }
    }

    void v9938::render_text_i_scanline(int line, std::uint32_t* out) noexcept {
        const std::uint8_t bg_colour = static_cast<std::uint8_t>(reg_[7] & 0x0FU);
        const std::uint32_t bg = paletted_display_rgb(bg_colour);
        std::fill(out, out + display_width_256, bg);

        const int row = line >> 3;
        const int fine_y = line & 7;
        const std::uint32_t nt = name_base_tms(reg_);
        const std::uint32_t pt = pattern_base_g1(reg_);
        const std::uint8_t fg = static_cast<std::uint8_t>(reg_[7] >> 4U);
        constexpr int x_offset = 8;
        for (int col = 0; col < 40; ++col) {
            const std::uint8_t pattern =
                vram_at(nt + static_cast<std::uint32_t>(row * 40 + col));
            const std::uint8_t bits = vram_at(
                pt + static_cast<std::uint32_t>(pattern) * 8U +
                static_cast<std::uint32_t>(fine_y));
            for (int px = 0; px < 6; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[x_offset + col * 6 + px] = paletted_display_rgb(on ? fg : bg_colour);
            }
        }
    }

    bool v9938::text_ii_cell_blink_enabled(int row, int col) const noexcept {
        constexpr int columns = 80;
        constexpr int bits_per_byte = 8;
        const std::uint32_t offset =
            static_cast<std::uint32_t>(row * (columns / bits_per_byte) + (col / bits_per_byte));
        const std::uint8_t mask = static_cast<std::uint8_t>(0x80U >> (col & 7));
        return (vram_at(blink_base_text_ii(reg_) + offset) & mask) != 0U;
    }

    bool v9938::text_ii_blink_phase_active() const noexcept {
        const std::uint8_t original_units = static_cast<std::uint8_t>(reg_[13] >> 4U);
        const std::uint8_t blink_units = static_cast<std::uint8_t>(reg_[13] & 0x0FU);
        if (original_units == 0U || blink_units == 0U) {
            return false;
        }

        const std::uint64_t frames_per_unit = pal_mode_ ? 8U : 10U;
        const std::uint64_t period_units =
            static_cast<std::uint64_t>(original_units) + static_cast<std::uint64_t>(blink_units);
        const std::uint64_t phase = (frame_index_ / frames_per_unit) % period_units;
        return phase >= original_units;
    }

    void v9938::render_text_ii_scanline(int line, std::uint32_t* out) noexcept {
        const std::uint8_t bg_colour = static_cast<std::uint8_t>(reg_[7] & 0x0FU);
        const std::uint32_t bg = paletted_display_rgb(bg_colour);
        std::fill(out, out + max_width, bg);

        const int row = line >> 3;
        const int fine_y = line & 7;
        const std::uint32_t nt = name_base_text_ii(reg_);
        const std::uint32_t pt = pattern_base_g1(reg_);
        const std::uint8_t fg = static_cast<std::uint8_t>(reg_[7] >> 4U);
        const std::uint8_t blink_fg = static_cast<std::uint8_t>(reg_[12] >> 4U);
        const std::uint8_t blink_bg = static_cast<std::uint8_t>(reg_[12] & 0x0FU);
        const bool blink_phase = text_ii_blink_phase_active();
        constexpr int columns = 80;
        constexpr int x_offset = 16;
        for (int col = 0; col < columns; ++col) {
            const std::uint8_t pattern =
                vram_at(nt + static_cast<std::uint32_t>(row * columns + col));
            const std::uint8_t bits = vram_at(
                pt + static_cast<std::uint32_t>(pattern) * 8U +
                static_cast<std::uint32_t>(fine_y));
            const bool blink_cell = blink_phase && text_ii_cell_blink_enabled(row, col);
            const std::uint8_t cell_fg = blink_cell ? blink_fg : fg;
            const std::uint8_t cell_bg = blink_cell ? blink_bg : bg_colour;
            for (int px = 0; px < 6; ++px) {
                const bool on = (bits & (0x80U >> px)) != 0U;
                out[x_offset + col * 6 + px] = paletted_display_rgb(on ? cell_fg : cell_bg);
            }
        }
    }

    void v9938::render_multicolor_scanline(int line, std::uint32_t* out) noexcept {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const std::uint32_t nt = name_base_tms(reg_);
        const std::uint32_t pt = pattern_base_g1(reg_);
        for (int col = 0; col < 32; ++col) {
            const std::uint8_t pattern =
                vram_at(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
            const std::uint16_t row_pair =
                static_cast<std::uint16_t>(((tile_y & 3) * 2) + ((fine_y >> 2) & 1));
            const std::uint8_t colours =
                vram_at(pt + static_cast<std::uint32_t>(pattern) * 8U + row_pair);
            const std::uint8_t left = static_cast<std::uint8_t>(colours >> 4U);
            const std::uint8_t right = static_cast<std::uint8_t>(colours & 0x0FU);
            for (int px = 0; px < 4; ++px) {
                out[col * 8 + px] = paletted_display_rgb(left);
                out[col * 8 + 4 + px] = paletted_display_rgb(right);
            }
        }
    }

    void v9938::render_graphics_iv_scanline(int line, bool second_field,
                                            std::uint32_t* out) noexcept {
        const std::uint32_t base = active_bitmap_base_graphics_iv(second_field);
        const std::uint32_t row = static_cast<std::uint32_t>(line) * 128U;
        for (int x = 0; x < display_width_256; x += 2) {
            const std::uint8_t packed =
                vram_[(base + row + static_cast<std::uint32_t>(x / 2)) & (vram_size - 1U)];
            out[x] = paletted_display_rgb(static_cast<std::uint8_t>(packed >> 4U));
            out[x + 1] = paletted_display_rgb(static_cast<std::uint8_t>(packed & 0x0FU));
        }
    }

    void v9938::render_graphics_v_scanline(int line, bool second_field,
                                           std::uint32_t* out) noexcept {
        const std::uint32_t base = active_bitmap_base_graphics_iv(second_field);
        const std::uint32_t row = static_cast<std::uint32_t>(line) * 128U;
        for (int x = 0; x < max_width; x += 4) {
            const std::uint8_t packed =
                vram_[(base + row + static_cast<std::uint32_t>(x / 4)) & (vram_size - 1U)];
            out[x] = paletted_display_rgb(static_cast<std::uint8_t>((packed >> 6U) & 0x03U));
            out[x + 1] = paletted_display_rgb(static_cast<std::uint8_t>((packed >> 4U) & 0x03U));
            out[x + 2] = paletted_display_rgb(static_cast<std::uint8_t>((packed >> 2U) & 0x03U));
            out[x + 3] = paletted_display_rgb(static_cast<std::uint8_t>(packed & 0x03U));
        }
    }

    void v9938::render_graphics_vi_scanline(int line, bool second_field,
                                            std::uint32_t* out) noexcept {
        const std::uint32_t base = active_bitmap_base_graphics_vi(second_field);
        const std::uint32_t row = static_cast<std::uint32_t>(line) * 256U;
        for (int x = 0; x < max_width; x += 2) {
            const std::uint8_t packed =
                vram_[(base + row + static_cast<std::uint32_t>(x / 2)) & (vram_size - 1U)];
            out[x] = paletted_display_rgb(static_cast<std::uint8_t>(packed >> 4U));
            out[x + 1] = paletted_display_rgb(static_cast<std::uint8_t>(packed & 0x0FU));
        }
    }

    void v9938::render_graphics_vii_scanline(int line, bool second_field,
                                             std::uint32_t* out) noexcept {
        const std::uint32_t base = active_bitmap_base_graphics_vi(second_field);
        const std::uint32_t row = static_cast<std::uint32_t>(line) * 256U;
        for (int x = 0; x < display_width_256; ++x) {
            const std::uint8_t colour =
                vram_[(base + row + static_cast<std::uint32_t>(x)) & (vram_size - 1U)];
            out[x] = display_rgb(fixed_rgb(colour));
        }
    }

    int v9938::vertical_scrolled_line(int line) const noexcept {
        return (line + static_cast<int>(reg_[23])) & 0xFF;
    }

    bool v9938::second_display_field() const noexcept { return (frame_index_ & 1U) != 0U; }

    bool v9938::alternate_display_uses_odd_page(std::uint8_t selected_page,
                                                bool second_field) const noexcept {
        if ((selected_page & 1U) == 0U) {
            return false;
        }
        if ((reg_[9] & k_register9_even_odd_page) != 0U) {
            return second_field;
        }

        const std::uint8_t even_units = static_cast<std::uint8_t>(reg_[13] >> 4U);
        const std::uint8_t odd_units = static_cast<std::uint8_t>(reg_[13] & 0x0FU);
        if (even_units == 0U && odd_units == 0U) {
            return true;
        }
        if (even_units == 0U) {
            return true;
        }
        if (odd_units == 0U) {
            return false;
        }

        const std::uint64_t frames_per_unit = pal_mode_ ? 8U : 10U;
        const std::uint64_t period_units =
            static_cast<std::uint64_t>(even_units) + static_cast<std::uint64_t>(odd_units);
        const std::uint64_t phase = (frame_index_ / frames_per_unit) % period_units;
        return phase >= even_units;
    }

    std::uint32_t v9938::active_bitmap_base_graphics_iv(bool second_field) const noexcept {
        const std::uint8_t selected_page = static_cast<std::uint8_t>((reg_[2] >> 5U) & 0x03U);
        const std::uint8_t page = static_cast<std::uint8_t>(
            (selected_page & 0xFEU) |
            (alternate_display_uses_odd_page(selected_page, second_field) ? 1U : 0U));
        return static_cast<std::uint32_t>(page) * 0x8000U;
    }

    std::uint32_t v9938::active_bitmap_base_graphics_vi(bool second_field) const noexcept {
        const std::uint8_t selected_page = (reg_[2] & 0x40U) != 0U ? 1U : 0U;
        const std::uint8_t page =
            alternate_display_uses_odd_page(selected_page, second_field) ? 1U : 0U;
        return static_cast<std::uint32_t>(page) * 0x10000U;
    }

    void v9938::render_sprites_mode1(int line, std::uint32_t* out) noexcept {
        if ((reg_[8] & 0x02U) != 0U) {
            return;
        }

        const int base_size = sprite_16x16(reg_) ? 16 : 8;
        const int zoom = sprite_magnified(reg_) ? 2 : 1;
        const int visible_size = base_size * zoom;
        const std::uint32_t sat = sprite_attr_base(reg_);
        const std::uint32_t spt = sprite_pattern_base(reg_);

        std::array<bool, display_width_256> collision_occupied{};
        std::array<bool, display_width_256> draw_occupied{};
        int sprites_on_line = 0;
        const bool colour0_visible = (reg_[8] & k_register8_colour0_opaque) != 0U;

        for (int i = 0; i < 32; ++i) {
            const std::uint32_t attr = sat + static_cast<std::uint32_t>(i) * 4U;
            const std::uint8_t raw_y = vram_at(attr);
            if (raw_y == 0xD0U) {
                break;
            }
            const int sy = signed_sprite_y(raw_y);
            if (line < sy || line >= sy + visible_size) {
                continue;
            }

            if (sprites_on_line >= 4) {
                if ((status_[0] & 0x40U) == 0U) {
                    status_[0] =
                        static_cast<std::uint8_t>((status_[0] & 0xE0U) | (i & 0x1F) | 0x40U);
                }
                continue;
            }
            ++sprites_on_line;

            int sx = vram_at(attr + 1U);
            std::uint8_t pattern = vram_at(attr + 2U);
            const std::uint8_t colour = vram_at(attr + 3U);
            if ((colour & 0x80U) != 0U) {
                sx -= 32;
            }
            const std::uint8_t pen = static_cast<std::uint8_t>(colour & 0x0FU);

            const int row = (line - sy) / zoom;
            if (base_size == 16) {
                pattern = static_cast<std::uint8_t>(pattern & 0xFCU);
            }
            const int tile_x = base_size == 16 ? 2 : 1;
            const int sub_y = row >> 3;
            const int row_in_tile = row & 7;

            for (int tx = 0; tx < tile_x; ++tx) {
                const std::uint8_t tile =
                    static_cast<std::uint8_t>(pattern + sub_y + (tx == 0 ? 0 : 2));
                const std::uint8_t bits =
                    vram_at(spt + static_cast<std::uint32_t>(tile) * 8U +
                            static_cast<std::uint32_t>(row_in_tile));
                for (int px = 0; px < 8; ++px) {
                    if ((bits & (0x80U >> px)) == 0U) {
                        continue;
                    }
                    for (int zx = 0; zx < zoom; ++zx) {
                        const int x = sx + ((tx * 8 + px) * zoom) + zx;
                        if (x < 0 || x >= display_width_256) {
                            continue;
                        }
                        if (pen == 0U && !colour0_visible) {
                            continue;
                        }
                        const auto xs = static_cast<std::size_t>(x);
                        if (collision_occupied[xs]) {
                            status_[0] |= 0x20U;
                        } else {
                            collision_occupied[xs] = true;
                        }
                        if (draw_occupied[xs]) {
                            continue;
                        }
                        draw_occupied[xs] = true;
                        out[x] = paletted_display_rgb(pen);
                    }
                }
            }
        }
    }

    void v9938::render_sprites_mode2(int line, std::uint32_t* out) noexcept {
        if ((reg_[8] & 0x02U) != 0U) {
            return;
        }

        const display_mode active_mode = mode();
        const int horizontal_scale =
            (active_mode == display_mode::graphics_v || active_mode == display_mode::graphics_vi)
                ? 2
                : 1;
        const int screen_width = visible_width();
        const int base_size = sprite_16x16(reg_) ? 16 : 8;
        const int zoom = sprite_magnified(reg_) ? 2 : 1;
        const int visible_height = base_size * zoom;
        const std::uint32_t sat = sprite_attr_base_mode2(reg_);
        const std::uint32_t sct = (sat - 512U) & (vram_size - 1U);
        const std::uint32_t spt = sprite_pattern_base(reg_);

        std::array<bool, max_width> collision_occupied{};
        std::array<bool, max_width> draw_occupied{};
        std::array<std::uint8_t, max_width> draw_pen{};
        int sprites_on_line = 0;
        const bool colour0_visible = (reg_[8] & k_register8_colour0_opaque) != 0U;

        for (int i = 0; i < 32; ++i) {
            const std::uint32_t attr = sat + static_cast<std::uint32_t>(i) * 4U;
            const std::uint8_t raw_y = vram_at(attr);
            if (raw_y == 0xD8U) {
                break;
            }
            const int sy = signed_sprite_y(raw_y);
            if (line < sy || line >= sy + visible_height) {
                continue;
            }

            if (sprites_on_line >= 8) {
                if ((status_[0] & 0x40U) == 0U) {
                    status_[0] =
                        static_cast<std::uint8_t>((status_[0] & 0xE0U) | (i & 0x1F) | 0x40U);
                }
                continue;
            }
            ++sprites_on_line;

            const int sprite_row = (line - sy) / zoom;
            const std::uint32_t colour_attr = (sct + static_cast<std::uint32_t>(i) * 16U +
                                               static_cast<std::uint32_t>(sprite_row)) &
                                              (vram_size - 1U);
            const std::uint8_t colour_byte = vram_at(colour_attr);
            const bool ec = (colour_byte & 0x80U) != 0U;
            const bool cc = (colour_byte & 0x40U) != 0U;
            const bool ic = (colour_byte & 0x20U) != 0U;
            const std::uint8_t pen = static_cast<std::uint8_t>(colour_byte & 0x0FU);
            if (pen == 0U && !colour0_visible) {
                continue;
            }

            int sx = vram_at(attr + 1U);
            if (ec) {
                sx -= 32;
            }
            std::uint8_t pattern = vram_at(attr + 2U);
            if (base_size == 16) {
                pattern = static_cast<std::uint8_t>(pattern & 0xFCU);
            }

            const int tile_x = base_size == 16 ? 2 : 1;
            const int sub_y = sprite_row >> 3;
            const int row_in_tile = sprite_row & 7;

            for (int tx = 0; tx < tile_x; ++tx) {
                const std::uint8_t tile =
                    static_cast<std::uint8_t>(pattern + sub_y + (tx == 0 ? 0 : 2));
                const std::uint8_t bits = vram_at(spt + static_cast<std::uint32_t>(tile) * 8U +
                                                  static_cast<std::uint32_t>(row_in_tile));
                for (int px = 0; px < 8; ++px) {
                    if ((bits & (0x80U >> px)) == 0U) {
                        continue;
                    }
                    for (int zx = 0; zx < zoom * horizontal_scale; ++zx) {
                        const int x = sx + ((tx * 8 + px) * zoom * horizontal_scale) + zx;
                        if (x < 0 || x >= screen_width) {
                            continue;
                        }
                        const auto xs = static_cast<std::size_t>(x);
                        if (!ic && !cc) {
                            if (collision_occupied[xs]) {
                                if ((status_[0] & 0x20U) == 0U) {
                                    const int cx = x + 12;
                                    const int cy = line + 8;
                                    status_[3] = static_cast<std::uint8_t>(cx & 0xFF);
                                    status_[4] = static_cast<std::uint8_t>(0xFEU | ((cx >> 8) & 1));
                                    status_[5] = static_cast<std::uint8_t>(cy & 0xFF);
                                    status_[6] = static_cast<std::uint8_t>(0xFCU | ((cy >> 8) & 3));
                                }
                                status_[0] |= 0x20U;
                            } else {
                                collision_occupied[xs] = true;
                            }
                        }

                        if (draw_occupied[xs]) {
                            if (cc) {
                                draw_pen[xs] = static_cast<std::uint8_t>(draw_pen[xs] | pen);
                                out[x] = paletted_display_rgb(draw_pen[xs]);
                            }
                            continue;
                        }
                        draw_occupied[xs] = true;
                        draw_pen[xs] = pen;
                        out[x] = paletted_display_rgb(pen);
                    }
                }
            }
        }
    }

    void v9938::render_unadjusted_scanline(int line, bool second_field,
                                           std::uint32_t* out) noexcept {
        if (line < 0 || line >= field_visible_height()) {
            return;
        }
        if (!display_enabled(reg_)) {
            std::fill(out, out + visible_width(), backdrop_rgb());
            return;
        }

        switch (mode()) {
        case display_mode::text_i:
            render_text_i_scanline(line, out);
            break;
        case display_mode::text_ii:
            render_text_ii_scanline(line, out);
            break;
        case display_mode::multicolor:
            render_multicolor_scanline(line, out);
            render_sprites_mode1(line, out);
            break;
        case display_mode::graphics_ii:
            render_graphics_ii_scanline(line, out);
            render_sprites_mode1(line, out);
            break;
        case display_mode::graphics_iii:
            render_graphics_ii_scanline(line, out);
            render_sprites_mode2(line, out);
            break;
        case display_mode::graphics_iv:
            render_graphics_iv_scanline(vertical_scrolled_line(line), second_field, out);
            render_sprites_mode2(vertical_scrolled_line(line), out);
            break;
        case display_mode::graphics_v:
            render_graphics_v_scanline(vertical_scrolled_line(line), second_field, out);
            render_sprites_mode2(vertical_scrolled_line(line), out);
            break;
        case display_mode::graphics_vi:
            render_graphics_vi_scanline(vertical_scrolled_line(line), second_field, out);
            render_sprites_mode2(vertical_scrolled_line(line), out);
            break;
        case display_mode::graphics_vii:
            render_graphics_vii_scanline(vertical_scrolled_line(line), second_field, out);
            render_sprites_mode2(vertical_scrolled_line(line), out);
            break;
        case display_mode::graphics_i:
            render_graphics_i_scanline(line, out);
            render_sprites_mode1(line, out);
            break;
        default:
            std::fill(out, out + visible_width(), backdrop_rgb());
            break;
        }
    }

    void v9938::render_scanline(int line) noexcept {
        if (line < 0 || line >= visible_height()) {
            return;
        }
        std::uint32_t* const out = framebuffer_.data() + static_cast<std::size_t>(line) * max_width;

        const int x_adjust = display_adjust_delta(reg_[18]);
        const int y_adjust = display_adjust_delta(static_cast<std::uint8_t>(reg_[18] >> 4U));
        if (x_adjust == 0 && y_adjust == 0) {
            render_unadjusted_scanline(display_line_to_field_line(line),
                                       display_line_uses_second_field(line), out);
            return;
        }

        std::array<std::uint32_t, max_width> adjusted{};
        const int width = visible_width();
        std::fill(adjusted.begin(), adjusted.begin() + width, backdrop_rgb());

        const int source_line = line - y_adjust;
        if (source_line >= 0 && source_line < visible_height()) {
            render_unadjusted_scanline(display_line_to_field_line(source_line),
                                       display_line_uses_second_field(source_line),
                                       adjusted.data());
        }

        std::fill(out, out + width, backdrop_rgb());
        for (int x = 0; x < width; ++x) {
            const int source_x = x - x_adjust;
            if (source_x >= 0 && source_x < width) {
                out[x] = adjusted[static_cast<std::size_t>(source_x)];
            }
        }
    }

    void v9938::render_frame() noexcept {
        for (int line = 0; line < visible_height(); ++line) {
            render_scanline(line);
        }
    }

    void v9938::update_irq() noexcept {
        const bool asserted =
            (((status_[0] & k_status_frame_irq) != 0U) && frame_irq_enabled(reg_)) ||
            (((status_[1] & k_status_scanline_irq) != 0U) && scanline_irq_enabled(reg_));
        if (asserted == irq_asserted_) {
            return;
        }
        irq_asserted_ = asserted;
        if (irq_callback_) {
            irq_callback_(asserted);
        }
    }

    void v9938::finish_scanline() noexcept {
        const int field_height = field_visible_height();
        if (scanline_ < field_height) {
            const int display_line = interlace_enabled()
                                         ? (scanline_ * 2 + (second_display_field() ? 1 : 0))
                                         : scanline_;
            render_scanline(display_line);
        } else if (scanline_ == field_height) {
            status_[0] |= k_status_frame_irq;
            update_irq();
        }
        if (scanline_ == static_cast<int>(reg_[19])) {
            status_[1] |= k_status_scanline_irq;
            update_irq();
        }

        ++scanline_;
        if (scanline_ >= total_scanlines_) {
            scanline_ = 0;
            ++frame_index_;
        }
    }

    void v9938::tick(std::uint64_t cycles) {
        advance_command_timers(cycles);
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++scanline_cycle_;
            if (scanline_cycle_ >= cycles_per_line) {
                scanline_cycle_ = 0;
                finish_scanline();
            }
        }
    }

    frame_buffer_view v9938::framebuffer() const noexcept {
        return {.pixels = framebuffer_.data(),
                .width = static_cast<std::uint32_t>(visible_width()),
                .height = static_cast<std::uint32_t>(visible_height()),
                .stride = static_cast<std::uint32_t>(max_width)};
    }

    void v9938::reset(reset_kind /*kind*/) {
        vram_.fill(0U);
        expanded_vram_.fill(0U);
        reg_.fill(0U);
        status_.fill(0U);
        palette_.fill(0U);
        palette_[1] = 0x000U; // black
        palette_[2] = 0x071U; // medium green
        palette_[3] = 0x0FBU; // light green
        palette_[4] = 0x04FU; // dark blue
        palette_[5] = 0x09FU; // light blue
        palette_[6] = 0x149U; // dark red
        palette_[7] = 0x0B7U; // cyan
        palette_[8] = 0x1C9U; // medium red
        palette_[9] = 0x1DBU; // light red
        palette_[10] = 0x1B1U; // dark yellow
        palette_[11] = 0x1B4U; // light yellow
        palette_[12] = 0x061U; // dark green
        palette_[13] = 0x195U; // magenta
        palette_[14] = 0x16DU; // gray
        palette_[15] = 0x1FFU; // white
        sync_palette_bytes();
        addr_low_ = 0U;
        code_ = 0U;
        cmd_pending_ = false;
        cmd_first_ = 0U;
        read_buffer_ = 0U;
        palette_second_ = false;
        palette_first_ = 0U;
        command_stream_ = command_stream_kind::none;
        stream_x_ = 0U;
        stream_y_ = 0U;
        stream_nx_ = 0U;
        stream_ny_ = 0U;
        stream_col_ = 0U;
        stream_row_ = 0U;
        stream_x_step_ = 1;
        stream_y_step_ = 1;
        stream_op_ = 0U;
        stream_high_speed_ = false;
        stream_source_expansion_ = false;
        stream_dest_expansion_ = false;
        stream_mode_ = display_mode::graphics_i;
        command_busy_cycles_ = 0U;
        stream_ready_delay_cycles_ = 0U;
        irq_asserted_ = false;
        scanline_ = 0;
        scanline_cycle_ = 0;
        total_scanlines_ = pal_mode_ ? scanlines_pal : scanlines_ntsc;
        frame_index_ = 0U;
        std::memset(framebuffer_.data(), 0, framebuffer_.size() * sizeof(std::uint32_t));
        update_irq();
    }

    void v9938::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(vram_);
        writer.bytes(expanded_vram_);
        writer.bytes(reg_);
        writer.bytes(status_);
        for (std::uint16_t colour : palette_) {
            writer.u16(colour);
        }
        writer.u16(addr_low_);
        writer.u8(code_);
        writer.boolean(cmd_pending_);
        writer.u8(cmd_first_);
        writer.u8(read_buffer_);
        writer.boolean(palette_second_);
        writer.u8(palette_first_);
        writer.u8(static_cast<std::uint8_t>(command_stream_));
        writer.u16(stream_x_);
        writer.u16(stream_y_);
        writer.u16(stream_nx_);
        writer.u16(stream_ny_);
        writer.u16(stream_col_);
        writer.u16(stream_row_);
        writer.u8(stream_x_step_ < 0 ? 1U : 0U);
        writer.u8(stream_y_step_ < 0 ? 1U : 0U);
        writer.u8(stream_op_);
        writer.boolean(stream_high_speed_);
        writer.boolean(stream_source_expansion_);
        writer.boolean(stream_dest_expansion_);
        writer.u8(static_cast<std::uint8_t>(stream_mode_));
        writer.u64(command_busy_cycles_);
        writer.u64(stream_ready_delay_cycles_);
        writer.boolean(irq_asserted_);
        writer.u32(static_cast<std::uint32_t>(scanline_));
        writer.u32(static_cast<std::uint32_t>(scanline_cycle_));
        writer.u32(static_cast<std::uint32_t>(total_scanlines_));
        writer.boolean(pal_mode_);
        writer.u64(frame_index_);
    }

    void v9938::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version < 1U || version > k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(vram_);
        if (version >= 4U) {
            reader.bytes(expanded_vram_);
        } else {
            expanded_vram_.fill(0U);
        }
        reader.bytes(reg_);
        reader.bytes(status_);
        for (std::uint16_t& colour : palette_) {
            colour = reader.u16();
        }
        sync_palette_bytes();
        addr_low_ = reader.u16();
        code_ = reader.u8();
        cmd_pending_ = reader.boolean();
        cmd_first_ = reader.u8();
        read_buffer_ = reader.u8();
        palette_second_ = reader.boolean();
        palette_first_ = reader.u8();
        if (version >= 2U) {
            const auto raw_stream = reader.u8();
            command_stream_ = raw_stream <= static_cast<std::uint8_t>(command_stream_kind::lmcm)
                                  ? static_cast<command_stream_kind>(raw_stream)
                                  : command_stream_kind::none;
            stream_x_ = reader.u16();
            stream_y_ = reader.u16();
            stream_nx_ = reader.u16();
            stream_ny_ = reader.u16();
            stream_col_ = reader.u16();
            stream_row_ = reader.u16();
            stream_x_step_ = reader.u8() != 0U ? -1 : 1;
            stream_y_step_ = reader.u8() != 0U ? -1 : 1;
            stream_op_ = reader.u8();
            stream_high_speed_ = reader.boolean();
            if (version >= 4U) {
                stream_source_expansion_ = reader.boolean();
                stream_dest_expansion_ = reader.boolean();
            } else {
                stream_source_expansion_ = false;
                stream_dest_expansion_ = false;
            }
            const auto raw_mode = reader.u8();
            stream_mode_ = raw_mode <= static_cast<std::uint8_t>(display_mode::graphics_vii)
                               ? static_cast<display_mode>(raw_mode)
                               : display_mode::graphics_i;
        } else {
            command_stream_ = command_stream_kind::none;
            stream_x_ = 0U;
            stream_y_ = 0U;
            stream_nx_ = 0U;
            stream_ny_ = 0U;
            stream_col_ = 0U;
            stream_row_ = 0U;
            stream_x_step_ = 1;
            stream_y_step_ = 1;
            stream_op_ = 0U;
            stream_high_speed_ = false;
            stream_source_expansion_ = false;
            stream_dest_expansion_ = false;
            stream_mode_ = display_mode::graphics_i;
        }
        if (version >= 3U) {
            command_busy_cycles_ = reader.u64();
            stream_ready_delay_cycles_ = reader.u64();
        } else {
            command_busy_cycles_ = 0U;
            stream_ready_delay_cycles_ = 0U;
        }
        irq_asserted_ = reader.boolean();
        scanline_ = static_cast<int>(reader.u32());
        scanline_cycle_ = static_cast<int>(reader.u32());
        total_scanlines_ = static_cast<int>(reader.u32());
        pal_mode_ = reader.boolean();
        frame_index_ = reader.u64();
        reg_[14] &= 0x07U;
        reg_[15] &= 0x0FU;
        reg_[16] &= 0x0FU;
        reg_[17] &= 0xBFU;
        if (command_stream_ != command_stream_kind::none) {
            update_command_stream_status();
        } else if (command_busy_cycles_ != 0U) {
            status_[2] |= k_status_command_execute;
            status_[2] &= static_cast<std::uint8_t>(~k_status_transfer_ready);
        } else {
            status_[2] &=
                static_cast<std::uint8_t>(~(k_status_command_execute | k_status_transfer_ready));
        }
        update_irq();
    }

    namespace {
        [[maybe_unused]] const auto v9938_registration =
            register_factory("yamaha.v9938", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<v9938>(); });
    } // namespace

} // namespace mnemos::chips::video
