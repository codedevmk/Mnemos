// Sega CD stamp / rotation graphics ASIC: rotates/scales "stamp" pixels out of
// word RAM into an image buffer, driven by per-line trace vectors, and raises
// the graphics-complete (sub-CPU level-1) IRQ. Ported from the Emu reference
// (systems/sega/segacd). Config lives in gate-array $58-$6B.

#include "segacd_system.hpp"

#include <cstdio>

namespace mnemos::manifests::segacd {

    void segacd_system::graphics_complete() {
        raise_sub_irq(irq_graphics); // level 1
    }

    void segacd_system::stamp_reg_write(std::uint8_t offset, std::uint8_t value) {
        if (segacd_trace_enabled()) {
            std::fprintf(stderr, "[stamp] sub_pc=%06X $%02X=%02X\n", sub_cpu.cpu_registers().pc,
                         offset, value);
        }
        if (offset == 0x58U) {
            stamp_size = static_cast<std::uint16_t>((stamp_size & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U));
            return;
        }
        if (offset == 0x59U) {
            stamp_size = static_cast<std::uint16_t>((stamp_size & 0xFF00U) | value);
            // Legacy Emu render trigger (NOT hardware -- the real GFX start is the $66
            // Trace Vector Base write handled below). Retained to keep stamp_renderer_run
            // reachable + unit-tested; the CD BIOS never sets this bit so it is inert
            // during a real boot.
            if ((value & 0x01U) != 0U) {
                stamp_renderer_run();
                graphics_complete();
            }
            return;
        }
        std::uint16_t* reg = nullptr;
        switch (offset & 0xFEU) {
        case 0x5A:
            reg = &stamp_map_addr;
            break;
        case 0x5C:
            reg = &img_buf_v_cell;
            break;
        case 0x5E:
            reg = &img_buf_vector;
            break;
        case 0x60:
            reg = &img_v_step;
            break;
        case 0x62:
            reg = &img_h_step;
            break;
        case 0x64:
            reg = &img_buf_width;
            break;
        case 0x66:
            reg = &img_buf_height;
            break;
        case 0x68:
            reg = &img_buf_offset;
            break;
        case 0x6A:
            reg = &trace_vector_addr;
            break;
        default:
            break;
        }
        if (reg != nullptr) {
            if ((offset & 1U) != 0U) {
                *reg = static_cast<std::uint16_t>((*reg & 0xFF00U) | value); // low byte (odd)
            } else {
                *reg = static_cast<std::uint16_t>((*reg & 0x00FFU) |
                                                  (static_cast<std::uint16_t>(value) << 8U));
            }
        }
        // The hardware starts the GFX operation when the sub-CPU writes the Trace Vector
        // Base register ($66); the CD BIOS triggers here every logo-animation frame. (It
        // never sets the Emu-derived $59 bit 0 -- it writes $59=04.) Model the op as
        // instantaneous and raise the graphics-complete IRQ (INT1, level 1) so the sub
        // leaves its GFX-complete wait. The pixel render is intentionally NOT run here:
        // stamp_renderer_run uses an 8bpp word-RAM layout that does not match the
        // hardware's 4bpp packed image buffer, so it would write garbage. A faithful 4bpp
        // render (stamp map + cell/pixel LUTs) is a deferred follow-up.
        if (offset == 0x66U) {
            graphics_complete();
        }
    }

    void segacd_system::stamp_renderer_run() {
        const int width = static_cast<int>(img_buf_width & 0x1FFU);
        const int height = static_cast<int>(img_buf_height & 0x1FFU);
        if (width == 0 || height == 0) {
            return;
        }
        constexpr std::uint32_t wsize = static_cast<std::uint32_t>(word_ram_size);
        const std::uint32_t tv_base = static_cast<std::uint32_t>(trace_vector_addr) << 2U;
        const std::uint32_t imgbuf_off = static_cast<std::uint32_t>(img_buf_offset) << 2U;

        const auto rd16 = [this](std::uint32_t o) {
            return static_cast<std::int32_t>(
                static_cast<std::int16_t>((word_ram[o % wsize] << 8) | word_ram[(o + 1U) % wsize]));
        };

        for (int y = 0; y < height && y < 256; ++y) {
            const std::uint32_t tv_off = (tv_base + static_cast<std::uint32_t>(y) * 12U) % wsize;
            std::int32_t src_x = rd16(tv_off);      // Q12.4 source X
            std::int32_t src_y = rd16(tv_off + 2U); // Q12.4 source Y
            const std::int32_t dx = rd16(tv_off + 4U);
            const std::int32_t dy = rd16(tv_off + 6U);
            const std::uint32_t out_row =
                (imgbuf_off + static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(width)) %
                wsize;
            for (int x = 0; x < width; ++x) {
                const int sx_int = static_cast<int>(src_x >> 4);
                const int sy_int = static_cast<int>(src_y >> 4);
                std::uint8_t pixel = 0;
                if (sx_int >= 0 && sy_int >= 0) {
                    const std::uint32_t src_addr =
                        static_cast<std::uint32_t>(sy_int * 256 + sx_int) % wsize;
                    pixel = word_ram[src_addr];
                }
                word_ram[(out_row + static_cast<std::uint32_t>(x)) % wsize] = pixel;
                src_x += dx;
                src_y += dy;
            }
        }
    }

} // namespace mnemos::manifests::segacd
