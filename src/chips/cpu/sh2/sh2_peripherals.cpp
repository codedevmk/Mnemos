#include "sh2_peripherals.hpp"

#include "ibus.hpp"
#include "state.hpp"

namespace mnemos::chips::cpu {

    namespace {
        // DMAC bits + byte-access helpers for its 32-bit registers.
        constexpr std::uint32_t chcr_de = 0x01U;              // channel enable
        constexpr std::uint32_t chcr_te = 0x02U;              // transfer end
        constexpr std::uint32_t chcr_valid = 0xFFFFU;         // CHCR writable bits
        constexpr std::uint32_t dmac_tcr_valid = 0x00FFFFFFU; // 24-bit transfer count
        constexpr std::uint32_t dmaor_dme = 0x01U;            // DMA master enable
        constexpr std::uint32_t dmaor_nmif = 0x02U;           // NMI flag (blocks DMA)
        constexpr std::uint32_t dmaor_ae = 0x04U;             // address-error flag (blocks DMA)
        constexpr std::uint32_t dmaor_valid = 0x0FU;

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
        constexpr std::uint16_t iprb_valid = 0xFF00U; // SCI[15:12], FRT[11:8]
        constexpr std::uint16_t vcr_valid = 0x7F7FU;  // two 7-bit vectors
        constexpr std::uint16_t vcrd_valid = 0x7F00U; // OVI vector in the high byte

        // WDT ($FE80-$FE83) bits + keyed-write keys.
        constexpr std::uint8_t wtcsr_ovf = 0x80U;       // interval-timer overflow flag
        constexpr std::uint8_t wtcsr_wtit = 0x40U;      // watchdog (1) vs interval (0) mode
        constexpr std::uint8_t wtcsr_tme = 0x20U;       // timer enable
        constexpr std::uint8_t wtcsr_reserved = 0x18U;  // reserved bits read 1
        constexpr std::uint8_t wtcsr_cks = 0x07U;       // clock select
        constexpr std::uint8_t rstcsr_wovf = 0x80U;     // watchdog overflow flag
        constexpr std::uint8_t rstcsr_writable = 0x60U; // RSTE | RSTS
        constexpr std::uint8_t rstcsr_reserved = 0x1FU;
        constexpr std::uint8_t wdt_key_wtcsr = 0xA5U; // high-byte key: write WTCSR / RSTCSR
        constexpr std::uint8_t wdt_key_wtcnt = 0x5AU; // high-byte key: write WTCNT / clear WOVF

        [[nodiscard]] int wdt_prescale_from_wtcsr(std::uint8_t wtcsr) noexcept {
            constexpr std::array<int, 8> prescales{{2, 64, 128, 256, 512, 1024, 4096, 8192}};
            return prescales[wtcsr & wtcsr_cks];
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
        default:
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
            if (off >= 0x1B0U && off < 0x1B4U) { // DMAOR
                return reg_byte(dmaor_, off);
            }
            return regs_[off];
        }
    }

    void sh2_peripherals::write8(std::uint32_t addr, std::uint8_t value) noexcept {
        const std::uint32_t off = addr & (window_size - 1U);
        switch (off) {
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
                rstcsr_ = static_cast<std::uint8_t>((value & rstcsr_writable) | rstcsr_reserved |
                                                    (rstcsr_ & rstcsr_wovf));
            } else if (wdt_key_ == wdt_key_wtcnt) {
                rstcsr_ &= static_cast<std::uint8_t>(~rstcsr_wovf); // 0x5A clears WOVF
            }
            return;
        default:
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
            if (off >= 0x1B0U && off < 0x1B4U) { // DMAOR
                dmaor_ = set_reg_byte(dmaor_, off, value) & dmaor_valid;
                return;
            }
            regs_[off] = value;
            return;
        }
    }

    void sh2_peripherals::tick(std::uint64_t cycles) noexcept {
        // FRT: a free-running counter unless TCR selects the (unmodelled) external
        // clock.
        const int frt_prescale = frt_prescale_from_tcr(tcr_);
        if (frt_prescale > 0) {
            frt_prescale_acc_ += static_cast<int>(cycles);
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

        // WDT: an 8-bit counter that advances while TME is set; overflow flags
        // the interval timer (or, in watchdog mode, the reset latch).
        if ((wtcsr_ & wtcsr_tme) != 0U) {
            const int wdt_prescale = wdt_prescale_from_wtcsr(wtcsr_);
            wdt_prescale_acc_ += static_cast<int>(cycles);
            while (wdt_prescale_acc_ >= wdt_prescale) {
                wdt_prescale_acc_ -= wdt_prescale;
                const std::uint8_t prev = wtcnt_;
                wtcnt_ = static_cast<std::uint8_t>(wtcnt_ + 1U);
                if (wtcnt_ < prev) { // overflow $FF -> $00
                    if ((wtcsr_ & wtcsr_wtit) == 0U) {
                        wtcsr_ |= wtcsr_ovf; // interval-timer flag
                    } else {
                        rstcsr_ |= rstcsr_wovf; // watchdog flag (reset deferred)
                    }
                }
            }
        }

        run_dmac();
    }

    void sh2_peripherals::run_dmac() noexcept {
        if (bus_ == nullptr) {
            return;
        }
        // Master enable, and no NMI/address-error halting the controller.
        if ((dmaor_ & dmaor_dme) == 0U || (dmaor_ & (dmaor_nmif | dmaor_ae)) != 0U) {
            return;
        }
        constexpr std::array<std::uint32_t, 4> unit_bytes{{1U, 2U, 4U, 16U}};
        for (dma_channel& ch : dma_) {
            if ((ch.chcr & chcr_de) == 0U || (ch.chcr & chcr_te) != 0U) {
                continue;
            }
            std::uint32_t count = ch.tcr & dmac_tcr_valid;
            if (count == 0U) {
                continue;
            }
            const std::uint32_t ts = (ch.chcr >> 10U) & 3U;
            const std::uint32_t sm = (ch.chcr >> 12U) & 3U;
            const std::uint32_t dm = (ch.chcr >> 14U) & 3U;
            const std::uint32_t bpu = unit_bytes[ts];
            const std::uint32_t count_step = (ts == 3U) ? 4U : 1U;
            // Auto-request: move the whole block now (cycle-exact metering is
            // deferred). Each unit copies `bpu` bytes; SAR/DAR step per CHCR.
            while (count > 0U) {
                for (std::uint32_t i = 0; i < bpu; ++i) {
                    bus_->write8(ch.dar + i, bus_->read8(ch.sar + i));
                }
                if (sm == 1U) {
                    ch.sar += bpu;
                } else if (sm == 2U) {
                    ch.sar -= bpu;
                }
                if (dm == 1U) {
                    ch.dar += bpu;
                } else if (dm == 2U) {
                    ch.dar -= bpu;
                }
                count = (count > count_step) ? (count - count_step) : 0U;
            }
            ch.tcr = 0U;
            ch.chcr |= chcr_te; // transfer end (the interrupt is deferred)
        }
    }

    sh2_peripherals::onchip_irq sh2_peripherals::pending_onchip_irq() const noexcept {
        const int frt_level = static_cast<int>((iprb_ >> 8U) & 0x0FU); // IPRB[11:8]
        if (frt_level == 0) {
            return {};
        }
        // Output compare (OCFA/OCFB share the OCI vector in VCRC's low byte)
        // outranks overflow (OVI vector in VCRD's high byte).
        const bool oci = ((ftcsr_ & ftcsr_ocfa) != 0U && (tier_ & tier_ociae) != 0U) ||
                         ((ftcsr_ & ftcsr_ocfb) != 0U && (tier_ & tier_ocibe) != 0U);
        if (oci) {
            const auto vector = static_cast<std::uint8_t>(vcrc_ & 0xFFU);
            if (vector != 0U) {
                return {frt_level, vector};
            }
        }
        if ((ftcsr_ & ftcsr_ovf) != 0U && (tier_ & tier_ovie) != 0U) {
            const auto vector = static_cast<std::uint8_t>(vcrd_ >> 8U);
            if (vector != 0U) {
                return {frt_level, vector};
            }
        }
        return {};
    }

    void sh2_peripherals::reset() noexcept {
        regs_.fill(0U);
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
        iprb_ = 0U;
        vcrc_ = 0U;
        vcrd_ = 0U;
        wtcsr_ = 0x18U;
        wtcnt_ = 0U;
        rstcsr_ = 0x1FU;
        wdt_prescale_acc_ = 0;
        wdt_key_ = 0U;
        wtcsr_ovf_read_ = false;
        dma_ = {};
        dmaor_ = 0U;
        // bus_ is the attached handle, not reset state -- leave it intact.
    }

    void sh2_peripherals::save_state(state_writer& writer) const {
        writer.bytes(regs_);
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
        writer.u16(iprb_);
        writer.u16(vcrc_);
        writer.u16(vcrd_);
        writer.u8(wtcsr_);
        writer.u8(wtcnt_);
        writer.u8(rstcsr_);
        writer.u32(static_cast<std::uint32_t>(wdt_prescale_acc_));
        writer.u8(wdt_key_);
        writer.u8(wtcsr_ovf_read_ ? 1U : 0U);
        for (const dma_channel& ch : dma_) {
            writer.u32(ch.sar);
            writer.u32(ch.dar);
            writer.u32(ch.tcr);
            writer.u32(ch.chcr);
        }
        writer.u32(dmaor_);
    }

    void sh2_peripherals::load_state(state_reader& reader) {
        reader.bytes(regs_);
        frc_ = reader.u16();
        ocra_ = reader.u16();
        ocrb_ = reader.u16();
        tier_ = reader.u8();
        ftcsr_ = reader.u8();
        tcr_ = reader.u8();
        tocr_ = reader.u8();
        frt_prescale_acc_ = static_cast<int>(reader.u32());
        frt_temp_ = reader.u8();
        ftcsr_read_flags_ = reader.u8();
        iprb_ = reader.u16();
        vcrc_ = reader.u16();
        vcrd_ = reader.u16();
        wtcsr_ = reader.u8();
        wtcnt_ = reader.u8();
        rstcsr_ = reader.u8();
        wdt_prescale_acc_ = static_cast<int>(reader.u32());
        wdt_key_ = reader.u8();
        wtcsr_ovf_read_ = reader.u8() != 0U;
        for (dma_channel& ch : dma_) {
            ch.sar = reader.u32();
            ch.dar = reader.u32();
            ch.tcr = reader.u32();
            ch.chcr = reader.u32();
        }
        dmaor_ = reader.u32();
    }

} // namespace mnemos::chips::cpu
