#include "wdc_65c816.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>

// Ported from the Emu reference (chips/wdc_65c816); clean-room per the WDC
// 65C816 datasheet. Cycle accounting mirrors the reference exactly: each bus
// access costs one cycle, plus per-op internal cycles where the reference adds
// them. The result is integer-identical to the C core.

namespace mnemos::chips::cpu {

    chip_metadata wdc_65c816::metadata() const noexcept {
        return {
            .manufacturer = "WDC",
            .part_number = "65C816",
            .family = "65xx",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- memory access ----

    std::uint8_t wdc_65c816::bus_read(std::uint32_t addr24) noexcept {
        ++step_cycles_;
        return bus_ != nullptr ? bus_->read8(addr24 & 0x00FFFFFFU) : 0xFFU;
    }
    void wdc_65c816::bus_write(std::uint32_t addr24, std::uint8_t value) noexcept {
        ++step_cycles_;
        if (bus_ != nullptr) {
            bus_->write8(addr24 & 0x00FFFFFFU, value);
        }
    }
    std::uint8_t wdc_65c816::fetch_pc_byte() noexcept {
        const std::uint8_t v = bus_read((static_cast<std::uint32_t>(pbr_) << 16U) | pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return v;
    }
    std::uint16_t wdc_65c816::fetch_pc_word() noexcept {
        const std::uint16_t lo = fetch_pc_byte();
        const std::uint16_t hi = fetch_pc_byte();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    // ---- stack ----

    void wdc_65c816::stack_push8(std::uint8_t v) noexcept {
        bus_write(s_, v);
        s_ = static_cast<std::uint16_t>(s_ - 1U);
        if (e_) {
            s_ = static_cast<std::uint16_t>(0x0100U | (s_ & 0x00FFU));
        }
    }
    std::uint8_t wdc_65c816::stack_pop8() noexcept {
        s_ = static_cast<std::uint16_t>(s_ + 1U);
        if (e_) {
            s_ = static_cast<std::uint16_t>(0x0100U | (s_ & 0x00FFU));
        }
        return bus_read(s_);
    }

    // ---- mode enforcement ----

    void wdc_65c816::enforce_emulation_invariants() noexcept {
        if (e_) {
            // Emulation mode: M and X set; S forced to page-1 layout (high byte
            // fixed at $01); X and Y are 8-bit.
            p_ |= static_cast<std::uint8_t>(flag_m | flag_x);
            s_ = static_cast<std::uint16_t>(0x0100U | (s_ & 0x00FFU));
            x_ &= 0x00FFU;
            y_ &= 0x00FFU;
        } else if ((p_ & flag_x) != 0U) {
            // Native mode with X=1: high bytes of X/Y are masked to 0.
            x_ &= 0x00FFU;
            y_ &= 0x00FFU;
        }
    }

    // ---- flag helpers ----

    void wdc_65c816::set_nz8(std::uint8_t v) noexcept {
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_n | flag_z)) | ((v == 0U) ? flag_z : 0U) |
                                       ((v & 0x80U) != 0U ? flag_n : 0U));
    }
    void wdc_65c816::set_nz16(std::uint16_t v) noexcept {
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_n | flag_z)) | ((v == 0U) ? flag_z : 0U) |
                                       ((v & 0x8000U) != 0U ? flag_n : 0U));
    }

    // ---- addressing ----

    std::uint32_t wdc_65c816::addr_abs_data() noexcept {
        const std::uint16_t addr16 = fetch_pc_word();
        return (static_cast<std::uint32_t>(dbr_) << 16U) | addr16;
    }
    std::uint32_t wdc_65c816::addr_dp() noexcept {
        const std::uint8_t off = fetch_pc_byte();
        return static_cast<std::uint16_t>(d_ + off); // bank 0
    }
    std::uint32_t wdc_65c816::addr_abs_x() noexcept {
        const std::uint32_t base = addr_abs_data();
        return base + (xy_is_8bit() ? (x_ & 0x00FFU) : x_);
    }
    std::uint32_t wdc_65c816::addr_abs_y() noexcept {
        const std::uint32_t base = addr_abs_data();
        return base + (xy_is_8bit() ? (y_ & 0x00FFU) : y_);
    }
    std::uint32_t wdc_65c816::addr_dp_x() noexcept {
        const std::uint8_t off = fetch_pc_byte();
        const std::uint16_t x = xy_is_8bit() ? static_cast<std::uint16_t>(x_ & 0x00FFU) : x_;
        return static_cast<std::uint16_t>(d_ + off + x);
    }
    std::uint32_t wdc_65c816::addr_dp_indirect() noexcept {
        const std::uint8_t off = fetch_pc_byte();
        const auto ptr = static_cast<std::uint16_t>(d_ + off);
        const std::uint16_t lo = bus_read(ptr);
        const std::uint16_t hi = bus_read(static_cast<std::uint16_t>(ptr + 1U));
        const auto addr16 = static_cast<std::uint16_t>(lo | (hi << 8U));
        return (static_cast<std::uint32_t>(dbr_) << 16U) | addr16;
    }
    std::uint32_t wdc_65c816::addr_dp_indirect_y() noexcept {
        const std::uint32_t base = addr_dp_indirect();
        return base + (xy_is_8bit() ? (y_ & 0x00FFU) : y_);
    }

    // ---- A load/store ----

    void wdc_65c816::load_a_from(std::uint32_t addr) noexcept {
        if (a_is_8bit()) {
            const std::uint8_t v = bus_read(addr);
            a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
            set_nz8(v);
        } else {
            const std::uint16_t lo = bus_read(addr);
            const std::uint16_t hi = bus_read(addr + 1U);
            const auto v = static_cast<std::uint16_t>(lo | (hi << 8U));
            a_ = v;
            set_nz16(v);
        }
    }
    void wdc_65c816::store_a_to(std::uint32_t addr) noexcept {
        if (a_is_8bit()) {
            bus_write(addr, static_cast<std::uint8_t>(a_ & 0xFFU));
        } else {
            bus_write(addr, static_cast<std::uint8_t>(a_ & 0xFFU));
            bus_write(addr + 1U, static_cast<std::uint8_t>((a_ >> 8U) & 0xFFU));
        }
    }

    // ---- ALU ----

    void wdc_65c816::adc_decimal8(std::uint8_t operand) noexcept {
        const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
        const unsigned carry = (p_ & flag_c) != 0U ? 1U : 0U;
        unsigned lo = (a & 0x0FU) + (operand & 0x0FU) + carry;
        if (lo > 9U) {
            lo += 6U;
        }
        unsigned hi = (a >> 4U) + (operand >> 4U) + (lo > 0x0FU ? 1U : 0U);
        if (hi > 9U) {
            hi += 6U;
        }
        const auto result = static_cast<std::uint8_t>(((hi & 0x0FU) << 4U) | (lo & 0x0FU));
        const bool overflow = ((a ^ result) & (operand ^ result) & 0x80U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((hi > 0x0FU) ? flag_c : 0U) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x80U) != 0U ? flag_n : 0U));
        a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
    }

    void wdc_65c816::adc_decimal16(std::uint16_t operand) noexcept {
        const std::uint16_t a = a_;
        const unsigned carry = (p_ & flag_c) != 0U ? 1U : 0U;
        unsigned d0 = (a & 0x000FU) + (operand & 0x000FU) + carry;
        if (d0 > 9U) {
            d0 += 6U;
        }
        unsigned d1 = ((a >> 4U) & 0x0FU) + ((operand >> 4U) & 0x0FU) + (d0 > 0x0FU ? 1U : 0U);
        if (d1 > 9U) {
            d1 += 6U;
        }
        unsigned d2 = ((a >> 8U) & 0x0FU) + ((operand >> 8U) & 0x0FU) + (d1 > 0x0FU ? 1U : 0U);
        if (d2 > 9U) {
            d2 += 6U;
        }
        unsigned d3 = ((a >> 12U) & 0x0FU) + ((operand >> 12U) & 0x0FU) + (d2 > 0x0FU ? 1U : 0U);
        if (d3 > 9U) {
            d3 += 6U;
        }
        const auto result = static_cast<std::uint16_t>(
            ((d3 & 0x0FU) << 12U) | ((d2 & 0x0FU) << 8U) | ((d1 & 0x0FU) << 4U) | (d0 & 0x0FU));
        const bool overflow = ((a ^ result) & (operand ^ result) & 0x8000U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((d3 > 0x0FU) ? flag_c : 0U) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x8000U) != 0U ? flag_n : 0U));
        a_ = result;
    }

    void wdc_65c816::sbc_decimal8(std::uint8_t operand) noexcept {
        const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
        const int borrow = (p_ & flag_c) != 0U ? 0 : 1;
        int lo = static_cast<int>(a & 0x0FU) - static_cast<int>(operand & 0x0FU) - borrow;
        const int lo_borrow = (lo < 0) ? 1 : 0;
        if (lo_borrow != 0) {
            lo -= 6;
        }
        int hi = static_cast<int>((a >> 4U) & 0x0FU) - static_cast<int>((operand >> 4U) & 0x0FU) -
                 lo_borrow;
        const int hi_borrow = (hi < 0) ? 1 : 0;
        if (hi_borrow != 0) {
            hi -= 6;
        }
        const auto result = static_cast<std::uint8_t>(((hi & 0x0F) << 4) | (lo & 0x0F));
        const bool overflow = ((a ^ operand) & (a ^ result) & 0x80U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((hi_borrow != 0) ? 0U : flag_c) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x80U) != 0U ? flag_n : 0U));
        a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
    }

    void wdc_65c816::sbc_decimal16(std::uint16_t operand) noexcept {
        const std::uint16_t a = a_;
        const int borrow = (p_ & flag_c) != 0U ? 0 : 1;
        int d0 = static_cast<int>(a & 0x000FU) - static_cast<int>(operand & 0x000FU) - borrow;
        const int b0 = (d0 < 0) ? 1 : 0;
        if (b0 != 0) {
            d0 -= 6;
        }
        int d1 =
            static_cast<int>((a >> 4U) & 0x0FU) - static_cast<int>((operand >> 4U) & 0x0FU) - b0;
        const int b1 = (d1 < 0) ? 1 : 0;
        if (b1 != 0) {
            d1 -= 6;
        }
        int d2 =
            static_cast<int>((a >> 8U) & 0x0FU) - static_cast<int>((operand >> 8U) & 0x0FU) - b1;
        const int b2 = (d2 < 0) ? 1 : 0;
        if (b2 != 0) {
            d2 -= 6;
        }
        int d3 =
            static_cast<int>((a >> 12U) & 0x0FU) - static_cast<int>((operand >> 12U) & 0x0FU) - b2;
        const int b3 = (d3 < 0) ? 1 : 0;
        if (b3 != 0) {
            d3 -= 6;
        }
        const auto result = static_cast<std::uint16_t>(((d3 & 0x0F) << 12) | ((d2 & 0x0F) << 8) |
                                                       ((d1 & 0x0F) << 4) | (d0 & 0x0F));
        const bool overflow = ((a ^ operand) & (a ^ result) & 0x8000U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((b3 != 0) ? 0U : flag_c) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x8000U) != 0U ? flag_n : 0U));
        a_ = result;
    }

    void wdc_65c816::adc_body8(std::uint8_t operand) noexcept {
        if ((p_ & flag_d) != 0U) {
            adc_decimal8(operand);
            return;
        }
        const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
        const unsigned carry = (p_ & flag_c) != 0U ? 1U : 0U;
        const unsigned sum = static_cast<unsigned>(a) + operand + carry;
        const auto result = static_cast<std::uint8_t>(sum & 0xFFU);
        const bool overflow = ((a ^ result) & (operand ^ result) & 0x80U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((sum > 0xFFU) ? flag_c : 0U) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x80U) != 0U ? flag_n : 0U));
        a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
    }

    void wdc_65c816::adc_body16(std::uint16_t operand) noexcept {
        if ((p_ & flag_d) != 0U) {
            adc_decimal16(operand);
            return;
        }
        const std::uint16_t a = a_;
        const unsigned carry = (p_ & flag_c) != 0U ? 1U : 0U;
        const unsigned sum = static_cast<unsigned>(a) + operand + carry;
        const auto result = static_cast<std::uint16_t>(sum & 0xFFFFU);
        const bool overflow = ((a ^ result) & (operand ^ result) & 0x8000U) != 0U;
        p_ = static_cast<std::uint8_t>((p_ & ~(flag_c | flag_v | flag_z | flag_n)) |
                                       ((sum > 0xFFFFU) ? flag_c : 0U) | (overflow ? flag_v : 0U) |
                                       ((result == 0U) ? flag_z : 0U) |
                                       ((result & 0x8000U) != 0U ? flag_n : 0U));
        a_ = result;
    }

    void wdc_65c816::sbc_body8(std::uint8_t operand) noexcept {
        if ((p_ & flag_d) != 0U) {
            sbc_decimal8(operand);
            return;
        }
        // Binary SBC is ADC with the operand inverted; C acts as borrow.
        adc_body8(static_cast<std::uint8_t>(~operand));
    }
    void wdc_65c816::sbc_body16(std::uint16_t operand) noexcept {
        if ((p_ & flag_d) != 0U) {
            sbc_decimal16(operand);
            return;
        }
        adc_body16(static_cast<std::uint16_t>(~operand));
    }

    void wdc_65c816::cmp_body8(std::uint8_t operand) noexcept {
        const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
        const unsigned diff = static_cast<unsigned>(a) - operand;
        p_ = static_cast<std::uint8_t>(
            (p_ & ~(flag_c | flag_z | flag_n)) | ((a >= operand) ? flag_c : 0U) |
            ((a == operand) ? flag_z : 0U) | ((diff & 0x80U) != 0U ? flag_n : 0U));
    }
    void wdc_65c816::cmp_body16(std::uint16_t operand) noexcept {
        const std::uint16_t a = a_;
        const unsigned diff = static_cast<unsigned>(a) - operand;
        p_ = static_cast<std::uint8_t>(
            (p_ & ~(flag_c | flag_z | flag_n)) | ((a >= operand) ? flag_c : 0U) |
            ((a == operand) ? flag_z : 0U) | ((diff & 0x8000U) != 0U ? flag_n : 0U));
    }

    // ---- index inc/dec ----

    void wdc_65c816::inc_xy(std::uint16_t& reg) noexcept {
        if (xy_is_8bit()) {
            const auto v = static_cast<std::uint8_t>((reg & 0xFFU) + 1U);
            reg = v;
            set_nz8(v);
        } else {
            const auto v = static_cast<std::uint16_t>(reg + 1U);
            reg = v;
            set_nz16(v);
        }
        ++step_cycles_;
    }
    void wdc_65c816::dec_xy(std::uint16_t& reg) noexcept {
        if (xy_is_8bit()) {
            const auto v = static_cast<std::uint8_t>((reg & 0xFFU) - 1U);
            reg = v;
            set_nz8(v);
        } else {
            const auto v = static_cast<std::uint16_t>(reg - 1U);
            reg = v;
            set_nz16(v);
        }
        ++step_cycles_;
    }

    // ---- branch ----

    void wdc_65c816::branch_if(bool taken) noexcept {
        const auto offset = static_cast<std::int8_t>(fetch_pc_byte());
        if (taken) {
            pc_ = static_cast<std::uint16_t>(pc_ + offset);
            ++step_cycles_;
        }
    }

    // ---- interrupts ----

    void wdc_65c816::push_interrupt_context(std::uint8_t p_to_push) noexcept {
        if (!e_) {
            stack_push8(pbr_);
        }
        stack_push8(static_cast<std::uint8_t>((pc_ >> 8U) & 0xFFU));
        stack_push8(static_cast<std::uint8_t>(pc_ & 0xFFU));
        stack_push8(p_to_push);
    }

    std::uint16_t wdc_65c816::fetch_vector(std::uint16_t native_addr,
                                           std::uint16_t emu_addr) noexcept {
        const std::uint16_t addr = e_ ? emu_addr : native_addr;
        const std::uint16_t lo = bus_read(addr);
        const std::uint16_t hi = bus_read(static_cast<std::uint32_t>(addr) + 1U);
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    void wdc_65c816::service_nmi() noexcept {
        push_interrupt_context(p_);
        p_ |= flag_i;
        if (!e_) {
            p_ &= static_cast<std::uint8_t>(~flag_d);
        }
        pbr_ = 0U;
        pc_ = fetch_vector(0xFFEAU, 0xFFFAU);
        nmi_pending_ = false;
        step_cycles_ += 7;
    }

    void wdc_65c816::service_irq() noexcept {
        push_interrupt_context(p_);
        p_ |= flag_i;
        if (!e_) {
            p_ &= static_cast<std::uint8_t>(~flag_d);
        }
        pbr_ = 0U;
        pc_ = fetch_vector(0xFFEEU, 0xFFFEU);
        step_cycles_ += 7;
    }

    // ---- decode / execute ----

    void wdc_65c816::execute(std::uint8_t opcode) {
        switch (opcode) {
        case 0xEA: // NOP
            ++step_cycles_;
            break;
        case 0xFB: { // XCE: swap carry and emulation
            const bool c = (p_ & flag_c) != 0U;
            const bool e = e_;
            if (e) {
                p_ |= flag_c;
            } else {
                p_ &= static_cast<std::uint8_t>(~flag_c);
            }
            e_ = c;
            enforce_emulation_invariants();
            ++step_cycles_;
            break;
        }
        case 0xC2: { // REP #imm
            const std::uint8_t mask = fetch_pc_byte();
            p_ &= static_cast<std::uint8_t>(~mask);
            enforce_emulation_invariants();
            ++step_cycles_;
            break;
        }
        case 0xE2: { // SEP #imm
            const std::uint8_t mask = fetch_pc_byte();
            p_ |= mask;
            enforce_emulation_invariants();
            ++step_cycles_;
            break;
        }
        case 0x18: // CLC
            p_ &= static_cast<std::uint8_t>(~flag_c);
            ++step_cycles_;
            break;
        case 0x38: // SEC
            p_ |= flag_c;
            ++step_cycles_;
            break;
        case 0x58: // CLI
            p_ &= static_cast<std::uint8_t>(~flag_i);
            ++step_cycles_;
            break;
        case 0x78: // SEI
            p_ |= flag_i;
            ++step_cycles_;
            break;
        case 0xD8: // CLD
            p_ &= static_cast<std::uint8_t>(~flag_d);
            ++step_cycles_;
            break;
        case 0xF8: // SED
            p_ |= flag_d;
            ++step_cycles_;
            break;

        case 0xA9: // LDA #
            if (a_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const std::uint16_t v = fetch_pc_word();
                a_ = v;
                set_nz16(v);
            }
            break;
        case 0xAD: // LDA abs
            load_a_from(addr_abs_data());
            break;
        case 0xA5: // LDA dp
            load_a_from(addr_dp());
            break;
        case 0x8D: // STA abs
            store_a_to(addr_abs_data());
            break;
        case 0x85: // STA dp
            store_a_to(addr_dp());
            break;
        case 0x4C: // JMP abs
            pc_ = fetch_pc_word();
            break;

        case 0xA2: // LDX #
            if (xy_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                x_ = v;
                set_nz8(v);
            } else {
                const std::uint16_t v = fetch_pc_word();
                x_ = v;
                set_nz16(v);
            }
            break;
        case 0xA0: // LDY #
            if (xy_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                y_ = v;
                set_nz8(v);
            } else {
                const std::uint16_t v = fetch_pc_word();
                y_ = v;
                set_nz16(v);
            }
            break;
        case 0x8E: { // STX abs
            const std::uint32_t a = addr_abs_data();
            if (xy_is_8bit()) {
                bus_write(a, static_cast<std::uint8_t>(x_ & 0xFFU));
            } else {
                bus_write(a, static_cast<std::uint8_t>(x_ & 0xFFU));
                bus_write(a + 1U, static_cast<std::uint8_t>((x_ >> 8U) & 0xFFU));
            }
            break;
        }
        case 0x8C: { // STY abs
            const std::uint32_t a = addr_abs_data();
            if (xy_is_8bit()) {
                bus_write(a, static_cast<std::uint8_t>(y_ & 0xFFU));
            } else {
                bus_write(a, static_cast<std::uint8_t>(y_ & 0xFFU));
                bus_write(a + 1U, static_cast<std::uint8_t>((y_ >> 8U) & 0xFFU));
            }
            break;
        }

        case 0xAA: // TAX
            if (xy_is_8bit()) {
                const auto v = static_cast<std::uint8_t>(a_ & 0xFFU);
                x_ = v;
                set_nz8(v);
            } else {
                const std::uint16_t v = a_is_8bit() ? static_cast<std::uint16_t>(a_ & 0xFFU) : a_;
                x_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;
        case 0xA8: // TAY
            if (xy_is_8bit()) {
                const auto v = static_cast<std::uint8_t>(a_ & 0xFFU);
                y_ = v;
                set_nz8(v);
            } else {
                const std::uint16_t v = a_is_8bit() ? static_cast<std::uint16_t>(a_ & 0xFFU) : a_;
                y_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;
        case 0x8A: // TXA
            if (a_is_8bit()) {
                const auto v = static_cast<std::uint8_t>(x_ & 0xFFU);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const std::uint16_t v = xy_is_8bit() ? static_cast<std::uint16_t>(x_ & 0xFFU) : x_;
                a_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;
        case 0x98: // TYA
            if (a_is_8bit()) {
                const auto v = static_cast<std::uint8_t>(y_ & 0xFFU);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const std::uint16_t v = xy_is_8bit() ? static_cast<std::uint16_t>(y_ & 0xFFU) : y_;
                a_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;

        case 0x48: // PHA
            if (a_is_8bit()) {
                stack_push8(static_cast<std::uint8_t>(a_ & 0xFFU));
            } else {
                stack_push8(static_cast<std::uint8_t>((a_ >> 8U) & 0xFFU));
                stack_push8(static_cast<std::uint8_t>(a_ & 0xFFU));
            }
            break;
        case 0x68: // PLA
            if (a_is_8bit()) {
                const std::uint8_t v = stack_pop8();
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const std::uint16_t lo = stack_pop8();
                const std::uint16_t hi = stack_pop8();
                const auto v = static_cast<std::uint16_t>(lo | (hi << 8U));
                a_ = v;
                set_nz16(v);
            }
            break;
        case 0x08: // PHP
            stack_push8(p_);
            break;
        case 0x28: // PLP
            p_ = stack_pop8();
            enforce_emulation_invariants();
            break;

        case 0x80: // BRA
            branch_if(true);
            break;
        case 0x10: // BPL
            branch_if((p_ & flag_n) == 0U);
            break;
        case 0x30: // BMI
            branch_if((p_ & flag_n) != 0U);
            break;
        case 0x50: // BVC
            branch_if((p_ & flag_v) == 0U);
            break;
        case 0x70: // BVS
            branch_if((p_ & flag_v) != 0U);
            break;
        case 0x90: // BCC
            branch_if((p_ & flag_c) == 0U);
            break;
        case 0xB0: // BCS
            branch_if((p_ & flag_c) != 0U);
            break;
        case 0xD0: // BNE
            branch_if((p_ & flag_z) == 0U);
            break;
        case 0xF0: // BEQ
            branch_if((p_ & flag_z) != 0U);
            break;

        case 0x69: // ADC #
            if (a_is_8bit()) {
                adc_body8(fetch_pc_byte());
            } else {
                adc_body16(fetch_pc_word());
            }
            break;
        case 0xE9: // SBC #
            if (a_is_8bit()) {
                sbc_body8(fetch_pc_byte());
            } else {
                sbc_body16(fetch_pc_word());
            }
            break;
        case 0xC9: // CMP #
            if (a_is_8bit()) {
                cmp_body8(fetch_pc_byte());
            } else {
                cmp_body16(fetch_pc_word());
            }
            break;
        case 0x1A: // INC A
            if (a_is_8bit()) {
                const auto v = static_cast<std::uint8_t>((a_ & 0xFFU) + 1U);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const auto v = static_cast<std::uint16_t>(a_ + 1U);
                a_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;
        case 0x3A: // DEC A
            if (a_is_8bit()) {
                const auto v = static_cast<std::uint8_t>((a_ & 0xFFU) - 1U);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | v);
                set_nz8(v);
            } else {
                const auto v = static_cast<std::uint16_t>(a_ - 1U);
                a_ = v;
                set_nz16(v);
            }
            ++step_cycles_;
            break;

        case 0x20: { // JSR abs
            const std::uint16_t target = fetch_pc_word();
            const auto ret = static_cast<std::uint16_t>(pc_ - 1U);
            stack_push8(static_cast<std::uint8_t>(ret >> 8U));
            stack_push8(static_cast<std::uint8_t>(ret & 0xFFU));
            pc_ = target;
            break;
        }
        case 0x60: { // RTS
            const std::uint16_t lo = stack_pop8();
            const std::uint16_t hi = stack_pop8();
            pc_ = static_cast<std::uint16_t>((lo | (hi << 8U)) + 1U);
            break;
        }

        case 0xBD: // LDA abs,X
            load_a_from(addr_abs_x());
            break;
        case 0xB9: // LDA abs,Y
            load_a_from(addr_abs_y());
            break;
        case 0xB5: // LDA dp,X
            load_a_from(addr_dp_x());
            break;
        case 0x9D: // STA abs,X
            store_a_to(addr_abs_x());
            break;
        case 0x99: // STA abs,Y
            store_a_to(addr_abs_y());
            break;
        case 0x95: // STA dp,X
            store_a_to(addr_dp_x());
            break;

        case 0xE8: // INX
            inc_xy(x_);
            break;
        case 0xCA: // DEX
            dec_xy(x_);
            break;
        case 0xC8: // INY
            inc_xy(y_);
            break;
        case 0x88: // DEY
            dec_xy(y_);
            break;
        case 0xBA: // TSX
            if (xy_is_8bit()) {
                const auto v = static_cast<std::uint8_t>(s_ & 0xFFU);
                x_ = v;
                set_nz8(v);
            } else {
                x_ = s_;
                set_nz16(s_);
            }
            ++step_cycles_;
            break;
        case 0x9A: // TXS
            if (e_) {
                s_ = static_cast<std::uint16_t>(0x0100U | (x_ & 0xFFU));
            } else {
                s_ = x_;
            }
            ++step_cycles_;
            break;

        case 0x29: // AND #
            if (a_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                const auto result = static_cast<std::uint8_t>((a_ & 0xFFU) & v);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                set_nz8(result);
            } else {
                const std::uint16_t v = fetch_pc_word();
                a_ &= v;
                set_nz16(a_);
            }
            break;
        case 0x09: // ORA #
            if (a_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                const auto result = static_cast<std::uint8_t>((a_ & 0xFFU) | v);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                set_nz8(result);
            } else {
                const std::uint16_t v = fetch_pc_word();
                a_ |= v;
                set_nz16(a_);
            }
            break;
        case 0x49: // EOR #
            if (a_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                const auto result = static_cast<std::uint8_t>((a_ & 0xFFU) ^ v);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                set_nz8(result);
            } else {
                const std::uint16_t v = fetch_pc_word();
                a_ ^= v;
                set_nz16(a_);
            }
            break;

        case 0x0A: // ASL A
            if (a_is_8bit()) {
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                const std::uint8_t carry = (a & 0x80U) != 0U ? 1U : 0U;
                const auto result = static_cast<std::uint8_t>(a << 1U);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz8(result);
            } else {
                const std::uint16_t a = a_;
                const std::uint8_t carry = (a & 0x8000U) != 0U ? 1U : 0U;
                const auto result = static_cast<std::uint16_t>(a << 1U);
                a_ = result;
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz16(result);
            }
            ++step_cycles_;
            break;
        case 0x4A: // LSR A
            if (a_is_8bit()) {
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                const std::uint8_t carry = a & 0x01U;
                const auto result = static_cast<std::uint8_t>(a >> 1U);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz8(result);
            } else {
                const std::uint16_t a = a_;
                const auto carry = static_cast<std::uint8_t>(a & 0x01U);
                const auto result = static_cast<std::uint16_t>(a >> 1U);
                a_ = result;
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz16(result);
            }
            ++step_cycles_;
            break;
        case 0x2A: { // ROL A
            const std::uint8_t cin = (p_ & flag_c) != 0U ? 1U : 0U;
            if (a_is_8bit()) {
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                const std::uint8_t carry = (a & 0x80U) != 0U ? 1U : 0U;
                const auto result = static_cast<std::uint8_t>((a << 1U) | cin);
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz8(result);
            } else {
                const std::uint16_t a = a_;
                const std::uint8_t carry = (a & 0x8000U) != 0U ? 1U : 0U;
                const auto result = static_cast<std::uint16_t>((a << 1U) | cin);
                a_ = result;
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz16(result);
            }
            ++step_cycles_;
            break;
        }
        case 0x6A: { // ROR A
            const std::uint8_t cin = (p_ & flag_c) != 0U ? 1U : 0U;
            if (a_is_8bit()) {
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                const std::uint8_t carry = a & 0x01U;
                const auto result = static_cast<std::uint8_t>((a >> 1U) | (cin << 7U));
                a_ = static_cast<std::uint16_t>((a_ & 0xFF00U) | result);
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz8(result);
            } else {
                const std::uint16_t a = a_;
                const auto carry = static_cast<std::uint8_t>(a & 0x01U);
                const auto result = static_cast<std::uint16_t>(
                    (a >> 1U) | (static_cast<std::uint16_t>(cin) << 15U));
                a_ = result;
                p_ = static_cast<std::uint8_t>((p_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
                set_nz16(result);
            }
            ++step_cycles_;
            break;
        }

        case 0xB2: // LDA (dp)
            load_a_from(addr_dp_indirect());
            break;
        case 0xB1: // LDA (dp),Y
            load_a_from(addr_dp_indirect_y());
            break;
        case 0x91: // STA (dp),Y
            store_a_to(addr_dp_indirect_y());
            break;

        case 0xCD: { // CMP abs
            const std::uint32_t addr = addr_abs_data();
            if (a_is_8bit()) {
                cmp_body8(bus_read(addr));
            } else {
                const std::uint16_t lo = bus_read(addr);
                const std::uint16_t hi = bus_read(addr + 1U);
                cmp_body16(static_cast<std::uint16_t>(lo | (hi << 8U)));
            }
            break;
        }
        case 0xC5: { // CMP dp
            const std::uint32_t addr = addr_dp();
            if (a_is_8bit()) {
                cmp_body8(bus_read(addr));
            } else {
                const std::uint16_t lo = bus_read(addr);
                const std::uint16_t hi = bus_read((addr + 1U) & 0xFFFFU);
                cmp_body16(static_cast<std::uint16_t>(lo | (hi << 8U)));
            }
            break;
        }

        case 0x2C: { // BIT abs
            const std::uint32_t addr = addr_abs_data();
            if (a_is_8bit()) {
                const std::uint8_t v = bus_read(addr);
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                p_ = static_cast<std::uint8_t>(
                    (p_ & ~(flag_n | flag_v | flag_z)) | ((v & 0x80U) != 0U ? flag_n : 0U) |
                    ((v & 0x40U) != 0U ? flag_v : 0U) | (((a & v) == 0U) ? flag_z : 0U));
            } else {
                const std::uint16_t lo = bus_read(addr);
                const std::uint16_t hi = bus_read(addr + 1U);
                const auto v = static_cast<std::uint16_t>(lo | (hi << 8U));
                const std::uint16_t a = a_;
                p_ = static_cast<std::uint8_t>(
                    (p_ & ~(flag_n | flag_v | flag_z)) | ((v & 0x8000U) != 0U ? flag_n : 0U) |
                    ((v & 0x4000U) != 0U ? flag_v : 0U) | (((a & v) == 0U) ? flag_z : 0U));
            }
            break;
        }
        case 0x89: // BIT # (immediate updates only Z)
            if (a_is_8bit()) {
                const std::uint8_t v = fetch_pc_byte();
                const auto a = static_cast<std::uint8_t>(a_ & 0xFFU);
                p_ = static_cast<std::uint8_t>((p_ & ~flag_z) | (((a & v) == 0U) ? flag_z : 0U));
            } else {
                const std::uint16_t v = fetch_pc_word();
                const std::uint16_t a = a_;
                p_ = static_cast<std::uint8_t>((p_ & ~flag_z) | (((a & v) == 0U) ? flag_z : 0U));
            }
            break;

        case 0x00: { // BRK
            pc_ = static_cast<std::uint16_t>(pc_ + 1U);
            std::uint8_t p_push = p_;
            if (e_) {
                p_push |= flag_x; // emulation break == P.X bit
            }
            push_interrupt_context(p_push);
            p_ |= flag_i;
            if (!e_) {
                p_ &= static_cast<std::uint8_t>(~flag_d);
            }
            pbr_ = 0U;
            pc_ = fetch_vector(0xFFE6U, 0xFFFEU);
            break;
        }
        case 0x02: { // COP
            pc_ = static_cast<std::uint16_t>(pc_ + 1U);
            push_interrupt_context(p_);
            p_ |= flag_i;
            if (!e_) {
                p_ &= static_cast<std::uint8_t>(~flag_d);
            }
            pbr_ = 0U;
            pc_ = fetch_vector(0xFFE4U, 0xFFF4U);
            break;
        }
        case 0x40: { // RTI
            p_ = stack_pop8();
            const std::uint16_t lo = stack_pop8();
            const std::uint16_t hi = stack_pop8();
            pc_ = static_cast<std::uint16_t>(lo | (hi << 8U));
            if (!e_) {
                pbr_ = stack_pop8();
            }
            enforce_emulation_invariants();
            break;
        }

        default:
            // Unimplemented opcode: park the CPU so coverage gaps are caught.
            halted_ = true;
            break;
        }
    }

    // ---- step / tick ----

    int wdc_65c816::step_instruction() {
        step_cycles_ = 0;

        // /RESET held: no work is performed; cycles still elapse so the system
        // schedule keeps its pacing.
        if (reset_line_) {
            elapsed_ += 4U;
            return 4;
        }

        if (halted_) {
            elapsed_ += 4U;
            return 4;
        }

        // Service pending interrupts before fetching. NMI wins over IRQ; IRQ is
        // masked while I=1.
        if (nmi_pending_) {
            service_nmi();
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }
        if (irq_line_ && (p_ & flag_i) == 0U) {
            service_irq();
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        if (trace_callback_) {
            trace_callback_(pc_);
        }

        const std::uint8_t opcode = fetch_pc_byte();
        execute(opcode);
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void wdc_65c816::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void wdc_65c816::set_nmi_line(bool asserted) noexcept {
        // Edge-triggered: latch one NMI per inactive->active transition.
        if (asserted && !nmi_line_) {
            nmi_pending_ = true;
        }
        nmi_line_ = asserted;
    }

    void wdc_65c816::set_reset_line(bool asserted) noexcept {
        // The /RESET pin. Asserting resets architectural state and parks the
        // CPU; releasing restarts from the reset vector. The pacing counters
        // survive so the system schedule stays anchored.
        if (asserted && !reset_line_) {
            const std::int64_t debt = cycle_debt_;
            const std::uint64_t elapsed = elapsed_;
            reset(reset_kind::soft);
            cycle_debt_ = debt;
            elapsed_ = elapsed;
        }
        reset_line_ = asserted;
    }

    void wdc_65c816::reset(reset_kind /*kind*/) {
        // Power-on/reset: emulation mode forced, M/X/I set, D=0, banks cleared,
        // S high byte forced to page 1, PC loaded from the reset vector.
        e_ = true;
        p_ = static_cast<std::uint8_t>(flag_m | flag_x | flag_i);
        d_ = 0U;
        dbr_ = 0U;
        pbr_ = 0U;
        s_ = static_cast<std::uint16_t>(0x0100U | (s_ & 0x00FFU));
        x_ &= 0x00FFU;
        y_ &= 0x00FFU;
        a_ &= 0x00FFU;
        halted_ = false;
        irq_line_ = false;
        nmi_line_ = false;
        nmi_pending_ = false;
        reset_line_ = false;
        // Reset vector at $00FFFC/$00FFFD (the vector fetch is not metered).
        const std::uint8_t lo = bus_ != nullptr ? bus_->read8(0x00FFFCU) : 0x00U;
        const std::uint8_t hi = bus_ != nullptr ? bus_->read8(0x00FFFDU) : 0x00U;
        pc_ = static_cast<std::uint16_t>(lo | (hi << 8U));
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    wdc_65c816::registers wdc_65c816::cpu_registers() const noexcept {
        return {.a = a_,
                .x = x_,
                .y = y_,
                .d = d_,
                .s = s_,
                .pc = pc_,
                .pbr = pbr_,
                .dbr = dbr_,
                .p = p_,
                .e = e_,
                .halted = halted_};
    }

    void wdc_65c816::set_registers(const registers& v) noexcept {
        a_ = v.a;
        x_ = v.x;
        y_ = v.y;
        d_ = v.d;
        s_ = v.s;
        pc_ = v.pc;
        pbr_ = v.pbr;
        dbr_ = v.dbr;
        p_ = v.p;
        e_ = v.e;
        halted_ = v.halted;
    }

    void wdc_65c816::save_state(state_writer& writer) const {
        writer.u16(a_);
        writer.u16(x_);
        writer.u16(y_);
        writer.u16(d_);
        writer.u16(s_);
        writer.u16(pc_);
        writer.u8(pbr_);
        writer.u8(dbr_);
        writer.u8(p_);
        writer.boolean(e_);
        writer.boolean(halted_);
        writer.boolean(irq_line_);
        writer.boolean(nmi_line_);
        writer.boolean(nmi_pending_);
        writer.boolean(reset_line_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void wdc_65c816::load_state(state_reader& reader) {
        a_ = reader.u16();
        x_ = reader.u16();
        y_ = reader.u16();
        d_ = reader.u16();
        s_ = reader.u16();
        pc_ = reader.u16();
        pbr_ = reader.u8();
        dbr_ = reader.u8();
        p_ = reader.u8();
        e_ = reader.boolean();
        halted_ = reader.boolean();
        irq_line_ = reader.boolean();
        nmi_line_ = reader.boolean();
        nmi_pending_ = reader.boolean();
        reset_line_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& wdc_65c816::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> wdc_65c816::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"A", a_, 16U, fmt::unsigned_integer};
        register_view_[1] = {"X", x_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"Y", y_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"D", d_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"S", s_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"PBR", pbr_, 8U, fmt::unsigned_integer};
        register_view_[7] = {"DBR", dbr_, 8U, fmt::unsigned_integer};
        register_view_[8] = {"P", p_, 8U, fmt::flags};
        register_view_[9] = {"E", e_ ? 1U : 0U, 8U, fmt::unsigned_integer};
        register_view_[10] = {"PBR:PC", (static_cast<std::uint64_t>(pbr_) << 16U) | pc_, 24U,
                              fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto wdc_65c816_registration =
            register_factory("wdc.65c816", chip_class::cpu, []() -> std::unique_ptr<ichip> {
                return std::make_unique<wdc_65c816>();
            });
    } // namespace

} // namespace mnemos::chips::cpu
