// Sega CD CDC (LC8951 CD data controller): the indirect register file, the
// sector decoder (fills the 16 KB ring buffer + raises the decoder IRQ), and the
// DMA paths to PRG/word/PCM RAM or the host data port. Ported from the Emu
// reference (systems/sega/segacd). NOTE: the reference defined cdc_reg_r but
// never wired it; here reads of the CDC data port ($06/$07) go through it (see
// segacd_system::gate_read) so CDC register reads actually work.

#include "segacd_system.hpp"

#include <array>
#include <cstring>

namespace mnemos::manifests::segacd {
    namespace {
        constexpr std::uint8_t ifstat_dtei = 0x40U;  // data-transfer-end IRQ
        constexpr std::uint8_t ifstat_deci = 0x20U;  // decoder IRQ
        constexpr std::uint8_t ifstat_dtbsy = 0x08U; // data transfer busy
        constexpr std::uint8_t ifstat_dten = 0x02U;  // data transfer enable
        constexpr std::uint8_t ifctrl_dteien = 0x40U;
        constexpr std::uint8_t ifctrl_decien = 0x20U;
        constexpr std::uint8_t ifctrl_douten = 0x02U;
        constexpr std::uint8_t ctrl0_decen = 0x80U;
        constexpr std::uint8_t ctrl0_autorq = 0x10U;
        constexpr std::uint8_t ctrl0_wrrq = 0x04U;
        constexpr std::uint8_t ctrl1_modrq = 0x08U;
        constexpr std::uint8_t ctrl1_formrq = 0x04U;
        constexpr std::uint8_t ctrl1_shdren = 0x01U;
        constexpr std::uint8_t stat3_valst = 0x80U;
    } // namespace

    void segacd_system::cdc_update_irq(std::uint8_t prev_irq) {
        // The CDC /INT line is active while any source (DTEI/DECI) is set; a
        // 0->non-0 edge latches a sub-CPU level-5 request when enabled by $33.5.
        if (cdc_irq != 0U && prev_irq == 0U) {
            if ((gate_array[0x33] & irq_cdc) != 0U) { // $33 bit 5 enables CDC (level 5)
                raise_sub_irq(irq_cdc);               // level 5
            }
        }
    }

    void segacd_system::cdc_dma_init() {
        if ((cdc_ifstat & ifstat_dten) != 0U) {
            return; // transfer not armed
        }
        cdc_dma_dest = gate_array[0x04] & 0x07U;
        if (segacd_trace_enabled()) {
            const auto o = static_cast<std::uint16_t>(cdc_dac & 0x3FFEU);
            std::fprintf(stderr,
                         "[cdcx] init dest=%u dac=%04X dbc=%04X "
                         "src8=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                         cdc_dma_dest, cdc_dac, cdc_dbc, cdc_ram[o], cdc_ram[(o + 1U) & 0x3FFFU],
                         cdc_ram[(o + 2U) & 0x3FFFU], cdc_ram[(o + 3U) & 0x3FFFU],
                         cdc_ram[(o + 4U) & 0x3FFFU], cdc_ram[(o + 5U) & 0x3FFFU],
                         cdc_ram[(o + 6U) & 0x3FFFU], cdc_ram[(o + 7U) & 0x3FFFU]);
        }
        switch (cdc_dma_dest) {
        case 2: // MAIN-CPU host read
        case 3: // SUB-CPU host read
        {
            const auto off = static_cast<std::uint16_t>(cdc_dac & 0x3FFEU);
            gate_array[0x08] = cdc_ram[off];
            gate_array[0x09] = cdc_ram[off + 1U];
            gate_array[0x04] |= 0x40U; // DSR
            cdc_dac = static_cast<std::uint16_t>(cdc_dac + 2U);
            cdc_dbc = static_cast<std::uint16_t>(cdc_dbc - 2U);
            break;
        }
        case 4:    // PCM RAM
        case 5:    // PRG-RAM
        case 7:    // Word-RAM
            break; // serviced incrementally in cdc_dma_run()
        default:
            cdc_dma_dest = 0;
            break;
        }
    }

    void segacd_system::cdc_dma_finish() {
        cdc_dbc = 0xFFFFU;
        cdc_ifstat |= static_cast<std::uint8_t>(ifstat_dtbsy | ifstat_dten);
        cdc_ifstat &= static_cast<std::uint8_t>(~ifstat_dtei); // DTEI pending (active low)
        if ((cdc_ifctrl & ifctrl_dteien) != 0U) {
            const std::uint8_t prev = cdc_irq;
            cdc_irq |= ifstat_dtei;
            cdc_update_irq(prev);
        }
        gate_array[0x04] = static_cast<std::uint8_t>((gate_array[0x04] & 0x07U) | 0x80U); // EDT
        cdc_dma_dest = 0;
    }

    void segacd_system::cdc_dma_run() {
        if (cdc_dma_dest < 4) {
            return; // idle or host-read mode
        }
        const std::uint32_t len = static_cast<std::uint32_t>(cdc_dbc) + 1U;
        const std::uint32_t src = cdc_dac;
        if (segacd_trace_enabled()) {
            std::fprintf(stderr,
                         "[cdcx] dest=%u dac=%04X dbc=%04X len=%u $0A0B=%02X%02X "
                         "src8=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                         cdc_dma_dest, cdc_dac, cdc_dbc, len, gate_array[0x0A], gate_array[0x0B],
                         cdc_ram[src & 0x3FFFU], cdc_ram[(src + 1U) & 0x3FFFU],
                         cdc_ram[(src + 2U) & 0x3FFFU], cdc_ram[(src + 3U) & 0x3FFFU],
                         cdc_ram[(src + 4U) & 0x3FFFU], cdc_ram[(src + 5U) & 0x3FFFU],
                         cdc_ram[(src + 6U) & 0x3FFFU], cdc_ram[(src + 7U) & 0x3FFFU]);
        }

        switch (cdc_dma_dest) {
        case 5: { // PRG-RAM (word index in $0A..$0B)
            const std::uint32_t dst =
                (static_cast<std::uint32_t>((gate_array[0x0A] << 8) | gate_array[0x0B]) << 3) &
                0x7FFFFU;
            for (std::uint32_t i = 0; i < len; ++i) {
                prg_ram[(dst + i) & (prg_ram_size - 1U)] = cdc_ram[(src + i) & 0x3FFFU];
            }
            auto a = static_cast<std::uint16_t>((gate_array[0x0A] << 8) | gate_array[0x0B]);
            a = static_cast<std::uint16_t>(a + (len >> 3));
            gate_array[0x0A] = static_cast<std::uint8_t>(a >> 8);
            gate_array[0x0B] = static_cast<std::uint8_t>(a);
            break;
        }
        case 7: { // Word-RAM
            const std::uint32_t dst =
                (static_cast<std::uint32_t>((gate_array[0x0A] << 8) | gate_array[0x0B]) << 3) &
                0x3FFFFU;
            for (std::uint32_t i = 0; i < len; ++i) {
                word_ram[(dst + i) & (word_ram_size - 1U)] = cdc_ram[(src + i) & 0x3FFFU];
            }
            auto a = static_cast<std::uint16_t>((gate_array[0x0A] << 8) | gate_array[0x0B]);
            a = static_cast<std::uint16_t>(a + (len >> 3));
            gate_array[0x0A] = static_cast<std::uint8_t>(a >> 8);
            gate_array[0x0B] = static_cast<std::uint8_t>(a);
            break;
        }
        case 4: { // PCM RAM: dest = ($0A:$0B << 2) into the current 4 KB wave-RAM
                  // bank (byte DMA), NOT the CDC source offset.
            std::uint16_t dst = static_cast<std::uint16_t>(
                (((gate_array[0x0A] << 8) | gate_array[0x0B]) << 2) & 0x0FFFU);
            for (std::uint32_t i = 0; i < len; ++i) {
                pcm.write_waveram(dst, cdc_ram[(src + i) & 0x3FFFU]);
                dst = static_cast<std::uint16_t>((dst + 1U) & 0x0FFFU);
            }
            auto a = static_cast<std::uint16_t>((gate_array[0x0A] << 8) | gate_array[0x0B]);
            a = static_cast<std::uint16_t>(a + (len >> 2));
            gate_array[0x0A] = static_cast<std::uint8_t>(a >> 8);
            gate_array[0x0B] = static_cast<std::uint8_t>(a);
            break;
        }
        default:
            break;
        }
        cdc_dac = static_cast<std::uint16_t>(cdc_dac + len);
        cdc_dma_finish();
    }

    void segacd_system::cdc_host_advance() {
        if (cdc_dma_dest != 2 && cdc_dma_dest != 3) {
            return; // not a host-read transfer
        }
        if ((gate_array[0x04] & 0x80U) != 0U) {                    // EDT -- transfer done
            gate_array[0x04] &= static_cast<std::uint8_t>(~0x40U); // clear DSR
            return;
        }
        const auto off = static_cast<std::uint16_t>(cdc_dac & 0x3FFEU);
        gate_array[0x08] = cdc_ram[off];
        gate_array[0x09] = cdc_ram[off + 1U];
        cdc_dac = static_cast<std::uint16_t>(cdc_dac + 2U);
        cdc_dbc = static_cast<std::uint16_t>(cdc_dbc - 2U);
        if (static_cast<std::int16_t>(cdc_dbc) < 0) {
            cdc_dma_finish();
        }
    }

    void segacd_system::cdc_reg_w(std::uint8_t value) {
        switch (cdc_ar) {
        case 0x01: { // IFCTRL
            const std::uint8_t prev = cdc_irq;
            cdc_irq =
                static_cast<std::uint8_t>(~cdc_ifstat & value & (ifctrl_dteien | ifctrl_decien));
            cdc_update_irq(prev);
            if ((value & ifctrl_douten) == 0U) {
                cdc_ifstat |= static_cast<std::uint8_t>(ifstat_dtbsy | ifstat_dten);
                cdc_dma_dest = 0;
            }
            cdc_ifctrl = value;
            break;
        }
        case 0x02:
            cdc_dbc = static_cast<std::uint16_t>((cdc_dbc & 0xFF00U) | value);
            break;
        case 0x03:
            cdc_dbc = static_cast<std::uint16_t>((cdc_dbc & 0x00FFU) | ((value & 0x0FU) << 8U));
            break;
        case 0x04:
            cdc_dac = static_cast<std::uint16_t>((cdc_dac & 0xFF00U) | value);
            break;
        case 0x05:
            cdc_dac = static_cast<std::uint16_t>((cdc_dac & 0x00FFU) |
                                                 (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 0x06: // DTRG -- start data transfer
            if (segacd_trace_enabled()) {
                std::fprintf(stderr,
                             "[cdcw] DTRG ifctrl=%02X douten=%d dest=%u dac=%04X dbc=%04X\n",
                             cdc_ifctrl, (cdc_ifctrl & ifctrl_douten) != 0U,
                             gate_array[0x04] & 0x07U, cdc_dac, cdc_dbc);
            }
            if ((cdc_ifctrl & ifctrl_douten) != 0U) {
                cdc_ifstat &= static_cast<std::uint8_t>(~(ifstat_dtbsy | ifstat_dten));
                gate_array[0x04] &= 0x07U; // clear DSR + EDT
                cdc_dma_init();
            }
            break;
        case 0x07: // DTACK -- acknowledge data-transfer-end IRQ
            cdc_ifstat |= ifstat_dtei;
            cdc_irq &= static_cast<std::uint8_t>(~ifstat_dtei);
            break;
        case 0x08:
            cdc_wa = static_cast<std::uint16_t>((cdc_wa & 0xFF00U) | value);
            break;
        case 0x09:
            cdc_wa = static_cast<std::uint16_t>((cdc_wa & 0x00FFU) |
                                                (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 0x0A: // CTRL0
            cdc_stat[0] = static_cast<std::uint8_t>(value & ctrl0_decen);
            if ((value & ctrl0_decen) == 0U) {
                cdc_ifstat |= ifstat_deci;
                cdc_irq &= static_cast<std::uint8_t>(~ifstat_deci);
            }
            if ((value & ctrl0_autorq) != 0U) {
                cdc_stat[2] = static_cast<std::uint8_t>((cdc_ctrl[1] & ctrl1_modrq) |
                                                        ((cdc_head[1][2] & 0x20U) >> 3U));
            } else {
                cdc_stat[2] = static_cast<std::uint8_t>(cdc_ctrl[1] & (ctrl1_modrq | ctrl1_formrq));
            }
            cdc_ctrl[0] = value;
            break;
        case 0x0B: // CTRL1
            if ((cdc_ctrl[0] & ctrl0_autorq) != 0U) {
                cdc_stat[2] = static_cast<std::uint8_t>((value & ctrl1_modrq) |
                                                        ((cdc_head[1][2] & 0x20U) >> 3U));
            } else {
                cdc_stat[2] = static_cast<std::uint8_t>(value & (ctrl1_modrq | ctrl1_formrq));
            }
            cdc_ctrl[1] = value;
            break;
        case 0x0C:
            cdc_pt = static_cast<std::uint16_t>((cdc_pt & 0xFF00U) | value);
            break;
        case 0x0D:
            cdc_pt = static_cast<std::uint16_t>((cdc_pt & 0x00FFU) |
                                                (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 0x0F: // RESET
            cdc_ifstat = 0xFFU;
            cdc_ifctrl = 0;
            cdc_ctrl[0] = 0;
            cdc_ctrl[1] = 0;
            cdc_stat[0] = 0;
            cdc_stat[1] = 0;
            cdc_stat[2] = 0;
            cdc_stat[3] = stat3_valst;
            cdc_irq = 0;
            cdc_dma_dest = 0;
            break;
        default:
            break;
        }
        if (cdc_ar != 0U) {
            cdc_ar = static_cast<std::uint8_t>((cdc_ar + 1U) & 0x1FU);
        }
    }

    std::uint8_t segacd_system::cdc_reg_r() {
        std::uint8_t data;
        const std::size_t shdr = (cdc_ctrl[1] & ctrl1_shdren) != 0U ? 1U : 0U;
        switch (cdc_ar) {
        case 0x01:
            data = cdc_ifstat;
            break;
        case 0x02:
            data = static_cast<std::uint8_t>(cdc_dbc);
            break;
        case 0x03:
            data = static_cast<std::uint8_t>(cdc_dbc >> 8U);
            break;
        case 0x04:
            data = cdc_head[shdr][0];
            break;
        case 0x05:
            data = cdc_head[shdr][1];
            break;
        case 0x06:
            data = cdc_head[shdr][2];
            break;
        case 0x07:
            data = cdc_head[shdr][3];
            break;
        case 0x08:
            data = static_cast<std::uint8_t>(cdc_pt);
            break;
        case 0x09:
            data = static_cast<std::uint8_t>(cdc_pt >> 8U);
            break;
        case 0x0A:
            data = static_cast<std::uint8_t>(cdc_wa);
            break;
        case 0x0B:
            data = static_cast<std::uint8_t>(cdc_wa >> 8U);
            break;
        case 0x0C:
            data = cdc_stat[0];
            break;
        case 0x0D:
            data = 0x00;
            break;
        case 0x0E:
            data = cdc_stat[2];
            break;
        case 0x0F:
            data = cdc_stat[3];
            cdc_stat[3] = stat3_valst;
            cdc_ifstat |= ifstat_deci;
            cdc_irq &= static_cast<std::uint8_t>(~ifstat_deci);
            break;
        default:
            data = 0xFF;
            break;
        }
        if (segacd_trace_enabled()) {
            static int n = 0;
            if (n++ < 400) {
                std::fprintf(stderr, "[cdcr] ar=%02X -> %02X\n", cdc_ar, data);
            }
        }
        if (cdc_ar != 0U) {
            cdc_ar = static_cast<std::uint8_t>((cdc_ar + 1U) & 0x1FU);
        }
        return data;
    }

    void segacd_system::cdc_decoder_update(std::uint32_t header) {
        ++cdc_sectors_decoded;
        last_sector_header = header;
        if ((cdc_ctrl[0] & ctrl0_decen) == 0U) {
            return;
        }
        cdc_head[0][0] = static_cast<std::uint8_t>(header);
        cdc_head[0][1] = static_cast<std::uint8_t>(header >> 8U);
        cdc_head[0][2] = static_cast<std::uint8_t>(header >> 16U);
        cdc_head[0][3] = static_cast<std::uint8_t>(header >> 24U);

        cdc_stat[3] = 0x00;                                    // clear !VALST
        cdc_ifstat &= static_cast<std::uint8_t>(~ifstat_deci); // DECI pending

        if ((cdc_ifctrl & ifctrl_decien) != 0U) {
            const std::uint8_t prev = cdc_irq;
            if ((cdc_irq & ifstat_dtei) == 0U) {
                cdc_irq |= ifstat_deci;
            }
            cdc_update_irq(prev);
        }

        if ((cdc_ctrl[0] & ctrl0_wrrq) == 0U) {
            return;
        }

        cdc_pt = static_cast<std::uint16_t>(cdc_pt + 2352U);
        cdc_wa = static_cast<std::uint16_t>(cdc_wa + 2352U);
        std::size_t offset = cdc_pt & 0x3FFFU;

        cdc_ram[offset + 0] = cdc_head[0][0];
        cdc_ram[offset + 1] = cdc_head[0][1];
        cdc_ram[offset + 2] = cdc_head[0][2];
        cdc_ram[offset + 3] = cdc_head[0][3];
        offset += 4;

        std::array<std::uint8_t, mnemos::disc::disc_image::raw_sector_size> raw{};
        if (disc != nullptr && cdd_lba >= 0 &&
            disc->read_raw_sector(
                static_cast<std::uint32_t>(cdd_lba),
                std::span<std::uint8_t, mnemos::disc::disc_image::raw_sector_size>{raw})) {
            if (cdc_head[0][3] == 0x01U) { // Mode 1: 2048 user-data bytes at offset 16
                std::memcpy(cdc_ram.data() + offset, raw.data() + 16, 2048U);
                offset += 2048U;
            } else { // Mode 2
                cdc_head[1][0] = raw[16];
                cdc_head[1][1] = raw[17];
                cdc_head[1][2] = raw[18];
                cdc_head[1][3] = raw[19];
                if ((cdc_ctrl[1] & ctrl1_modrq) != 0U) {
                    std::memcpy(cdc_ram.data() + offset, cdc_head[1].data(), 4U);
                    std::memcpy(cdc_ram.data() + offset + 4U, cdc_head[1].data(), 4U);
                    std::memcpy(cdc_ram.data() + offset + 8U, raw.data() + 24, 2328U);
                    offset += 2336U;
                } else {
                    std::memcpy(cdc_ram.data() + offset, raw.data() + 24, 2328U);
                    offset += 2328U;
                }
            }
        } else {
            offset += 2048U;
        }

        if (offset > 0x4000U) { // wrap the 16 KB ring
            std::memcpy(cdc_ram.data(), cdc_ram.data() + 0x4000U, offset - 0x4000U);
        }
    }

} // namespace mnemos::manifests::segacd
