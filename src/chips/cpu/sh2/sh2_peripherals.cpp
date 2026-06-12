#include "sh2_peripherals.hpp"

#include "ibus.hpp"
#include "state.hpp"

#include <algorithm>
#include <limits>

namespace mnemos::chips::cpu {

    namespace {
        // DMAC bits + byte-access helpers for its 32-bit registers.
        constexpr std::uint32_t chcr_de = 0x01U;              // channel enable
        constexpr std::uint32_t chcr_te = 0x02U;              // transfer end
        constexpr std::uint32_t chcr_ie = 0x04U;              // transfer-end IRQ enable
        constexpr std::uint32_t chcr_tb = 0x10U;              // burst (1) vs cycle-steal (0)
        constexpr std::uint32_t chcr_ds = 0x40U;              // DREQ edge (1) vs level (0)
        constexpr std::uint32_t chcr_ar = 0x200U;             // auto (1) vs module (0) request
        constexpr std::uint32_t chcr_valid = 0xFFFFU;         // CHCR writable bits
        constexpr std::uint32_t dmac_tcr_valid = 0x00FFFFFFU; // 24-bit transfer count
        constexpr std::uint32_t dmaor_dme = 0x01U;            // DMA master enable
        constexpr std::uint32_t dmaor_nmif = 0x02U;           // NMI flag (blocks DMA)
        constexpr std::uint32_t dmaor_ae = 0x04U;             // address-error flag (blocks DMA)
        constexpr std::uint32_t dmaor_pr = 0x08U;             // round-robin priority mode
        constexpr std::uint32_t dmaor_valid = 0x0FU;

        // Burst mode holds the bus until the block drains; a guest can program a
        // 24-bit TCR (up to ~16M units), so cap the units moved per peripheral
        // tick. The channel stays enabled (TE clears only at TCR==0) and resumes
        // on the next tick, bounding the work charged to a single instruction.
        constexpr std::uint32_t dmac_burst_units_per_tick = 0x400U;

        [[nodiscard]] std::uint8_t reg_byte(std::uint32_t reg, std::uint32_t off) noexcept {
            return static_cast<std::uint8_t>(reg >> ((3U - (off & 3U)) * 8U)); // big-endian
        }
        [[nodiscard]] std::uint32_t set_reg_byte(std::uint32_t reg, std::uint32_t off,
                                                 std::uint8_t value) noexcept {
            const unsigned shift = (3U - (off & 3U)) * 8U;
            return (reg & ~(static_cast<std::uint32_t>(0xFFU) << shift)) |
                   (static_cast<std::uint32_t>(value) << shift);
        }

        // FTCSR ($FE11) status/control bits.
        constexpr std::uint8_t ftcsr_cclr = 0x01U;     // clear FRC on an OCRA match
        constexpr std::uint8_t ftcsr_ovf = 0x02U;      // counter overflow
        constexpr std::uint8_t ftcsr_ocfb = 0x04U;     // OCRB match
        constexpr std::uint8_t ftcsr_ocfa = 0x08U;     // OCRA match
        constexpr std::uint8_t ftcsr_flags = 0x8EU;    // OVF | OCFB | OCFA | ICF
        constexpr std::uint8_t ftcsr_readable = 0x8FU; // flags | CCLR

        // TIER ($FE10) interrupt-enable bits.
        constexpr std::uint8_t tier_ovie = 0x02U;  // overflow IRQ enable
        constexpr std::uint8_t tier_ocibe = 0x04U; // output-compare B IRQ enable
        constexpr std::uint8_t tier_ociae = 0x08U; // output-compare A IRQ enable

        // INTC register valid-bit masks (reserved bits read 0).
        constexpr std::uint16_t ipra_valid = 0xFFF0U; // DIVU[15:12], DMAC[11:8], WDT[7:4]
        constexpr std::uint16_t iprb_valid = 0xFF00U; // SCI[15:12], FRT[11:8]
        constexpr std::uint16_t vcr_valid = 0x7F7FU;  // two 7-bit vectors
        constexpr std::uint16_t vcrd_valid = 0x7F00U; // OVI vector in the high byte
        constexpr std::uint32_t vcrdma_valid = 0x7FU; // lower byte, vector 0..127

        // WDT ($FE80-$FE83) bits + keyed-write keys.
        constexpr std::uint8_t wtcsr_ovf = 0x80U;       // interval-timer overflow flag
        constexpr std::uint8_t wtcsr_wtit = 0x40U;      // watchdog (1) vs interval (0) mode
        constexpr std::uint8_t wtcsr_tme = 0x20U;       // timer enable
        constexpr std::uint8_t wtcsr_reserved = 0x18U;  // reserved bits read 1
        constexpr std::uint8_t wtcsr_cks = 0x07U;       // clock select
        constexpr std::uint8_t rstcsr_wovf = 0x80U;     // watchdog overflow flag
        constexpr std::uint8_t rstcsr_rste = 0x40U;     // internal reset enable
        constexpr std::uint8_t rstcsr_rsts = 0x20U;     // hard/manual reset select
        constexpr std::uint8_t rstcsr_writable = 0x60U; // RSTE | RSTS
        constexpr std::uint8_t rstcsr_reserved = 0x1FU;
        constexpr std::uint8_t wdt_key_wtcsr = 0xA5U; // write WTCSR / clear RSTCSR.WOVF
        constexpr std::uint8_t wdt_key_wtcnt = 0x5AU; // write WTCNT / RSTCSR.RSTE,RSTS

        // SCI ($FE00-$FE05) request bits. This block models register-visible
        // byte/status flag production; exact baud pacing is a later slice.
        constexpr std::uint8_t sci_scr_tie = 0x80U;
        constexpr std::uint8_t sci_scr_rie = 0x40U;
        constexpr std::uint8_t sci_scr_te = 0x20U;
        constexpr std::uint8_t sci_scr_re = 0x10U;
        constexpr std::uint8_t sci_scr_teie = 0x04U;
        constexpr std::uint8_t sci_ssr_tdre = 0x80U;
        constexpr std::uint8_t sci_ssr_rdrf = 0x40U;
        constexpr std::uint8_t sci_ssr_orer = 0x20U;
        constexpr std::uint8_t sci_ssr_fer = 0x10U;
        constexpr std::uint8_t sci_ssr_per = 0x08U;
        constexpr std::uint8_t sci_ssr_tend = 0x04U;
        constexpr std::uint8_t sci_ssr_errors =
            static_cast<std::uint8_t>(sci_ssr_orer | sci_ssr_fer | sci_ssr_per);
        constexpr std::uint8_t sci_ssr_valid = 0xFFU;
        constexpr std::uint8_t sci_ssr_reset =
            static_cast<std::uint8_t>(sci_ssr_tdre | sci_ssr_tend);
        constexpr int sci_transmit_cycles = 1;

        constexpr std::uint32_t sh2_peripherals_state_magic = 0x46503253U; // "S2PF"
        constexpr int divu_normal_cycles = 39;
        constexpr int divu_overflow_cycles = 6;

        // Bump on EVERY save_state layout change (per ADR-0021: pre-release
        // states need not stay loadable, but save/load must change together and
        // old blobs must fail the ok() gate, not load wrong). v4 covers SCI,
        // IPRA/VCRA/VCRB/VCRWDT, VCRDMA, the watchdog-reset request, DREQ-edge +
        // round-robin DMAC state, and the in-flight DIVU result.
        constexpr std::uint16_t sh2_peripherals_state_version = 4U;

        [[nodiscard]] int wdt_prescale_from_wtcsr(std::uint8_t wtcsr) noexcept {
            constexpr std::array<int, 8> prescales{{2, 64, 128, 256, 512, 1024, 4096, 8192}};
            return prescales[wtcsr & wtcsr_cks];
        }

        [[nodiscard]] int bounded_state_int(std::uint32_t value) noexcept {
            return value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                       ? std::numeric_limits<int>::max()
                       : static_cast<int>(value);
        }

        // Saturating add for the (always non-negative) prescale accumulators: a
        // board returning a huge wait could otherwise overflow the int += int.
        // Realistic per-call counts are tiny, so this only bites at the edge.
        [[nodiscard]] int add_bounded_acc(int acc, std::uint64_t cycles) noexcept {
            const auto room = static_cast<std::uint64_t>(std::numeric_limits<int>::max() - acc);
            return cycles > room ? std::numeric_limits<int>::max() : acc + static_cast<int>(cycles);
        }

        [[nodiscard]] bool in_divu_registers(std::uint32_t addr) noexcept {
            const std::uint32_t off = addr & (sh2_peripherals::window_size - 1U);
            return off >= 0x100U && off < 0x140U;
        }

        // FRT clock prescale from TCR[1:0]. Mode 3 is the external FTCI pin, which
        // has no host surface here, so it leaves the timer stopped.
        [[nodiscard]] int frt_prescale_from_tcr(std::uint8_t tcr) noexcept {
            switch (tcr & 0x03U) {
            case 0x00U:
                return 8;
            case 0x01U:
                return 32;
            case 0x02U:
                return 128;
            default:
                return 0;
            }
        }
    } // namespace

    std::uint8_t sh2_peripherals::read8(std::uint32_t addr) const noexcept {
        const std::uint32_t off = addr & (window_size - 1U);
        switch (off) {
        case 0x00U: // SMR
            return sci_smr_;
        case 0x01U: // BRR
            return sci_brr_;
        case 0x02U: // SCR
            return sci_scr_;
        case 0x03U: // TDR
            return sci_tdr_;
        case 0x04U: // SSR
            return sci_ssr_;
        case 0x05U: // RDR
            return sci_rdr_;
        case 0x10U: // TIER
            return static_cast<std::uint8_t>(tier_ & 0x8FU);
        case 0x11U: { // FTCSR -- reading observes the event flags (write-0 clears)
            const auto value = static_cast<std::uint8_t>(ftcsr_ & ftcsr_readable);
            ftcsr_read_flags_ |= static_cast<std::uint8_t>(value & ftcsr_flags);
            return value;
        }
        case 0x12U: // FRC high (latches the low byte for an atomic 16-bit read)
            frt_temp_ = static_cast<std::uint8_t>(frc_);
            return static_cast<std::uint8_t>(frc_ >> 8U);
        case 0x13U: // FRC low (the latched byte)
            return frt_temp_;
        case 0x14U: // OCRA/OCRB high (selected by TOCR.OCRS)
            return static_cast<std::uint8_t>(selected_ocr() >> 8U);
        case 0x15U: // OCRA/OCRB low
            return static_cast<std::uint8_t>(selected_ocr());
        case 0x16U: // TCR
            return static_cast<std::uint8_t>(tcr_ & 0x83U);
        case 0x17U: // TOCR
            return static_cast<std::uint8_t>(0xE0U | (tocr_ & 0x13U));
        case 0x60U: // IPRB high
            return static_cast<std::uint8_t>(iprb_ >> 8U);
        case 0x61U: // IPRB low
            return static_cast<std::uint8_t>(iprb_);
        case 0x62U: // VCRA high
            return static_cast<std::uint8_t>(vcra_ >> 8U);
        case 0x63U: // VCRA low
            return static_cast<std::uint8_t>(vcra_);
        case 0x64U: // VCRB high
            return static_cast<std::uint8_t>(vcrb_ >> 8U);
        case 0x65U: // VCRB low
            return static_cast<std::uint8_t>(vcrb_);
        case 0x66U: // VCRC high
            return static_cast<std::uint8_t>(vcrc_ >> 8U);
        case 0x67U: // VCRC low
            return static_cast<std::uint8_t>(vcrc_);
        case 0x68U: // VCRD high
            return static_cast<std::uint8_t>(vcrd_ >> 8U);
        case 0x69U: // VCRD low
            return static_cast<std::uint8_t>(vcrd_);
        case 0x80U: // WTCSR -- reading observes the overflow flag
            if ((wtcsr_ & wtcsr_ovf) != 0U) {
                wtcsr_ovf_read_ = true;
            }
            return wtcsr_;
        case 0x81U: // WTCNT
            return wtcnt_;
        case 0x82U: // RSTCSR
        case 0x83U:
            return rstcsr_;
        case 0xE2U: // IPRA high
            return static_cast<std::uint8_t>(ipra_ >> 8U);
        case 0xE3U: // IPRA low
            return static_cast<std::uint8_t>(ipra_);
        case 0xE4U: // VCRWDT high
            return static_cast<std::uint8_t>(vcrwdt_ >> 8U);
        case 0xE5U: // VCRWDT low
            return static_cast<std::uint8_t>(vcrwdt_);
        default:
            if (off >= 0x100U && off < 0x140U) { // DIVU (mirrored at +$20)
                const std::uint32_t canon = off >= 0x120U ? off - 0x20U : off;
                switch (canon & 0x1CU) {
                case 0x00U:
                    return reg_byte(dvsr_, off);
                case 0x04U:
                    return reg_byte(dvdnt_, off);
                case 0x08U:
                    return reg_byte(dvcr_ & 0x03U, off);
                case 0x0CU:
                    return reg_byte(vcrdiv_ & 0x7FU, off);
                case 0x10U:
                    return reg_byte(dvdnth_, off);
                case 0x14U:
                    return reg_byte(dvdntl_, off);
                case 0x18U:
                    return reg_byte(dvdntuh_, off);
                default:
                    return reg_byte(dvdntul_, off);
                }
            }
            if (off >= 0x180U && off < 0x1A0U) { // DMAC channel registers (32-bit)
                const dma_channel& ch = dma_[(off >> 4U) & 1U];
                switch ((off >> 2U) & 3U) {
                case 0U:
                    return reg_byte(ch.sar, off);
                case 1U:
                    return reg_byte(ch.dar, off);
                case 2U:
                    return reg_byte(ch.tcr, off);
                default:
                    return reg_byte(ch.chcr, off);
                }
            }
            if ((off >= 0x1A0U && off < 0x1A4U) || (off >= 0x1A8U && off < 0x1ACU)) {
                const std::size_t chan = (off >= 0x1A8U) ? 1U : 0U;
                return reg_byte(vcrdma_[chan] & vcrdma_valid, off);
            }
            if (off >= 0x1B0U && off < 0x1B4U) { // DMAOR
                return reg_byte(dmaor_, off);
            }
            return regs_[off];
        }
    }

    void sh2_peripherals::write8(std::uint32_t addr, std::uint8_t value) noexcept {
        const std::uint32_t off = addr & (window_size - 1U);
        switch (off) {
        case 0x00U: // SMR
            sci_smr_ = value;
            return;
        case 0x01U: // BRR
            sci_brr_ = value;
            return;
        case 0x02U: { // SCR
            const bool had_te = (sci_scr_ & sci_scr_te) != 0U;
            sci_scr_ = value;
            if ((sci_scr_ & sci_scr_te) == 0U) {
                sci_tx_cycles_ = 0;
            } else if (!had_te) {
                start_sci_transmit_if_ready();
            }
            return;
        }
        case 0x03U: // TDR
            sci_tdr_ = value;
            sci_ssr_ = static_cast<std::uint8_t>(
                sci_ssr_ & static_cast<std::uint8_t>(~(sci_ssr_tdre | sci_ssr_tend)));
            sci_tx_cycles_ = 0;
            start_sci_transmit_if_ready();
            return;
        case 0x04U: // SSR
            sci_ssr_ = static_cast<std::uint8_t>(value & sci_ssr_valid);
            if ((sci_ssr_ & sci_ssr_tdre) != 0U) {
                sci_tx_cycles_ = 0;
            }
            return;
        case 0x05U: // RDR storage for state/debug writes; receive uses sci_receive_byte().
            sci_rdr_ = value;
            return;
        case 0x10U: // TIER
            tier_ = static_cast<std::uint8_t>((value & 0x8EU) | 0x01U);
            return;
        case 0x11U: { // FTCSR -- a flag clears only if a read observed it and the
                      // write puts 0 there; CCLR is freely writable.
            const auto observed =
                static_cast<std::uint8_t>(value | static_cast<std::uint8_t>(~ftcsr_read_flags_));
            const auto preserved = static_cast<std::uint8_t>(ftcsr_ & observed & ftcsr_flags);
            ftcsr_ = static_cast<std::uint8_t>(preserved | (value & ftcsr_cclr));
            ftcsr_read_flags_ &= value;
            return;
        }
        case 0x12U: // FRC high (latched until the low byte completes the word)
            frt_temp_ = value;
            return;
        case 0x13U: // FRC low
            frc_ =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(frt_temp_) << 8U) | value);
            return;
        case 0x14U: // OCR high (latched)
            frt_temp_ = value;
            return;
        case 0x15U: { // OCR low -> commit the selected output-compare register
            std::uint16_t& ocr = (tocr_ & 0x10U) != 0U ? ocrb_ : ocra_;
            ocr = static_cast<std::uint16_t>((static_cast<std::uint16_t>(frt_temp_) << 8U) | value);
            return;
        }
        case 0x16U: // TCR -- changing the clock source restarts the prescaler
            tcr_ = static_cast<std::uint8_t>(value & 0x83U);
            frt_prescale_acc_ = 0;
            return;
        case 0x17U: // TOCR
            tocr_ = static_cast<std::uint8_t>(0xE0U | (value & 0x13U));
            return;
        case 0x60U: // IPRB high
            iprb_ = static_cast<std::uint16_t>(
                ((iprb_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & iprb_valid);
            return;
        case 0x61U: // IPRB low
            iprb_ = static_cast<std::uint16_t>(((iprb_ & 0xFF00U) | value) & iprb_valid);
            return;
        case 0x62U: // VCRA high
            vcra_ = static_cast<std::uint16_t>(
                ((vcra_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & vcr_valid);
            return;
        case 0x63U: // VCRA low
            vcra_ = static_cast<std::uint16_t>(((vcra_ & 0xFF00U) | value) & vcr_valid);
            return;
        case 0x64U: // VCRB high
            vcrb_ = static_cast<std::uint16_t>(
                ((vcrb_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & vcr_valid);
            return;
        case 0x65U: // VCRB low
            vcrb_ = static_cast<std::uint16_t>(((vcrb_ & 0xFF00U) | value) & vcr_valid);
            return;
        case 0x66U: // VCRC high
            vcrc_ = static_cast<std::uint16_t>(
                ((vcrc_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & vcr_valid);
            return;
        case 0x67U: // VCRC low
            vcrc_ = static_cast<std::uint16_t>(((vcrc_ & 0xFF00U) | value) & vcr_valid);
            return;
        case 0x68U: // VCRD high
            vcrd_ = static_cast<std::uint16_t>(
                ((vcrd_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & vcrd_valid);
            return;
        case 0x69U: // VCRD low
            vcrd_ = static_cast<std::uint16_t>(((vcrd_ & 0xFF00U) | value) & vcrd_valid);
            return;
        case 0x80U: // WTCSR/WTCNT keyed write: latch the high-byte key
        case 0x82U: // RSTCSR keyed write: latch the key
            wdt_key_ = value;
            return;
        case 0x81U: // data byte completes a WTCSR or WTCNT write
            if (wdt_key_ == wdt_key_wtcsr) {
                // WTCSR: OVF is write-0-to-clear after a read; reserved bits set.
                std::uint8_t ovf = wtcsr_ & wtcsr_ovf;
                if ((value & wtcsr_ovf) == 0U && wtcsr_ovf_read_) {
                    ovf = 0U;
                }
                wtcsr_ovf_read_ = false;
                wtcsr_ = static_cast<std::uint8_t>(
                    ovf | (value & (wtcsr_wtit | wtcsr_tme | wtcsr_cks)) | wtcsr_reserved);
                if ((wtcsr_ & wtcsr_tme) == 0U) {
                    wtcnt_ = 0U;
                    wdt_prescale_acc_ = 0;
                }
            } else if (wdt_key_ == wdt_key_wtcnt) {
                wtcnt_ = value;
                wdt_prescale_acc_ = 0;
            }
            return;
        case 0x83U: // data byte completes an RSTCSR write
            if (wdt_key_ == wdt_key_wtcsr) {
                if (value == 0U) {
                    rstcsr_ = static_cast<std::uint8_t>(rstcsr_ & ~rstcsr_wovf);
                }
            } else if (wdt_key_ == wdt_key_wtcnt) {
                rstcsr_ = static_cast<std::uint8_t>((value & rstcsr_writable) | rstcsr_reserved |
                                                    (rstcsr_ & rstcsr_wovf));
            }
            return;
        case 0xE2U: // IPRA high
            ipra_ = static_cast<std::uint16_t>(
                ((ipra_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & ipra_valid);
            return;
        case 0xE3U: // IPRA low
            ipra_ = static_cast<std::uint16_t>(((ipra_ & 0xFF00U) | value) & ipra_valid);
            return;
        case 0xE4U: // VCRWDT high
            vcrwdt_ = static_cast<std::uint16_t>(
                ((vcrwdt_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U)) & vcr_valid);
            return;
        case 0xE5U: // VCRWDT low
            vcrwdt_ = static_cast<std::uint16_t>(((vcrwdt_ & 0xFF00U) | value) & vcr_valid);
            return;
        default:
            if (off >= 0x100U && off < 0x140U) { // DIVU (mirrored at +$20)
                const std::uint32_t canon = off >= 0x120U ? off - 0x20U : off;
                // The divide fires when the LAST byte of the trigger register
                // completes (a 32-bit store arrives as four byte writes here).
                const bool last_byte = (off & 3U) == 3U;
                switch (canon & 0x1CU) {
                case 0x00U:
                    dvsr_ = set_reg_byte(dvsr_, off, value);
                    return;
                case 0x04U:
                    dvdnt_ = set_reg_byte(dvdnt_, off, value);
                    if (last_byte) {
                        divu_run_32();
                    }
                    return;
                case 0x08U:
                    dvcr_ = set_reg_byte(dvcr_, off, value) & 0x03U;
                    return;
                case 0x0CU:
                    vcrdiv_ = static_cast<std::uint16_t>(set_reg_byte(vcrdiv_, off, value) & 0x7FU);
                    return;
                case 0x10U:
                    dvdnth_ = set_reg_byte(dvdnth_, off, value);
                    return;
                case 0x14U:
                    dvdntl_ = set_reg_byte(dvdntl_, off, value);
                    if (last_byte) {
                        divu_run_64();
                    }
                    return;
                case 0x18U:
                    dvdntuh_ = set_reg_byte(dvdntuh_, off, value);
                    return;
                default:
                    dvdntul_ = set_reg_byte(dvdntul_, off, value);
                    return;
                }
            }
            if (off >= 0x180U && off < 0x1A0U) { // DMAC channel registers (32-bit)
                dma_channel& ch = dma_[(off >> 4U) & 1U];
                switch ((off >> 2U) & 3U) {
                case 0U:
                    ch.sar = set_reg_byte(ch.sar, off, value);
                    return;
                case 1U:
                    ch.dar = set_reg_byte(ch.dar, off, value);
                    return;
                case 2U:
                    ch.tcr = set_reg_byte(ch.tcr, off, value) & dmac_tcr_valid;
                    return;
                default:
                    ch.chcr = set_reg_byte(ch.chcr, off, value) & chcr_valid;
                    return;
                }
            }
            if ((off >= 0x1A0U && off < 0x1A4U) || (off >= 0x1A8U && off < 0x1ACU)) {
                const std::size_t chan = (off >= 0x1A8U) ? 1U : 0U;
                vcrdma_[chan] = set_reg_byte(vcrdma_[chan], off, value) & vcrdma_valid;
                return;
            }
            if (off >= 0x1B0U && off < 0x1B4U) { // DMAOR
                dmaor_ = set_reg_byte(dmaor_, off, value) & dmaor_valid;
                return;
            }
            regs_[off] = value;
            return;
        }
    }

    void sh2_peripherals::start_sci_transmit_if_ready() noexcept {
        if ((sci_scr_ & sci_scr_te) == 0U || (sci_ssr_ & sci_ssr_tdre) != 0U ||
            sci_tx_cycles_ > 0) {
            return;
        }
        sci_tx_cycles_ = sci_transmit_cycles;
    }

    void sh2_peripherals::sci_receive_byte(std::uint8_t value, std::uint8_t error_flags) noexcept {
        if ((sci_scr_ & sci_scr_re) == 0U) {
            return;
        }

        const auto errors = static_cast<std::uint8_t>(error_flags & sci_ssr_errors);
        if ((sci_ssr_ & sci_ssr_rdrf) != 0U) {
            sci_ssr_ = static_cast<std::uint8_t>(sci_ssr_ | sci_ssr_orer | errors);
            return;
        }

        sci_rdr_ = value;
        sci_ssr_ = static_cast<std::uint8_t>(sci_ssr_ | sci_ssr_rdrf | errors);
    }

    void sh2_peripherals::divu_start(bool overflow, std::uint32_t quotient,
                                     std::uint32_t remainder) noexcept {
        divu_pending_overflow_ = overflow;
        divu_pending_quotient_ = quotient;
        divu_pending_remainder_ = remainder;
        divu_cycles_remaining_ = overflow ? divu_overflow_cycles : divu_normal_cycles;
    }

    void sh2_peripherals::divu_finish(bool overflow, std::uint32_t quotient,
                                      std::uint32_t remainder) noexcept {
        dvdnt_ = quotient;
        dvdntl_ = quotient;
        dvdnth_ = remainder;
        if (overflow) {
            dvcr_ |= 0x01U; // OVF
        }
        dvdntuh_ = dvdnth_;
        dvdntul_ = dvdntl_;
    }

    void sh2_peripherals::divu_complete_pending() noexcept {
        if (divu_cycles_remaining_ <= 0) {
            return;
        }
        divu_cycles_remaining_ = 0;
        divu_finish(divu_pending_overflow_, divu_pending_quotient_, divu_pending_remainder_);
    }

    void sh2_peripherals::divu_advance(std::uint64_t cycles) noexcept {
        if (divu_cycles_remaining_ <= 0) {
            return;
        }
        if (cycles < static_cast<std::uint64_t>(divu_cycles_remaining_)) {
            divu_cycles_remaining_ -= static_cast<int>(cycles);
            return;
        }
        divu_complete_pending();
    }

    void sh2_peripherals::divu_run_32() noexcept {
        // Signed 32/32. The unit first widens the dividend into DVDNTH:DVDNTL.
        dvdntl_ = dvdnt_;
        dvdnth_ = (dvdnt_ & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x00000000U;
        const auto dividend = static_cast<std::int32_t>(dvdnt_);
        const auto divisor = static_cast<std::int32_t>(dvsr_);
        if (divisor == 0) {
            // Divide-by-zero: the aborted hardware iteration leaves a shifted
            // remainder; the quotient saturates unless OVFIE selects the raw
            // partial result.
            const std::uint32_t rem =
                static_cast<std::uint32_t>(static_cast<std::int64_t>(dividend) >> 29);
            std::uint32_t quot = 0;
            if ((dvcr_ & 0x02U) != 0U) { // OVFIE: raw partial quotient
                quot = (dvdnt_ << 3U) | (dividend < 0 ? 0U : 7U);
            } else {
                quot = dividend < 0 ? 0x80000000U : 0x7FFFFFFFU;
            }
            divu_start(true, quot, rem);
            return;
        }
        if (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
            divu_start(false, 0x80000000U, 0U);
            return;
        }
        const std::int32_t q = dividend / divisor;
        const std::int32_t r = dividend - q * divisor;
        divu_start(false, static_cast<std::uint32_t>(q), static_cast<std::uint32_t>(r));
    }

    void sh2_peripherals::divu_run_64() noexcept {
        // Signed 64/32 of DVDNTH:DVDNTL by DVSR.
        const std::uint64_t dividend_bits = (static_cast<std::uint64_t>(dvdnth_) << 32U) | dvdntl_;
        const auto dividend = static_cast<std::int64_t>(dividend_bits);
        const auto divisor = static_cast<std::int32_t>(dvsr_);
        if (dividend == static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) &&
            (divisor == 1 || divisor == -1)) {
            divu_start(false, 0x80000000U, 0U);
            return;
        }
        const bool zero = divisor == 0;
        // INT64_MIN / -1 is the one non-zero pair whose quotient (2^63) does not
        // fit the host's int64 -- evaluating it is UB (SIGFPE on x86), and guest
        // code can write exactly those register values. It is an overflow case
        // architecturally too, so route it down the overflow path undivided.
        const bool host_overflow =
            divisor == -1 && dividend == std::numeric_limits<std::int64_t>::min();
        std::int64_t q = 0;
        std::int64_t r = 0;
        if (!zero && !host_overflow) {
            q = dividend / divisor;
            r = dividend - q * static_cast<std::int64_t>(divisor);
        }
        if (zero || host_overflow || q > 0x7FFFFFFFLL || q < -0x80000000LL) {
            // Overflow: run the three aborted non-restoring iterations the
            // hardware performs before stopping, for the remainder image; the
            // quotient saturates by the dividend/divisor signs unless OVFIE
            // selects the raw partial result.
            std::uint64_t work = dividend_bits;
            const std::uint64_t divisor_shifted = static_cast<std::uint64_t>(dvsr_) << 32U;
            const bool divisor_negative = divisor < 0;
            for (unsigned i = 0; i < 3U; ++i) {
                bool work_negative = (work & 0x8000000000000000ULL) != 0U;
                if (work_negative == divisor_negative) {
                    work -= divisor_shifted;
                } else {
                    work += divisor_shifted;
                }
                work_negative = (work & 0x8000000000000000ULL) != 0U;
                work = (work << 1U) | (work_negative == divisor_negative ? 1ULL : 0ULL);
            }
            std::uint32_t quot = 0;
            if ((dvcr_ & 0x02U) != 0U) { // OVFIE: raw partial quotient
                quot = static_cast<std::uint32_t>(work);
            } else {
                const auto high = static_cast<std::int32_t>(dvdnth_);
                quot = (high ^ divisor) < 0 ? 0x80000000U : 0x7FFFFFFFU;
            }
            divu_start(true, quot, static_cast<std::uint32_t>(work >> 32U));
            return;
        }
        divu_start(false, static_cast<std::uint32_t>(q & 0xFFFFFFFFU),
                   static_cast<std::uint32_t>(r & 0xFFFFFFFFU));
    }

    std::uint64_t sh2_peripherals::tick(std::uint64_t cycles) noexcept {
        advance_time(cycles);
        if (watchdog_reset_pending_) {
            return 0U;
        }
        const std::uint64_t wait = run_dmac();
        if (wait != 0U) {
            const auto timed_wait =
                std::min(wait, static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
            advance_time(timed_wait);
        }
        return wait;
    }

    void sh2_peripherals::advance_time(std::uint64_t cycles) noexcept {
        divu_advance(cycles);

        // FRT: a free-running counter unless TCR selects the (unmodelled) external
        // clock.
        const int frt_prescale = frt_prescale_from_tcr(tcr_);
        if (frt_prescale > 0) {
            frt_prescale_acc_ = add_bounded_acc(frt_prescale_acc_, cycles);
            while (frt_prescale_acc_ >= frt_prescale) {
                frt_prescale_acc_ -= frt_prescale;
                const std::uint16_t prev = frc_;
                frc_ = static_cast<std::uint16_t>(frc_ + 1U);
                if (frc_ < prev) {
                    ftcsr_ |= ftcsr_ovf; // wrapped $FFFF -> $0000
                }
                if (frc_ == ocra_) {
                    ftcsr_ |= ftcsr_ocfa;
                    if ((ftcsr_ & ftcsr_cclr) != 0U) {
                        frc_ = 0U; // compare-clear cadence
                    }
                }
                if (frc_ == ocrb_) {
                    ftcsr_ |= ftcsr_ocfb;
                }
            }
        }

        if (sci_tx_cycles_ > 0) {
            if (cycles >= static_cast<std::uint64_t>(sci_tx_cycles_)) {
                sci_tx_cycles_ = 0;
                sci_ssr_ = static_cast<std::uint8_t>(sci_ssr_ | sci_ssr_tdre | sci_ssr_tend);
            } else {
                sci_tx_cycles_ -= static_cast<int>(cycles);
            }
        }

        // WDT: an 8-bit counter that advances while TME is set; overflow flags
        // the interval timer (or, in watchdog mode, the reset latch).
        if ((wtcsr_ & wtcsr_tme) != 0U) {
            const int wdt_prescale = wdt_prescale_from_wtcsr(wtcsr_);
            wdt_prescale_acc_ = add_bounded_acc(wdt_prescale_acc_, cycles);
            while (wdt_prescale_acc_ >= wdt_prescale) {
                wdt_prescale_acc_ -= wdt_prescale;
                const std::uint8_t prev = wtcnt_;
                wtcnt_ = static_cast<std::uint8_t>(wtcnt_ + 1U);
                if (wtcnt_ < prev) { // overflow $FF -> $00
                    if ((wtcsr_ & wtcsr_wtit) == 0U) {
                        wtcsr_ |= wtcsr_ovf; // interval-timer flag
                    } else {
                        rstcsr_ =
                            static_cast<std::uint8_t>(rstcsr_ | rstcsr_wovf | rstcsr_reserved);
                        const bool internal_reset = (rstcsr_ & rstcsr_rste) != 0U;
                        const bool manual_reset = (rstcsr_ & rstcsr_rsts) != 0U;
                        reset_watchdog_counter_control();
                        if (internal_reset) {
                            watchdog_reset_pending_ = true;
                            watchdog_reset_kind_ =
                                manual_reset ? reset_kind::hard : reset_kind::power_on;
                        }
                        break;
                    }
                }
            }
        }
    }

    std::uint64_t sh2_peripherals::consume_divu_access_wait(std::uint32_t addr) noexcept {
        if (!in_divu_registers(addr) || divu_cycles_remaining_ <= 0) {
            return 0U;
        }
        const auto wait = static_cast<std::uint64_t>(divu_cycles_remaining_);
        divu_complete_pending();
        return wait;
    }

    void sh2_peripherals::reset_watchdog_counter_control() noexcept {
        wtcsr_ = wtcsr_reserved;
        wtcnt_ = 0U;
        wdt_prescale_acc_ = 0;
        wdt_key_ = 0U;
        wtcsr_ovf_read_ = false;
    }

    std::uint64_t sh2_peripherals::run_dmac() noexcept {
        if (bus_ == nullptr) {
            return 0U;
        }
        // Master enable, and no NMI/address-error halting the controller.
        if ((dmaor_ & dmaor_dme) == 0U || (dmaor_ & (dmaor_nmif | dmaor_ae)) != 0U) {
            return 0U;
        }
        constexpr std::array<std::uint32_t, 4> unit_bytes{{1U, 2U, 4U, 16U}};

        const auto request_ready = [this](std::size_t chan) noexcept {
            const dma_channel& ch = dma_[chan];
            if ((ch.chcr & chcr_ar) != 0U) {
                return true;
            }
            if (!dreq_query_) {
                return false;
            }

            const bool active = dreq_query_(static_cast<int>(chan));
            if (active && !dreq_active_[chan]) {
                dreq_edge_pending_[chan] = true;
            }
            dreq_active_[chan] = active;

            if ((ch.chcr & chcr_ds) != 0U) {
                return dreq_edge_pending_[chan];
            }
            return active;
        };

        const auto channel_ready = [this, &request_ready](std::size_t chan) noexcept {
            const dma_channel& ch = dma_[chan];
            if ((ch.chcr & chcr_de) == 0U || (ch.chcr & chcr_te) != 0U) {
                return false;
            }
            if ((ch.tcr & dmac_tcr_valid) == 0U) {
                return false;
            }
            return request_ready(chan);
        };

        const auto select_channel = [this, &channel_ready]() noexcept -> int {
            if ((dmaor_ & dmaor_pr) != 0U) {
                const std::size_t first = dmac_rr_top_ & 1U;
                if (channel_ready(first)) {
                    return static_cast<int>(first);
                }
                const std::size_t second = first ^ 1U;
                return channel_ready(second) ? static_cast<int>(second) : -1;
            }
            if (channel_ready(0U)) {
                return 0;
            }
            return channel_ready(1U) ? 1 : -1;
        };

        const auto account_wait = [this](std::uint64_t current, std::uint32_t address,
                                         std::uint32_t bytes) noexcept {
            if (!bus_wait_ || bytes == 0U) {
                return current;
            }
            const int wait = bus_wait_(address, static_cast<std::uint8_t>(bytes), false);
            if (wait <= 0) {
                return current;
            }
            const auto add = static_cast<std::uint64_t>(wait);
            const auto room = std::numeric_limits<std::uint64_t>::max() - current;
            return add > room ? std::numeric_limits<std::uint64_t>::max() : current + add;
        };

        const auto transfer_one = [this, &account_wait, &unit_bytes](std::size_t chan) noexcept {
            std::uint64_t wait_cycles = 0U;
            dma_channel& ch = dma_[chan];
            std::uint32_t count = ch.tcr & dmac_tcr_valid;
            if (count == 0U) {
                return wait_cycles;
            }
            const std::uint32_t ts = (ch.chcr >> 10U) & 3U;
            const std::uint32_t sm = (ch.chcr >> 12U) & 3U;
            const std::uint32_t dm = (ch.chcr >> 14U) & 3U;
            const std::uint32_t bpu = unit_bytes[ts];

            wait_cycles = account_wait(wait_cycles, ch.sar, bpu);
            for (std::uint32_t i = 0; i < bpu; ++i) {
                bus_->write8(ch.dar + i, bus_->read8(ch.sar + i));
            }
            wait_cycles = account_wait(wait_cycles, ch.dar, bpu);

            if ((ch.chcr & chcr_ar) == 0U && (ch.chcr & chcr_ds) != 0U) {
                dreq_edge_pending_[chan] = false;
            }

            // In 16-byte mode the source advances by +16 regardless of SM, per
            // the SH7604 DMAC rules; destination keeps its selected mode.
            if (ts == 3U) {
                ch.sar += bpu;
            } else {
                if (sm == 1U) {
                    ch.sar += bpu;
                } else if (sm == 2U) {
                    ch.sar -= bpu;
                }
            }

            if (dm == 1U) {
                ch.dar += bpu;
            } else if (dm == 2U) {
                ch.dar -= bpu;
            }

            count = count > 0U ? count - 1U : 0U;
            ch.tcr = count;
            if (count == 0U) {
                ch.chcr |= chcr_te;
            }
            return wait_cycles;
        };

        std::uint64_t wait_cycles = 0U;
        std::uint32_t burst_units = 0U;
        while (true) {
            const int selected = select_channel();
            if (selected < 0) {
                break;
            }

            const auto chan = static_cast<std::size_t>(selected);
            const bool burst = (dma_[chan].chcr & chcr_tb) != 0U;
            const auto unit_wait = transfer_one(chan);
            const auto room = std::numeric_limits<std::uint64_t>::max() - wait_cycles;
            wait_cycles = unit_wait > room ? std::numeric_limits<std::uint64_t>::max()
                                           : wait_cycles + unit_wait;

            if ((dmaor_ & dmaor_pr) != 0U) {
                dmac_rr_top_ = static_cast<std::uint8_t>(chan ^ 1U);
            }
            if (!burst) {
                break;
            }
            if (++burst_units >= dmac_burst_units_per_tick) {
                break; // resume the remaining block on the next tick
            }
        }
        return wait_cycles;
    }

    sh2_peripherals::onchip_irq sh2_peripherals::pending_onchip_irq() const noexcept {
        onchip_irq best{};
        const auto consider = [&best](int level, std::uint8_t vector) noexcept {
            if (level > best.level && vector != 0U) {
                best = {level, vector};
            }
        };

        const int divu_level = static_cast<int>((ipra_ >> 12U) & 0x0FU);
        if ((dvcr_ & 0x03U) == 0x03U) { // OVF and OVFIE
            consider(divu_level, static_cast<std::uint8_t>(vcrdiv_ & vcrdma_valid));
        }

        const int dmac_level = static_cast<int>((ipra_ >> 8U) & 0x0FU);
        for (std::size_t chan = 0; chan < dma_.size(); ++chan) {
            const dma_channel& ch = dma_[chan];
            if ((ch.chcr & (chcr_te | chcr_ie)) == (chcr_te | chcr_ie)) {
                consider(dmac_level, static_cast<std::uint8_t>(vcrdma_[chan] & vcrdma_valid));
            }
        }

        const int wdt_level = static_cast<int>((ipra_ >> 4U) & 0x0FU);
        if ((wtcsr_ & wtcsr_ovf) != 0U && (wtcsr_ & wtcsr_wtit) == 0U) {
            consider(wdt_level, static_cast<std::uint8_t>(vcrwdt_ >> 8U));
        }

        const int sci_level = static_cast<int>((iprb_ >> 12U) & 0x0FU);
        const std::uint8_t scr = sci_scr_;
        const std::uint8_t ssr = sci_ssr_;
        if ((scr & sci_scr_rie) != 0U && (ssr & sci_ssr_errors) != 0U) {
            consider(sci_level, static_cast<std::uint8_t>(vcra_ >> 8U)); // ERI
        }
        if ((scr & sci_scr_rie) != 0U && (ssr & sci_ssr_rdrf) != 0U) {
            consider(sci_level, static_cast<std::uint8_t>(vcra_)); // RXI
        }
        if ((scr & sci_scr_tie) != 0U && (ssr & sci_ssr_tdre) != 0U) {
            consider(sci_level, static_cast<std::uint8_t>(vcrb_ >> 8U)); // TXI
        }
        if ((scr & sci_scr_teie) != 0U && (ssr & sci_ssr_tend) != 0U) {
            consider(sci_level, static_cast<std::uint8_t>(vcrb_)); // TEI
        }

        const int frt_level = static_cast<int>((iprb_ >> 8U) & 0x0FU); // IPRB[11:8]
        // Output compare (OCFA/OCFB share the OCI vector in VCRC's low byte)
        // outranks overflow (OVI vector in VCRD's high byte).
        const bool oci = ((ftcsr_ & ftcsr_ocfa) != 0U && (tier_ & tier_ociae) != 0U) ||
                         ((ftcsr_ & ftcsr_ocfb) != 0U && (tier_ & tier_ocibe) != 0U);
        if (oci) {
            consider(frt_level, static_cast<std::uint8_t>(vcrc_));
        }
        if ((ftcsr_ & ftcsr_ovf) != 0U && (tier_ & tier_ovie) != 0U) {
            consider(frt_level, static_cast<std::uint8_t>(vcrd_ >> 8U));
        }

        return best;
    }

    void sh2_peripherals::reset() noexcept {
        regs_.fill(0U);
        sci_smr_ = 0U;
        sci_brr_ = 0xFFU;
        sci_scr_ = 0U;
        sci_tdr_ = 0xFFU;
        sci_ssr_ = sci_ssr_reset;
        sci_rdr_ = 0U;
        sci_tx_cycles_ = 0;
        frc_ = 0U;
        ocra_ = 0xFFFFU;
        ocrb_ = 0xFFFFU;
        tier_ = 0x01U;
        ftcsr_ = 0U;
        tcr_ = 0U;
        tocr_ = 0xE0U;
        frt_prescale_acc_ = 0;
        frt_temp_ = 0U;
        ftcsr_read_flags_ = 0U;
        ipra_ = 0U;
        iprb_ = 0U;
        vcra_ = 0U;
        vcrb_ = 0U;
        vcrc_ = 0U;
        vcrd_ = 0U;
        vcrwdt_ = 0U;
        wtcsr_ = 0x18U;
        wtcnt_ = 0U;
        rstcsr_ = 0x1FU;
        wdt_prescale_acc_ = 0;
        wdt_key_ = 0U;
        wtcsr_ovf_read_ = false;
        watchdog_reset_pending_ = false;
        watchdog_reset_kind_ = reset_kind::power_on;
        dma_ = {};
        vcrdma_ = {};
        dmaor_ = 0U;
        dreq_active_.fill(false);
        dreq_edge_pending_.fill(false);
        dmac_rr_top_ = 1U;
        dvsr_ = 0U;
        dvdnt_ = 0U;
        dvcr_ = 0U;
        vcrdiv_ = 0U;
        dvdnth_ = 0U;
        dvdntl_ = 0U;
        dvdntuh_ = 0U;
        dvdntul_ = 0U;
        divu_cycles_remaining_ = 0;
        divu_pending_overflow_ = false;
        divu_pending_quotient_ = 0U;
        divu_pending_remainder_ = 0U;
        // bus_ is the attached handle, not reset state -- leave it intact.
    }

    sh2_peripherals::watchdog_reset_request sh2_peripherals::consume_watchdog_reset() noexcept {
        const watchdog_reset_request request{
            .asserted = watchdog_reset_pending_,
            .kind = watchdog_reset_kind_,
        };
        watchdog_reset_pending_ = false;
        watchdog_reset_kind_ = reset_kind::power_on;
        return request;
    }

    void sh2_peripherals::reset_preserving_watchdog_status() noexcept {
        const std::uint8_t preserved_rstcsr =
            static_cast<std::uint8_t>(rstcsr_ & (rstcsr_wovf | rstcsr_writable));
        reset();
        rstcsr_ = static_cast<std::uint8_t>(preserved_rstcsr | rstcsr_reserved);
    }

    void sh2_peripherals::save_state(state_writer& writer) const {
        writer.u32(sh2_peripherals_state_magic);
        writer.u16(sh2_peripherals_state_version);
        writer.u16(0U); // padding: must-be-zero in v4 (the version number, not
                        // this field, is how a later layout extends -- ADR-0021).
        writer.bytes(regs_);

        writer.u8(sci_smr_);
        writer.u8(sci_brr_);
        writer.u8(sci_scr_);
        writer.u8(sci_tdr_);
        writer.u8(sci_ssr_);
        writer.u8(sci_rdr_);
        writer.u32(static_cast<std::uint32_t>(sci_tx_cycles_));

        writer.u16(frc_);
        writer.u16(ocra_);
        writer.u16(ocrb_);
        writer.u8(tier_);
        writer.u8(ftcsr_);
        writer.u8(tcr_);
        writer.u8(tocr_);
        writer.u32(static_cast<std::uint32_t>(frt_prescale_acc_));
        writer.u8(frt_temp_);
        writer.u8(ftcsr_read_flags_);

        writer.u16(ipra_);
        writer.u16(iprb_);
        writer.u16(vcra_);
        writer.u16(vcrb_);
        writer.u16(vcrc_);
        writer.u16(vcrd_);
        writer.u16(vcrwdt_);
        for (const std::uint32_t vector : vcrdma_) {
            writer.u32(vector);
        }

        writer.u8(wtcsr_);
        writer.u8(wtcnt_);
        writer.u8(rstcsr_);
        writer.u32(static_cast<std::uint32_t>(wdt_prescale_acc_));
        writer.u8(wdt_key_);
        writer.u8(wtcsr_ovf_read_ ? 1U : 0U);
        writer.u8(watchdog_reset_pending_ ? 1U : 0U);
        writer.u8(static_cast<std::uint8_t>(watchdog_reset_kind_));

        for (const dma_channel& ch : dma_) {
            writer.u32(ch.sar);
            writer.u32(ch.dar);
            writer.u32(ch.tcr);
            writer.u32(ch.chcr);
        }
        writer.u32(dmaor_);
        for (const bool active : dreq_active_) {
            writer.u8(active ? 1U : 0U);
        }
        for (const bool pending : dreq_edge_pending_) {
            writer.u8(pending ? 1U : 0U);
        }
        writer.u8(dmac_rr_top_);

        writer.u32(dvsr_);
        writer.u32(dvdnt_);
        writer.u32(dvcr_);
        writer.u16(vcrdiv_);
        writer.u32(dvdnth_);
        writer.u32(dvdntl_);
        writer.u32(dvdntuh_);
        writer.u32(dvdntul_);
        writer.u32(static_cast<std::uint32_t>(divu_cycles_remaining_));
        writer.u8(divu_pending_overflow_ ? 1U : 0U);
        writer.u32(divu_pending_quotient_);
        writer.u32(divu_pending_remainder_);
    }

    void sh2_peripherals::load_state(state_reader& reader) {
        const std::uint32_t magic = reader.u32();
        const std::uint16_t version = reader.u16();
        const std::uint16_t reserved = reader.u16();
        if (magic != sh2_peripherals_state_magic || version != sh2_peripherals_state_version ||
            reserved != 0U) {
            reader.fail();
            return;
        }

        reader.bytes(regs_);

        sci_smr_ = reader.u8();
        sci_brr_ = reader.u8();
        sci_scr_ = reader.u8();
        sci_tdr_ = reader.u8();
        sci_ssr_ = reader.u8();
        sci_rdr_ = reader.u8();
        sci_tx_cycles_ = bounded_state_int(reader.u32());

        frc_ = reader.u16();
        ocra_ = reader.u16();
        ocrb_ = reader.u16();
        tier_ = reader.u8();
        ftcsr_ = reader.u8();
        tcr_ = reader.u8();
        tocr_ = reader.u8();
        frt_prescale_acc_ = bounded_state_int(reader.u32());
        frt_temp_ = reader.u8();
        ftcsr_read_flags_ = reader.u8();

        ipra_ = reader.u16();
        iprb_ = reader.u16();
        vcra_ = reader.u16();
        vcrb_ = reader.u16();
        vcrc_ = reader.u16();
        vcrd_ = reader.u16();
        vcrwdt_ = reader.u16();
        for (std::uint32_t& vector : vcrdma_) {
            vector = reader.u32();
        }

        wtcsr_ = reader.u8();
        wtcnt_ = reader.u8();
        rstcsr_ = reader.u8();
        wdt_prescale_acc_ = bounded_state_int(reader.u32());
        wdt_key_ = reader.u8();
        wtcsr_ovf_read_ = reader.u8() != 0U;
        watchdog_reset_pending_ = reader.u8() != 0U;
        switch (static_cast<reset_kind>(reader.u8())) {
        case reset_kind::power_on:
            watchdog_reset_kind_ = reset_kind::power_on;
            break;
        case reset_kind::hard:
            watchdog_reset_kind_ = reset_kind::hard;
            break;
        case reset_kind::soft:
            watchdog_reset_kind_ = reset_kind::soft;
            break;
        default:
            reader.fail();
            return;
        }

        for (dma_channel& ch : dma_) {
            ch.sar = reader.u32();
            ch.dar = reader.u32();
            ch.tcr = reader.u32();
            ch.chcr = reader.u32();
        }
        dmaor_ = reader.u32();
        for (bool& active : dreq_active_) {
            active = reader.u8() != 0U;
        }
        for (bool& pending : dreq_edge_pending_) {
            pending = reader.u8() != 0U;
        }
        dmac_rr_top_ = static_cast<std::uint8_t>(reader.u8() & 1U);

        dvsr_ = reader.u32();
        dvdnt_ = reader.u32();
        dvcr_ = reader.u32();
        vcrdiv_ = reader.u16();
        dvdnth_ = reader.u32();
        dvdntl_ = reader.u32();
        dvdntuh_ = reader.u32();
        dvdntul_ = reader.u32();
        divu_cycles_remaining_ = bounded_state_int(reader.u32());
        divu_pending_overflow_ = reader.u8() != 0U;
        divu_pending_quotient_ = reader.u32();
        divu_pending_remainder_ = reader.u32();
    }

} // namespace mnemos::chips::cpu
