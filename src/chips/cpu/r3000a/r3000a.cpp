#include "r3000a.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace mnemos::chips::cpu {

    namespace {
        [[nodiscard]] constexpr std::uint8_t rs(std::uint32_t op) noexcept {
            return static_cast<std::uint8_t>((op >> 21U) & 31U);
        }
        [[nodiscard]] constexpr std::uint8_t rt(std::uint32_t op) noexcept {
            return static_cast<std::uint8_t>((op >> 16U) & 31U);
        }
        [[nodiscard]] constexpr std::uint8_t rd(std::uint32_t op) noexcept {
            return static_cast<std::uint8_t>((op >> 11U) & 31U);
        }
        [[nodiscard]] constexpr std::uint8_t shamt(std::uint32_t op) noexcept {
            return static_cast<std::uint8_t>((op >> 6U) & 31U);
        }
        [[nodiscard]] constexpr std::uint16_t imm16(std::uint32_t op) noexcept {
            return static_cast<std::uint16_t>(op);
        }
        [[nodiscard]] constexpr std::int32_t simm16(std::uint32_t op) noexcept {
            return static_cast<std::int16_t>(imm16(op));
        }
        [[nodiscard]] constexpr bool add_overflows(std::uint32_t a, std::uint32_t b,
                                                   std::uint32_t r) noexcept {
            return ((~(a ^ b) & (a ^ r)) & 0x80000000U) != 0U;
        }
        [[nodiscard]] constexpr bool sub_overflows(std::uint32_t a, std::uint32_t b,
                                                   std::uint32_t r) noexcept {
            return (((a ^ b) & (a ^ r)) & 0x80000000U) != 0U;
        }
    } // namespace

    chip_metadata r3000a::metadata() const noexcept {
        return {
            .manufacturer = "Sony",
            .part_number = "R3000A",
            .family = "MIPS-I",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void r3000a::reset(reset_kind /*kind*/) {
        r_.fill(0U);
        pc_ = reset_vector;
        hi_ = 0U;
        lo_ = 0U;
        cop0_.fill(0U);
        cop0_[cop0_status] = status_bev;
        cop0_[cop0_prid] = 0x00000002U;
        cop2_data_.fill(0U);
        cop2_control_.fill(0U);
        cop2_command_ = 0U;
        branch_pending_ = false;
        branch_target_ = 0U;
        delayed_load_reg_ = -1;
        delayed_load_value_ = 0U;
        next_load_reg_ = -1;
        next_load_value_ = 0U;
        exception_taken_ = false;
        last_exception_ = exception_code::interrupt;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    void r3000a::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void r3000a::set_external_interrupt_line(bool asserted) noexcept {
        if (asserted) {
            cop0_[cop0_cause] |= cause_external_irq2_pending;
        } else {
            cop0_[cop0_cause] &= ~cause_external_irq2_pending;
        }
    }

    bool r3000a::external_interrupt_line() const noexcept {
        return (cop0_[cop0_cause] & cause_external_irq2_pending) != 0U;
    }

    std::uint8_t r3000a::rb(std::uint32_t address) noexcept {
        return bus_ != nullptr ? bus_->read8(physical_address(address)) : 0xFFU;
    }

    void r3000a::wb(std::uint32_t address, std::uint8_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(physical_address(address), value);
        }
    }

    std::uint16_t r3000a::rh(std::uint32_t address) noexcept {
        const std::uint32_t physical = physical_address(address);
        if (bus_ != nullptr) {
            return bus_->read16_le(physical);
        }
        return 0xFFFFU;
    }

    void r3000a::wh(std::uint32_t address, std::uint16_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write16_le(physical_address(address), value);
        }
    }

    std::uint32_t r3000a::rw(std::uint32_t address) noexcept {
        const std::uint32_t physical = physical_address(address);
        if (bus_ == nullptr) {
            return 0xFFFFFFFFU;
        }
        const std::uint32_t lo = bus_->read16_le(physical);
        const std::uint32_t hi = bus_->read16_le(physical + 2U);
        return lo | (hi << 16U);
    }

    void r3000a::ww(std::uint32_t address, std::uint32_t value) noexcept {
        if (bus_ != nullptr) {
            const std::uint32_t physical = physical_address(address);
            bus_->write16_le(physical, static_cast<std::uint16_t>(value));
            bus_->write16_le(physical + 2U, static_cast<std::uint16_t>(value >> 16U));
        }
    }

    std::uint32_t r3000a::fetch32(std::uint32_t address) noexcept { return rw(address); }

    void r3000a::write_reg(std::uint8_t reg, std::uint32_t value) noexcept {
        if ((reg & 31U) != 0U) {
            r_[reg & 31U] = value;
        }
    }

    void r3000a::schedule_load(std::uint8_t reg, std::uint32_t value) noexcept {
        if ((reg & 31U) == 0U) {
            return;
        }
        next_load_reg_ = static_cast<std::int32_t>(reg & 31U);
        next_load_value_ = value;
    }

    void r3000a::schedule_branch(std::uint32_t target) noexcept {
        branch_pending_ = true;
        branch_target_ = target;
    }

    bool r3000a::interrupt_pending() const noexcept {
        const std::uint32_t status = cop0_[cop0_status];
        return (status & status_interrupt_enable) != 0U &&
               ((status & cop0_[cop0_cause] & cause_interrupt_pending_mask) != 0U);
    }

    void r3000a::raise_exception(exception_code code, std::uint32_t fault_pc, bool delay_slot,
                                 std::uint32_t badvaddr) noexcept {
        last_exception_ = code;
        exception_taken_ = true;
        if (badvaddr != 0U) {
            cop0_[cop0_badvaddr] = badvaddr;
        }

        const std::uint32_t epc = delay_slot ? fault_pc - 4U : fault_pc;
        cop0_[cop0_epc] = epc;
        const std::uint32_t pending = cop0_[cop0_cause] & cause_interrupt_pending_mask;
        cop0_[cop0_cause] = pending | (delay_slot ? cause_bd : 0U) |
                            ((static_cast<std::uint32_t>(code) & 31U) << 2U);

        // Push KU/IE current/previous/old mode bits on exception entry.
        const std::uint32_t status = cop0_[cop0_status];
        cop0_[cop0_status] = (status & ~0x3FU) | ((status << 2U) & 0x3FU);
        pc_ = (cop0_[cop0_status] & status_bev) != 0U ? boot_exception_vector : exception_vector;
        branch_pending_ = false;
        next_load_reg_ = -1;
    }

    void r3000a::exec_special(std::uint32_t op, std::uint32_t inst_pc,
                              bool delay_slot) noexcept {
        const std::uint8_t s = rs(op);
        const std::uint8_t t = rt(op);
        const std::uint8_t d = rd(op);
        const std::uint8_t sh = shamt(op);
        const std::uint8_t fn = static_cast<std::uint8_t>(op & 0x3FU);

        switch (fn) {
        case 0x00U: // SLL
            write_reg(d, r_[t] << sh);
            return;
        case 0x02U: // SRL
            write_reg(d, r_[t] >> sh);
            return;
        case 0x03U: // SRA
            write_reg(d, static_cast<std::uint32_t>(static_cast<std::int32_t>(r_[t]) >> sh));
            return;
        case 0x04U: // SLLV
            write_reg(d, r_[t] << (r_[s] & 31U));
            return;
        case 0x06U: // SRLV
            write_reg(d, r_[t] >> (r_[s] & 31U));
            return;
        case 0x07U: // SRAV
            write_reg(d,
                      static_cast<std::uint32_t>(static_cast<std::int32_t>(r_[t]) >>
                                                 (r_[s] & 31U)));
            return;
        case 0x08U: // JR
            schedule_branch(r_[s]);
            return;
        case 0x09U: // JALR
            write_reg(d == 0U ? 31U : d, inst_pc + 8U);
            schedule_branch(r_[s]);
            return;
        case 0x0CU: // SYSCALL
            raise_exception(exception_code::syscall, inst_pc, delay_slot);
            return;
        case 0x0DU: // BREAK
            raise_exception(exception_code::breakpoint, inst_pc, delay_slot);
            return;
        case 0x10U: // MFHI
            write_reg(d, hi_);
            return;
        case 0x11U: // MTHI
            hi_ = r_[s];
            return;
        case 0x12U: // MFLO
            write_reg(d, lo_);
            return;
        case 0x13U: // MTLO
            lo_ = r_[s];
            return;
        case 0x18U: { // MULT
            const auto lhs = static_cast<std::int64_t>(static_cast<std::int32_t>(r_[s]));
            const auto rhs = static_cast<std::int64_t>(static_cast<std::int32_t>(r_[t]));
            const auto product = static_cast<std::uint64_t>(lhs * rhs);
            lo_ = static_cast<std::uint32_t>(product);
            hi_ = static_cast<std::uint32_t>(product >> 32U);
            step_cycles_ = 12;
            return;
        }
        case 0x19U: { // MULTU
            const std::uint64_t product = static_cast<std::uint64_t>(r_[s]) * r_[t];
            lo_ = static_cast<std::uint32_t>(product);
            hi_ = static_cast<std::uint32_t>(product >> 32U);
            step_cycles_ = 12;
            return;
        }
        case 0x1AU: { // DIV
            const auto lhs = static_cast<std::int32_t>(r_[s]);
            const auto rhs = static_cast<std::int32_t>(r_[t]);
            if (rhs != 0) {
                lo_ = static_cast<std::uint32_t>(lhs / rhs);
                hi_ = static_cast<std::uint32_t>(lhs % rhs);
            }
            step_cycles_ = 36;
            return;
        }
        case 0x1BU: { // DIVU
            if (r_[t] != 0U) {
                lo_ = r_[s] / r_[t];
                hi_ = r_[s] % r_[t];
            }
            step_cycles_ = 36;
            return;
        }
        case 0x20U: { // ADD
            const std::uint32_t result = r_[s] + r_[t];
            if (add_overflows(r_[s], r_[t], result)) {
                raise_exception(exception_code::arithmetic_overflow, inst_pc, delay_slot);
                return;
            }
            write_reg(d, result);
            return;
        }
        case 0x21U: // ADDU
            write_reg(d, r_[s] + r_[t]);
            return;
        case 0x22U: { // SUB
            const std::uint32_t result = r_[s] - r_[t];
            if (sub_overflows(r_[s], r_[t], result)) {
                raise_exception(exception_code::arithmetic_overflow, inst_pc, delay_slot);
                return;
            }
            write_reg(d, result);
            return;
        }
        case 0x23U: // SUBU
            write_reg(d, r_[s] - r_[t]);
            return;
        case 0x24U: // AND
            write_reg(d, r_[s] & r_[t]);
            return;
        case 0x25U: // OR
            write_reg(d, r_[s] | r_[t]);
            return;
        case 0x26U: // XOR
            write_reg(d, r_[s] ^ r_[t]);
            return;
        case 0x27U: // NOR
            write_reg(d, ~(r_[s] | r_[t]));
            return;
        case 0x2AU: // SLT
            write_reg(d, static_cast<std::int32_t>(r_[s]) < static_cast<std::int32_t>(r_[t]) ? 1U
                                                                                              : 0U);
            return;
        case 0x2BU: // SLTU
            write_reg(d, r_[s] < r_[t] ? 1U : 0U);
            return;
        default:
            raise_exception(exception_code::reserved_instruction, inst_pc, delay_slot);
            return;
        }
    }

    void r3000a::exec_regimm(std::uint32_t op, std::uint32_t inst_pc) noexcept {
        const std::uint8_t s = rs(op);
        const std::uint8_t t = rt(op);
        const std::uint32_t target = pc_ + (static_cast<std::uint32_t>(simm16(op)) << 2U);
        const bool signed_negative = static_cast<std::int32_t>(r_[s]) < 0;
        bool take = false;

        switch (t) {
        case 0x00U: // BLTZ
            take = signed_negative;
            break;
        case 0x01U: // BGEZ
            take = !signed_negative;
            break;
        case 0x10U: // BLTZAL
            write_reg(31U, inst_pc + 8U);
            take = signed_negative;
            break;
        case 0x11U: // BGEZAL
            write_reg(31U, inst_pc + 8U);
            take = !signed_negative;
            break;
        default:
            raise_exception(exception_code::reserved_instruction, inst_pc, false);
            return;
        }

        if (take) {
            schedule_branch(target);
        }
    }

    void r3000a::exec_cop0(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept {
        const std::uint8_t cop_rs = rs(op);
        const std::uint8_t t = rt(op);
        const std::uint8_t d = rd(op);

        switch (cop_rs) {
        case 0x00U: // MFC0
            schedule_load(t, cop0_[d]);
            return;
        case 0x04U: // MTC0
            cop0_[d] = r_[t];
            r_[0] = 0U;
            return;
        case 0x10U: // COP0 operation
            if ((op & 0x3FU) == 0x10U) { // RFE
                cop0_[cop0_status] = (cop0_[cop0_status] & ~0x0FU) |
                                     ((cop0_[cop0_status] >> 2U) & 0x0FU);
                return;
            }
            break;
        default:
            break;
        }

        raise_exception(exception_code::reserved_instruction, inst_pc, delay_slot);
    }

    void r3000a::exec_cop2(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept {
        const std::uint8_t cop_rs = rs(op);
        const std::uint8_t t = rt(op);
        const std::uint8_t d = rd(op);

        switch (cop_rs) {
        case 0x00U: // MFC2
            schedule_load(t, cop2_data_[d]);
            return;
        case 0x02U: // CFC2
            schedule_load(t, cop2_control_[d]);
            return;
        case 0x04U: // MTC2
            cop2_data_[d] = r_[t];
            r_[0] = 0U;
            return;
        case 0x06U: // CTC2
            cop2_control_[d] = r_[t];
            r_[0] = 0U;
            return;
        default:
            if ((cop_rs & 0x10U) != 0U) {
                cop2_command_ = op & 0x01FFFFFFU;
                return;
            }
            break;
        }

        raise_exception(exception_code::reserved_instruction, inst_pc, delay_slot);
    }

    void r3000a::exec_one(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept {
        const std::uint8_t opcode = static_cast<std::uint8_t>(op >> 26U);
        const std::uint8_t s = rs(op);
        const std::uint8_t t = rt(op);

        switch (opcode) {
        case 0x00U:
            exec_special(op, inst_pc, delay_slot);
            return;
        case 0x01U:
            exec_regimm(op, inst_pc);
            return;
        case 0x02U: // J
            schedule_branch((pc_ & 0xF0000000U) | ((op & 0x03FFFFFFU) << 2U));
            return;
        case 0x03U: // JAL
            write_reg(31U, inst_pc + 8U);
            schedule_branch((pc_ & 0xF0000000U) | ((op & 0x03FFFFFFU) << 2U));
            return;
        case 0x04U: // BEQ
            if (r_[s] == r_[t]) {
                schedule_branch(pc_ + (static_cast<std::uint32_t>(simm16(op)) << 2U));
            }
            return;
        case 0x05U: // BNE
            if (r_[s] != r_[t]) {
                schedule_branch(pc_ + (static_cast<std::uint32_t>(simm16(op)) << 2U));
            }
            return;
        case 0x06U: // BLEZ
            if (static_cast<std::int32_t>(r_[s]) <= 0) {
                schedule_branch(pc_ + (static_cast<std::uint32_t>(simm16(op)) << 2U));
            }
            return;
        case 0x07U: // BGTZ
            if (static_cast<std::int32_t>(r_[s]) > 0) {
                schedule_branch(pc_ + (static_cast<std::uint32_t>(simm16(op)) << 2U));
            }
            return;
        case 0x08U: { // ADDI
            const std::uint32_t imm = static_cast<std::uint32_t>(simm16(op));
            const std::uint32_t result = r_[s] + imm;
            if (add_overflows(r_[s], imm, result)) {
                raise_exception(exception_code::arithmetic_overflow, inst_pc, delay_slot);
                return;
            }
            write_reg(t, result);
            return;
        }
        case 0x09U: // ADDIU
            write_reg(t, r_[s] + static_cast<std::uint32_t>(simm16(op)));
            return;
        case 0x0AU: // SLTI
            write_reg(t, static_cast<std::int32_t>(r_[s]) < simm16(op) ? 1U : 0U);
            return;
        case 0x0BU: // SLTIU
            write_reg(t, r_[s] < static_cast<std::uint32_t>(simm16(op)) ? 1U : 0U);
            return;
        case 0x0CU: // ANDI
            write_reg(t, r_[s] & imm16(op));
            return;
        case 0x0DU: // ORI
            write_reg(t, r_[s] | imm16(op));
            return;
        case 0x0EU: // XORI
            write_reg(t, r_[s] ^ imm16(op));
            return;
        case 0x0FU: // LUI
            write_reg(t, static_cast<std::uint32_t>(imm16(op)) << 16U);
            return;
        case 0x10U:
            exec_cop0(op, inst_pc, delay_slot);
            return;
        case 0x12U:
            exec_cop2(op, inst_pc, delay_slot);
            return;
        default:
            break;
        }

        const std::uint32_t address = r_[s] + static_cast<std::uint32_t>(simm16(op));
        switch (opcode) {
        case 0x20U: { // LB
            const auto value = static_cast<std::int8_t>(rb(address));
            schedule_load(t, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
            return;
        }
        case 0x21U: // LH
            if ((address & 1U) != 0U) {
                raise_exception(exception_code::address_load, inst_pc, delay_slot, address);
                return;
            }
            schedule_load(t, static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                 static_cast<std::int16_t>(rh(address)))));
            return;
        case 0x23U: // LW
            if ((address & 3U) != 0U) {
                raise_exception(exception_code::address_load, inst_pc, delay_slot, address);
                return;
            }
            schedule_load(t, rw(address));
            return;
        case 0x24U: // LBU
            schedule_load(t, rb(address));
            return;
        case 0x25U: // LHU
            if ((address & 1U) != 0U) {
                raise_exception(exception_code::address_load, inst_pc, delay_slot, address);
                return;
            }
            schedule_load(t, rh(address));
            return;
        case 0x28U: // SB
            wb(address, static_cast<std::uint8_t>(r_[t]));
            return;
        case 0x29U: // SH
            if ((address & 1U) != 0U) {
                raise_exception(exception_code::address_store, inst_pc, delay_slot, address);
                return;
            }
            wh(address, static_cast<std::uint16_t>(r_[t]));
            return;
        case 0x2BU: // SW
            if ((address & 3U) != 0U) {
                raise_exception(exception_code::address_store, inst_pc, delay_slot, address);
                return;
            }
            ww(address, r_[t]);
            return;
        default:
            raise_exception(exception_code::reserved_instruction, inst_pc, delay_slot);
            return;
        }
    }

    int r3000a::step_instruction() {
        step_cycles_ = 1;
        next_load_reg_ = -1;
        next_load_value_ = 0U;
        exception_taken_ = false;
        r_[0] = 0U;

        const std::uint32_t inst_pc = pc_;
        const bool was_delay_slot = branch_pending_;

        if (interrupt_pending()) {
            raise_exception(exception_code::interrupt, inst_pc, was_delay_slot);
            r_[0] = 0U;
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        const std::uint32_t pending_branch_target = branch_target_;
        branch_pending_ = false;

        const std::int32_t commit_reg = delayed_load_reg_;
        const std::uint32_t commit_value = delayed_load_value_;
        delayed_load_reg_ = -1;

        if (trace_callback_) {
            trace_callback_(inst_pc);
        }

        const std::uint32_t op = fetch32(inst_pc);
        pc_ = inst_pc + 4U;
        exec_one(op, inst_pc, was_delay_slot);

        if (commit_reg > 0) {
            write_reg(static_cast<std::uint8_t>(commit_reg), commit_value);
        }
        delayed_load_reg_ = next_load_reg_;
        delayed_load_value_ = next_load_value_;
        next_load_reg_ = -1;

        if (was_delay_slot && !exception_taken_) {
            pc_ = pending_branch_target;
            branch_pending_ = false;
        }

        r_[0] = 0U;
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    r3000a::registers r3000a::cpu_registers() const noexcept {
        return registers{
            .r = r_,
            .pc = pc_,
            .hi = hi_,
            .lo = lo_,
            .cop0 = cop0_,
            .cop2_data = cop2_data_,
            .cop2_control = cop2_control_,
            .cop2_command = cop2_command_,
            .branch_pending = branch_pending_,
            .branch_target = branch_target_,
            .delayed_load_reg = delayed_load_reg_,
            .delayed_load_value = delayed_load_value_,
            .last_exception = last_exception_,
        };
    }

    void r3000a::set_registers(const registers& values) noexcept {
        r_ = values.r;
        r_[0] = 0U;
        pc_ = values.pc;
        hi_ = values.hi;
        lo_ = values.lo;
        cop0_ = values.cop0;
        cop2_data_ = values.cop2_data;
        cop2_control_ = values.cop2_control;
        cop2_command_ = values.cop2_command;
        branch_pending_ = values.branch_pending;
        branch_target_ = values.branch_target;
        delayed_load_reg_ = values.delayed_load_reg >= 0 && values.delayed_load_reg < 32
                                ? values.delayed_load_reg
                                : -1;
        delayed_load_value_ = values.delayed_load_value;
        last_exception_ = values.last_exception;
    }

    constexpr std::uint32_t r3000a_save_state_version = 2U;

    void r3000a::save_state(state_writer& writer) const {
        writer.u32(r3000a_save_state_version);
        for (const std::uint32_t v : r_) {
            writer.u32(v);
        }
        writer.u32(pc_);
        writer.u32(hi_);
        writer.u32(lo_);
        for (const std::uint32_t v : cop0_) {
            writer.u32(v);
        }
        for (const std::uint32_t v : cop2_data_) {
            writer.u32(v);
        }
        for (const std::uint32_t v : cop2_control_) {
            writer.u32(v);
        }
        writer.u32(cop2_command_);
        writer.boolean(branch_pending_);
        writer.u32(branch_target_);
        writer.u32(delayed_load_reg_ >= 0 ? static_cast<std::uint32_t>(delayed_load_reg_)
                                           : 0xFFFFFFFFU);
        writer.u32(delayed_load_value_);
        writer.u32(static_cast<std::uint32_t>(last_exception_));
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void r3000a::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version != r3000a_save_state_version) {
            reader.fail();
            return;
        }
        for (std::uint32_t& v : r_) {
            v = reader.u32();
        }
        r_[0] = 0U;
        pc_ = reader.u32();
        hi_ = reader.u32();
        lo_ = reader.u32();
        for (std::uint32_t& v : cop0_) {
            v = reader.u32();
        }
        for (std::uint32_t& v : cop2_data_) {
            v = reader.u32();
        }
        for (std::uint32_t& v : cop2_control_) {
            v = reader.u32();
        }
        cop2_command_ = reader.u32();
        branch_pending_ = reader.boolean();
        branch_target_ = reader.u32();
        const std::uint32_t raw_delayed_load_reg = reader.u32();
        delayed_load_reg_ = raw_delayed_load_reg < 32U ? static_cast<std::int32_t>(raw_delayed_load_reg)
                                                       : -1;
        if (delayed_load_reg_ < 0 || delayed_load_reg_ >= 32) {
            delayed_load_reg_ = -1;
        }
        delayed_load_value_ = reader.u32();
        last_exception_ = static_cast<exception_code>(reader.u32());
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
        next_load_reg_ = -1;
        next_load_value_ = 0U;
        exception_taken_ = false;
    }

    instrumentation::ichip_introspection& r3000a::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> r3000a::register_snapshot() noexcept {
        using fmt = register_value_format;
        static constexpr std::array<std::string_view, 32> rn = {
            "R0",  "AT", "V0", "V1", "A0", "A1", "A2", "A3",
            "T0",  "T1", "T2", "T3", "T4", "T5", "T6", "T7",
            "S0",  "S1", "S2", "S3", "S4", "S5", "S6", "S7",
            "T8",  "T9", "K0", "K1", "GP", "SP", "FP", "RA",
        };
        for (std::size_t i = 0; i < 32; ++i) {
            register_view_[i] = {rn[i], r_[i], 32U, fmt::unsigned_integer};
        }
        register_view_[32] = {"PC", pc_, 32U, fmt::unsigned_integer};
        register_view_[33] = {"HI", hi_, 32U, fmt::unsigned_integer};
        register_view_[34] = {"LO", lo_, 32U, fmt::unsigned_integer};
        register_view_[35] = {"CP0_STATUS", cop0_[cop0_status], 32U, fmt::flags};
        register_view_[36] = {"CP0_CAUSE", cop0_[cop0_cause], 32U, fmt::flags};
        register_view_[37] = {"CP0_EPC", cop0_[cop0_epc], 32U, fmt::unsigned_integer};
        register_view_[38] = {"CP0_BADVADDR", cop0_[cop0_badvaddr], 32U, fmt::unsigned_integer};
        register_view_[39] = {"CP2_D0", cop2_data_[0], 32U, fmt::unsigned_integer};
        register_view_[40] = {"CP2_C31", cop2_control_[31], 32U, fmt::flags};
        register_view_[41] = {"CP2_COMMAND", cop2_command_, 32U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto r3000a_registration =
            register_factory("sony.r3000a", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<r3000a>(); });
    } // namespace

} // namespace mnemos::chips::cpu
