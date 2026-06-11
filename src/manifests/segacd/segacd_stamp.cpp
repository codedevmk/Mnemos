// Sega CD stamp / rotation graphics ASIC: samples "stamp" pixels out of word
// RAM along per-line trace vectors (rotation/scaling = the vector deltas) and
// writes them 4bpp-packed into the cell-arranged image buffer, then raises the
// graphics-complete (sub-CPU level-1) IRQ. Config lives in gate-array $58-$67;
// the op starts on the Trace Vector Base ($66) write. The op is modeled as
// instantaneous (cycle metering is a deferred refinement).

#include "segacd_system.hpp"

#include <array>
#include <cstdio>

namespace mnemos::manifests::segacd {
    namespace {
        // Cell/pixel lookup tables: map a stamp's HFLIP+ROTATION bits and the
        // in-stamp position to the cell offset (stamps are 2x2 or 4x4 cells)
        // and the in-cell pixel offset.
        struct stamp_luts final {
            std::array<std::uint8_t, 0x100> cell{};  // yyxxshrr -> cell offset
            std::array<std::uint8_t, 0x200> pixel{}; // yyyxxxhrr -> pixel offset
            stamp_luts() noexcept {
                for (unsigned i = 0; i < 0x100U; ++i) {
                    const std::uint8_t mask = ((i & 8U) != 0U) ? 3U : 1U;
                    auto row = static_cast<std::uint8_t>((i >> 6U) & mask);
                    auto col = static_cast<std::uint8_t>((i >> 4U) & mask);
                    if ((i & 4U) != 0U) { // HFLIP (always first)
                        col = static_cast<std::uint8_t>(col ^ mask);
                    }
                    if ((i & 2U) != 0U) { // ROLL1
                        col = static_cast<std::uint8_t>(col ^ mask);
                        row = static_cast<std::uint8_t>(row ^ mask);
                    }
                    if ((i & 1U) != 0U) { // ROLL0
                        const std::uint8_t t = col;
                        col = static_cast<std::uint8_t>(row ^ mask);
                        row = t;
                    }
                    cell[i] = static_cast<std::uint8_t>(row + col * (mask + 1U));
                }
                for (unsigned i = 0; i < 0x200U; ++i) {
                    auto row = static_cast<std::uint8_t>((i >> 6U) & 7U);
                    auto col = static_cast<std::uint8_t>((i >> 3U) & 7U);
                    if ((i & 4U) != 0U) {
                        col = static_cast<std::uint8_t>(col ^ 7U);
                    }
                    if ((i & 2U) != 0U) {
                        col = static_cast<std::uint8_t>(col ^ 7U);
                        row = static_cast<std::uint8_t>(row ^ 7U);
                    }
                    if ((i & 1U) != 0U) {
                        const std::uint8_t t = col;
                        col = static_cast<std::uint8_t>(row ^ 7U);
                        row = t;
                    }
                    pixel[i] = static_cast<std::uint8_t>(col + row * 8U);
                }
            }
        };
        const stamp_luts luts;

        // The $03 PM1:PM0 write-priority rules, applied per packed byte.
        std::uint8_t prio_byte(std::uint8_t pm, std::uint8_t prev, std::uint8_t data) noexcept {
            switch (pm & 3U) {
            case 1: // underwrite: existing non-zero pixels win
                return static_cast<std::uint8_t>(
                    (((prev & 0x0FU) != 0U) ? (prev & 0x0FU) : (data & 0x0FU)) |
                    (((prev & 0xF0U) != 0U) ? (prev & 0xF0U) : (data & 0xF0U)));
            case 2: // overwrite: only non-zero source pixels land
                return static_cast<std::uint8_t>(
                    (((data & 0x0FU) != 0U) ? (data & 0x0FU) : (prev & 0x0FU)) |
                    (((data & 0xF0U) != 0U) ? (data & 0xF0U) : (prev & 0xF0U)));
            case 3: // invalid: writes are ignored
                return prev;
            default:
                return data;
            }
        }
    } // namespace

    void segacd_system::graphics_complete() {
        raise_sub_irq(irq_graphics); // level 1
    }

    void segacd_system::stamp_reg_write(std::uint8_t offset, std::uint8_t value) {
        if (segacd_trace_enabled()) {
            std::fprintf(stderr, "[stamp] sub_pc=%06X $%02X=%02X\n", sub_cpu.cpu_registers().pc,
                         offset, value);
        }
        // The raw bytes already landed in gate_array[] (the caller stores before
        // dispatching here); the GFX config is read from there at op start. The
        // op starts when the sub-CPU writes the Trace Vector Base register ($66
        // word, so the $67 low-byte write commits it).
        if (offset == 0x67U) {
            stamp_renderer_run();
            graphics_complete();
        }
    }

    void segacd_system::stamp_renderer_run() {
        // Stamp / stamp-map geometry from $58 (low byte: bit0 = map repeat,
        // bits 2:1 = stamp size + map size).
        const std::uint8_t cfg = gate_array[0x59];
        std::uint32_t dot_mask;
        std::uint32_t stamp_shift;
        std::uint32_t map_shift;
        std::uint32_t map_mask;
        switch ((cfg >> 1U) & 0x03U) {
        case 1: // 32x32 stamps, 8x8 map (256x256 dots)
            dot_mask = 0x07FFFFU;
            stamp_shift = 16U;
            map_shift = 3U;
            map_mask = 0x3FF80U;
            break;
        case 2: // 16x16 stamps, 256x256 map (4096x4096 dots)
            dot_mask = 0x7FFFFFU;
            stamp_shift = 15U;
            map_shift = 8U;
            map_mask = 0x20000U;
            break;
        case 3: // 32x32 stamps, 128x128 map (4096x4096 dots)
            dot_mask = 0x7FFFFFU;
            stamp_shift = 16U;
            map_shift = 7U;
            map_mask = 0x38000U;
            break;
        default: // 16x16 stamps, 16x16 map (256x256 dots)
            dot_mask = 0x07FFFFU;
            stamp_shift = 15U;
            map_shift = 4U;
            map_mask = 0x3FE00U;
            break;
        }
        // Bits [1:0] of a 32x32 stamp number are masked by the hardware.
        const std::uint16_t stamp_num_mask = ((cfg & 0x02U) != 0U) ? 0x7FCU : 0x7FFU;

        const auto rd16 = [this](std::uint32_t o) -> std::uint16_t {
            return static_cast<std::uint16_t>((word_ram[o & 0x3FFFEU] << 8U) |
                                              word_ram[(o & 0x3FFFEU) + 1U]);
        };

        const std::uint32_t map_addr = ((static_cast<std::uint32_t>(gate_array[0x5A]) << 10U) |
                                        (static_cast<std::uint32_t>(gate_array[0x5B]) << 2U)) &
                                       map_mask;
        // Image buffer: column step (cells are 64 pixels; -7 returns to the next
        // column's first line), start index (dot units, 2 pixels/byte) + the
        // in-cell dot offset, width in dots, remaining lines.
        const std::uint32_t buffer_col_step =
            ((static_cast<std::uint32_t>(gate_array[0x5D] & 0x1FU) + 1U) << 6U) - 7U;
        std::uint32_t buffer_line = (((static_cast<std::uint32_t>(gate_array[0x5E]) << 11U) |
                                      (static_cast<std::uint32_t>(gate_array[0x5F]) << 3U)) &
                                     0x7FFC0U) +
                                    (gate_array[0x61] & 0x3FU);
        const std::uint32_t width =
            ((static_cast<std::uint32_t>(gate_array[0x62]) << 8U) | gate_array[0x63]) & 0x1FFU;
        std::uint32_t lines = gate_array[0x65];
        std::uint32_t trace = ((static_cast<std::uint32_t>(gate_array[0x66]) << 10U) |
                               (static_cast<std::uint32_t>(gate_array[0x67]) << 2U)) &
                              0x3FFF8U;
        const auto pm = static_cast<std::uint8_t>((gate_array[0x03] >> 3U) & 0x03U);

        gate_array[0x58] = static_cast<std::uint8_t>(gate_array[0x58] | 0x80U); // GRON busy
        while (lines-- != 0U) {
            std::uint32_t xpos = static_cast<std::uint32_t>(rd16(trace)) << 8U; // 13.3 -> 13.11
            std::uint32_t ypos = static_cast<std::uint32_t>(rd16(trace + 2U)) << 8U;
            const auto xstep = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(rd16(trace + 4U))));
            const auto ystep = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(rd16(trace + 6U))));
            trace = (trace + 8U) & 0x3FFFFU;

            std::uint32_t buf = buffer_line;
            for (std::uint32_t w = width; w != 0U; --w) {
                if ((cfg & 0x01U) != 0U) { // repeated stamp map
                    xpos &= dot_mask;
                    ypos &= dot_mask;
                } else {
                    xpos &= 0xFFFFFFU;
                    ypos &= 0xFFFFFFU;
                }
                std::uint8_t out = 0;
                if (((xpos | ypos) & ~dot_mask) == 0U) {
                    const std::uint32_t map_idx =
                        (xpos >> stamp_shift) | ((ypos >> stamp_shift) << map_shift);
                    const std::uint16_t stamp = rd16(map_addr + map_idx * 2U);
                    std::uint32_t pix_index = static_cast<std::uint32_t>(stamp & stamp_num_mask)
                                              << 8U;
                    if (pix_index != 0U) { // stamp 0 never renders
                        const auto hrr = static_cast<std::uint8_t>((stamp >> 13U) & 0x07U);
                        pix_index |=
                            static_cast<std::uint32_t>(
                                luts.cell[hrr | ((cfg & 0x02U) << 2U) | ((ypos >> 8U) & 0xC0U) |
                                          ((xpos >> 10U) & 0x30U)])
                            << 6U;
                        pix_index |=
                            luts.pixel[hrr | ((xpos >> 8U) & 0x38U) | ((ypos >> 5U) & 0x1C0U)];
                        out = word_ram[(pix_index >> 1U) & 0x3FFFFU];
                        out = ((pix_index & 1U) != 0U) ? static_cast<std::uint8_t>(out & 0x0FU)
                                                       : static_cast<std::uint8_t>(out >> 4U);
                    }
                }
                const std::uint8_t prev = word_ram[(buf >> 1U) & 0x3FFFFU];
                const auto merged = static_cast<std::uint8_t>(
                    ((buf & 1U) != 0U) ? (out | (prev & 0xF0U))
                                       : (static_cast<std::uint8_t>(out << 4U) | (prev & 0x0FU)));
                word_ram[(buf >> 1U) & 0x3FFFFU] = prio_byte(pm, prev, merged);
                // Within a cell: next dot; at the cell's last column: jump one
                // image-buffer column (minus the 7 dots just walked).
                buf = ((buf & 7U) != 7U) ? (buf + 1U) : (buf + buffer_col_step);
                xpos += xstep;
                ypos += ystep;
            }
            buffer_line += 8U; // next line within the cell column
        }
        gate_array[0x65] = 0;                                                   // V dots remaining
        gate_array[0x58] = static_cast<std::uint8_t>(gate_array[0x58] & 0x7FU); // busy clear
    }

} // namespace mnemos::manifests::segacd
