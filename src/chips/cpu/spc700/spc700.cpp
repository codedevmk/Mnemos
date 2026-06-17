#include "spc700.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>

// Ported from the Emu reference (chips/spc700); clean-room per the Sony SPC700
// datasheet. The cycle model mirrors the reference: every bus access (operand
// fetch, load, store, stack) costs one cycle, with internal-operation opcodes
// charging an extra cycle of their own; the instruction's total is published as
// its step cost. This is an instruction subset (see the header), faithful to
// the reference's integer semantics.

namespace mnemos::chips::cpu {

    chip_metadata spc700::metadata() const noexcept {
        return {
            .manufacturer = "Sony",
            .part_number = "SPC700",
            .family = "SPC700",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- memory access (each access charges one cycle, per the datasheet) ----

    std::uint8_t spc700::rb(std::uint16_t addr) noexcept {
        ++step_cycles_;
        return bus_ != nullptr ? bus_->read8(addr) : 0xFFU;
    }
    void spc700::wb(std::uint16_t addr, std::uint8_t value) noexcept {
        ++step_cycles_;
        if (bus_ != nullptr) {
            bus_->write8(addr, value);
        }
    }
    std::uint8_t spc700::fetch8() noexcept {
        const std::uint8_t v = rb(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return v;
    }
    std::uint16_t spc700::fetch16() noexcept {
        const std::uint8_t lo = fetch8();
        const std::uint8_t hi = fetch8();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    // The stack lives in page $01; SP is 8-bit. Push writes then post-decrements;
    // pop pre-increments then reads.
    void spc700::push8(std::uint8_t v) noexcept {
        wb(static_cast<std::uint16_t>(0x0100U | sp_), v);
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
    }
    std::uint8_t spc700::pop8() noexcept {
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        return rb(static_cast<std::uint16_t>(0x0100U | sp_));
    }

    // ---- flag / ALU helpers ----

    void spc700::set_nz(std::uint8_t v) noexcept {
        psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_n | flag_z));
        if (v == 0U) {
            psw_ |= flag_z;
        }
        if ((v & 0x80U) != 0U) {
            psw_ |= flag_n;
        }
    }

    void spc700::do_adc(std::uint8_t operand) noexcept {
        const unsigned carry = (psw_ & flag_c) != 0U ? 1U : 0U;
        const unsigned sum = static_cast<unsigned>(a_) + operand + carry;
        const auto result = static_cast<std::uint8_t>(sum);
        const bool overflow = ((a_ ^ result) & (operand ^ result) & 0x80U) != 0U;
        // Half-carry: carry out of bit 3.
        const bool half = ((a_ & 0x0FU) + (operand & 0x0FU) + carry) > 0x0FU;
        psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_c | flag_v | flag_h | flag_z | flag_n));
        if (sum > 0xFFU) {
            psw_ |= flag_c;
        }
        if (overflow) {
            psw_ |= flag_v;
        }
        if (half) {
            psw_ |= flag_h;
        }
        if (result == 0U) {
            psw_ |= flag_z;
        }
        if ((result & 0x80U) != 0U) {
            psw_ |= flag_n;
        }
        a_ = result;
    }

    // SBC is ADC of the one's complement (carry = inverted borrow), per the
    // 6502-family convention the SPC700 shares.
    void spc700::do_sbc(std::uint8_t operand) noexcept {
        do_adc(static_cast<std::uint8_t>(~operand));
    }

    void spc700::do_cmp(std::uint8_t lhs, std::uint8_t operand) noexcept {
        const unsigned diff = static_cast<unsigned>(lhs) - operand;
        psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_c | flag_z | flag_n));
        if (lhs >= operand) {
            psw_ |= flag_c;
        }
        if (lhs == operand) {
            psw_ |= flag_z;
        }
        if ((diff & 0x80U) != 0U) {
            psw_ |= flag_n;
        }
    }

    void spc700::branch_if(bool taken) noexcept {
        const auto offset = static_cast<std::int8_t>(fetch8());
        if (taken) {
            pc_ = static_cast<std::uint16_t>(pc_ + offset);
            ++step_cycles_; // a taken branch costs an extra internal cycle
        }
    }

    // ---- decode / execute ----

    void spc700::exec(std::uint8_t op) {
        switch (op) {
        case 0x00: // NOP
            ++step_cycles_;
            break;

        // ---- flag twiddling ----
        case 0x60: // CLRC
            psw_ = static_cast<std::uint8_t>(psw_ & ~flag_c);
            ++step_cycles_;
            break;
        case 0x80: // SETC
            psw_ |= flag_c;
            ++step_cycles_;
            break;
        case 0xA0: // EI
            psw_ = static_cast<std::uint8_t>(psw_ & ~flag_i);
            ++step_cycles_;
            break;
        case 0xC0: // DI
            psw_ |= flag_i;
            ++step_cycles_;
            break;

        // ---- MOV immediate / absolute ----
        case 0xE8: { // MOV A,#imm
            const std::uint8_t v = fetch8();
            a_ = v;
            set_nz(v);
            break;
        }
        case 0xCD: { // MOV X,#imm
            const std::uint8_t v = fetch8();
            x_ = v;
            set_nz(v);
            break;
        }
        case 0x8D: { // MOV Y,#imm
            const std::uint8_t v = fetch8();
            y_ = v;
            set_nz(v);
            break;
        }
        case 0xE5: { // MOV A,!abs
            const std::uint16_t addr = fetch16();
            const std::uint8_t v = rb(addr);
            a_ = v;
            set_nz(v);
            break;
        }
        case 0xC5: { // MOV !abs,A
            const std::uint16_t addr = fetch16();
            wb(addr, a_);
            break;
        }

        case 0x5F: // JMP !abs
            pc_ = fetch16();
            break;

        // ---- register transfers ----
        case 0x7D: // MOV A,X
            a_ = x_;
            set_nz(a_);
            ++step_cycles_;
            break;
        case 0x5D: // MOV X,A
            x_ = a_;
            set_nz(x_);
            ++step_cycles_;
            break;
        case 0xDD: // MOV A,Y
            a_ = y_;
            set_nz(a_);
            ++step_cycles_;
            break;
        case 0xFD: // MOV Y,A
            y_ = a_;
            set_nz(y_);
            ++step_cycles_;
            break;

        // ---- stack ----
        case 0x2D: // PUSH A
            push8(a_);
            break;
        case 0x4D: // PUSH X
            push8(x_);
            break;
        case 0x6D: // PUSH Y
            push8(y_);
            break;
        case 0x0D: // PUSH PSW
            push8(psw_);
            break;
        case 0xAE: // POP A
            a_ = pop8();
            break;
        case 0xCE: // POP X
            x_ = pop8();
            break;
        case 0xEE: // POP Y
            y_ = pop8();
            break;
        case 0x8E: // POP PSW
            psw_ = pop8();
            break;

        // ---- arithmetic / logic, immediate ----
        case 0x88: // ADC A,#imm
            do_adc(fetch8());
            break;
        case 0xA8: // SBC A,#imm
            do_sbc(fetch8());
            break;
        case 0x68: // CMP A,#imm
            do_cmp(a_, fetch8());
            break;
        case 0x28: // AND A,#imm
            a_ = static_cast<std::uint8_t>(a_ & fetch8());
            set_nz(a_);
            break;
        case 0x08: // OR A,#imm
            a_ = static_cast<std::uint8_t>(a_ | fetch8());
            set_nz(a_);
            break;
        case 0x48: // EOR A,#imm
            a_ = static_cast<std::uint8_t>(a_ ^ fetch8());
            set_nz(a_);
            break;

        // ---- INC / DEC register ----
        case 0xBC: // INC A
            a_ = static_cast<std::uint8_t>(a_ + 1U);
            set_nz(a_);
            ++step_cycles_;
            break;
        case 0x9C: // DEC A
            a_ = static_cast<std::uint8_t>(a_ - 1U);
            set_nz(a_);
            ++step_cycles_;
            break;
        case 0x3D: // INC X
            x_ = static_cast<std::uint8_t>(x_ + 1U);
            set_nz(x_);
            ++step_cycles_;
            break;
        case 0x1D: // DEC X
            x_ = static_cast<std::uint8_t>(x_ - 1U);
            set_nz(x_);
            ++step_cycles_;
            break;
        case 0xFC: // INC Y
            y_ = static_cast<std::uint8_t>(y_ + 1U);
            set_nz(y_);
            ++step_cycles_;
            break;
        case 0xDC: // DEC Y
            y_ = static_cast<std::uint8_t>(y_ - 1U);
            set_nz(y_);
            ++step_cycles_;
            break;

        // ---- branches ----
        case 0x2F: // BRA
            branch_if(true);
            break;
        case 0x10: // BPL
            branch_if((psw_ & flag_n) == 0U);
            break;
        case 0x30: // BMI
            branch_if((psw_ & flag_n) != 0U);
            break;
        case 0x50: // BVC
            branch_if((psw_ & flag_v) == 0U);
            break;
        case 0x70: // BVS
            branch_if((psw_ & flag_v) != 0U);
            break;
        case 0x90: // BCC
            branch_if((psw_ & flag_c) == 0U);
            break;
        case 0xB0: // BCS
            branch_if((psw_ & flag_c) != 0U);
            break;
        case 0xD0: // BNE
            branch_if((psw_ & flag_z) == 0U);
            break;
        case 0xF0: // BEQ
            branch_if((psw_ & flag_z) != 0U);
            break;

        // ---- CALL / RET ----
        case 0x3F: { // CALL !abs
            const std::uint16_t target = fetch16();
            // The return address is the instruction after CALL.
            push8(static_cast<std::uint8_t>(pc_ >> 8U));
            push8(static_cast<std::uint8_t>(pc_));
            pc_ = target;
            break;
        }
        case 0x6F: { // RET
            const std::uint16_t lo = pop8();
            const std::uint16_t hi = pop8();
            pc_ = static_cast<std::uint16_t>(lo | (hi << 8U));
            break;
        }

        // ---- direct-page loads / stores ----
        case 0xE4: { // MOV A,dp
            const std::uint8_t dp = fetch8();
            const std::uint8_t v = rb(dp_addr(dp));
            a_ = v;
            set_nz(v);
            break;
        }
        case 0xC4: { // MOV dp,A
            const std::uint8_t dp = fetch8();
            wb(dp_addr(dp), a_);
            break;
        }
        case 0xF8: { // MOV X,dp
            const std::uint8_t dp = fetch8();
            const std::uint8_t v = rb(dp_addr(dp));
            x_ = v;
            set_nz(v);
            break;
        }
        case 0xD8: { // MOV dp,X
            const std::uint8_t dp = fetch8();
            wb(dp_addr(dp), x_);
            break;
        }
        case 0xEB: { // MOV Y,dp
            const std::uint8_t dp = fetch8();
            const std::uint8_t v = rb(dp_addr(dp));
            y_ = v;
            set_nz(v);
            break;
        }
        case 0xCB: { // MOV dp,Y
            const std::uint8_t dp = fetch8();
            wb(dp_addr(dp), y_);
            break;
        }

        // ---- (X) register-indirect ----
        case 0xE6: { // MOV A,(X)
            const std::uint8_t v = rb(dp_addr(x_));
            a_ = v;
            set_nz(v);
            break;
        }
        case 0xC6: // MOV (X),A
            wb(dp_addr(x_), a_);
            break;

        // ---- shifts / rotates on A ----
        case 0x1C: { // ASL A
            const std::uint8_t carry = (a_ & 0x80U) != 0U ? 1U : 0U;
            a_ = static_cast<std::uint8_t>(a_ << 1U);
            psw_ = static_cast<std::uint8_t>((psw_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
            set_nz(a_);
            ++step_cycles_;
            break;
        }
        case 0x5C: { // LSR A
            const std::uint8_t carry = a_ & 0x01U;
            a_ = static_cast<std::uint8_t>(a_ >> 1U);
            psw_ = static_cast<std::uint8_t>((psw_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
            set_nz(a_);
            ++step_cycles_;
            break;
        }
        case 0x3C: { // ROL A
            const std::uint8_t cin = (psw_ & flag_c) != 0U ? 1U : 0U;
            const std::uint8_t carry = (a_ & 0x80U) != 0U ? 1U : 0U;
            a_ = static_cast<std::uint8_t>((a_ << 1U) | cin);
            psw_ = static_cast<std::uint8_t>((psw_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
            set_nz(a_);
            ++step_cycles_;
            break;
        }
        case 0x7C: { // ROR A
            const std::uint8_t cin = (psw_ & flag_c) != 0U ? 1U : 0U;
            const std::uint8_t carry = a_ & 0x01U;
            a_ = static_cast<std::uint8_t>((a_ >> 1U) | (cin << 7U));
            psw_ = static_cast<std::uint8_t>((psw_ & ~flag_c) | (carry != 0U ? flag_c : 0U));
            set_nz(a_);
            ++step_cycles_;
            break;
        }

        // ---- additional compares ----
        case 0x64: { // CMP A,dp
            const std::uint8_t dp = fetch8();
            do_cmp(a_, rb(dp_addr(dp)));
            break;
        }
        case 0xC8: // CMP X,#imm
            do_cmp(x_, fetch8());
            break;
        case 0xAD: // CMP Y,#imm
            do_cmp(y_, fetch8());
            break;

        // ---- 16-bit word operations ----
        case 0xBA: { // MOVW YA,dp
            const std::uint8_t dp = fetch8();
            a_ = rb(dp_addr(dp));
            y_ = rb(dp_addr(static_cast<std::uint8_t>(dp + 1U)));
            // N from the high bit of Y; Z over the whole 16-bit value.
            psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_n | flag_z));
            if (ya() == 0U) {
                psw_ |= flag_z;
            }
            if ((y_ & 0x80U) != 0U) {
                psw_ |= flag_n;
            }
            break;
        }
        case 0xDA: { // MOVW dp,YA
            const std::uint8_t dp = fetch8();
            wb(dp_addr(dp), a_);
            wb(dp_addr(static_cast<std::uint8_t>(dp + 1U)), y_);
            break;
        }
        case 0x3A: { // INCW dp
            const std::uint8_t dp = fetch8();
            const std::uint16_t lo_addr = dp_addr(dp);
            const std::uint16_t hi_addr = dp_addr(static_cast<std::uint8_t>(dp + 1U));
            const std::uint8_t lo = rb(lo_addr);
            const std::uint8_t hi = rb(hi_addr);
            auto val = static_cast<std::uint16_t>(lo | (hi << 8U));
            val = static_cast<std::uint16_t>(val + 1U);
            wb(lo_addr, static_cast<std::uint8_t>(val));
            wb(hi_addr, static_cast<std::uint8_t>(val >> 8U));
            psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_n | flag_z));
            if (val == 0U) {
                psw_ |= flag_z;
            }
            if ((val & 0x8000U) != 0U) {
                psw_ |= flag_n;
            }
            break;
        }
        case 0x1A: { // DECW dp
            const std::uint8_t dp = fetch8();
            const std::uint16_t lo_addr = dp_addr(dp);
            const std::uint16_t hi_addr = dp_addr(static_cast<std::uint8_t>(dp + 1U));
            const std::uint8_t lo = rb(lo_addr);
            const std::uint8_t hi = rb(hi_addr);
            auto val = static_cast<std::uint16_t>(lo | (hi << 8U));
            val = static_cast<std::uint16_t>(val - 1U);
            wb(lo_addr, static_cast<std::uint8_t>(val));
            wb(hi_addr, static_cast<std::uint8_t>(val >> 8U));
            psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_n | flag_z));
            if (val == 0U) {
                psw_ |= flag_z;
            }
            if ((val & 0x8000U) != 0U) {
                psw_ |= flag_n;
            }
            break;
        }

        // ---- indexed direct-page (dp+X) ----
        case 0xF4: { // MOV A,dp+X
            const std::uint8_t dp = fetch8();
            const std::uint8_t v = rb(dp_addr(static_cast<std::uint8_t>(dp + x_)));
            a_ = v;
            set_nz(v);
            break;
        }
        case 0xD4: { // MOV dp+X,A
            const std::uint8_t dp = fetch8();
            wb(dp_addr(static_cast<std::uint8_t>(dp + x_)), a_);
            break;
        }

        // ---- MUL YA / DIV YA,X ----
        case 0xCF: { // MUL YA
            const auto prod = static_cast<std::uint16_t>(y_ * a_);
            set_ya(prod);
            // N from the high bit of Y; Z over the whole product.
            psw_ = static_cast<std::uint8_t>(psw_ & ~(flag_n | flag_z));
            if (prod == 0U) {
                psw_ |= flag_z;
            }
            if ((y_ & 0x80U) != 0U) {
                psw_ |= flag_n;
            }
            ++step_cycles_;
            break;
        }
        case 0x9E: { // DIV YA,X
            // 16/8 -> 8-bit quotient (A) + 8-bit remainder (Y). V sets on
            // quotient overflow; divide-by-zero yields 0xFF and sets V.
            if (x_ == 0U) {
                a_ = 0xFFU;
                psw_ |= flag_v;
                set_nz(a_);
            } else {
                const std::uint16_t dividend = ya();
                const std::uint32_t quot = static_cast<std::uint32_t>(dividend) / x_;
                const auto rem = static_cast<std::uint8_t>(dividend % x_);
                if (quot > 0xFFU) {
                    psw_ |= flag_v;
                    a_ = 0xFFU;
                } else {
                    psw_ = static_cast<std::uint8_t>(psw_ & ~flag_v);
                    a_ = static_cast<std::uint8_t>(quot);
                }
                y_ = rem;
                set_nz(a_);
            }
            ++step_cycles_;
            break;
        }

        default:
            // Opcode outside the implemented subset: park the core at a defined
            // stop (the reference's halt behaviour) rather than misexecute.
            halted_ = true;
            ++step_cycles_;
            break;
        }
    }

    // ---- step / tick ----

    int spc700::step_instruction() {
        step_cycles_ = 0;

        // /RESET held: the CPU performs no work; cycles still elapse so the
        // system schedule keeps its pacing.
        if (reset_line_) {
            elapsed_ += 2U;
            return 2;
        }

        if (halted_) {
            elapsed_ += 1U;
            return 1;
        }

        if (trace_callback_) {
            trace_callback_(pc_);
        }
        const std::uint8_t op = fetch8();
        exec(op);
        if (step_cycles_ < 1) {
            step_cycles_ = 1;
        }
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void spc700::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void spc700::set_reset_line(bool asserted) noexcept {
        // The /RESET pin. Asserting resets the architectural state and parks the
        // CPU; releasing restarts from the reset vector. Boards whose audio CPU
        // program lives in host-uploaded RAM hold this until the upload
        // completes. The cycle pacing counters survive so the system schedule
        // stays anchored.
        if (asserted && !reset_line_) {
            const std::int64_t debt = cycle_debt_;
            const std::uint64_t elapsed = elapsed_;
            reset(reset_kind::soft);
            cycle_debt_ = debt;
            elapsed_ = elapsed;
        }
        reset_line_ = asserted;
    }

    void spc700::reset(reset_kind /*kind*/) {
        a_ = x_ = y_ = 0U;
        sp_ = 0xEFU;   // documented reset SP
        psw_ = flag_i; // IRQ disabled out of reset
        halted_ = false;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
        // Reset vector at $FFFE/$FFFF (the IPL boot-ROM entry on a real S-SMP).
        const std::uint8_t lo = bus_ != nullptr ? bus_->read8(0xFFFEU) : 0x00U;
        const std::uint8_t hi = bus_ != nullptr ? bus_->read8(0xFFFFU) : 0xFFU;
        pc_ = static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    spc700::registers spc700::cpu_registers() const noexcept {
        return {.a = a_, .x = x_, .y = y_, .sp = sp_, .psw = psw_, .pc = pc_, .halted = halted_};
    }

    void spc700::set_registers(const registers& v) noexcept {
        a_ = v.a;
        x_ = v.x;
        y_ = v.y;
        sp_ = v.sp;
        psw_ = v.psw;
        pc_ = v.pc;
        halted_ = v.halted;
    }

    void spc700::save_state(state_writer& writer) const {
        writer.u8(a_);
        writer.u8(x_);
        writer.u8(y_);
        writer.u8(sp_);
        writer.u8(psw_);
        writer.u16(pc_);
        writer.boolean(halted_);
        writer.boolean(reset_line_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void spc700::load_state(state_reader& reader) {
        a_ = reader.u8();
        x_ = reader.u8();
        y_ = reader.u8();
        sp_ = reader.u8();
        psw_ = reader.u8();
        pc_ = reader.u16();
        halted_ = reader.boolean();
        reset_line_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& spc700::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> spc700::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"A", a_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"X", x_, 8U, fmt::unsigned_integer};
        register_view_[2] = {"Y", y_, 8U, fmt::unsigned_integer};
        register_view_[3] = {"SP", sp_, 8U, fmt::unsigned_integer};
        register_view_[4] = {"PSW", psw_, 8U, fmt::flags};
        register_view_[5] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"YA", ya(), 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto spc700_registration =
            register_factory("sony.spc700", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<spc700>(); });
    } // namespace

} // namespace mnemos::chips::cpu
