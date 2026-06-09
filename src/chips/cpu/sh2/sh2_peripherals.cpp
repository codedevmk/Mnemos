#include "sh2_peripherals.hpp"

#include "state.hpp"

namespace mnemos::chips::cpu {

    namespace {
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
        default:
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
        default:
            regs_[off] = value;
            return;
        }
    }

    void sh2_peripherals::tick(std::uint64_t cycles) noexcept {
        const int prescale = frt_prescale_from_tcr(tcr_);
        if (prescale <= 0) {
            return; // FRT stopped (external clock source not modelled)
        }
        frt_prescale_acc_ += static_cast<int>(cycles);
        while (frt_prescale_acc_ >= prescale) {
            frt_prescale_acc_ -= prescale;
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
    }

} // namespace mnemos::chips::cpu
