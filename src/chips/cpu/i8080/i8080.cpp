#include "i8080.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <array>
#include <memory>

namespace mnemos::chips::cpu {
    namespace {
        [[nodiscard]] bool even_parity(std::uint8_t value) noexcept {
            value ^= static_cast<std::uint8_t>(value >> 4U);
            value &= 0x0FU;
            return ((0x6996U >> value) & 1U) == 0U;
        }

        [[nodiscard]] std::uint8_t clean_flags(std::uint8_t flags) noexcept {
            return static_cast<std::uint8_t>((flags & (i8080::flag_s | i8080::flag_z |
                                                       i8080::flag_ac | i8080::flag_p |
                                                       i8080::flag_c)) |
                                             i8080::flag_const);
        }

        [[nodiscard]] std::uint8_t variant_code(i8080::variant model) noexcept {
            return model == i8080::variant::intel_8085 ? 1U : 0U;
        }
    } // namespace

    i8080::i8080() {
        introspection_.with_registers([this] { return register_snapshot(); })
            .with_trace(instrumentation::pc_trace_installer(
                trace_callback_, [this] { return elapsed_cycles(); }));
        reset(reset_kind::power_on);
    }

    chip_metadata i8080::metadata() const noexcept {
        return {.manufacturer = "Intel",
                .part_number = variant_ == variant::intel_8085 ? "8085A" : "8080A",
                .family = "8080",
                .klass = chip_class::cpu,
                .revision = 1U};
    }

    std::uint8_t i8080::rb(std::uint16_t address) noexcept {
        return bus_ != nullptr ? bus_->read8(address) : 0xFFU;
    }

    void i8080::wb(std::uint16_t address, std::uint8_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(address, value);
        }
    }

    std::uint16_t i8080::rw(std::uint16_t address) noexcept {
        return static_cast<std::uint16_t>(rb(address) |
                                          (rb(static_cast<std::uint16_t>(address + 1U)) << 8U));
    }

    void i8080::ww(std::uint16_t address, std::uint16_t value) noexcept {
        wb(address, static_cast<std::uint8_t>(value));
        wb(static_cast<std::uint16_t>(address + 1U), static_cast<std::uint8_t>(value >> 8U));
    }

    std::uint8_t i8080::fetch8() noexcept {
        const std::uint8_t value = bus_ != nullptr ? bus_->fetch_opcode8(pc_) : 0xFFU;
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return value;
    }

    std::uint16_t i8080::fetch16() noexcept {
        const std::uint8_t lo = rb(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        const std::uint8_t hi = rb(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    std::uint8_t i8080::port_in8(std::uint8_t port) {
        return port_in_ ? port_in_(port) : 0xFFU;
    }

    void i8080::port_out8(std::uint8_t port, std::uint8_t value) {
        if (port_out_) {
            port_out_(port, value);
        }
    }

    std::uint8_t i8080::get_reg8(int reg) noexcept {
        switch (reg) {
        case 0:
            return b();
        case 1:
            return c();
        case 2:
            return d();
        case 3:
            return e();
        case 4:
            return h();
        case 5:
            return l();
        case 6:
            return rb(hl_);
        default:
            return a_;
        }
    }

    void i8080::set_reg8(int reg, std::uint8_t value) noexcept {
        switch (reg) {
        case 0:
            set_b(value);
            break;
        case 1:
            set_c(value);
            break;
        case 2:
            set_d(value);
            break;
        case 3:
            set_e(value);
            break;
        case 4:
            set_h(value);
            break;
        case 5:
            set_l(value);
            break;
        case 6:
            wb(hl_, value);
            break;
        default:
            a_ = value;
            break;
        }
    }

    std::uint16_t* i8080::rp(int p) noexcept {
        switch (p) {
        case 0:
            return &bc_;
        case 1:
            return &de_;
        case 2:
            return &hl_;
        default:
            return &sp_;
        }
    }

    std::uint16_t i8080::rp_value_psw(int p) const noexcept {
        switch (p) {
        case 0:
            return bc_;
        case 1:
            return de_;
        case 2:
            return hl_;
        default:
            return static_cast<std::uint16_t>((a_ << 8U) | clean_flags(f_));
        }
    }

    void i8080::set_rp_value_psw(int p, std::uint16_t value) noexcept {
        switch (p) {
        case 0:
            bc_ = value;
            break;
        case 1:
            de_ = value;
            break;
        case 2:
            hl_ = value;
            break;
        default:
            a_ = static_cast<std::uint8_t>(value >> 8U);
            f_ = clean_flags(static_cast<std::uint8_t>(value));
            break;
        }
    }

    void i8080::push16(std::uint16_t value) noexcept {
        sp_ = static_cast<std::uint16_t>(sp_ - 2U);
        ww(sp_, value);
    }

    std::uint16_t i8080::pop16() noexcept {
        const std::uint16_t value = rw(sp_);
        sp_ = static_cast<std::uint16_t>(sp_ + 2U);
        return value;
    }

    bool i8080::condition(int cc) const noexcept {
        switch (cc) {
        case 0:
            return (f_ & flag_z) == 0U;
        case 1:
            return (f_ & flag_z) != 0U;
        case 2:
            return (f_ & flag_c) == 0U;
        case 3:
            return (f_ & flag_c) != 0U;
        case 4:
            return (f_ & flag_p) == 0U;
        case 5:
            return (f_ & flag_p) != 0U;
        case 6:
            return (f_ & flag_s) == 0U;
        default:
            return (f_ & flag_s) != 0U;
        }
    }

    void i8080::set_szp(std::uint8_t value) noexcept {
        f_ = clean_flags(static_cast<std::uint8_t>(
            (f_ & (flag_c | flag_ac)) | (value == 0U ? flag_z : 0U) |
            ((value & 0x80U) != 0U ? flag_s : 0U) | (even_parity(value) ? flag_p : 0U)));
    }

    void i8080::add_a(std::uint8_t value, std::uint8_t carry) noexcept {
        const std::uint16_t sum = static_cast<std::uint16_t>(a_ + value + carry);
        const std::uint8_t result = static_cast<std::uint8_t>(sum);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (sum > 0xFFU ? flag_c : 0U) | (((a_ ^ value ^ result) & 0x10U) != 0U ? flag_ac : 0U) |
            (result == 0U ? flag_z : 0U) | ((result & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(result) ? flag_p : 0U)));
        a_ = result;
    }

    void i8080::sub_a(std::uint8_t value, std::uint8_t borrow, bool store) noexcept {
        const std::uint16_t diff = static_cast<std::uint16_t>(a_ - value - borrow);
        const std::uint8_t result = static_cast<std::uint8_t>(diff);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (diff > 0xFFU ? flag_c : 0U) | (((a_ ^ value ^ result) & 0x10U) != 0U ? flag_ac : 0U) |
            (result == 0U ? flag_z : 0U) | ((result & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(result) ? flag_p : 0U)));
        if (store) {
            a_ = result;
        }
    }

    void i8080::ana(std::uint8_t value) noexcept {
        const std::uint8_t result = static_cast<std::uint8_t>(a_ & value);
        f_ = clean_flags(static_cast<std::uint8_t>(
            flag_ac | (result == 0U ? flag_z : 0U) | ((result & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(result) ? flag_p : 0U)));
        a_ = result;
    }

    void i8080::xra(std::uint8_t value) noexcept {
        a_ = static_cast<std::uint8_t>(a_ ^ value);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (a_ == 0U ? flag_z : 0U) | ((a_ & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(a_) ? flag_p : 0U)));
    }

    void i8080::ora(std::uint8_t value) noexcept {
        a_ = static_cast<std::uint8_t>(a_ | value);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (a_ == 0U ? flag_z : 0U) | ((a_ & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(a_) ? flag_p : 0U)));
    }

    std::uint8_t i8080::inr(std::uint8_t value) noexcept {
        const std::uint8_t result = static_cast<std::uint8_t>(value + 1U);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (f_ & flag_c) | (((value ^ result) & 0x10U) != 0U ? flag_ac : 0U) |
            (result == 0U ? flag_z : 0U) | ((result & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(result) ? flag_p : 0U)));
        return result;
    }

    std::uint8_t i8080::dcr(std::uint8_t value) noexcept {
        const std::uint8_t result = static_cast<std::uint8_t>(value - 1U);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (f_ & flag_c) | (((value ^ result) & 0x10U) != 0U ? flag_ac : 0U) |
            (result == 0U ? flag_z : 0U) | ((result & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(result) ? flag_p : 0U)));
        return result;
    }

    void i8080::dad(std::uint16_t value) noexcept {
        const std::uint32_t sum = static_cast<std::uint32_t>(hl_) + value;
        hl_ = static_cast<std::uint16_t>(sum);
        f_ = clean_flags(static_cast<std::uint8_t>((f_ & ~flag_c) | (sum > 0xFFFFU ? flag_c : 0U)));
    }

    void i8080::daa() noexcept {
        const std::uint8_t old = a_;
        std::uint8_t correction = 0U;
        bool carry = (f_ & flag_c) != 0U;
        if ((f_ & flag_ac) != 0U || (a_ & 0x0FU) > 9U) {
            correction = static_cast<std::uint8_t>(correction | 0x06U);
        }
        if (carry || a_ > 0x99U) {
            correction = static_cast<std::uint8_t>(correction | 0x60U);
            carry = true;
        }
        a_ = static_cast<std::uint8_t>(a_ + correction);
        f_ = clean_flags(static_cast<std::uint8_t>(
            (carry ? flag_c : 0U) | (((old ^ a_) & 0x10U) != 0U ? flag_ac : 0U) |
            (a_ == 0U ? flag_z : 0U) | ((a_ & 0x80U) != 0U ? flag_s : 0U) |
            (even_parity(a_) ? flag_p : 0U)));
    }

    void i8080::alu(int op, std::uint8_t value) noexcept {
        switch (op) {
        case 0:
            add_a(value, 0U);
            break;
        case 1:
            add_a(value, (f_ & flag_c) != 0U ? 1U : 0U);
            break;
        case 2:
            sub_a(value, 0U, true);
            break;
        case 3:
            sub_a(value, (f_ & flag_c) != 0U ? 1U : 0U, true);
            break;
        case 4:
            ana(value);
            break;
        case 5:
            xra(value);
            break;
        case 6:
            ora(value);
            break;
        default:
            sub_a(value, 0U, false);
            break;
        }
    }

    void i8080::rim() noexcept {
        if (variant_ != variant::intel_8085) {
            return;
        }
        a_ = static_cast<std::uint8_t>((pending_8085_interrupts_ & 0x07U) |
                                       (interrupts_enabled_ ? 0x08U : 0U) |
                                       ((interrupt_mask_8085_ & 0x07U) << 4U));
    }

    void i8080::sim() noexcept {
        if (variant_ != variant::intel_8085) {
            return;
        }
        if ((a_ & 0x08U) != 0U) {
            interrupt_mask_8085_ = static_cast<std::uint8_t>(a_ & 0x07U);
        }
        if ((a_ & 0x10U) != 0U) {
            pending_8085_interrupts_ = static_cast<std::uint8_t>(pending_8085_interrupts_ & ~0x04U);
        }
        if ((a_ & 0x40U) != 0U) {
            serial_output_ = static_cast<std::uint8_t>((a_ >> 7U) & 0x01U);
        }
    }

    void i8080::exec(std::uint8_t op) {
        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;
        const int p = y >> 1;
        const int q = y & 1;

        if (x == 0) {
            switch (z) {
            case 0:
                if (y == 4) {
                    rim();
                } else if (y == 6) {
                    sim();
                }
                step_cycles_ += 4;
                break;
            case 1:
                if (q == 0) {
                    *rp(p) = fetch16();
                    step_cycles_ += 10;
                } else {
                    dad(*rp(p));
                    step_cycles_ += 10;
                }
                break;
            case 2:
                switch (y) {
                case 0:
                    wb(bc_, a_);
                    step_cycles_ += 7;
                    break;
                case 1:
                    a_ = rb(bc_);
                    step_cycles_ += 7;
                    break;
                case 2:
                    wb(de_, a_);
                    step_cycles_ += 7;
                    break;
                case 3:
                    a_ = rb(de_);
                    step_cycles_ += 7;
                    break;
                case 4: {
                    const std::uint16_t addr = fetch16();
                    ww(addr, hl_);
                    step_cycles_ += 16;
                    break;
                }
                case 5:
                    hl_ = rw(fetch16());
                    step_cycles_ += 16;
                    break;
                case 6:
                    wb(fetch16(), a_);
                    step_cycles_ += 13;
                    break;
                default:
                    a_ = rb(fetch16());
                    step_cycles_ += 13;
                    break;
                }
                break;
            case 3:
                if (q == 0) {
                    *rp(p) = static_cast<std::uint16_t>(*rp(p) + 1U);
                } else {
                    *rp(p) = static_cast<std::uint16_t>(*rp(p) - 1U);
                }
                step_cycles_ += 5;
                break;
            case 4:
                set_reg8(y, inr(get_reg8(y)));
                step_cycles_ += y == 6 ? 10 : 5;
                break;
            case 5:
                set_reg8(y, dcr(get_reg8(y)));
                step_cycles_ += y == 6 ? 10 : 5;
                break;
            case 6:
                set_reg8(y, static_cast<std::uint8_t>(rb(pc_)));
                pc_ = static_cast<std::uint16_t>(pc_ + 1U);
                step_cycles_ += y == 6 ? 10 : 7;
                break;
            default:
                switch (y) {
                case 0: {
                    const std::uint8_t carry = static_cast<std::uint8_t>(a_ >> 7U);
                    a_ = static_cast<std::uint8_t>((a_ << 1U) | carry);
                    f_ = clean_flags(static_cast<std::uint8_t>((f_ & ~flag_c) | carry));
                    break;
                }
                case 1: {
                    const std::uint8_t carry = static_cast<std::uint8_t>(a_ & 0x01U);
                    a_ = static_cast<std::uint8_t>((a_ >> 1U) | (carry << 7U));
                    f_ = clean_flags(static_cast<std::uint8_t>((f_ & ~flag_c) | carry));
                    break;
                }
                case 2: {
                    const std::uint8_t carry = static_cast<std::uint8_t>(a_ >> 7U);
                    a_ = static_cast<std::uint8_t>((a_ << 1U) | ((f_ & flag_c) != 0U ? 1U : 0U));
                    f_ = clean_flags(static_cast<std::uint8_t>((f_ & ~flag_c) | carry));
                    break;
                }
                case 3: {
                    const std::uint8_t carry = static_cast<std::uint8_t>(a_ & 0x01U);
                    a_ = static_cast<std::uint8_t>((a_ >> 1U) | ((f_ & flag_c) != 0U ? 0x80U : 0U));
                    f_ = clean_flags(static_cast<std::uint8_t>((f_ & ~flag_c) | carry));
                    break;
                }
                case 4:
                    daa();
                    break;
                case 5:
                    a_ = static_cast<std::uint8_t>(~a_);
                    break;
                case 6:
                    f_ = clean_flags(static_cast<std::uint8_t>(f_ | flag_c));
                    break;
                default:
                    f_ = clean_flags(static_cast<std::uint8_t>(f_ ^ flag_c));
                    break;
                }
                step_cycles_ += 4;
                break;
            }
            return;
        }

        if (x == 1) {
            if (y == 6 && z == 6) {
                halted_ = true;
                step_cycles_ += 7;
                return;
            }
            set_reg8(y, get_reg8(z));
            step_cycles_ += y == 6 || z == 6 ? 7 : 5;
            return;
        }

        if (x == 2) {
            alu(y, get_reg8(z));
            step_cycles_ += z == 6 ? 7 : 4;
            return;
        }

        switch (z) {
        case 0:
            if (condition(y)) {
                pc_ = pop16();
                step_cycles_ += 11;
            } else {
                step_cycles_ += 5;
            }
            break;
        case 1:
            if (q == 0) {
                set_rp_value_psw(p, pop16());
                step_cycles_ += 10;
            } else if (p == 0 || p == 1) {
                pc_ = pop16();
                step_cycles_ += 10;
            } else if (p == 2) {
                pc_ = hl_;
                step_cycles_ += 5;
            } else {
                sp_ = hl_;
                step_cycles_ += 5;
            }
            break;
        case 2: {
            const std::uint16_t addr = fetch16();
            if (condition(y)) {
                pc_ = addr;
            }
            step_cycles_ += 10;
            break;
        }
        case 3:
            switch (y) {
            case 0:
                pc_ = fetch16();
                step_cycles_ += 10;
                break;
            case 2:
                port_out8(static_cast<std::uint8_t>(rb(pc_)), a_);
                pc_ = static_cast<std::uint16_t>(pc_ + 1U);
                step_cycles_ += 10;
                break;
            case 3:
                a_ = port_in8(static_cast<std::uint8_t>(rb(pc_)));
                pc_ = static_cast<std::uint16_t>(pc_ + 1U);
                step_cycles_ += 10;
                break;
            case 4: {
                const std::uint16_t tmp = rw(sp_);
                ww(sp_, hl_);
                hl_ = tmp;
                step_cycles_ += 18;
                break;
            }
            case 5: {
                const std::uint16_t tmp = de_;
                de_ = hl_;
                hl_ = tmp;
                step_cycles_ += 4;
                break;
            }
            case 6:
                interrupts_enabled_ = false;
                ei_pending_ = false;
                step_cycles_ += 4;
                break;
            case 7:
                ei_pending_ = true;
                step_cycles_ += 4;
                break;
            default:
                step_cycles_ += 4;
                break;
            }
            break;
        case 4: {
            const std::uint16_t addr = fetch16();
            if (condition(y)) {
                push16(pc_);
                pc_ = addr;
                step_cycles_ += 17;
            } else {
                step_cycles_ += 11;
            }
            break;
        }
        case 5:
            if (q == 0) {
                push16(rp_value_psw(p));
                step_cycles_ += 11;
            } else if (p == 0) {
                const std::uint16_t addr = fetch16();
                push16(pc_);
                pc_ = addr;
                step_cycles_ += 17;
            } else {
                step_cycles_ += 4;
            }
            break;
        case 6:
            alu(y, static_cast<std::uint8_t>(rb(pc_)));
            pc_ = static_cast<std::uint16_t>(pc_ + 1U);
            step_cycles_ += 7;
            break;
        default:
            push16(pc_);
            pc_ = static_cast<std::uint16_t>(y * 8);
            step_cycles_ += 11;
            break;
        }
    }

    int i8080::step_instruction() {
        step_cycles_ = 0;
        if (reset_line_) {
            elapsed_ += 4U;
            return 4;
        }
        if (irq_line_ && interrupts_enabled_ && !ei_pending_) {
            halted_ = false;
            interrupts_enabled_ = false;
            exec(irq_vector_ ? irq_vector_() : 0xFFU);
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }
        if (ei_pending_) {
            ei_pending_ = false;
            interrupts_enabled_ = true;
        }
        if (halted_) {
            elapsed_ += 4U;
            return 4;
        }
        if (trace_callback_) {
            trace_callback_(pc_);
        }
        exec(fetch8());
        if (step_cycles_ < 4) {
            step_cycles_ = 4;
        }
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void i8080::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void i8080::reset(reset_kind /*kind*/) {
        a_ = 0U;
        f_ = flag_const;
        bc_ = de_ = hl_ = 0U;
        sp_ = 0xFFFFU;
        pc_ = 0U;
        interrupts_enabled_ = ei_pending_ = halted_ = irq_line_ = reset_line_ = false;
        interrupt_mask_8085_ = pending_8085_interrupts_ = serial_output_ = 0U;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    void i8080::set_reset_line(bool asserted) noexcept {
        if (asserted && !reset_line_) {
            const std::int64_t debt = cycle_debt_;
            const std::uint64_t elapsed = elapsed_;
            reset(reset_kind::soft);
            cycle_debt_ = debt;
            elapsed_ = elapsed;
        }
        reset_line_ = asserted;
    }

    i8080::registers i8080::cpu_registers() const noexcept {
        return {.a = a_,
                .f = clean_flags(f_),
                .bc = bc_,
                .de = de_,
                .hl = hl_,
                .sp = sp_,
                .pc = pc_,
                .interrupts_enabled = interrupts_enabled_,
                .halted = halted_};
    }

    void i8080::set_registers(const registers& values) noexcept {
        a_ = values.a;
        f_ = clean_flags(values.f);
        bc_ = values.bc;
        de_ = values.de;
        hl_ = values.hl;
        sp_ = values.sp;
        pc_ = values.pc;
        interrupts_enabled_ = values.interrupts_enabled;
        halted_ = values.halted;
    }

    void i8080::save_state(state_writer& writer) const {
        writer.u8(variant_code(variant_));
        writer.u8(a_);
        writer.u8(clean_flags(f_));
        writer.u16(bc_);
        writer.u16(de_);
        writer.u16(hl_);
        writer.u16(sp_);
        writer.u16(pc_);
        writer.boolean(interrupts_enabled_);
        writer.boolean(ei_pending_);
        writer.boolean(halted_);
        writer.boolean(irq_line_);
        writer.boolean(reset_line_);
        writer.u8(interrupt_mask_8085_);
        writer.u8(pending_8085_interrupts_);
        writer.u8(serial_output_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void i8080::load_state(state_reader& reader) {
        variant_ = reader.u8() == 1U ? variant::intel_8085 : variant::intel_8080;
        a_ = reader.u8();
        f_ = clean_flags(reader.u8());
        bc_ = reader.u16();
        de_ = reader.u16();
        hl_ = reader.u16();
        sp_ = reader.u16();
        pc_ = reader.u16();
        interrupts_enabled_ = reader.boolean();
        ei_pending_ = reader.boolean();
        halted_ = reader.boolean();
        irq_line_ = reader.boolean();
        reset_line_ = reader.boolean();
        interrupt_mask_8085_ = reader.u8();
        pending_8085_interrupts_ = reader.u8();
        serial_output_ = reader.u8();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    std::span<const register_descriptor> i8080::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"A", a_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"F", clean_flags(f_), 8U, fmt::flags};
        register_view_[2] = {"BC", bc_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"DE", de_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"HL", hl_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"SP", sp_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"IE", interrupts_enabled_ ? 1U : 0U, 1U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto i8080_registration =
            register_factory("intel.i8080", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<i8080>(); });
        [[maybe_unused]] const auto i8085_registration =
            register_factory("intel.i8085", chip_class::cpu, []() -> std::unique_ptr<ichip> {
                auto cpu = std::make_unique<i8080>();
                cpu->set_variant(i8080::variant::intel_8085);
                return cpu;
            });
    } // namespace

} // namespace mnemos::chips::cpu
