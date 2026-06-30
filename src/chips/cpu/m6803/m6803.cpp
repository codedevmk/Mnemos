#include "m6803.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <cstdint>
#include <memory>

namespace mnemos::chips::cpu {
    namespace {
        constexpr std::uint32_t state_version = 2U;

        [[nodiscard]] std::uint8_t low8(std::uint16_t value) noexcept {
            return static_cast<std::uint8_t>(value & 0x00FFU);
        }

        [[nodiscard]] std::uint8_t high8(std::uint16_t value) noexcept {
            return static_cast<std::uint8_t>(value >> 8U);
        }

        [[nodiscard]] std::uint16_t make16(std::uint8_t high, std::uint8_t low) noexcept {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
        }
    } // namespace

    m6803::m6803() {
        introspection_.with_registers([this] { return register_snapshot(); })
            .with_trace(instrumentation::pc_trace_installer(trace_callback_,
                                                            [this] { return elapsed_cycles(); }));
        reset(reset_kind::power_on);
    }

    chip_metadata m6803::metadata() const noexcept {
        return {.manufacturer = "Motorola",
                .part_number = "MC6803",
                .family = "M6800",
                .klass = chip_class::cpu,
                .revision = 1U};
    }

    void m6803::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void m6803::reset(reset_kind /*kind*/) {
        regs_ = {};
        regs_.sp = 0x00FFU;
        regs_.ccr = flag_i;
        regs_.pc = read16_be(reset_vector);
        elapsed_ = 0U;
        cycle_debt_ = 0;
    }

    void m6803::save_state(state_writer& writer) const {
        writer.u32(state_version);
        writer.u8(regs_.a);
        writer.u8(regs_.b);
        writer.u16(regs_.x);
        writer.u16(regs_.sp);
        writer.u16(regs_.pc);
        writer.u8(regs_.ccr);
        writer.u64(elapsed_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.boolean(irq_line_);
        writer.boolean(nmi_line_);
        writer.boolean(reset_line_);
    }

    void m6803::load_state(state_reader& reader) {
        if (reader.u32() != state_version) {
            reader.fail();
            return;
        }
        registers next{};
        next.a = reader.u8();
        next.b = reader.u8();
        next.x = reader.u16();
        next.sp = reader.u16();
        next.pc = reader.u16();
        next.ccr = static_cast<std::uint8_t>(reader.u8() & 0x3FU);
        const std::uint64_t elapsed = reader.u64();
        const auto debt = static_cast<std::int64_t>(reader.u64());
        const bool irq = reader.boolean();
        const bool nmi = reader.boolean();
        const bool reset_line = reader.boolean();
        if (reader.ok()) {
            regs_ = next;
            elapsed_ = elapsed;
            cycle_debt_ = debt;
            irq_line_ = irq;
            nmi_line_ = nmi;
            reset_line_ = reset_line;
        }
    }

    void m6803::set_registers(const registers& values) noexcept {
        regs_ = values;
        regs_.ccr &= 0x3FU;
        cycle_debt_ = 0;
    }

    std::uint8_t m6803::read8(std::uint16_t address) noexcept {
        return bus_ != nullptr ? bus_->read8(address) : 0xFFU;
    }

    void m6803::write8(std::uint16_t address, std::uint8_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(address, value);
        }
    }

    std::uint16_t m6803::read16_be(std::uint16_t address) noexcept {
        const std::uint8_t high = read8(address);
        const std::uint8_t low = read8(static_cast<std::uint16_t>(address + 1U));
        return make16(high, low);
    }

    void m6803::write16_be(std::uint16_t address, std::uint16_t value) noexcept {
        write8(address, high8(value));
        write8(static_cast<std::uint16_t>(address + 1U), low8(value));
    }

    std::uint8_t m6803::fetch8() noexcept {
        const std::uint8_t value =
            bus_ != nullptr ? bus_->fetch_opcode8(regs_.pc) : std::uint8_t{0xFFU};
        regs_.pc = static_cast<std::uint16_t>(regs_.pc + 1U);
        return value;
    }

    std::uint16_t m6803::fetch16() noexcept {
        const std::uint8_t high = fetch8();
        const std::uint8_t low = fetch8();
        return make16(high, low);
    }

    void m6803::push8(std::uint8_t value) noexcept {
        write8(regs_.sp, value);
        regs_.sp = static_cast<std::uint16_t>(regs_.sp - 1U);
    }

    std::uint8_t m6803::pull8() noexcept {
        regs_.sp = static_cast<std::uint16_t>(regs_.sp + 1U);
        return read8(regs_.sp);
    }

    void m6803::push16(std::uint16_t value) noexcept {
        push8(low8(value));
        push8(high8(value));
    }

    std::uint16_t m6803::pull16() noexcept {
        const std::uint8_t high = pull8();
        const std::uint8_t low = pull8();
        return make16(high, low);
    }

    void m6803::set_flag(std::uint8_t mask, bool enabled) noexcept {
        if (enabled) {
            regs_.ccr = static_cast<std::uint8_t>(regs_.ccr | mask);
        } else {
            regs_.ccr = static_cast<std::uint8_t>(regs_.ccr & ~mask);
        }
    }

    void m6803::set_nz8(std::uint8_t value) noexcept {
        set_flag(flag_n, (value & 0x80U) != 0U);
        set_flag(flag_z, value == 0U);
    }

    void m6803::set_nz16(std::uint16_t value) noexcept {
        set_flag(flag_n, (value & 0x8000U) != 0U);
        set_flag(flag_z, value == 0U);
    }

    void m6803::set_load8_flags(std::uint8_t value) noexcept {
        set_nz8(value);
        set_flag(flag_v, false);
    }

    void m6803::set_load16_flags(std::uint16_t value) noexcept {
        set_nz16(value);
        set_flag(flag_v, false);
    }

    void m6803::set_store8_flags(std::uint8_t value) noexcept { set_load8_flags(value); }

    std::uint8_t m6803::add8(std::uint8_t lhs, std::uint8_t rhs, std::uint8_t carry) noexcept {
        const unsigned result = static_cast<unsigned>(lhs) + rhs + carry;
        const auto out = static_cast<std::uint8_t>(result);
        set_nz8(out);
        set_flag(flag_h, ((lhs & 0x0FU) + (rhs & 0x0FU) + carry) > 0x0FU);
        set_flag(flag_v, ((~(lhs ^ rhs) & (lhs ^ out)) & 0x80U) != 0U);
        set_flag(flag_c, result > 0xFFU);
        return out;
    }

    std::uint8_t m6803::sub8(std::uint8_t lhs, std::uint8_t rhs, std::uint8_t borrow) noexcept {
        const unsigned subtrahend = static_cast<unsigned>(rhs) + borrow;
        const auto out = static_cast<std::uint8_t>(static_cast<unsigned>(lhs) - subtrahend);
        set_nz8(out);
        set_flag(flag_h, (lhs & 0x0FU) < ((rhs & 0x0FU) + borrow));
        set_flag(flag_v, (((lhs ^ rhs) & (lhs ^ out)) & 0x80U) != 0U);
        set_flag(flag_c, static_cast<unsigned>(lhs) < subtrahend);
        return out;
    }

    std::uint16_t m6803::add16(std::uint16_t lhs, std::uint16_t rhs) noexcept {
        const std::uint32_t result = static_cast<std::uint32_t>(lhs) + rhs;
        const auto out = static_cast<std::uint16_t>(result);
        set_nz16(out);
        set_flag(flag_v, ((~(lhs ^ rhs) & (lhs ^ out)) & 0x8000U) != 0U);
        set_flag(flag_c, result > 0xFFFFU);
        return out;
    }

    std::uint16_t m6803::sub16(std::uint16_t lhs, std::uint16_t rhs) noexcept {
        const auto out = static_cast<std::uint16_t>(lhs - rhs);
        set_nz16(out);
        set_flag(flag_v, (((lhs ^ rhs) & (lhs ^ out)) & 0x8000U) != 0U);
        set_flag(flag_c, lhs < rhs);
        return out;
    }

    std::uint8_t m6803::inc8(std::uint8_t value) noexcept {
        const auto out = static_cast<std::uint8_t>(value + 1U);
        set_nz8(out);
        set_flag(flag_v, value == 0x7FU);
        return out;
    }

    std::uint8_t m6803::dec8(std::uint8_t value) noexcept {
        const auto out = static_cast<std::uint8_t>(value - 1U);
        set_nz8(out);
        set_flag(flag_v, value == 0x80U);
        return out;
    }

    std::uint16_t m6803::direct_address() noexcept { return fetch8(); }

    std::uint16_t m6803::indexed_address() noexcept {
        return static_cast<std::uint16_t>(regs_.x + fetch8());
    }

    std::uint16_t m6803::extended_address() noexcept { return fetch16(); }

    bool m6803::branch_condition(std::uint8_t opcode) const noexcept {
        switch (opcode) {
        case 0x20U: // BRA
            return true;
        case 0x22U: // BHI
            return !flag(flag_c) && !flag(flag_z);
        case 0x23U: // BLS
            return flag(flag_c) || flag(flag_z);
        case 0x24U: // BCC/BHS
            return !flag(flag_c);
        case 0x25U: // BCS/BLO
            return flag(flag_c);
        case 0x26U: // BNE
            return !flag(flag_z);
        case 0x27U: // BEQ
            return flag(flag_z);
        case 0x28U: // BVC
            return !flag(flag_v);
        case 0x29U: // BVS
            return flag(flag_v);
        case 0x2AU: // BPL
            return !flag(flag_n);
        case 0x2BU: // BMI
            return flag(flag_n);
        case 0x2CU: // BGE
            return flag(flag_n) == flag(flag_v);
        case 0x2DU: // BLT
            return flag(flag_n) != flag(flag_v);
        case 0x2EU: // BGT
            return !flag(flag_z) && (flag(flag_n) == flag(flag_v));
        case 0x2FU: // BLE
            return flag(flag_z) || (flag(flag_n) != flag(flag_v));
        default:
            return false;
        }
    }

    void m6803::branch_if(bool condition) noexcept {
        const auto displacement = static_cast<std::int8_t>(fetch8());
        if (condition) {
            regs_.pc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(regs_.pc) +
                                                  static_cast<std::int32_t>(displacement));
        }
    }

    void m6803::exec_accumulator(std::uint8_t opcode) {
        std::uint8_t* target = nullptr;
        switch (opcode & 0xF0U) {
        case 0x40U:
            target = &regs_.a;
            break;
        case 0x50U:
            target = &regs_.b;
            break;
        default:
            return;
        }

        switch (opcode & 0x0FU) {
        case 0x00U: { // NEG
            *target = sub8(0U, *target, 0U);
            break;
        }
        case 0x03U: // COM
            *target = static_cast<std::uint8_t>(~*target);
            set_nz8(*target);
            set_flag(flag_v, false);
            set_flag(flag_c, true);
            break;
        case 0x04U: // LSR
            set_flag(flag_c, (*target & 0x01U) != 0U);
            *target = static_cast<std::uint8_t>(*target >> 1U);
            set_nz8(*target);
            set_flag(flag_n, false);
            set_flag(flag_v, flag(flag_c));
            break;
        case 0x06U: { // ROR
            const bool old_c = flag(flag_c);
            set_flag(flag_c, (*target & 0x01U) != 0U);
            *target = static_cast<std::uint8_t>((*target >> 1U) | (old_c ? 0x80U : 0U));
            set_nz8(*target);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x07U: // ASR
            set_flag(flag_c, (*target & 0x01U) != 0U);
            *target = static_cast<std::uint8_t>((*target >> 1U) | (*target & 0x80U));
            set_nz8(*target);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        case 0x08U: { // ASL/LSL
            const std::uint8_t old = *target;
            *target = static_cast<std::uint8_t>(old << 1U);
            set_nz8(*target);
            set_flag(flag_c, (old & 0x80U) != 0U);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x09U: { // ROL
            const bool old_c = flag(flag_c);
            const std::uint8_t old = *target;
            *target = static_cast<std::uint8_t>((old << 1U) | (old_c ? 1U : 0U));
            set_nz8(*target);
            set_flag(flag_c, (old & 0x80U) != 0U);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x0AU: // DEC
            *target = dec8(*target);
            break;
        case 0x0CU: // INC
            *target = inc8(*target);
            break;
        case 0x0DU: // TST
            set_nz8(*target);
            set_flag(flag_v, false);
            set_flag(flag_c, false);
            break;
        case 0x0FU: // CLR
            *target = 0U;
            set_nz8(0U);
            set_flag(flag_v, false);
            set_flag(flag_c, false);
            break;
        default:
            break;
        }
    }

    void m6803::exec_memory_mutate(std::uint8_t opcode, std::uint16_t address) {
        std::uint8_t value = read8(address);
        switch (opcode & 0x0FU) {
        case 0x00U:
            value = sub8(0U, value, 0U);
            break;
        case 0x03U:
            value = static_cast<std::uint8_t>(~value);
            set_nz8(value);
            set_flag(flag_v, false);
            set_flag(flag_c, true);
            break;
        case 0x04U:
            set_flag(flag_c, (value & 0x01U) != 0U);
            value = static_cast<std::uint8_t>(value >> 1U);
            set_nz8(value);
            set_flag(flag_n, false);
            set_flag(flag_v, flag(flag_c));
            break;
        case 0x06U: {
            const bool old_c = flag(flag_c);
            set_flag(flag_c, (value & 0x01U) != 0U);
            value = static_cast<std::uint8_t>((value >> 1U) | (old_c ? 0x80U : 0U));
            set_nz8(value);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x07U:
            set_flag(flag_c, (value & 0x01U) != 0U);
            value = static_cast<std::uint8_t>((value >> 1U) | (value & 0x80U));
            set_nz8(value);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        case 0x08U: {
            const std::uint8_t old = value;
            value = static_cast<std::uint8_t>(old << 1U);
            set_nz8(value);
            set_flag(flag_c, (old & 0x80U) != 0U);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x09U: {
            const bool old_c = flag(flag_c);
            const std::uint8_t old = value;
            value = static_cast<std::uint8_t>((old << 1U) | (old_c ? 1U : 0U));
            set_nz8(value);
            set_flag(flag_c, (old & 0x80U) != 0U);
            set_flag(flag_v, flag(flag_n) != flag(flag_c));
            break;
        }
        case 0x0AU:
            value = dec8(value);
            break;
        case 0x0CU:
            value = inc8(value);
            break;
        case 0x0DU:
            set_nz8(value);
            set_flag(flag_v, false);
            set_flag(flag_c, false);
            return;
        case 0x0EU:
            regs_.pc = address;
            return;
        case 0x0FU:
            value = 0U;
            set_nz8(value);
            set_flag(flag_v, false);
            set_flag(flag_c, false);
            break;
        default:
            return;
        }
        write8(address, value);
    }

    void m6803::exec_group(std::uint8_t opcode, std::uint16_t address, bool memory_operand) {
        std::uint8_t* accumulator = (opcode & 0x40U) == 0U ? &regs_.a : &regs_.b;
        std::uint8_t operand = memory_operand ? read8(address) : fetch8();
        switch (static_cast<alu_op>(opcode & 0x0FU)) {
        case alu_op::sub:
            *accumulator = sub8(*accumulator, operand, 0U);
            break;
        case alu_op::cmp:
            static_cast<void>(sub8(*accumulator, operand, 0U));
            break;
        case alu_op::sbc:
            *accumulator = sub8(*accumulator, operand, flag(flag_c) ? 1U : 0U);
            break;
        case alu_op::and_:
            *accumulator = static_cast<std::uint8_t>(*accumulator & operand);
            set_load8_flags(*accumulator);
            break;
        case alu_op::bit:
            set_load8_flags(static_cast<std::uint8_t>(*accumulator & operand));
            break;
        case alu_op::load:
            *accumulator = operand;
            set_load8_flags(*accumulator);
            break;
        case alu_op::store:
            write8(address, *accumulator);
            set_store8_flags(*accumulator);
            break;
        case alu_op::eor:
            *accumulator = static_cast<std::uint8_t>(*accumulator ^ operand);
            set_load8_flags(*accumulator);
            break;
        case alu_op::adc:
            *accumulator = add8(*accumulator, operand, flag(flag_c) ? 1U : 0U);
            break;
        case alu_op::or_:
            *accumulator = static_cast<std::uint8_t>(*accumulator | operand);
            set_load8_flags(*accumulator);
            break;
        case alu_op::add:
            *accumulator = add8(*accumulator, operand, 0U);
            break;
        default:
            break;
        }
    }

    void m6803::exec_immediate(std::uint8_t opcode) {
        switch (opcode) {
        case 0x8CU: { // CPX #imm16
            const std::uint16_t operand = fetch16();
            static_cast<void>(sub16(regs_.x, operand));
            break;
        }
        case 0x8DU: // BSR
            branch_if(true);
            push16(static_cast<std::uint16_t>(regs_.pc));
            break;
        case 0x8EU: // LDS #imm16
            regs_.sp = fetch16();
            set_load16_flags(regs_.sp);
            break;
        case 0xCCU: { // LDD #imm16
            const std::uint16_t value = fetch16();
            regs_.a = high8(value);
            regs_.b = low8(value);
            set_load16_flags(value);
            break;
        }
        case 0xCEU: // LDX #imm16
            regs_.x = fetch16();
            set_load16_flags(regs_.x);
            break;
        default:
            exec_group(opcode, 0U, false);
            break;
        }
    }

    void m6803::exec_single(std::uint8_t opcode) {
        switch (opcode) {
        case 0x01U: // NOP
            break;
        case 0x06U: // TAP
            regs_.ccr = static_cast<std::uint8_t>(regs_.a & 0x3FU);
            break;
        case 0x07U: // TPA
            regs_.a = regs_.ccr;
            break;
        case 0x08U: // INX
            regs_.x = static_cast<std::uint16_t>(regs_.x + 1U);
            set_flag(flag_z, regs_.x == 0U);
            break;
        case 0x09U: // DEX
            regs_.x = static_cast<std::uint16_t>(regs_.x - 1U);
            set_flag(flag_z, regs_.x == 0U);
            break;
        case 0x0AU:
            set_flag(flag_v, false);
            break;
        case 0x0BU:
            set_flag(flag_v, true);
            break;
        case 0x0CU:
            set_flag(flag_c, false);
            break;
        case 0x0DU:
            set_flag(flag_c, true);
            break;
        case 0x0EU:
            set_flag(flag_i, false);
            break;
        case 0x0FU:
            set_flag(flag_i, true);
            break;
        case 0x16U: // TAB
            regs_.b = regs_.a;
            set_load8_flags(regs_.b);
            break;
        case 0x17U: // TBA
            regs_.a = regs_.b;
            set_load8_flags(regs_.a);
            break;
        case 0x1BU: // ABA
            regs_.a = add8(regs_.a, regs_.b, 0U);
            break;
        case 0x30U: // TSX
            regs_.x = static_cast<std::uint16_t>(regs_.sp + 1U);
            break;
        case 0x31U: // INS
            regs_.sp = static_cast<std::uint16_t>(regs_.sp + 1U);
            break;
        case 0x32U: // PULA
            regs_.a = pull8();
            break;
        case 0x33U: // PULB
            regs_.b = pull8();
            break;
        case 0x34U: // DES
            regs_.sp = static_cast<std::uint16_t>(regs_.sp - 1U);
            break;
        case 0x35U: // TXS
            regs_.sp = static_cast<std::uint16_t>(regs_.x - 1U);
            break;
        case 0x36U: // PSHA
            push8(regs_.a);
            break;
        case 0x37U: // PSHB
            push8(regs_.b);
            break;
        case 0x39U: // RTS
            regs_.pc = pull16();
            break;
        default:
            break;
        }
    }

    int m6803::step_instruction() {
        if (reset_line_) {
            elapsed_ += 4U;
            return 4;
        }
        if (trace_callback_) {
            trace_callback_(regs_.pc);
        }
        const std::uint16_t before = regs_.pc;
        const std::uint8_t opcode = fetch8();

        if ((opcode >= 0x20U && opcode <= 0x2FU) || opcode == 0x8DU) {
            if (opcode == 0x8DU) {
                const std::int8_t displacement = static_cast<std::int8_t>(fetch8());
                push16(regs_.pc);
                regs_.pc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(regs_.pc) +
                                                      static_cast<std::int32_t>(displacement));
            } else {
                branch_if(branch_condition(opcode));
            }
        } else if ((opcode >= 0x40U && opcode <= 0x5FU)) {
            exec_accumulator(opcode);
        } else if ((opcode >= 0x60U && opcode <= 0x6FU)) {
            exec_memory_mutate(opcode, indexed_address());
        } else if ((opcode >= 0x70U && opcode <= 0x7FU)) {
            exec_memory_mutate(opcode, extended_address());
        } else if ((opcode >= 0x80U && opcode <= 0x8FU) || (opcode >= 0xC0U && opcode <= 0xCFU)) {
            exec_immediate(opcode);
        } else if ((opcode >= 0x90U && opcode <= 0x9FU) || (opcode >= 0xD0U && opcode <= 0xDFU)) {
            const std::uint16_t address = direct_address();
            if (opcode == 0x9DU) {
                push16(regs_.pc);
                regs_.pc = address;
            } else if (opcode == 0x9CU) {
                static_cast<void>(sub16(regs_.x, read16_be(address)));
            } else if (opcode == 0x9EU) {
                regs_.sp = read16_be(address);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0x9FU) {
                write16_be(address, regs_.sp);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0xDCU) {
                const std::uint16_t value = read16_be(address);
                regs_.a = high8(value);
                regs_.b = low8(value);
                set_load16_flags(value);
            } else if (opcode == 0xDDU) {
                write16_be(address, make16(regs_.a, regs_.b));
                set_load16_flags(make16(regs_.a, regs_.b));
            } else if (opcode == 0xDEU) {
                regs_.x = read16_be(address);
                set_load16_flags(regs_.x);
            } else if (opcode == 0xDFU) {
                write16_be(address, regs_.x);
                set_load16_flags(regs_.x);
            } else {
                exec_group(opcode, address, true);
            }
        } else if ((opcode >= 0xA0U && opcode <= 0xAFU) || (opcode >= 0xE0U && opcode <= 0xEFU)) {
            const std::uint16_t address = indexed_address();
            if (opcode == 0xADU) {
                push16(regs_.pc);
                regs_.pc = address;
            } else if (opcode == 0xACU) {
                static_cast<void>(sub16(regs_.x, read16_be(address)));
            } else if (opcode == 0xAEU) {
                regs_.sp = read16_be(address);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0xAFU) {
                write16_be(address, regs_.sp);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0xECU) {
                const std::uint16_t value = read16_be(address);
                regs_.a = high8(value);
                regs_.b = low8(value);
                set_load16_flags(value);
            } else if (opcode == 0xEDU) {
                write16_be(address, make16(regs_.a, regs_.b));
                set_load16_flags(make16(regs_.a, regs_.b));
            } else if (opcode == 0xEEU) {
                regs_.x = read16_be(address);
                set_load16_flags(regs_.x);
            } else if (opcode == 0xEFU) {
                write16_be(address, regs_.x);
                set_load16_flags(regs_.x);
            } else {
                exec_group(opcode, address, true);
            }
        } else if ((opcode >= 0xB0U && opcode <= 0xBFU) || opcode >= 0xF0U) {
            const std::uint16_t address = extended_address();
            if (opcode == 0xBDU) {
                push16(regs_.pc);
                regs_.pc = address;
            } else if (opcode == 0xBCU) {
                static_cast<void>(sub16(regs_.x, read16_be(address)));
            } else if (opcode == 0xBEU) {
                regs_.sp = read16_be(address);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0xBFU) {
                write16_be(address, regs_.sp);
                set_load16_flags(regs_.sp);
            } else if (opcode == 0xFCU) {
                const std::uint16_t value = read16_be(address);
                regs_.a = high8(value);
                regs_.b = low8(value);
                set_load16_flags(value);
            } else if (opcode == 0xFDU) {
                write16_be(address, make16(regs_.a, regs_.b));
                set_load16_flags(make16(regs_.a, regs_.b));
            } else if (opcode == 0xFEU) {
                regs_.x = read16_be(address);
                set_load16_flags(regs_.x);
            } else if (opcode == 0xFFU) {
                write16_be(address, regs_.x);
                set_load16_flags(regs_.x);
            } else {
                exec_group(opcode, address, true);
            }
        } else {
            exec_single(opcode);
        }

        elapsed_ += 4U;
        (void)before;
        return 4;
    }

    void m6803::set_reset_line(bool asserted) noexcept {
        if (asserted && !reset_line_) {
            const std::int64_t debt = cycle_debt_;
            const std::uint64_t elapsed = elapsed_;
            reset(reset_kind::soft);
            cycle_debt_ = debt;
            elapsed_ = elapsed;
        }
        reset_line_ = asserted;
    }

    std::span<const register_descriptor> m6803::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"A", regs_.a, 8U, fmt::unsigned_integer};
        register_view_[1] = {"B", regs_.b, 8U, fmt::unsigned_integer};
        register_view_[2] = {"X", regs_.x, 16U, fmt::unsigned_integer};
        register_view_[3] = {"SP", regs_.sp, 16U, fmt::unsigned_integer};
        register_view_[4] = {"PC", regs_.pc, 16U, fmt::unsigned_integer};
        register_view_[5] = {"CCR", regs_.ccr, 8U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto m6803_registration =
            register_factory("motorola.m6803", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<m6803>(); });
    } // namespace

} // namespace mnemos::chips::cpu
