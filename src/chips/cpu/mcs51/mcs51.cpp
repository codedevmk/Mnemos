#include "mcs51.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>
#include <utility>

namespace mnemos::chips::cpu {

    namespace {
        // SFR direct addresses.
        constexpr std::uint8_t sfr_p0 = 0x80U;
        constexpr std::uint8_t sfr_sp = 0x81U;
        constexpr std::uint8_t sfr_dpl = 0x82U;
        constexpr std::uint8_t sfr_dph = 0x83U;
        constexpr std::uint8_t sfr_tcon = 0x88U;
        constexpr std::uint8_t sfr_tmod = 0x89U;
        constexpr std::uint8_t sfr_tl0 = 0x8AU;
        constexpr std::uint8_t sfr_tl1 = 0x8BU;
        constexpr std::uint8_t sfr_th0 = 0x8CU;
        constexpr std::uint8_t sfr_th1 = 0x8DU;
        constexpr std::uint8_t sfr_p1 = 0x90U;
        constexpr std::uint8_t sfr_p2 = 0xA0U;
        constexpr std::uint8_t sfr_ie = 0xA8U;
        constexpr std::uint8_t sfr_p3 = 0xB0U;
        constexpr std::uint8_t sfr_psw = 0xD0U;
        constexpr std::uint8_t sfr_acc = 0xE0U;
        constexpr std::uint8_t sfr_b = 0xF0U;

        // TCON bits.
        constexpr std::uint8_t tcon_it0 = 0x01U;
        constexpr std::uint8_t tcon_ie0 = 0x02U;
        constexpr std::uint8_t tcon_it1 = 0x04U;
        constexpr std::uint8_t tcon_ie1 = 0x08U;
        constexpr std::uint8_t tcon_tr0 = 0x10U;
        constexpr std::uint8_t tcon_tf0 = 0x20U;
        constexpr std::uint8_t tcon_tr1 = 0x40U;
        constexpr std::uint8_t tcon_tf1 = 0x80U;

        // IE bits.
        constexpr std::uint8_t ie_ex0 = 0x01U;
        constexpr std::uint8_t ie_et0 = 0x02U;
        constexpr std::uint8_t ie_ex1 = 0x04U;
        constexpr std::uint8_t ie_et1 = 0x08U;
        constexpr std::uint8_t ie_ea = 0x80U;

        [[nodiscard]] constexpr bool odd_parity(std::uint8_t value) noexcept {
            unsigned bits = value;
            bits ^= bits >> 4U;
            bits ^= bits >> 2U;
            bits ^= bits >> 1U;
            return (bits & 1U) != 0U;
        }
    } // namespace

    chip_metadata mcs51::metadata() const noexcept {
        return {
            .manufacturer = "Intel",
            .part_number = "mcs51",
            .family = "mcs51",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void mcs51::reset(reset_kind /*kind*/) {
        iram_.fill(0U);
        sfr_.fill(0U);
        // Port latches reset high (quasi-bidirectional inputs).
        sfr_[sfr_p0 - 0x80U] = 0xFFU;
        sfr_[sfr_p1 - 0x80U] = 0xFFU;
        sfr_[sfr_p2 - 0x80U] = 0xFFU;
        sfr_[sfr_p3 - 0x80U] = 0xFFU;
        acc_ = 0U;
        b_ = 0U;
        psw_ = 0U;
        sp_ = 0x07U;
        dptr_ = 0U;
        pc_ = 0U;
        int0_line_ = false;
        int1_line_ = false;
        in_interrupt_ = false;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    void mcs51::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    // ---- spaces ---------------------------------------------------------------

    std::uint8_t mcs51::fetch8() noexcept {
        const std::uint8_t value = code_read(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return value;
    }

    std::uint8_t mcs51::reg_r(int n) noexcept {
        return iram_[((psw_ & psw_rs) >> 3U) * 8U + static_cast<unsigned>(n & 7)];
    }

    void mcs51::set_reg_r(int n, std::uint8_t value) noexcept {
        iram_[((psw_ & psw_rs) >> 3U) * 8U + static_cast<unsigned>(n & 7)] = value;
    }

    std::uint8_t mcs51::read_direct(std::uint8_t address) noexcept {
        if (address < 0x80U) {
            return iram_[address];
        }
        switch (address) {
        case sfr_acc:
            return acc_;
        case sfr_b:
            return b_;
        case sfr_psw:
            // The parity bit is hardware-computed from ACC on every read.
            return static_cast<std::uint8_t>((psw_ & ~psw_p) | (odd_parity(acc_) ? psw_p : 0U));
        case sfr_sp:
            return sp_;
        case sfr_dpl:
            return static_cast<std::uint8_t>(dptr_);
        case sfr_dph:
            return static_cast<std::uint8_t>(dptr_ >> 8U);
        case sfr_p0:
        case sfr_p1:
        case sfr_p2:
        case sfr_p3: {
            // Reading a port sees the pins ANDed with the latch (a written 0
            // drives the pin low regardless of the external source).
            const int port = (address - sfr_p0) >> 4U;
            const std::uint8_t latch = sfr_[address - 0x80U];
            const std::uint8_t pins = port_in_ ? port_in_(port) : 0xFFU;
            return static_cast<std::uint8_t>(latch & pins);
        }
        default:
            return sfr_[address - 0x80U];
        }
    }

    void mcs51::write_direct(std::uint8_t address, std::uint8_t value) noexcept {
        if (address < 0x80U) {
            iram_[address] = value;
            return;
        }
        switch (address) {
        case sfr_acc:
            acc_ = value;
            return;
        case sfr_b:
            b_ = value;
            return;
        case sfr_psw:
            psw_ = static_cast<std::uint8_t>(value & ~psw_p);
            return;
        case sfr_sp:
            sp_ = value;
            return;
        case sfr_dpl:
            dptr_ = static_cast<std::uint16_t>((dptr_ & 0xFF00U) | value);
            return;
        case sfr_dph:
            dptr_ = static_cast<std::uint16_t>((dptr_ & 0x00FFU) | (value << 8U));
            return;
        case sfr_p0:
        case sfr_p1:
        case sfr_p2:
        case sfr_p3:
            sfr_[address - 0x80U] = value;
            if (port_out_) {
                port_out_((address - sfr_p0) >> 4U, value);
            }
            return;
        default:
            sfr_[address - 0x80U] = value;
            return;
        }
    }

    std::uint8_t mcs51::read_indirect(std::uint8_t address) noexcept {
        // @Ri reaches internal RAM only; the classic 8051 has no upper 128.
        return address < 0x80U ? iram_[address] : 0xFFU;
    }

    void mcs51::write_indirect(std::uint8_t address, std::uint8_t value) noexcept {
        if (address < 0x80U) {
            iram_[address] = value;
        }
    }

    std::uint8_t mcs51::read_bit_source(std::uint8_t bit) noexcept {
        if (bit < 0x80U) {
            return iram_[0x20U + (bit >> 3U)];
        }
        return read_direct(static_cast<std::uint8_t>(bit & 0xF8U));
    }

    bool mcs51::read_bit(std::uint8_t bit) noexcept {
        return (read_bit_source(bit) & (1U << (bit & 7U))) != 0U;
    }

    void mcs51::write_bit(std::uint8_t bit, bool value) noexcept {
        const auto mask = static_cast<std::uint8_t>(1U << (bit & 7U));
        if (bit < 0x80U) {
            std::uint8_t& byte = iram_[0x20U + (bit >> 3U)];
            byte = value ? static_cast<std::uint8_t>(byte | mask)
                         : static_cast<std::uint8_t>(byte & ~mask);
            return;
        }
        const auto address = static_cast<std::uint8_t>(bit & 0xF8U);
        const std::uint8_t byte = read_direct(address);
        write_direct(address, value ? static_cast<std::uint8_t>(byte | mask)
                                    : static_cast<std::uint8_t>(byte & ~mask));
    }

    void mcs51::push8(std::uint8_t value) noexcept {
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        if (sp_ < 0x80U) {
            iram_[sp_] = value;
        }
    }

    std::uint8_t mcs51::pop8() noexcept {
        const std::uint8_t value = sp_ < 0x80U ? iram_[sp_] : 0xFFU;
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        return value;
    }

    // ---- ALU --------------------------------------------------------------------

    void mcs51::do_add(std::uint8_t value, bool with_carry) noexcept {
        const unsigned carry = (with_carry && flag(psw_cy)) ? 1U : 0U;
        const unsigned wide = static_cast<unsigned>(acc_) + value + carry;
        const auto result = static_cast<std::uint8_t>(wide);
        assign_flag(psw_cy, wide > 0xFFU);
        assign_flag(psw_ac, ((acc_ & 0x0FU) + (value & 0x0FU) + carry) > 0x0FU);
        assign_flag(psw_ov, ((acc_ ^ result) & (value ^ result) & 0x80U) != 0U);
        acc_ = result;
    }

    void mcs51::do_subb(std::uint8_t value) noexcept {
        const unsigned borrow = flag(psw_cy) ? 1U : 0U;
        const unsigned wide = static_cast<unsigned>(acc_) - value - borrow;
        const auto result = static_cast<std::uint8_t>(wide);
        assign_flag(psw_cy, static_cast<unsigned>(value) + borrow > acc_);
        assign_flag(psw_ac, (acc_ & 0x0FU) < (value & 0x0FU) + borrow);
        assign_flag(psw_ov, ((acc_ ^ value) & (acc_ ^ result) & 0x80U) != 0U);
        acc_ = result;
    }

    // ---- interrupts -----------------------------------------------------------------

    void mcs51::set_int0_line(bool asserted) noexcept {
        std::uint8_t tcon = sfr_[sfr_tcon - 0x80U];
        if ((tcon & tcon_it0) != 0U) { // edge sense: latch on assertion
            if (asserted && !int0_line_) {
                tcon |= tcon_ie0;
            }
        } else { // level sense: the flag mirrors the pin
            tcon = asserted ? static_cast<std::uint8_t>(tcon | tcon_ie0)
                            : static_cast<std::uint8_t>(tcon & ~tcon_ie0);
        }
        sfr_[sfr_tcon - 0x80U] = tcon;
        int0_line_ = asserted;
    }

    void mcs51::set_int1_line(bool asserted) noexcept {
        std::uint8_t tcon = sfr_[sfr_tcon - 0x80U];
        if ((tcon & tcon_it1) != 0U) {
            if (asserted && !int1_line_) {
                tcon |= tcon_ie1;
            }
        } else {
            tcon = asserted ? static_cast<std::uint8_t>(tcon | tcon_ie1)
                            : static_cast<std::uint8_t>(tcon & ~tcon_ie1);
        }
        sfr_[sfr_tcon - 0x80U] = tcon;
        int1_line_ = asserted;
    }

    void mcs51::interrupt(std::uint16_t vector) noexcept {
        push8(static_cast<std::uint8_t>(pc_));
        push8(static_cast<std::uint8_t>(pc_ >> 8U));
        pc_ = vector;
        in_interrupt_ = true;
        step_cycles_ += 2;
    }

    bool mcs51::service_interrupts() noexcept {
        const std::uint8_t ie = sfr_[sfr_ie - 0x80U];
        if (in_interrupt_ || (ie & ie_ea) == 0U) {
            return false;
        }
        std::uint8_t& tcon = sfr_[sfr_tcon - 0x80U];
        // Fixed polling order: INT0, timer 0, INT1, timer 1.
        if ((tcon & tcon_ie0) != 0U && (ie & ie_ex0) != 0U) {
            if ((tcon & tcon_it0) != 0U) {
                tcon = static_cast<std::uint8_t>(tcon & ~tcon_ie0); // edge flag self-clears
            }
            interrupt(0x0003U);
            return true;
        }
        if ((tcon & tcon_tf0) != 0U && (ie & ie_et0) != 0U) {
            tcon = static_cast<std::uint8_t>(tcon & ~tcon_tf0);
            interrupt(0x000BU);
            return true;
        }
        if ((tcon & tcon_ie1) != 0U && (ie & ie_ex1) != 0U) {
            if ((tcon & tcon_it1) != 0U) {
                tcon = static_cast<std::uint8_t>(tcon & ~tcon_ie1);
            }
            interrupt(0x0013U);
            return true;
        }
        if ((tcon & tcon_tf1) != 0U && (ie & ie_et1) != 0U) {
            tcon = static_cast<std::uint8_t>(tcon & ~tcon_tf1);
            interrupt(0x001BU);
            return true;
        }
        return false;
    }

    void mcs51::timers_tick(std::uint32_t machine_cycles) noexcept {
        std::uint8_t& tcon = sfr_[sfr_tcon - 0x80U];
        const std::uint8_t tmod = sfr_[sfr_tmod - 0x80U];
        for (std::uint32_t c = 0; c < machine_cycles; ++c) {
            if ((tcon & tcon_tr0) != 0U) {
                std::uint8_t& tl = sfr_[sfr_tl0 - 0x80U];
                std::uint8_t& th = sfr_[sfr_th0 - 0x80U];
                const std::uint8_t mode = tmod & 0x03U;
                if (mode == 2U) { // 8-bit auto-reload
                    if (++tl == 0U) {
                        tl = th;
                        tcon |= tcon_tf0;
                    }
                } else { // 16-bit (mode 0's 13-bit prescale folded in, first cut)
                    if (++tl == 0U && ++th == 0U) {
                        tcon |= tcon_tf0;
                    }
                }
            }
            if ((tcon & tcon_tr1) != 0U) {
                std::uint8_t& tl = sfr_[sfr_tl1 - 0x80U];
                std::uint8_t& th = sfr_[sfr_th1 - 0x80U];
                const std::uint8_t mode = (tmod >> 4U) & 0x03U;
                if (mode == 2U) {
                    if (++tl == 0U) {
                        tl = th;
                        tcon |= tcon_tf1;
                    }
                } else {
                    if (++tl == 0U && ++th == 0U) {
                        tcon |= tcon_tf1;
                    }
                }
            }
        }
    }

    // ---- execution ----------------------------------------------------------------

    int mcs51::step_instruction() {
        step_cycles_ = 0;
        if (!service_interrupts()) {
            if (trace_callback_) {
                trace_callback_(pc_);
            }
            step_cycles_ += exec_one();
        }
        timers_tick(static_cast<std::uint32_t>(step_cycles_));
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    int mcs51::exec_one() {
        const std::uint8_t op = fetch8();

        // AJMP / ACALL carry their 11-bit target across the whole column.
        if ((op & 0x1FU) == 0x01U) { // AJMP addr11
            const std::uint16_t addr11 = static_cast<std::uint16_t>(((op >> 5U) << 8U) | fetch8());
            pc_ = static_cast<std::uint16_t>((pc_ & 0xF800U) | addr11);
            return 2;
        }
        if ((op & 0x1FU) == 0x11U) { // ACALL addr11
            const std::uint16_t addr11 = static_cast<std::uint16_t>(((op >> 5U) << 8U) | fetch8());
            push8(static_cast<std::uint8_t>(pc_));
            push8(static_cast<std::uint8_t>(pc_ >> 8U));
            pc_ = static_cast<std::uint16_t>((pc_ & 0xF800U) | addr11);
            return 2;
        }

        // The regular ALU rows: low nibble selects the operand form.
        const auto operand = [this](std::uint8_t opcode) -> std::uint8_t {
            const std::uint8_t low = opcode & 0x0FU;
            if (low == 0x04U) {
                return fetch8(); // #imm
            }
            if (low == 0x05U) {
                return read_direct(fetch8());
            }
            if (low == 0x06U || low == 0x07U) {
                return read_indirect(reg_r(static_cast<int>(low - 6U)));
            }
            return reg_r(static_cast<int>(low - 8U)); // 0x08-0x0F
        };
        switch (op & 0xF0U) {
        case 0x20U:
            if ((op & 0x0FU) >= 4U) { // ADD A, <operand>
                do_add(operand(op), false);
                return 1;
            }
            break;
        case 0x30U:
            if ((op & 0x0FU) >= 4U) { // ADDC A, <operand>
                do_add(operand(op), true);
                return 1;
            }
            break;
        case 0x90U:
            if ((op & 0x0FU) >= 4U) { // SUBB A, <operand>
                do_subb(operand(op));
                return 1;
            }
            break;
        case 0x40U:
            if ((op & 0x0FU) >= 4U) { // ORL A, <operand>
                acc_ |= operand(op);
                return 1;
            }
            break;
        case 0x50U:
            if ((op & 0x0FU) >= 4U) { // ANL A, <operand>
                acc_ &= operand(op);
                return 1;
            }
            break;
        case 0x60U:
            if ((op & 0x0FU) >= 4U) { // XRL A, <operand>
                acc_ ^= operand(op);
                return 1;
            }
            break;
        case 0xE0U:
            if ((op & 0x0FU) >= 5U) { // MOV A, <operand> (no imm form here)
                acc_ = operand(op);
                return 1;
            }
            break;
        case 0xF0U:
            if (op >= 0xF5U) { // MOV <dest>, A
                const std::uint8_t low = op & 0x0FU;
                if (low == 0x05U) {
                    write_direct(fetch8(), acc_);
                } else if (low == 0x06U || low == 0x07U) {
                    write_indirect(reg_r(static_cast<int>(low - 6U)), acc_);
                } else {
                    set_reg_r(static_cast<int>(low - 8U), acc_);
                }
                return 1;
            }
            break;
        default:
            break;
        }

        switch (op) {
        case 0x00U: // NOP
            return 1;
        case 0x02U: { // LJMP addr16
            const std::uint8_t hi = fetch8();
            const std::uint8_t lo = fetch8();
            pc_ = static_cast<std::uint16_t>((hi << 8U) | lo);
            return 2;
        }
        case 0x12U: { // LCALL addr16
            const std::uint8_t hi = fetch8();
            const std::uint8_t lo = fetch8();
            push8(static_cast<std::uint8_t>(pc_));
            push8(static_cast<std::uint8_t>(pc_ >> 8U));
            pc_ = static_cast<std::uint16_t>((hi << 8U) | lo);
            return 2;
        }
        case 0x22U: { // RET
            const std::uint8_t hi = pop8();
            const std::uint8_t lo = pop8();
            pc_ = static_cast<std::uint16_t>((hi << 8U) | lo);
            return 2;
        }
        case 0x32U: { // RETI
            const std::uint8_t hi = pop8();
            const std::uint8_t lo = pop8();
            pc_ = static_cast<std::uint16_t>((hi << 8U) | lo);
            in_interrupt_ = false;
            return 2;
        }
        case 0x03U: // RR A
            acc_ = static_cast<std::uint8_t>((acc_ >> 1U) | (acc_ << 7U));
            return 1;
        case 0x13U: { // RRC A
            const bool carry = flag(psw_cy);
            assign_flag(psw_cy, (acc_ & 0x01U) != 0U);
            acc_ = static_cast<std::uint8_t>((acc_ >> 1U) | (carry ? 0x80U : 0U));
            return 1;
        }
        case 0x23U: // RL A
            acc_ = static_cast<std::uint8_t>((acc_ << 1U) | (acc_ >> 7U));
            return 1;
        case 0x33U: { // RLC A
            const bool carry = flag(psw_cy);
            assign_flag(psw_cy, (acc_ & 0x80U) != 0U);
            acc_ = static_cast<std::uint8_t>((acc_ << 1U) | (carry ? 1U : 0U));
            return 1;
        }
        case 0x04U: // INC A
            ++acc_;
            return 1;
        case 0x05U: { // INC direct
            const std::uint8_t address = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) + 1U));
            return 1;
        }
        case 0x06U:
        case 0x07U: { // INC @Ri
            const std::uint8_t address = reg_r(static_cast<int>(op - 6U));
            write_indirect(address, static_cast<std::uint8_t>(read_indirect(address) + 1U));
            return 1;
        }
        case 0x08U:
        case 0x09U:
        case 0x0AU:
        case 0x0BU:
        case 0x0CU:
        case 0x0DU:
        case 0x0EU:
        case 0x0FU: // INC Rn
            set_reg_r(static_cast<int>(op - 8U),
                      static_cast<std::uint8_t>(reg_r(static_cast<int>(op - 8U)) + 1U));
            return 1;
        case 0x14U: // DEC A
            --acc_;
            return 1;
        case 0x15U: { // DEC direct
            const std::uint8_t address = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) - 1U));
            return 1;
        }
        case 0x16U:
        case 0x17U: { // DEC @Ri
            const std::uint8_t address = reg_r(static_cast<int>(op - 0x16U));
            write_indirect(address, static_cast<std::uint8_t>(read_indirect(address) - 1U));
            return 1;
        }
        case 0x18U:
        case 0x19U:
        case 0x1AU:
        case 0x1BU:
        case 0x1CU:
        case 0x1DU:
        case 0x1EU:
        case 0x1FU: // DEC Rn
            set_reg_r(static_cast<int>(op - 0x18U),
                      static_cast<std::uint8_t>(reg_r(static_cast<int>(op - 0x18U)) - 1U));
            return 1;
        case 0x10U: { // JBC bit, rel
            const std::uint8_t bit = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (read_bit(bit)) {
                write_bit(bit, false);
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x20U: { // JB bit, rel
            const std::uint8_t bit = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (read_bit(bit)) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x30U: { // JNB bit, rel
            const std::uint8_t bit = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (!read_bit(bit)) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x40U: { // JC rel
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (flag(psw_cy)) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x50U: { // JNC rel
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (!flag(psw_cy)) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x60U: { // JZ rel
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (acc_ == 0U) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x70U: { // JNZ rel
            const auto rel = static_cast<std::int8_t>(fetch8());
            if (acc_ != 0U) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0x80U: { // SJMP rel
            const auto rel = static_cast<std::int8_t>(fetch8());
            pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            return 2;
        }
        case 0x42U: { // ORL direct, A
            const std::uint8_t address = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) | acc_));
            return 1;
        }
        case 0x43U: { // ORL direct, #imm
            const std::uint8_t address = fetch8();
            const std::uint8_t imm = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) | imm));
            return 2;
        }
        case 0x52U: { // ANL direct, A
            const std::uint8_t address = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) & acc_));
            return 1;
        }
        case 0x53U: { // ANL direct, #imm
            const std::uint8_t address = fetch8();
            const std::uint8_t imm = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) & imm));
            return 2;
        }
        case 0x62U: { // XRL direct, A
            const std::uint8_t address = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) ^ acc_));
            return 1;
        }
        case 0x63U: { // XRL direct, #imm
            const std::uint8_t address = fetch8();
            const std::uint8_t imm = fetch8();
            write_direct(address, static_cast<std::uint8_t>(read_direct(address) ^ imm));
            return 2;
        }
        case 0x72U: { // ORL C, bit
            const std::uint8_t bit = fetch8();
            assign_flag(psw_cy, flag(psw_cy) || read_bit(bit));
            return 2;
        }
        case 0x82U: { // ANL C, bit
            const std::uint8_t bit = fetch8();
            assign_flag(psw_cy, flag(psw_cy) && read_bit(bit));
            return 2;
        }
        case 0xA0U: { // ORL C, /bit
            const std::uint8_t bit = fetch8();
            assign_flag(psw_cy, flag(psw_cy) || !read_bit(bit));
            return 2;
        }
        case 0xB0U: { // ANL C, /bit
            const std::uint8_t bit = fetch8();
            assign_flag(psw_cy, flag(psw_cy) && !read_bit(bit));
            return 2;
        }
        case 0x73U: // JMP @A+DPTR
            pc_ = static_cast<std::uint16_t>(dptr_ + acc_);
            return 2;
        case 0x74U: // MOV A, #imm
            acc_ = fetch8();
            return 1;
        case 0x75U: { // MOV direct, #imm
            const std::uint8_t address = fetch8();
            write_direct(address, fetch8());
            return 2;
        }
        case 0x76U:
        case 0x77U: // MOV @Ri, #imm
            write_indirect(reg_r(static_cast<int>(op - 0x76U)), fetch8());
            return 1;
        case 0x78U:
        case 0x79U:
        case 0x7AU:
        case 0x7BU:
        case 0x7CU:
        case 0x7DU:
        case 0x7EU:
        case 0x7FU: // MOV Rn, #imm
            set_reg_r(static_cast<int>(op - 0x78U), fetch8());
            return 1;
        case 0x83U: // MOVC A, @A+PC (PC is already past this 1-byte opcode)
            acc_ = code_read(static_cast<std::uint16_t>(pc_ + acc_));
            return 2;
        case 0x93U: // MOVC A, @A+DPTR
            acc_ = code_read(static_cast<std::uint16_t>(dptr_ + acc_));
            return 2;
        case 0x84U: { // DIV AB
            assign_flag(psw_cy, false);
            if (b_ == 0U) {
                assign_flag(psw_ov, true);
                return 4;
            }
            assign_flag(psw_ov, false);
            const std::uint8_t quotient = static_cast<std::uint8_t>(acc_ / b_);
            b_ = static_cast<std::uint8_t>(acc_ % b_);
            acc_ = quotient;
            return 4;
        }
        case 0xA4U: { // MUL AB
            const unsigned product = static_cast<unsigned>(acc_) * b_;
            acc_ = static_cast<std::uint8_t>(product);
            b_ = static_cast<std::uint8_t>(product >> 8U);
            assign_flag(psw_cy, false);
            assign_flag(psw_ov, b_ != 0U);
            return 4;
        }
        case 0x85U: { // MOV direct, direct (source byte first in the stream)
            const std::uint8_t source = fetch8();
            const std::uint8_t destination = fetch8();
            write_direct(destination, read_direct(source));
            return 2;
        }
        case 0x86U:
        case 0x87U: { // MOV direct, @Ri
            const std::uint8_t value = read_indirect(reg_r(static_cast<int>(op - 0x86U)));
            write_direct(fetch8(), value);
            return 2;
        }
        case 0x88U:
        case 0x89U:
        case 0x8AU:
        case 0x8BU:
        case 0x8CU:
        case 0x8DU:
        case 0x8EU:
        case 0x8FU: // MOV direct, Rn
            write_direct(fetch8(), reg_r(static_cast<int>(op - 0x88U)));
            return 2;
        case 0x90U: { // MOV DPTR, #imm16
            const std::uint8_t hi = fetch8();
            const std::uint8_t lo = fetch8();
            dptr_ = static_cast<std::uint16_t>((hi << 8U) | lo);
            return 2;
        }
        case 0x92U: // MOV bit, C
            write_bit(fetch8(), flag(psw_cy));
            return 2;
        case 0xA2U: // MOV C, bit
            assign_flag(psw_cy, read_bit(fetch8()));
            return 1;
        case 0xA3U: // INC DPTR
            ++dptr_;
            return 2;
        case 0xA5U: // undefined: no-op (bring-up convention)
            return 1;
        case 0xA6U:
        case 0xA7U: // MOV @Ri, direct
            write_indirect(reg_r(static_cast<int>(op - 0xA6U)), read_direct(fetch8()));
            return 2;
        case 0xA8U:
        case 0xA9U:
        case 0xAAU:
        case 0xABU:
        case 0xACU:
        case 0xADU:
        case 0xAEU:
        case 0xAFU: // MOV Rn, direct
            set_reg_r(static_cast<int>(op - 0xA8U), read_direct(fetch8()));
            return 2;
        case 0xB2U: { // CPL bit
            const std::uint8_t bit = fetch8();
            write_bit(bit, !read_bit(bit));
            return 1;
        }
        case 0xB3U: // CPL C
            assign_flag(psw_cy, !flag(psw_cy));
            return 1;
        case 0xB4U: { // CJNE A, #imm, rel
            const std::uint8_t imm = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            assign_flag(psw_cy, acc_ < imm);
            if (acc_ != imm) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xB5U: { // CJNE A, direct, rel
            const std::uint8_t value = read_direct(fetch8());
            const auto rel = static_cast<std::int8_t>(fetch8());
            assign_flag(psw_cy, acc_ < value);
            if (acc_ != value) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xB6U:
        case 0xB7U: { // CJNE @Ri, #imm, rel
            const std::uint8_t value = read_indirect(reg_r(static_cast<int>(op - 0xB6U)));
            const std::uint8_t imm = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            assign_flag(psw_cy, value < imm);
            if (value != imm) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xB8U:
        case 0xB9U:
        case 0xBAU:
        case 0xBBU:
        case 0xBCU:
        case 0xBDU:
        case 0xBEU:
        case 0xBFU: { // CJNE Rn, #imm, rel
            const std::uint8_t value = reg_r(static_cast<int>(op - 0xB8U));
            const std::uint8_t imm = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            assign_flag(psw_cy, value < imm);
            if (value != imm) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xC0U: // PUSH direct
            push8(read_direct(fetch8()));
            return 2;
        case 0xD0U: // POP direct
            write_direct(fetch8(), pop8());
            return 2;
        case 0xC2U: // CLR bit
            write_bit(fetch8(), false);
            return 1;
        case 0xC3U: // CLR C
            assign_flag(psw_cy, false);
            return 1;
        case 0xD2U: // SETB bit
            write_bit(fetch8(), true);
            return 1;
        case 0xD3U: // SETB C
            assign_flag(psw_cy, true);
            return 1;
        case 0xC4U: // SWAP A
            acc_ = static_cast<std::uint8_t>((acc_ << 4U) | (acc_ >> 4U));
            return 1;
        case 0xC5U: { // XCH A, direct
            const std::uint8_t address = fetch8();
            const std::uint8_t value = read_direct(address);
            write_direct(address, acc_);
            acc_ = value;
            return 1;
        }
        case 0xC6U:
        case 0xC7U: { // XCH A, @Ri
            const std::uint8_t address = reg_r(static_cast<int>(op - 0xC6U));
            const std::uint8_t value = read_indirect(address);
            write_indirect(address, acc_);
            acc_ = value;
            return 1;
        }
        case 0xC8U:
        case 0xC9U:
        case 0xCAU:
        case 0xCBU:
        case 0xCCU:
        case 0xCDU:
        case 0xCEU:
        case 0xCFU: { // XCH A, Rn
            const int n = static_cast<int>(op - 0xC8U);
            const std::uint8_t value = reg_r(n);
            set_reg_r(n, acc_);
            acc_ = value;
            return 1;
        }
        case 0xD4U: { // DA A
            unsigned value = acc_;
            if ((value & 0x0FU) > 9U || flag(psw_ac)) {
                value += 6U;
            }
            if (value > 0xFFU) {
                assign_flag(psw_cy, true);
            }
            if (((value >> 4U) & 0x0FU) > 9U || flag(psw_cy)) {
                value += 0x60U;
            }
            if (value > 0xFFU) {
                assign_flag(psw_cy, true);
            }
            acc_ = static_cast<std::uint8_t>(value);
            return 1;
        }
        case 0xD5U: { // DJNZ direct, rel
            const std::uint8_t address = fetch8();
            const auto rel = static_cast<std::int8_t>(fetch8());
            const auto value = static_cast<std::uint8_t>(read_direct(address) - 1U);
            write_direct(address, value);
            if (value != 0U) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xD6U:
        case 0xD7U: { // XCHD A, @Ri
            const std::uint8_t address = reg_r(static_cast<int>(op - 0xD6U));
            const std::uint8_t value = read_indirect(address);
            write_indirect(address, static_cast<std::uint8_t>((value & 0xF0U) | (acc_ & 0x0FU)));
            acc_ = static_cast<std::uint8_t>((acc_ & 0xF0U) | (value & 0x0FU));
            return 1;
        }
        case 0xD8U:
        case 0xD9U:
        case 0xDAU:
        case 0xDBU:
        case 0xDCU:
        case 0xDDU:
        case 0xDEU:
        case 0xDFU: { // DJNZ Rn, rel
            const int n = static_cast<int>(op - 0xD8U);
            const auto rel = static_cast<std::int8_t>(fetch8());
            const auto value = static_cast<std::uint8_t>(reg_r(n) - 1U);
            set_reg_r(n, value);
            if (value != 0U) {
                pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::uint16_t>(rel));
            }
            return 2;
        }
        case 0xE0U: // MOVX A, @DPTR
            acc_ = bus_ != nullptr ? bus_->read8(dptr_) : 0xFFU;
            return 2;
        case 0xE2U:
        case 0xE3U: { // MOVX A, @Ri (P2 latch supplies the high byte)
            const std::uint32_t address = (static_cast<std::uint32_t>(sfr_[sfr_p2 - 0x80U]) << 8U) |
                                          reg_r(static_cast<int>(op - 0xE2U));
            acc_ = bus_ != nullptr ? bus_->read8(address) : 0xFFU;
            return 2;
        }
        case 0xE4U: // CLR A
            acc_ = 0U;
            return 1;
        case 0xF0U: // MOVX @DPTR, A
            if (bus_ != nullptr) {
                bus_->write8(dptr_, acc_);
            }
            return 2;
        case 0xF2U:
        case 0xF3U: { // MOVX @Ri, A
            const std::uint32_t address = (static_cast<std::uint32_t>(sfr_[sfr_p2 - 0x80U]) << 8U) |
                                          reg_r(static_cast<int>(op - 0xF2U));
            if (bus_ != nullptr) {
                bus_->write8(address, acc_);
            }
            return 2;
        }
        case 0xF4U: // CPL A
            acc_ = static_cast<std::uint8_t>(~acc_);
            return 1;
        default:
            return 1; // unreachable rows are covered above
        }
    }

    // ---- snapshots / persistence -------------------------------------------------

    mcs51::registers mcs51::cpu_registers() const noexcept {
        return {
            .acc = acc_,
            .b = b_,
            .psw = static_cast<std::uint8_t>((psw_ & ~psw_p) | (odd_parity(acc_) ? psw_p : 0U)),
            .sp = sp_,
            .dptr = dptr_,
            .pc = pc_,
        };
    }

    void mcs51::set_registers(const registers& values) noexcept {
        acc_ = values.acc;
        b_ = values.b;
        psw_ = static_cast<std::uint8_t>(values.psw & ~psw_p);
        sp_ = values.sp;
        dptr_ = values.dptr;
        pc_ = values.pc;
    }

    void mcs51::save_state(state_writer& writer) const {
        writer.bytes(iram_);
        writer.bytes(sfr_);
        writer.u8(acc_);
        writer.u8(b_);
        writer.u8(psw_);
        writer.u8(sp_);
        writer.u16(dptr_);
        writer.u16(pc_);
        writer.boolean(int0_line_);
        writer.boolean(int1_line_);
        writer.boolean(in_interrupt_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void mcs51::load_state(state_reader& reader) {
        reader.bytes(iram_);
        reader.bytes(sfr_);
        acc_ = reader.u8();
        b_ = reader.u8();
        psw_ = reader.u8();
        sp_ = reader.u8();
        dptr_ = reader.u16();
        pc_ = reader.u16();
        int0_line_ = reader.boolean();
        int1_line_ = reader.boolean();
        in_interrupt_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    // ---- introspection ---------------------------------------------------------

    std::span<const register_descriptor> mcs51::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"ACC", acc_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"B", b_, 8U, fmt::unsigned_integer};
        register_view_[2] = {"PSW", cpu_registers().psw, 8U, fmt::flags};
        register_view_[3] = {"SP", sp_, 8U, fmt::unsigned_integer};
        register_view_[4] = {"DPTR", dptr_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"PC", pc_, 16U, fmt::unsigned_integer};
        return register_view_;
    }

    instrumentation::ichip_introspection& mcs51::introspection() noexcept { return introspection_; }

    namespace {
        [[maybe_unused]] const auto mcs51_registration =
            register_factory("intel.mcs51", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<mcs51>(); });
    } // namespace

} // namespace mnemos::chips::cpu
