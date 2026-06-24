#include "v9938.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        constexpr std::uint32_t k_state_version = 4U;
        constexpr std::uint8_t status_vblank = 0x80U;
        constexpr std::uint8_t status_sprite_overflow = 0x40U;
        constexpr std::uint8_t status_sprite_collision = 0x20U;
        constexpr std::uint8_t status_sprite_number_mask = 0x1FU;
        constexpr std::uint8_t status_horizontal_interrupt = 0x01U;
        constexpr std::uint8_t status_light_detected = 0x80U;
        constexpr std::uint8_t status_pointing_switch_1 = 0x40U;
        constexpr std::uint8_t status_pointing_switch_2 = 0x80U;
        constexpr std::uint8_t status_command_execute = 0x01U;
        constexpr std::uint8_t status_display_field = 0x02U;
        constexpr std::uint8_t status_2_fixed = 0x0CU;
        constexpr std::uint8_t status_transfer_ready = 0x80U;
        constexpr std::uint8_t status_boundary_color = 0x10U;
        constexpr std::uint8_t status_horizontal_retrace = 0x20U;
        constexpr std::uint8_t status_vertical_retrace = 0x40U;
        constexpr std::uint8_t irq_enable_horizontal = 0x10U; // R#0 IE1
        constexpr std::uint8_t irq_enable_lightpen = 0x20U;   // R#0 IE2
        constexpr std::uint8_t irq_enable_vertical = 0x20U;   // R#1 IE0
        constexpr int horizontal_retrace_start_cycle = v9938::cycles_per_line - 32;
        constexpr std::uint8_t sprite_pixel_color_mask = 0x0FU;
        constexpr std::uint8_t sprite_pixel_visible = 0x10U;
        constexpr std::uint8_t sprite_pixel_collision_source = 0x20U;
        constexpr std::uint8_t sprite_pixel_priority_cancel = 0x40U;

        [[nodiscard]] constexpr std::uint8_t expand3(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((static_cast<unsigned>(value & 0x07U) * 255U) / 7U);
        }

        [[nodiscard]] constexpr std::uint32_t rgb3(std::uint8_t r, std::uint8_t g,
                                                   std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(expand3(r)) << 16U) |
                   (static_cast<std::uint32_t>(expand3(g)) << 8U) |
                   static_cast<std::uint32_t>(expand3(b));
        }

        constexpr std::array<std::uint32_t, v9938::palette_count> k_default_palette{{
            rgb3(0, 0, 0), // transparent/black
            rgb3(0, 0, 0),
            rgb3(1, 6, 1),
            rgb3(3, 7, 3),
            rgb3(1, 1, 7),
            rgb3(2, 3, 7),
            rgb3(5, 1, 1),
            rgb3(2, 6, 7),
            rgb3(7, 1, 1),
            rgb3(7, 3, 3),
            rgb3(6, 6, 1),
            rgb3(6, 6, 4),
            rgb3(1, 4, 1),
            rgb3(6, 2, 5),
            rgb3(5, 5, 5),
            rgb3(7, 7, 7),
        }};

        [[nodiscard]] std::uint32_t index(std::uint32_t x, std::uint32_t y) noexcept {
            return y * static_cast<std::uint32_t>(v9938::storage_width) + x;
        }

        [[nodiscard]] std::uint8_t pattern_byte(std::span<const std::uint8_t> vram,
                                                std::uint32_t addr) noexcept {
            return vram[addr % static_cast<std::uint32_t>(v9938::vram_size)];
        }

        [[nodiscard]] constexpr int display_adjust_axis(std::uint8_t value) noexcept {
            const int raw = static_cast<int>(value & 0x0FU);
            return raw <= 7 ? -raw : 16 - raw;
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

    v9938::display_mode v9938::active_mode() const noexcept {
        const bool m1 = (reg_[1] & 0x10U) != 0U;
        const bool m2 = (reg_[1] & 0x08U) != 0U;
        const bool m3 = (reg_[0] & 0x02U) != 0U;
        const bool m4 = (reg_[0] & 0x04U) != 0U;
        const bool m5 = (reg_[0] & 0x08U) != 0U;

        if (m4 && m5) {
            return display_mode::bitmap7; // SCREEN 8-style 256 x 212 x 256
        }
        if (m5 && m3) {
            return display_mode::bitmap4_512; // SCREEN 7-style 512 x 212 x 16
        }
        if (m5) {
            return display_mode::bitmap2_512; // SCREEN 6-style 512 x 212 x 4
        }
        if (m4) {
            return display_mode::bitmap4; // SCREEN 5-style 256 x 212 x 16
        }
        if (m1 && m3) {
            return display_mode::text2; // 80-column text approximation
        }
        if (m1 && !m2) {
            return display_mode::text1; // 40-column text
        }
        if (m2 && m3) {
            return display_mode::graphic3; // SCREEN 4: GRAPHIC 2 display with sprite mode 2
        }
        if (m2) {
            return display_mode::graphic2; // SCREEN 2 tile graphics
        }
        if (m3) {
            return display_mode::multicolor; // SCREEN 3: 64 x 48 4x4 colour blocks
        }
        return display_mode::graphic1;
    }

    int v9938::active_width() const noexcept {
        switch (active_mode()) {
        case display_mode::bitmap2_512:
        case display_mode::bitmap4_512:
        case display_mode::text2:
            return storage_width;
        default:
            return 256;
        }
    }

    int v9938::active_height() const noexcept {
        return (reg_[9] & 0x80U) != 0U ? visible_height : 192;
    }

    int v9938::framebuffer_height() const noexcept {
        const int height = active_height();
        return interlace_enabled() ? height * 2 : height;
    }

    bool v9938::interlace_enabled() const noexcept { return (reg_[9] & 0x08U) != 0U; }

    int v9938::framebuffer_y(int screen_y, int field) const noexcept {
        return interlace_enabled() ? screen_y * 2 + (field & 0x01) : screen_y;
    }

    int v9938::display_adjust_x() const noexcept { return display_adjust_axis(reg_[18]); }

    int v9938::display_adjust_y() const noexcept {
        return display_adjust_axis(static_cast<std::uint8_t>(reg_[18] >> 4U));
    }

    int v9938::display_source_y(int screen_y) const noexcept {
        return (screen_y + static_cast<int>(reg_[23])) & 0xFF;
    }

    std::uint32_t v9938::bitmap_display_base(display_mode mode) const noexcept {
        switch (mode) {
        case display_mode::bitmap4:
        case display_mode::bitmap2_512:
            return (static_cast<std::uint32_t>(reg_[2] & 0x60U) << 10U) &
                   static_cast<std::uint32_t>(vram_size - 1);
        case display_mode::bitmap4_512:
        case display_mode::bitmap7:
            return (static_cast<std::uint32_t>(reg_[2] & 0x40U) << 10U) &
                   static_cast<std::uint32_t>(vram_size - 1);
        default:
            return 0U;
        }
    }

    std::uint32_t v9938::bitmap_page_stride(display_mode mode) const noexcept {
        switch (mode) {
        case display_mode::bitmap4:
        case display_mode::bitmap2_512:
            return 0x8000U;
        case display_mode::bitmap4_512:
        case display_mode::bitmap7:
            return 0x10000U;
        default:
            return 0U;
        }
    }

    bool v9938::register_13_odd_phase_active() const noexcept {
        const std::uint64_t even_units = static_cast<std::uint64_t>((reg_[13] >> 4U) & 0x0FU);
        const std::uint64_t odd_units = static_cast<std::uint64_t>(reg_[13] & 0x0FU);
        if (even_units == 0U && odd_units == 0U) {
            return false;
        }

        const std::uint64_t frames_per_unit = pal_mode_ ? 8U : 10U;
        const std::uint64_t even_frames = even_units * frames_per_unit;
        const std::uint64_t odd_frames = odd_units * frames_per_unit;
        const std::uint64_t cycle = even_frames + odd_frames;
        if (cycle == 0U) {
            return odd_frames != 0U;
        }

        const std::uint64_t elapsed =
            frame_index_ >= blink_start_frame_ ? frame_index_ - blink_start_frame_ : 0U;
        return (elapsed % cycle) >= even_frames;
    }

    std::uint32_t v9938::visible_bitmap_display_base(display_mode mode, int field) const noexcept {
        const std::uint32_t odd_base = bitmap_display_base(mode);
        const std::uint32_t stride = bitmap_page_stride(mode);
        if (stride == 0U) {
            return odd_base;
        }

        const std::uint32_t even_base =
            (odd_base - stride) & static_cast<std::uint32_t>(vram_size - 1);
        if ((reg_[9] & 0x04U) != 0U) {
            if (interlace_enabled()) {
                return (field & 0x01) != 0 ? odd_base : even_base;
            }
            return (frame_index_ & 1U) != 0U ? odd_base : even_base;
        }
        if (reg_[13] != 0U) {
            return register_13_odd_phase_active() ? odd_base : even_base;
        }
        return odd_base;
    }

    frame_buffer_view v9938::framebuffer() const noexcept {
        return {.pixels = framebuffer_.data(),
                .width = static_cast<std::uint32_t>(active_width()),
                .height = static_cast<std::uint32_t>(framebuffer_height()),
                .stride = static_cast<std::uint32_t>(storage_width)};
    }

    bool v9938::bw_output_enabled() const noexcept { return (reg_[8] & 0x01U) != 0U; }

    std::uint32_t v9938::display_rgb(std::uint32_t rgb) const noexcept {
        if (!bw_output_enabled()) {
            return rgb;
        }

        const std::uint8_t r = static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU);
        const std::uint8_t g = static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU);
        const std::uint8_t b = static_cast<std::uint8_t>(rgb & 0xFFU);
        const unsigned luma = (77U * static_cast<unsigned>(r) + 150U * static_cast<unsigned>(g) +
                               29U * static_cast<unsigned>(b) + 128U) >>
                              8U;
        const unsigned tone = (luma * 31U + 127U) / 255U;
        const unsigned gray = (tone * 255U + 15U) / 31U;
        return (gray << 16U) | (gray << 8U) | gray;
    }

    std::uint32_t v9938::backdrop_rgb() const noexcept {
        return active_mode() == display_mode::bitmap7
                   ? fixed_332_rgb(reg_[7])
                   : palette_rgb(static_cast<std::uint8_t>(reg_[7] & 0x0FU));
    }

    std::uint32_t v9938::palette_rgb(std::uint8_t index_value) const noexcept {
        return display_rgb(palette_[index_value & 0x0FU]);
    }

    std::uint32_t v9938::palette_display_rgb(std::uint8_t index_value) const noexcept {
        const std::uint8_t color = static_cast<std::uint8_t>(index_value & 0x0FU);
        return color == 0U && !color_zero_opaque() ? backdrop_rgb() : palette_rgb(color);
    }

    std::uint32_t v9938::sprite_rgb(std::uint8_t index_value) const noexcept {
        const std::uint8_t color = static_cast<std::uint8_t>(index_value & 0x0FU);
        if (color == 0U && active_mode() == display_mode::bitmap7) {
            return fixed_332_rgb(0U);
        }
        return palette_rgb(color);
    }

    bool v9938::color_zero_opaque() const noexcept { return (reg_[8] & 0x20U) != 0U; }

    std::uint32_t v9938::text2_blink_table_base() const noexcept {
        return ((static_cast<std::uint32_t>(reg_[10] & 0x07U) << 14U) |
                (static_cast<std::uint32_t>(reg_[3] & 0xF8U) << 6U)) &
               static_cast<std::uint32_t>(vram_size - 1);
    }

    bool v9938::text2_blink_phase_active() const noexcept { return register_13_odd_phase_active(); }

    std::uint32_t v9938::fixed_332_rgb(std::uint8_t value) const noexcept {
        const std::uint8_t r = static_cast<std::uint8_t>((value >> 5U) & 0x07U);
        const std::uint8_t g = static_cast<std::uint8_t>((value >> 2U) & 0x07U);
        const std::uint8_t b = static_cast<std::uint8_t>(value & 0x03U);
        const std::uint8_t b8 = static_cast<std::uint8_t>((static_cast<unsigned>(b) * 255U) / 3U);
        return display_rgb((static_cast<std::uint32_t>(expand3(r)) << 16U) |
                           (static_cast<std::uint32_t>(expand3(g)) << 8U) | b8);
    }

    std::uint32_t v9938::vram_addr() const noexcept {
        const std::uint32_t high =
            (static_cast<std::uint32_t>(reg_[14] & 0x07U) << 14U); // A14..A16
        return (high | static_cast<std::uint32_t>(addr_)) &
               static_cast<std::uint32_t>(vram_size - 1);
    }

    std::uint8_t v9938::status_register(std::uint8_t selected) const noexcept {
        if (selected >= status_.size()) {
            return 0xFFU;
        }
        switch (selected) {
        case 2:
            return status_register_2();
        case 4:
        case 9:
            return static_cast<std::uint8_t>(0xFEU | (status_[selected] & 0x01U));
        case 6:
            if (lightpen_enabled()) {
                return static_cast<std::uint8_t>(0xF8U | (status_[6] & 0x07U));
            }
            return static_cast<std::uint8_t>(0xFCU | (status_[6] & 0x03U));
        default:
            return status_[selected];
        }
    }

    std::uint8_t v9938::status_register_2() const noexcept {
        std::uint8_t value = static_cast<std::uint8_t>(status_[2] | status_2_fixed);
        if (scanline_cycle_ >= horizontal_retrace_start_cycle) {
            value = static_cast<std::uint8_t>(value | status_horizontal_retrace);
        }
        if (scanline_ >= active_height()) {
            value = static_cast<std::uint8_t>(value | status_vertical_retrace);
        }
        if ((frame_index_ & 1U) != 0U) {
            value = static_cast<std::uint8_t>(value | status_display_field);
        }
        return value;
    }

    bool v9938::lightpen_enabled() const noexcept { return (reg_[8] & 0xC0U) == 0x40U; }

    bool v9938::mouse_enabled() const noexcept { return (reg_[8] & 0xC0U) == 0x80U; }

    bool v9938::pointing_device_active() const noexcept { return (reg_[8] & 0xC0U) != 0U; }

    bool v9938::vram_address_auto_carries() const noexcept {
        switch (active_mode()) {
        case display_mode::graphic1:
        case display_mode::graphic2:
        case display_mode::graphic3:
        case display_mode::multicolor:
        case display_mode::text1:
            return false;
        case display_mode::text2:
        case display_mode::bitmap4:
        case display_mode::bitmap2_512:
        case display_mode::bitmap4_512:
        case display_mode::bitmap7:
            return true;
        }
        return true;
    }

    void v9938::advance_vram_addr() noexcept {
        addr_ = static_cast<std::uint16_t>((addr_ + 1U) & 0x3FFFU);
        if (addr_ == 0U && vram_address_auto_carries()) {
            reg_[14] = static_cast<std::uint8_t>((reg_[14] + 1U) & 0x07U);
        }
    }

    std::uint8_t v9938::data_read() noexcept {
        const std::uint8_t value = read_buffer_;
        read_buffer_ = vram_[vram_addr()];
        advance_vram_addr();
        ctrl_pending_ = false;
        return value;
    }

    void v9938::data_write(std::uint8_t value) noexcept {
        vram_[vram_addr()] = value;
        read_buffer_ = value;
        advance_vram_addr();
        ctrl_pending_ = false;
    }

    void v9938::latch_lightpen(int x, int y, bool switch_pressed, bool second_field) noexcept {
        if (!lightpen_enabled()) {
            return;
        }

        const int latched_x = std::clamp(x, 0, 0x1FF);
        const int latched_y = std::clamp(y, 0, 0x3FF);
        status_[3] = static_cast<std::uint8_t>(latched_x & 0xFF);
        status_[4] = static_cast<std::uint8_t>((latched_x >> 8) & 0x01);
        status_[5] = static_cast<std::uint8_t>(latched_y & 0xFF);
        status_[6] =
            static_cast<std::uint8_t>(((second_field ? 0x04U : 0x00U) | ((latched_y >> 8) & 0x03)));
        status_[1] = static_cast<std::uint8_t>((status_[1] & ~status_pointing_switch_1) |
                                               status_light_detected |
                                               (switch_pressed ? status_pointing_switch_1 : 0x00U));
        update_irq();
    }

    void v9938::latch_mouse_delta(std::int8_t delta_x, std::int8_t delta_y, bool switch_1,
                                  bool switch_2) noexcept {
        if (!mouse_enabled()) {
            return;
        }

        status_[1] = static_cast<std::uint8_t>(
            (status_[1] & ~(status_pointing_switch_1 | status_pointing_switch_2)) |
            (switch_1 ? status_pointing_switch_1 : 0x00U) |
            (switch_2 ? status_pointing_switch_2 : 0x00U));

        const std::uint8_t selected_status = static_cast<std::uint8_t>(reg_[15] & 0x0FU);
        if (selected_status == 3U || selected_status == 5U) {
            return;
        }

        status_[3] = static_cast<std::uint8_t>(delta_x);
        status_[4] = 0U;
        status_[5] = static_cast<std::uint8_t>(delta_y);
        status_[6] = 0U;
    }

    std::uint8_t v9938::ctrl_read() noexcept {
        ctrl_pending_ = false;
        const std::uint8_t selected = static_cast<std::uint8_t>(reg_[15] & 0x0FU);
        const std::uint8_t value = status_register(selected);
        if (selected == 0U) {
            status_[0] =
                static_cast<std::uint8_t>(status_[0] & ~(status_vblank | status_sprite_collision));
            update_irq();
        } else if (selected == 1U) {
            std::uint8_t clear_mask = status_horizontal_interrupt;
            if (!mouse_enabled()) {
                clear_mask = static_cast<std::uint8_t>(clear_mask | status_light_detected);
            }
            status_[1] = static_cast<std::uint8_t>(status_[1] & ~clear_mask);
            update_irq();
        } else if (selected == 5U) {
            status_[3] = 0U;
            status_[4] = 0U;
            status_[5] = 0U;
            status_[6] = 0U;
        } else if (selected == 7U && command_cpu_read_transfer_) {
            command_stream_read();
        }
        return value;
    }

    void v9938::write_register(std::uint8_t index_value, std::uint8_t value) noexcept {
        index_value = static_cast<std::uint8_t>(index_value & 0x3FU);
        const std::uint8_t old_pointing_mode = static_cast<std::uint8_t>(reg_[8] & 0xC0U);
        reg_[index_value] = value;
        if (index_value == 0U || index_value == 1U) {
            update_irq();
        } else if (index_value == 8U) {
            if (old_pointing_mode != (value & 0xC0U)) {
                clear_pointing_device_status();
            }
        } else if (index_value == 15U) {
            ctrl_pending_ = false;
        } else if (index_value == 16U) {
            palette_index_ = static_cast<std::uint8_t>(value & 0x0FU);
            palette_pending_ = false;
        } else if (index_value == 9U) {
            set_pal((value & 0x02U) != 0U);
        } else if (index_value == 13U) {
            blink_start_frame_ = frame_index_;
        } else if (index_value == 44U && command_cpu_transfer_) {
            command_stream_write(value);
        } else if (index_value == 46U) {
            start_command(value);
        }
    }

    void v9938::ctrl_write(std::uint8_t value) noexcept {
        if (!ctrl_pending_) {
            ctrl_first_ = value;
            ctrl_pending_ = true;
            return;
        }

        ctrl_pending_ = false;
        if ((value & 0xC0U) == 0x80U) {
            write_register(static_cast<std::uint8_t>(value & 0x3FU), ctrl_first_);
            return;
        }

        addr_ = static_cast<std::uint16_t>(ctrl_first_ |
                                           (static_cast<std::uint16_t>(value & 0x3FU) << 8U));
        code_ = static_cast<std::uint8_t>((value >> 6U) & 0x03U);
        if (code_ == 0U) {
            read_buffer_ = vram_[vram_addr()];
            advance_vram_addr();
        }
    }

    void v9938::palette_write(std::uint8_t value) noexcept {
        if (!palette_pending_) {
            palette_first_ = value;
            palette_pending_ = true;
            return;
        }
        palette_pending_ = false;
        const std::uint8_t r = static_cast<std::uint8_t>((palette_first_ >> 4U) & 0x07U);
        const std::uint8_t b = static_cast<std::uint8_t>(palette_first_ & 0x07U);
        const std::uint8_t g = static_cast<std::uint8_t>(value & 0x07U);
        palette_[palette_index_ & 0x0FU] = rgb3(r, g, b);
        palette_index_ = static_cast<std::uint8_t>((palette_index_ + 1U) & 0x0FU);
        reg_[16] = palette_index_;
    }

    void v9938::register_indirect_write(std::uint8_t value) noexcept {
        const std::uint8_t index_value = static_cast<std::uint8_t>(reg_[17] & 0x3FU);
        write_register(index_value, value);
        if ((reg_[17] & 0x80U) == 0U) {
            reg_[17] = static_cast<std::uint8_t>((index_value + 1U) & 0x3FU);
        }
    }

    std::uint8_t v9938::mmio_read(std::uint16_t offset) {
        switch (offset & 0x03U) {
        case 0:
            return data_read();
        case 1:
            return ctrl_read();
        default:
            return 0xFFU;
        }
    }

    void v9938::mmio_write(std::uint16_t offset, std::uint8_t value) {
        switch (offset & 0x03U) {
        case 0:
            data_write(value);
            break;
        case 1:
            ctrl_write(value);
            break;
        case 2:
            palette_write(value);
            break;
        case 3:
            register_indirect_write(value);
            break;
        }
    }

    void v9938::update_irq() noexcept {
        const bool vertical =
            (status_[0] & status_vblank) != 0U && (reg_[1] & irq_enable_vertical) != 0U;
        const bool horizontal = (status_[1] & status_horizontal_interrupt) != 0U &&
                                (reg_[0] & irq_enable_horizontal) != 0U;
        const bool lightpen =
            (status_[1] & status_light_detected) != 0U && (reg_[0] & irq_enable_lightpen) != 0U;
        const bool asserted = vertical || horizontal || lightpen;
        if (asserted == irq_asserted_) {
            return;
        }
        irq_asserted_ = asserted;
        if (irq_callback_) {
            irq_callback_(asserted);
        }
    }

    void v9938::clear_pointing_device_status() noexcept {
        status_[1] = static_cast<std::uint8_t>(status_[1] &
                                               ~(status_light_detected | status_pointing_switch_1));
        status_[3] = 0U;
        status_[4] = 0U;
        status_[5] = 0U;
        status_[6] = 0U;
        update_irq();
    }

    void v9938::write_field_pixel(int x, int y, int field, std::uint32_t rgb) noexcept {
        const int out_y = framebuffer_y(y, field);
        if (x < 0 || x >= storage_width || out_y < 0 || out_y >= frame_height) {
            return;
        }
        framebuffer_[index(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(out_y))] = rgb;
    }

    void v9938::write_frame_pixel(int x, int y, std::uint32_t rgb) noexcept {
        write_field_pixel(x, y, 0, rgb);
        if (interlace_enabled()) {
            write_field_pixel(x, y, 1, rgb);
        }
    }

    int v9938::command_width() const noexcept {
        switch (active_mode()) {
        case display_mode::bitmap4:
        case display_mode::bitmap7:
            return 256;
        case display_mode::bitmap2_512:
        case display_mode::bitmap4_512:
            return storage_width;
        default:
            return 0;
        }
    }

    int v9938::command_height() const noexcept {
        switch (active_mode()) {
        case display_mode::bitmap4:
        case display_mode::bitmap2_512:
            return 1024;
        case display_mode::bitmap4_512:
        case display_mode::bitmap7:
            return 512;
        default:
            return 0;
        }
    }

    std::uint16_t v9938::command_x(int low_reg, int high_reg) const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(reg_[static_cast<std::size_t>(low_reg)]) |
            (static_cast<std::uint16_t>(reg_[static_cast<std::size_t>(high_reg)] & 0x01U) << 8U));
    }

    std::uint16_t v9938::command_y(int low_reg, int high_reg) const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(reg_[static_cast<std::size_t>(low_reg)]) |
            (static_cast<std::uint16_t>(reg_[static_cast<std::size_t>(high_reg)] & 0x03U) << 8U));
    }

    std::uint16_t v9938::command_nx() const noexcept {
        const std::uint16_t value = command_x(40, 41);
        return value == 0U ? 512U : value;
    }

    std::uint16_t v9938::command_ny() const noexcept {
        const std::uint16_t value = command_y(42, 43);
        return value == 0U ? 1024U : value;
    }

    int v9938::command_step_x() const noexcept { return (reg_[45] & 0x04U) != 0U ? -1 : 1; }

    int v9938::command_step_y() const noexcept { return (reg_[45] & 0x08U) != 0U ? -1 : 1; }

    std::uint8_t v9938::command_color_mask() const noexcept {
        switch (active_mode()) {
        case display_mode::bitmap2_512:
            return 0x03U;
        case display_mode::bitmap4:
        case display_mode::bitmap4_512:
            return 0x0FU;
        default:
            return 0xFFU;
        }
    }

    int v9938::high_speed_pixel_group_size() const noexcept {
        switch (active_mode()) {
        case display_mode::bitmap2_512:
            return 4;
        case display_mode::bitmap4:
        case display_mode::bitmap4_512:
            return 2;
        case display_mode::bitmap7:
        default:
            return 1;
        }
    }

    int v9938::high_speed_aligned_x(int x) const noexcept {
        const int group = high_speed_pixel_group_size();
        return group > 1 ? x & ~(group - 1) : x;
    }

    std::uint16_t v9938::high_speed_aligned_nx() const noexcept {
        const int group = high_speed_pixel_group_size();
        const std::uint16_t nx = command_nx();
        return group > 1 ? static_cast<std::uint16_t>(nx & ~static_cast<std::uint16_t>(group - 1))
                         : nx;
    }

    std::uint8_t v9938::command_pixel(int x, int y) const noexcept {
        const int width = command_width();
        const int height = command_height();
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return 0U;
        }
        if (active_mode() == display_mode::bitmap4) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 128 + x / 2);
            const std::uint8_t packed = pattern_byte(vram_, addr);
            return (x & 1) == 0 ? static_cast<std::uint8_t>(packed >> 4U)
                                : static_cast<std::uint8_t>(packed & 0x0FU);
        }
        if (active_mode() == display_mode::bitmap2_512) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 128 + x / 4);
            const std::uint8_t packed = pattern_byte(vram_, addr);
            const unsigned shift = static_cast<unsigned>((3 - (x & 0x03)) * 2);
            return static_cast<std::uint8_t>((packed >> shift) & 0x03U);
        }
        if (active_mode() == display_mode::bitmap4_512) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 256 + x / 2);
            const std::uint8_t packed = pattern_byte(vram_, addr);
            return (x & 1) == 0 ? static_cast<std::uint8_t>(packed >> 4U)
                                : static_cast<std::uint8_t>(packed & 0x0FU);
        }
        if (active_mode() == display_mode::bitmap7) {
            return pattern_byte(vram_, static_cast<std::uint32_t>(y * 256 + x));
        }
        return 0U;
    }

    std::uint8_t v9938::command_logic(std::uint8_t source,
                                      std::uint8_t destination) const noexcept {
        const std::uint8_t mask = command_color_mask();
        source = static_cast<std::uint8_t>(source & mask);
        destination = static_cast<std::uint8_t>(destination & mask);
        const std::uint8_t op = static_cast<std::uint8_t>(reg_[46] & 0x0FU);
        const bool transparent = (op & 0x08U) != 0U;
        if (transparent && source == 0U) {
            return destination;
        }

        switch (op & 0x07U) {
        case 0x00:
            return source;
        case 0x01:
            return static_cast<std::uint8_t>((source & destination) & mask);
        case 0x02:
            return static_cast<std::uint8_t>((source | destination) & mask);
        case 0x03:
            return static_cast<std::uint8_t>((source ^ destination) & mask);
        case 0x04:
            return static_cast<std::uint8_t>((~source) & mask);
        default:
            return source;
        }
    }

    void v9938::set_command_pixel(int x, int y, std::uint8_t value, bool logical) noexcept {
        const int width = command_width();
        const int height = command_height();
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::uint8_t source = static_cast<std::uint8_t>(value & command_color_mask());
        const std::uint8_t out = logical ? command_logic(source, command_pixel(x, y)) : source;
        if (active_mode() == display_mode::bitmap4) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 128 + x / 2);
            std::uint8_t packed = vram_[addr & static_cast<std::uint32_t>(vram_size - 1)];
            if ((x & 1) == 0) {
                packed = static_cast<std::uint8_t>((packed & 0x0FU) | (out << 4U));
            } else {
                packed = static_cast<std::uint8_t>((packed & 0xF0U) | out);
            }
            vram_[addr & static_cast<std::uint32_t>(vram_size - 1)] = packed;
        } else if (active_mode() == display_mode::bitmap2_512) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 128 + x / 4);
            const unsigned shift = static_cast<unsigned>((3 - (x & 0x03)) * 2);
            const std::uint8_t mask = static_cast<std::uint8_t>(0x03U << shift);
            std::uint8_t packed = vram_[addr & static_cast<std::uint32_t>(vram_size - 1)];
            packed = static_cast<std::uint8_t>((packed & ~mask) | ((out & 0x03U) << shift));
            vram_[addr & static_cast<std::uint32_t>(vram_size - 1)] = packed;
        } else if (active_mode() == display_mode::bitmap4_512) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 256 + x / 2);
            std::uint8_t packed = vram_[addr & static_cast<std::uint32_t>(vram_size - 1)];
            if ((x & 1) == 0) {
                packed = static_cast<std::uint8_t>((packed & 0x0FU) | (out << 4U));
            } else {
                packed = static_cast<std::uint8_t>((packed & 0xF0U) | out);
            }
            vram_[addr & static_cast<std::uint32_t>(vram_size - 1)] = packed;
        } else if (active_mode() == display_mode::bitmap7) {
            const std::uint32_t addr = static_cast<std::uint32_t>(y * 256 + x);
            vram_[addr & static_cast<std::uint32_t>(vram_size - 1)] = out;
        }
    }

    void v9938::set_high_speed_byte(int x, int y, int step_x, std::uint8_t value) noexcept {
        switch (active_mode()) {
        case display_mode::bitmap2_512:
            for (int dot = 0; dot < 4; ++dot) {
                const unsigned shift = static_cast<unsigned>((3 - dot) * 2);
                set_command_pixel(x + dot * step_x, y,
                                  static_cast<std::uint8_t>((value >> shift) & 0x03U), false);
            }
            break;
        case display_mode::bitmap4:
        case display_mode::bitmap4_512:
            set_command_pixel(x, y, static_cast<std::uint8_t>(value >> 4U), false);
            set_command_pixel(x + step_x, y, static_cast<std::uint8_t>(value & 0x0FU), false);
            break;
        case display_mode::bitmap7:
        default:
            set_command_pixel(x, y, value, false);
            break;
        }
    }

    void v9938::finish_command() noexcept {
        command_cpu_transfer_ = false;
        command_cpu_read_transfer_ = false;
        command_high_speed_transfer_ = false;
        status_[2] = static_cast<std::uint8_t>(status_[2] &
                                               ~(status_command_execute | status_transfer_ready));
    }

    void v9938::start_command(std::uint8_t value) noexcept {
        command_code_ = static_cast<std::uint8_t>((value >> 4U) & 0x0FU);
        command_cpu_transfer_ = false;
        command_cpu_read_transfer_ = false;
        command_high_speed_transfer_ = false;
        status_[2] = static_cast<std::uint8_t>(
            (status_[2] & ~(status_transfer_ready | status_boundary_color)) |
            status_command_execute);

        switch (command_code_) {
        case 0x00:
            finish_command();
            break;
        case 0x04:
            command_point();
            break;
        case 0x05:
            command_pset();
            break;
        case 0x06:
            command_search();
            break;
        case 0x07:
            command_line();
            break;
        case 0x08:
            command_fill(false, true);
            break;
        case 0x09:
            command_copy(true, false);
            break;
        case 0x0A:
            begin_cpu_read_transfer();
            break;
        case 0x0B:
            begin_cpu_transfer(false);
            break;
        case 0x0C:
            command_fill(true, false);
            break;
        case 0x0D:
            command_copy(false, true);
            break;
        case 0x0E:
            command_y_copy();
            break;
        case 0x0F:
            begin_cpu_transfer(true);
            break;
        default:
            finish_command();
            break;
        }
    }

    void v9938::begin_cpu_transfer(bool high_speed) noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }
        command_cpu_transfer_ = true;
        command_high_speed_transfer_ = high_speed;
        command_stream_x_ = 0U;
        command_stream_y_ = 0U;
        status_[2] = static_cast<std::uint8_t>(status_[2] | status_transfer_ready);
        command_stream_write(reg_[44]);
    }

    void v9938::command_stream_write(std::uint8_t value) noexcept {
        if (!command_cpu_transfer_) {
            return;
        }

        const int dx = static_cast<int>(command_x(36, 37));
        const int dy = static_cast<int>(command_y(38, 39));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        const std::uint16_t nx =
            command_high_speed_transfer_ ? high_speed_aligned_nx() : command_nx();
        const std::uint16_t ny = command_ny();
        if (nx == 0U) {
            finish_command();
            return;
        }

        if (command_high_speed_transfer_) {
            const int x0 = high_speed_aligned_x(dx) + static_cast<int>(command_stream_x_) * step_x;
            const int y = dy + static_cast<int>(command_stream_y_) * step_y;
            set_high_speed_byte(x0, y, step_x, value);
            command_stream_x_ = static_cast<std::uint16_t>(
                command_stream_x_ + static_cast<std::uint16_t>(high_speed_pixel_group_size()));
        } else {
            const int x = dx + static_cast<int>(command_stream_x_) * step_x;
            const int y = dy + static_cast<int>(command_stream_y_) * step_y;
            set_command_pixel(x, y, value, !command_high_speed_transfer_);
            ++command_stream_x_;
        }

        while (command_stream_x_ >= nx) {
            command_stream_x_ = static_cast<std::uint16_t>(command_stream_x_ - nx);
            ++command_stream_y_;
            if (command_stream_y_ >= ny) {
                finish_command();
                return;
            }
        }
    }

    void v9938::begin_cpu_read_transfer() noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }
        command_cpu_read_transfer_ = true;
        command_stream_x_ = 0U;
        command_stream_y_ = 0U;
        prepare_cpu_read_transfer();
        status_[2] = static_cast<std::uint8_t>(status_[2] | status_transfer_ready);
    }

    void v9938::prepare_cpu_read_transfer() noexcept {
        const int sx = static_cast<int>(command_x(32, 33));
        const int sy = static_cast<int>(command_y(34, 35));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        status_[7] = command_pixel(sx + static_cast<int>(command_stream_x_) * step_x,
                                   sy + static_cast<int>(command_stream_y_) * step_y);
    }

    void v9938::command_stream_read() noexcept {
        if (!command_cpu_read_transfer_) {
            return;
        }

        ++command_stream_x_;
        const std::uint16_t nx = command_nx();
        const std::uint16_t ny = command_ny();
        while (command_stream_x_ >= nx) {
            command_stream_x_ = static_cast<std::uint16_t>(command_stream_x_ - nx);
            ++command_stream_y_;
            if (command_stream_y_ >= ny) {
                finish_command();
                return;
            }
        }
        prepare_cpu_read_transfer();
    }

    void v9938::command_fill(bool high_speed, bool logical) noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }
        const int dx = static_cast<int>(command_x(36, 37));
        const int dy = static_cast<int>(command_y(38, 39));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        const std::uint16_t nx = high_speed ? high_speed_aligned_nx() : command_nx();
        const std::uint16_t ny = command_ny();
        if (nx == 0U) {
            finish_command();
            return;
        }
        const int x0 = high_speed ? high_speed_aligned_x(dx) : dx;
        const int group = high_speed ? high_speed_pixel_group_size() : 1;
        for (std::uint16_t y = 0U; y < ny; ++y) {
            for (std::uint16_t x = 0U; x < nx; x = static_cast<std::uint16_t>(x + group)) {
                const int dst_x = x0 + static_cast<int>(x) * step_x;
                const int dst_y = dy + static_cast<int>(y) * step_y;
                if (high_speed) {
                    set_high_speed_byte(dst_x, dst_y, step_x, reg_[44]);
                } else {
                    set_command_pixel(dst_x, dst_y, reg_[44], logical);
                }
            }
        }
        finish_command();
    }

    void v9938::command_y_copy() noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }

        const int width = command_width();
        const int x0 = high_speed_aligned_x(static_cast<int>(command_x(36, 37)));
        const int sy0 = static_cast<int>(command_y(34, 35));
        const int dy0 = static_cast<int>(command_y(38, 39));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        const std::uint16_t ny = command_ny();
        const int nx = step_x > 0 ? width - x0 : x0 + 1;
        if (nx <= 0) {
            finish_command();
            return;
        }

        std::vector<std::uint8_t> temp(static_cast<std::size_t>(nx) * ny);
        for (std::uint16_t y = 0U; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                temp[static_cast<std::size_t>(y) * static_cast<std::size_t>(nx) +
                     static_cast<std::size_t>(x)] =
                    command_pixel(x0 + x * step_x, sy0 + static_cast<int>(y) * step_y);
            }
        }
        for (std::uint16_t y = 0U; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                set_command_pixel(x0 + x * step_x, dy0 + static_cast<int>(y) * step_y,
                                  temp[static_cast<std::size_t>(y) * static_cast<std::size_t>(nx) +
                                       static_cast<std::size_t>(x)],
                                  false);
            }
        }
        finish_command();
    }

    void v9938::command_copy(bool logical, bool high_speed) noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }
        const int sx0 = high_speed ? high_speed_aligned_x(static_cast<int>(command_x(32, 33)))
                                   : static_cast<int>(command_x(32, 33));
        const int sy0 = static_cast<int>(command_y(34, 35));
        const int dx0 = high_speed ? high_speed_aligned_x(static_cast<int>(command_x(36, 37)))
                                   : static_cast<int>(command_x(36, 37));
        const int dy0 = static_cast<int>(command_y(38, 39));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        const std::uint16_t nx = high_speed ? high_speed_aligned_nx() : command_nx();
        const std::uint16_t ny = command_ny();
        if (nx == 0U) {
            finish_command();
            return;
        }

        std::vector<std::uint8_t> temp(static_cast<std::size_t>(nx) * ny);
        for (std::uint16_t y = 0U; y < ny; ++y) {
            for (std::uint16_t x = 0U; x < nx; ++x) {
                temp[static_cast<std::size_t>(y) * nx + x] = command_pixel(
                    sx0 + static_cast<int>(x) * step_x, sy0 + static_cast<int>(y) * step_y);
            }
        }
        for (std::uint16_t y = 0U; y < ny; ++y) {
            for (std::uint16_t x = 0U; x < nx; ++x) {
                set_command_pixel(dx0 + static_cast<int>(x) * step_x,
                                  dy0 + static_cast<int>(y) * step_y,
                                  temp[static_cast<std::size_t>(y) * nx + x], logical);
            }
        }
        finish_command();
    }

    void v9938::command_line() noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }

        int x = static_cast<int>(command_x(36, 37));
        int y = static_cast<int>(command_y(38, 39));
        const int major = static_cast<int>(command_y(40, 41));
        const int minor = static_cast<int>(command_x(42, 43));
        const int step_x = command_step_x();
        const int step_y = command_step_y();
        const bool y_major = (reg_[45] & 0x01U) != 0U;

        if (major == 0) {
            set_command_pixel(x, y, reg_[44], true);
            finish_command();
            return;
        }

        int error = major / 2;
        for (int i = 0; i <= major; ++i) {
            set_command_pixel(x, y, reg_[44], true);
            error -= minor;
            if (y_major) {
                y += step_y;
                if (error < 0) {
                    x += step_x;
                    error += major;
                }
            } else {
                x += step_x;
                if (error < 0) {
                    y += step_y;
                    error += major;
                }
            }
        }
        finish_command();
    }

    void v9938::command_search() noexcept {
        if (command_width() == 0 || command_height() == 0) {
            finish_command();
            return;
        }

        const int y = static_cast<int>(command_y(34, 35));
        const int step_x = command_step_x();
        const bool equal_ends = (reg_[45] & 0x02U) != 0U;
        const std::uint8_t border = static_cast<std::uint8_t>(reg_[44] & command_color_mask());
        int found_x = -1;

        if (y >= 0 && y < command_height()) {
            for (int x = static_cast<int>(command_x(32, 33)); x >= 0 && x < command_width();
                 x += step_x) {
                const bool equal = command_pixel(x, y) == border;
                if (equal == equal_ends) {
                    found_x = x;
                    break;
                }
            }
        }

        if (found_x >= 0) {
            status_[2] = static_cast<std::uint8_t>(status_[2] | status_boundary_color);
            status_[8] = static_cast<std::uint8_t>(found_x & 0xFF);
            status_[9] = static_cast<std::uint8_t>((found_x >> 8) & 0x01);
        } else {
            status_[2] = static_cast<std::uint8_t>(status_[2] & ~status_boundary_color);
            status_[8] = 0U;
            status_[9] = 0U;
        }
        finish_command();
    }

    void v9938::command_pset() noexcept {
        set_command_pixel(static_cast<int>(command_x(36, 37)), static_cast<int>(command_y(38, 39)),
                          reg_[44], true);
        finish_command();
    }

    void v9938::command_point() noexcept {
        status_[7] =
            command_pixel(static_cast<int>(command_x(32, 33)), static_cast<int>(command_y(34, 35)));
        finish_command();
    }

    void v9938::tick(std::uint64_t cycles) {
        for (std::uint64_t c = 0; c < cycles; ++c) {
            ++scanline_cycle_;
            if (scanline_cycle_ < cycles_per_line) {
                continue;
            }
            scanline_cycle_ = 0;
            ++scanline_;
            if (scanline_ == active_height()) {
                ++frame_index_;
                status_[0] = static_cast<std::uint8_t>(status_[0] | status_vblank);
                render_frame();
            }
            if (scanline_ >= total_scanlines_) {
                scanline_ = 0;
            }
            if (scanline_ == static_cast<int>(reg_[19])) {
                status_[1] = static_cast<std::uint8_t>(status_[1] | status_horizontal_interrupt);
            }
            update_irq();
        }
    }

    void v9938::render_text(int columns, int cell_width) noexcept {
        const bool text2 = columns == 80;
        const std::uint8_t name_mask = text2 ? 0x7CU : 0x7FU;
        const std::uint32_t name_base = (static_cast<std::uint32_t>(reg_[2] & name_mask) << 10U) &
                                        static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t blink_base = text2 ? text2_blink_table_base() : 0U;
        const bool blink_phase = text2 && text2_blink_phase_active();
        const std::uint32_t pattern_base = (static_cast<std::uint32_t>(reg_[4] & 0x3FU) << 11U) &
                                           static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t fg_normal =
            palette_display_rgb(static_cast<std::uint8_t>(reg_[7] >> 4U));
        const std::uint32_t bg_normal =
            palette_display_rgb(static_cast<std::uint8_t>(reg_[7] & 0x0FU));
        const std::uint32_t fg_blink =
            palette_display_rgb(static_cast<std::uint8_t>(reg_[12] >> 4U));
        const std::uint32_t bg_blink =
            palette_display_rgb(static_cast<std::uint8_t>(reg_[12] & 0x0FU));
        const int x_margin = (active_width() - columns * cell_width) / 2;
        const int height = active_height();
        const int rows = (height + 7) / 8;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < columns; ++col) {
                const std::uint32_t cell = static_cast<std::uint32_t>(row * columns + col);
                const std::uint8_t ch = pattern_byte(vram_, name_base + cell);
                const bool blink_cell =
                    blink_phase &&
                    (pattern_byte(vram_, blink_base + cell / 8U) &
                     static_cast<std::uint8_t>(0x80U >> static_cast<unsigned>(cell & 0x07U))) != 0U;
                const std::uint32_t fg = blink_cell ? fg_blink : fg_normal;
                const std::uint32_t bg = blink_cell ? bg_blink : bg_normal;
                for (int line = 0; line < 8; ++line) {
                    const std::uint8_t bits =
                        pattern_byte(vram_, pattern_base + static_cast<std::uint32_t>(ch) * 8U +
                                                static_cast<std::uint32_t>(line));
                    const int y = row * 8 + line;
                    if (y >= height) {
                        continue;
                    }
                    for (int px = 0; px < cell_width; ++px) {
                        const bool on = (bits & (0x80U >> static_cast<unsigned>(px))) != 0U;
                        const int x = x_margin + col * cell_width + px;
                        if (x >= 0 && x < active_width()) {
                            write_frame_pixel(x, y, on ? fg : bg);
                        }
                    }
                }
            }
        }
    }

    void v9938::render_graphic1() noexcept {
        const std::uint32_t name_base = (static_cast<std::uint32_t>(reg_[2] & 0x7FU) << 10U) &
                                        static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t color_base = ((static_cast<std::uint32_t>(reg_[10] & 0x07U) << 14U) |
                                          (static_cast<std::uint32_t>(reg_[3]) << 6U)) &
                                         static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t pattern_base = (static_cast<std::uint32_t>(reg_[4] & 0x3FU) << 11U) &
                                           static_cast<std::uint32_t>(vram_size - 1);
        const int height = active_height();
        const int rows = (height + 7) / 8;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t ch =
                    pattern_byte(vram_, name_base + static_cast<std::uint32_t>(row * 32 + col));
                const std::uint8_t colour =
                    pattern_byte(vram_, color_base + static_cast<std::uint32_t>(ch >> 3U));
                const std::uint32_t fg =
                    palette_display_rgb(static_cast<std::uint8_t>(colour >> 4U));
                const std::uint32_t bg =
                    palette_display_rgb(static_cast<std::uint8_t>(colour & 0x0FU));
                for (int line = 0; line < 8; ++line) {
                    const std::uint8_t bits =
                        pattern_byte(vram_, pattern_base + static_cast<std::uint32_t>(ch) * 8U +
                                                static_cast<std::uint32_t>(line));
                    for (int px = 0; px < 8; ++px) {
                        const int y = row * 8 + line;
                        if (y >= height) {
                            continue;
                        }
                        const bool on = (bits & (0x80U >> static_cast<unsigned>(px))) != 0U;
                        write_frame_pixel(col * 8 + px, y, on ? fg : bg);
                    }
                }
            }
        }
    }

    void v9938::render_graphic2() noexcept {
        const std::uint32_t name_base = (static_cast<std::uint32_t>(reg_[2] & 0x7FU) << 10U) &
                                        static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t color_base = ((static_cast<std::uint32_t>(reg_[10] & 0x07U) << 14U) |
                                          (static_cast<std::uint32_t>(reg_[3] & 0x80U) << 6U)) &
                                         static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t pattern_base = (static_cast<std::uint32_t>(reg_[4] & 0x3CU) << 11U) &
                                           static_cast<std::uint32_t>(vram_size - 1);
        const int height = active_height();
        for (int y = 0; y < height; ++y) {
            const int tile_row = y / 8;
            const int third = y / 64;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t ch = pattern_byte(
                    vram_, name_base + static_cast<std::uint32_t>(tile_row * 32 + col));
                const std::uint32_t table_off =
                    static_cast<std::uint32_t>(third * 0x800 + ch * 8 + (y & 7));
                const std::uint8_t bits = pattern_byte(vram_, pattern_base + table_off);
                const std::uint8_t colour = pattern_byte(vram_, color_base + table_off);
                const std::uint32_t fg =
                    palette_display_rgb(static_cast<std::uint8_t>(colour >> 4U));
                const std::uint32_t bg =
                    palette_display_rgb(static_cast<std::uint8_t>(colour & 0x0FU));
                for (int px = 0; px < 8; ++px) {
                    const bool on = (bits & (0x80U >> static_cast<unsigned>(px))) != 0U;
                    write_frame_pixel(col * 8 + px, y, on ? fg : bg);
                }
            }
        }
    }

    void v9938::render_multicolor() noexcept {
        const std::uint32_t name_base = (static_cast<std::uint32_t>(reg_[2] & 0x7FU) << 10U) &
                                        static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t pattern_base = (static_cast<std::uint32_t>(reg_[4] & 0x3FU) << 11U) &
                                           static_cast<std::uint32_t>(vram_size - 1);
        const int height = active_height();
        const int rows = (height + 7) / 8;
        for (int row = 0; row < rows; ++row) {
            const int row_group = row & 0x03;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t ch =
                    pattern_byte(vram_, name_base + static_cast<std::uint32_t>(row * 32 + col));
                for (int block_y = 0; block_y < 2; ++block_y) {
                    const std::uint8_t colors = pattern_byte(
                        vram_, pattern_base + static_cast<std::uint32_t>(ch) * 8U +
                                   static_cast<std::uint32_t>(row_group * 2 + block_y));
                    const std::uint32_t left =
                        palette_display_rgb(static_cast<std::uint8_t>(colors >> 4U));
                    const std::uint32_t right =
                        palette_display_rgb(static_cast<std::uint8_t>(colors & 0x0FU));
                    const int y0 = row * 8 + block_y * 4;
                    const int x0 = col * 8;
                    for (int y = 0; y < 4; ++y) {
                        const int dst_y = y0 + y;
                        if (dst_y >= height) {
                            continue;
                        }
                        for (int x = 0; x < 4; ++x) {
                            write_frame_pixel(x0 + x, dst_y, left);
                            write_frame_pixel(x0 + 4 + x, dst_y, right);
                        }
                    }
                }
            }
        }
    }

    void v9938::render_bitmap4() noexcept {
        const int height = active_height();
        const int fields = interlace_enabled() ? 2 : 1;
        for (int field = 0; field < fields; ++field) {
            const std::uint32_t base = visible_bitmap_display_base(display_mode::bitmap4, field);
            for (int y = 0; y < height; ++y) {
                const std::uint32_t line =
                    base + static_cast<std::uint32_t>(display_source_y(y) * 128);
                for (int xb = 0; xb < 128; ++xb) {
                    const std::uint8_t packed =
                        pattern_byte(vram_, line + static_cast<std::uint32_t>(xb));
                    write_field_pixel(xb * 2, y, field,
                                      palette_display_rgb(static_cast<std::uint8_t>(packed >> 4U)));
                    write_field_pixel(
                        xb * 2 + 1, y, field,
                        palette_display_rgb(static_cast<std::uint8_t>(packed & 0x0FU)));
                }
            }
        }
    }

    void v9938::render_bitmap2_512() noexcept {
        const int height = active_height();
        const int fields = interlace_enabled() ? 2 : 1;
        for (int field = 0; field < fields; ++field) {
            const std::uint32_t base =
                visible_bitmap_display_base(display_mode::bitmap2_512, field);
            for (int y = 0; y < height; ++y) {
                const std::uint32_t line =
                    base + static_cast<std::uint32_t>(display_source_y(y) * 128);
                for (int xb = 0; xb < 128; ++xb) {
                    const std::uint8_t packed =
                        pattern_byte(vram_, line + static_cast<std::uint32_t>(xb));
                    for (int dot = 0; dot < 4; ++dot) {
                        const unsigned shift = static_cast<unsigned>((3 - dot) * 2);
                        const std::uint8_t colour =
                            static_cast<std::uint8_t>((packed >> shift) & 0x03U);
                        write_field_pixel(xb * 4 + dot, y, field, palette_display_rgb(colour));
                    }
                }
            }
        }
    }

    void v9938::render_bitmap4_512() noexcept {
        const int height = active_height();
        const int fields = interlace_enabled() ? 2 : 1;
        for (int field = 0; field < fields; ++field) {
            const std::uint32_t base =
                visible_bitmap_display_base(display_mode::bitmap4_512, field);
            for (int y = 0; y < height; ++y) {
                const std::uint32_t line =
                    base + static_cast<std::uint32_t>(display_source_y(y) * 256);
                for (int xb = 0; xb < 256; ++xb) {
                    const std::uint8_t packed =
                        pattern_byte(vram_, line + static_cast<std::uint32_t>(xb));
                    write_field_pixel(xb * 2, y, field,
                                      palette_display_rgb(static_cast<std::uint8_t>(packed >> 4U)));
                    write_field_pixel(
                        xb * 2 + 1, y, field,
                        palette_display_rgb(static_cast<std::uint8_t>(packed & 0x0FU)));
                }
            }
        }
    }

    void v9938::render_bitmap7() noexcept {
        const int height = active_height();
        const int fields = interlace_enabled() ? 2 : 1;
        for (int field = 0; field < fields; ++field) {
            const std::uint32_t base = visible_bitmap_display_base(display_mode::bitmap7, field);
            for (int y = 0; y < height; ++y) {
                const std::uint32_t line =
                    base + static_cast<std::uint32_t>(display_source_y(y) * 256);
                for (int x = 0; x < 256; ++x) {
                    const std::uint8_t px =
                        pattern_byte(vram_, line + static_cast<std::uint32_t>(x));
                    write_field_pixel(x, y, field, fixed_332_rgb(px));
                }
            }
        }
    }

    void v9938::render_sprites() noexcept {
        const display_mode mode = active_mode();
        const bool sprite_mode1 = mode == display_mode::graphic1 ||
                                  mode == display_mode::graphic2 ||
                                  mode == display_mode::multicolor;
        const bool sprite_mode2 = mode == display_mode::graphic3 || mode == display_mode::bitmap4 ||
                                  mode == display_mode::bitmap2_512 ||
                                  mode == display_mode::bitmap4_512 ||
                                  mode == display_mode::bitmap7;
        const bool capture_collision_coordinates = !pointing_device_active();
        const bool double_horizontal =
            mode == display_mode::bitmap2_512 || mode == display_mode::bitmap4_512;
        status_[0] = static_cast<std::uint8_t>(status_[0] & status_vblank);
        if (capture_collision_coordinates) {
            status_[3] = 0U;
            status_[4] = 0U;
            status_[5] = 0U;
            status_[6] = 0U;
        }
        std::fill(sprite_occupancy_.begin(), sprite_occupancy_.end(), std::uint8_t{0});
        if ((!sprite_mode1 && !sprite_mode2) || (reg_[8] & 0x02U) != 0U) {
            return;
        }

        const bool sprite16 = (reg_[1] & 0x02U) != 0U;
        const bool magnified = (reg_[1] & 0x01U) != 0U;
        const int sprite_size = sprite16 ? 16 : 8;
        const int scale = magnified ? 2 : 1;
        const int draw_height = sprite_size * scale;
        const int horizontal_scale = scale * (double_horizontal ? 2 : 1);
        const int draw_width = sprite_size * horizontal_scale;
        const int width = active_width();
        const int height = active_height();
        const std::uint32_t attr_base = ((static_cast<std::uint32_t>(reg_[5] & 0x7FU) << 7U) |
                                         (static_cast<std::uint32_t>(reg_[11] & 0x03U) << 15U)) &
                                        static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t color_base =
            (attr_base - 512U) & static_cast<std::uint32_t>(vram_size - 1);
        const std::uint32_t pattern_base = (static_cast<std::uint32_t>(reg_[6] & 0x3FU) << 11U) &
                                           static_cast<std::uint32_t>(vram_size - 1);
        const std::uint8_t terminator = sprite_mode2 ? 0xD8U : 0xD0U;
        const auto sprite_visible_y = [this](int source_y) noexcept {
            return (source_y - static_cast<int>(reg_[23])) & 0xFF;
        };
        const auto source_line_in_sprite = [](int source_y, int sprite_y, int sprite_height) {
            return ((source_y - sprite_y) & 0xFF) < sprite_height;
        };
        int last_sprite = 31;
        for (int sprite = 0; sprite < 32; ++sprite) {
            const std::uint32_t attr = attr_base + static_cast<std::uint32_t>(sprite * 4);
            if (pattern_byte(vram_, attr) == terminator) {
                last_sprite = sprite - 1;
                break;
            }
        }

        const int sprite_limit = sprite_mode2 ? 9 : 5;
        bool overflow_set = false;
        for (int scan_y = 0; scan_y < height && !overflow_set; ++scan_y) {
            int count = 0;
            const int source_y = display_source_y(scan_y);
            for (int sprite = 0; sprite <= last_sprite; ++sprite) {
                const std::uint32_t attr = attr_base + static_cast<std::uint32_t>(sprite * 4);
                int y = static_cast<int>(pattern_byte(vram_, attr)) + 1;
                if (y >= 240) {
                    y -= 256;
                }
                if (!source_line_in_sprite(source_y, y, draw_height)) {
                    continue;
                }
                ++count;
                if (count == sprite_limit) {
                    status_[0] = static_cast<std::uint8_t>(
                        (status_[0] & ~status_sprite_number_mask) | status_sprite_overflow |
                        static_cast<std::uint8_t>(sprite & status_sprite_number_mask));
                    overflow_set = true;
                    break;
                }
            }
        }

        // Draw in hardware priority order. The occupancy byte keeps the winning color plus
        // the sprite-mode-2 CC/IC state needed by later overlapping sprites.
        for (int sprite = 0; sprite <= last_sprite; ++sprite) {
            const std::uint32_t attr = attr_base + static_cast<std::uint32_t>(sprite * 4);
            const std::uint8_t raw_y = pattern_byte(vram_, attr);
            int y = static_cast<int>(raw_y) + 1;
            if (y >= 240) {
                y -= 256;
            }
            int base_x = static_cast<int>(pattern_byte(vram_, attr + 1U));
            const std::uint8_t pattern = pattern_byte(vram_, attr + 2U);
            const std::uint8_t sprite_attr_color = pattern_byte(vram_, attr + 3U);
            if (!sprite_mode2 && (sprite_attr_color & 0x0FU) == 0U) {
                continue;
            }
            if (!sprite_mode2 && (sprite_attr_color & 0x80U) != 0U) {
                base_x -= 32;
            }

            const std::uint8_t pattern_base_index =
                sprite16 ? static_cast<std::uint8_t>(pattern & 0xFCU) : pattern;
            for (int sy = 0; sy < draw_height; ++sy) {
                const int dst_y = sprite_visible_y(y + sy);
                if (dst_y < 0 || dst_y >= height) {
                    continue;
                }
                const int logical_y = sy / scale;
                std::uint8_t color_attr = sprite_attr_color;
                int x = base_x;
                if (sprite_mode2) {
                    color_attr =
                        pattern_byte(vram_, color_base + static_cast<std::uint32_t>(sprite * 16) +
                                                static_cast<std::uint32_t>(logical_y));
                    if ((color_attr & 0x80U) != 0U) {
                        x -= 32;
                    }
                }
                if (double_horizontal) {
                    x *= 2;
                }
                const std::uint8_t color = static_cast<std::uint8_t>(color_attr & 0x0FU);
                if (color == 0U && !color_zero_opaque()) {
                    continue;
                }
                const bool priority_cancel = sprite_mode2 && (color_attr & 0x40U) != 0U;
                const bool collision_enabled = !priority_cancel && (color_attr & 0x20U) == 0U;
                const int tile_y = logical_y & 0x07;
                const int sprite_tile_y = sprite16 && logical_y >= 8 ? 1 : 0;
                for (int sx = 0; sx < draw_width; ++sx) {
                    const int dst_x = x + sx;
                    if (dst_x < 0 || dst_x >= width) {
                        continue;
                    }
                    const int logical_x = sx / horizontal_scale;
                    const int sprite_tile_x = sprite16 && logical_x >= 8 ? 1 : 0;
                    const std::uint8_t tile_pattern = static_cast<std::uint8_t>(
                        pattern_base_index + sprite_tile_y + sprite_tile_x * 2);
                    const std::uint32_t pattern_addr =
                        pattern_base + static_cast<std::uint32_t>(tile_pattern) * 8U +
                        static_cast<std::uint32_t>(tile_y);
                    const std::uint8_t bits = pattern_byte(vram_, pattern_addr);
                    if ((bits & (0x80U >> static_cast<unsigned>(logical_x & 0x07))) == 0U) {
                        continue;
                    }
                    const std::size_t out_index =
                        static_cast<std::size_t>(dst_y) * static_cast<std::size_t>(storage_width) +
                        static_cast<std::size_t>(dst_x);
                    std::uint8_t& occupancy = sprite_occupancy_[out_index];
                    if (collision_enabled && (occupancy & sprite_pixel_collision_source) != 0U &&
                        (status_[0] & status_sprite_collision) == 0U) {
                        status_[0] =
                            static_cast<std::uint8_t>(status_[0] | status_sprite_collision);
                        if (capture_collision_coordinates) {
                            status_[3] = static_cast<std::uint8_t>(dst_x & 0xFF);
                            status_[4] = static_cast<std::uint8_t>((dst_x >> 8) & 0x01);
                            status_[5] = static_cast<std::uint8_t>(dst_y & 0xFF);
                            status_[6] = static_cast<std::uint8_t>((dst_y >> 8) & 0x03);
                        }
                    }
                    if (collision_enabled) {
                        occupancy =
                            static_cast<std::uint8_t>(occupancy | sprite_pixel_collision_source);
                    }

                    const bool occupied = (occupancy & sprite_pixel_visible) != 0U;
                    const bool existing_priority_cancel =
                        (occupancy & sprite_pixel_priority_cancel) != 0U;
                    const bool combine_colors =
                        sprite_mode2 && occupied && (priority_cancel || existing_priority_cancel);
                    if (!occupied || combine_colors) {
                        const std::uint8_t output_color =
                            combine_colors ? static_cast<std::uint8_t>(
                                                 ((occupancy & sprite_pixel_color_mask) | color) &
                                                 sprite_pixel_color_mask)
                                           : color;
                        const std::uint8_t retained_collision =
                            static_cast<std::uint8_t>(occupancy & sprite_pixel_collision_source);
                        occupancy = static_cast<std::uint8_t>(
                            retained_collision | sprite_pixel_visible | output_color |
                            ((priority_cancel || existing_priority_cancel)
                                 ? sprite_pixel_priority_cancel
                                 : std::uint8_t{0U}));
                        write_frame_pixel(dst_x, dst_y, sprite_rgb(output_color));
                    }
                }
            }
        }
    }

    void v9938::apply_display_adjust() noexcept {
        const int dx = display_adjust_x();
        const int dy = display_adjust_y();
        if (dx == 0 && dy == 0) {
            return;
        }

        const int width = active_width();
        const int height = framebuffer_height();
        const std::uint32_t margin = backdrop_rgb();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int source_x = x - dx;
                const int source_y = y - dy;
                display_adjust_scratch_[index(static_cast<std::uint32_t>(x),
                                              static_cast<std::uint32_t>(y))] =
                    source_x >= 0 && source_x < width && source_y >= 0 && source_y < height
                        ? framebuffer_[index(static_cast<std::uint32_t>(source_x),
                                             static_cast<std::uint32_t>(source_y))]
                        : margin;
            }
        }

        for (int y = 0; y < height; ++y) {
            const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(storage_width);
            std::copy_n(display_adjust_scratch_.begin() + static_cast<std::ptrdiff_t>(row),
                        static_cast<std::size_t>(width),
                        framebuffer_.begin() + static_cast<std::ptrdiff_t>(row));
        }
    }

    void v9938::render_frame() noexcept {
        std::fill(framebuffer_.begin(), framebuffer_.end(), backdrop_rgb());
        if ((reg_[1] & 0x40U) == 0U) {
            return;
        }
        switch (active_mode()) {
        case display_mode::text1:
            render_text(40, 6);
            break;
        case display_mode::text2:
            render_text(80, 6);
            break;
        case display_mode::graphic2:
        case display_mode::graphic3:
            render_graphic2();
            break;
        case display_mode::multicolor:
            render_multicolor();
            break;
        case display_mode::bitmap4:
            render_bitmap4();
            break;
        case display_mode::bitmap2_512:
            render_bitmap2_512();
            break;
        case display_mode::bitmap4_512:
            render_bitmap4_512();
            break;
        case display_mode::bitmap7:
            render_bitmap7();
            break;
        case display_mode::graphic1:
        default:
            render_graphic1();
            break;
        }
        render_sprites();
        apply_display_adjust();
    }

    void v9938::reset(reset_kind /*kind*/) {
        std::memset(vram_.data(), 0, vram_.size());
        reg_ = {};
        status_ = {};
        palette_ = k_default_palette;
        addr_ = 0;
        code_ = 0;
        read_buffer_ = 0;
        ctrl_pending_ = false;
        ctrl_first_ = 0;
        palette_pending_ = false;
        palette_first_ = 0;
        palette_index_ = 0;
        scanline_cycle_ = 0;
        scanline_ = 0;
        frame_index_ = 0;
        blink_start_frame_ = 0;
        command_code_ = 0;
        command_cpu_transfer_ = false;
        command_cpu_read_transfer_ = false;
        command_high_speed_transfer_ = false;
        command_stream_x_ = 0;
        command_stream_y_ = 0;
        set_pal(pal_mode_);
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0U);
        irq_asserted_ = false;
        update_irq();
    }

    void v9938::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(vram_);
        writer.bytes(reg_);
        writer.bytes(status_);
        for (const std::uint32_t rgb : palette_) {
            writer.u32(rgb);
        }
        writer.u16(addr_);
        writer.u8(code_);
        writer.u8(read_buffer_);
        writer.boolean(ctrl_pending_);
        writer.u8(ctrl_first_);
        writer.boolean(palette_pending_);
        writer.u8(palette_first_);
        writer.u8(palette_index_);
        writer.u32(static_cast<std::uint32_t>(scanline_cycle_));
        writer.u32(static_cast<std::uint32_t>(scanline_));
        writer.boolean(pal_mode_);
        writer.boolean(irq_asserted_);
        writer.u8(command_code_);
        writer.boolean(command_cpu_transfer_);
        writer.boolean(command_cpu_read_transfer_);
        writer.boolean(command_high_speed_transfer_);
        writer.u16(command_stream_x_);
        writer.u16(command_stream_y_);
        writer.u64(frame_index_);
        writer.u64(blink_start_frame_);
    }

    void v9938::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version == 0U || version > k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(vram_);
        reader.bytes(reg_);
        if (version >= 3U) {
            reader.bytes(status_);
        } else {
            std::array<std::uint8_t, 8> legacy_status{};
            reader.bytes(legacy_status);
            status_ = {};
            std::copy(legacy_status.begin(), legacy_status.end(), status_.begin());
        }
        for (std::uint32_t& rgb : palette_) {
            rgb = reader.u32();
        }
        addr_ = reader.u16();
        code_ = reader.u8();
        read_buffer_ = reader.u8();
        ctrl_pending_ = reader.boolean();
        ctrl_first_ = reader.u8();
        palette_pending_ = reader.boolean();
        palette_first_ = reader.u8();
        palette_index_ = reader.u8();
        scanline_cycle_ = static_cast<int>(reader.u32());
        scanline_ = static_cast<int>(reader.u32());
        set_pal(reader.boolean());
        irq_asserted_ = reader.boolean();
        if (version >= 2U) {
            command_code_ = reader.u8();
            command_cpu_transfer_ = reader.boolean();
            command_cpu_read_transfer_ = version >= 3U ? reader.boolean() : false;
            command_high_speed_transfer_ = reader.boolean();
            command_stream_x_ = reader.u16();
            command_stream_y_ = reader.u16();
        } else {
            command_code_ = 0;
            command_cpu_transfer_ = false;
            command_cpu_read_transfer_ = false;
            command_high_speed_transfer_ = false;
            command_stream_x_ = 0;
            command_stream_y_ = 0;
        }
        frame_index_ = reader.u64();
        blink_start_frame_ = version >= 4U ? reader.u64() : frame_index_;
        if (reader.ok()) {
            render_frame();
            update_irq();
        }
    }

    std::span<const register_descriptor> v9938::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"VADDR", vram_addr(), 17U, fmt::unsigned_integer};
        register_view_[1] = {"R0", reg_[0], 8U, fmt::flags};
        register_view_[2] = {"R1", reg_[1], 8U, fmt::flags};
        register_view_[3] = {"R2", reg_[2], 8U, fmt::unsigned_integer};
        register_view_[4] = {"R3", reg_[3], 8U, fmt::unsigned_integer};
        register_view_[5] = {"R4", reg_[4], 8U, fmt::unsigned_integer};
        register_view_[6] = {"R7", reg_[7], 8U, fmt::unsigned_integer};
        register_view_[7] = {"R14", reg_[14], 8U, fmt::unsigned_integer};
        register_view_[8] = {"R15", reg_[15], 8U, fmt::unsigned_integer};
        register_view_[9] = {"R16", reg_[16], 8U, fmt::unsigned_integer};
        register_view_[10] = {"R17", reg_[17], 8U, fmt::unsigned_integer};
        register_view_[11] = {"S0", status_[0], 8U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto v9938_registration =
            register_factory("yamaha.v9938", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<v9938>(); });
    } // namespace

} // namespace mnemos::chips::video
