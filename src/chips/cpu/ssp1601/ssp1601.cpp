#include "ssp1601.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::cpu {

    // Ported from the Emu reference (chips/ssp1601); clean-room per the Samsung
    // SSP1601 DSP datasheet.
    //
    // NOTE ON ENCODING (partial port): the Emu reference's opcode dispatch is a
    // documented stub, so there is no reference bit-pattern table to mirror. The
    // encoding below is a self-consistent clean-room subset chosen to exercise
    // each implemented instruction CLASS end-to-end (register/immediate LD, the
    // ALU group, the multiplier, and stack-based control transfer). The opcode
    // FORMAT here is therefore not the silicon's exact micro-encoding; the
    // architectural BEHAVIOUR of each class (register file, 32-bit accumulator,
    // 16x16->32 product, 6-deep stack, N/Z/V/C flags) is datasheet-faithful.

    namespace {
        // General-register selector (3-bit). Mirrors the datasheet ordering of
        // the register file the Emu struct declares.
        enum gr : int {
            gr_blind = 0, // GR0: reads 0, writes discarded
            gr_x = 1,
            gr_y = 2,
            gr_a = 3, // accumulator high word (A_h)
            gr_st = 4,
            gr_stack = 5, // push/pop the hardware return stack
            gr_pc = 6,
            gr_p = 7, // product high word (P_h)
        };

        enum alu_op : int {
            alu_add = 0,
            alu_sub = 1,
            alu_cmp = 2, // SUB, result discarded (flags only)
            alu_and = 3,
            alu_or = 4,
            alu_eor = 5,
            alu_ld = 6, // load operand into the accumulator (flags from operand)
        };

        enum cond : int {
            cond_always = 0,
            cond_z = 1,  // taken when Z set
            cond_nz = 2, // taken when Z clear
            cond_n = 3,  // taken when N set
            cond_nn = 4, // taken when N clear
            cond_c = 5,  // taken when carry/link set
            cond_ret = 0xF,
        };

        constexpr std::uint16_t group(std::uint16_t op) noexcept {
            return static_cast<std::uint16_t>((op >> 12U) & 0x0FU);
        }
    } // namespace

    chip_metadata ssp1601::metadata() const noexcept {
        return {
            .manufacturer = "Samsung",
            .part_number = "SSP1601",
            .family = "SSP160x",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- program/operand memory (16-bit words, big-endian over the byte bus) ----

    std::uint16_t ssp1601::read_word(std::uint16_t word_addr) noexcept {
        if (bus_ == nullptr) {
            return 0U;
        }
        return bus_->read16_be(static_cast<std::uint32_t>(word_addr) << 1U);
    }

    void ssp1601::write_word(std::uint16_t word_addr, std::uint16_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write16_be(static_cast<std::uint32_t>(word_addr) << 1U, value);
        }
    }

    std::uint16_t ssp1601::fetch() noexcept {
        const std::uint16_t v = read_word(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return v;
    }

    // ---- hardware return stack (6-deep) ----

    void ssp1601::push_pc(std::uint16_t value) noexcept {
        if (sp_ < stack_depth) {
            stack_[sp_] = value;
            ++sp_;
        }
    }

    std::uint16_t ssp1601::pop_pc() noexcept {
        if (sp_ == 0U) {
            return 0U;
        }
        --sp_;
        return stack_[sp_];
    }

    // ---- general-register file ----

    std::uint16_t ssp1601::read_gr(int sel) noexcept {
        switch (sel & 7) {
        case gr_blind:
            return 0U;
        case gr_x:
            return x_;
        case gr_y:
            return y_;
        case gr_a:
            return a_hi();
        case gr_st:
            return st_;
        case gr_stack:
            return pop_pc();
        case gr_pc:
            return pc_;
        default: // gr_p
            return static_cast<std::uint16_t>(p_ >> 16U);
        }
    }

    void ssp1601::write_gr(int sel, std::uint16_t value) noexcept {
        switch (sel & 7) {
        case gr_blind:
            break;
        case gr_x:
            x_ = value;
            break;
        case gr_y:
            y_ = value;
            break;
        case gr_a:
            set_a_hi(value);
            break;
        case gr_st:
            st_ = value;
            break;
        case gr_stack:
            push_pc(value);
            break;
        case gr_pc:
            pc_ = value;
            break;
        default: // gr_p high word
            p_ = (p_ & 0x0000FFFFU) | (static_cast<std::uint32_t>(value) << 16U);
            break;
        }
    }

    // ---- ALU ----

    void ssp1601::set_nz(std::uint32_t result) noexcept {
        st_ &= static_cast<std::uint16_t>(~(flag_z | flag_n));
        if (result == 0U) {
            st_ |= flag_z;
        }
        if ((result & 0x80000000U) != 0U) {
            st_ |= flag_n;
        }
    }

    std::uint32_t ssp1601::alu(int op, std::uint32_t acc, std::uint16_t operand) noexcept {
        // The operand is sign-extended into the upper accumulator half the way
        // the datasheet ALU treats a 16-bit register source against the 32-bit
        // accumulator: it lands in the high word for arithmetic.
        const auto ext = static_cast<std::uint32_t>(operand) << 16U;
        std::uint32_t result = acc;

        switch (op) {
        case alu_add: {
            const std::uint64_t wide = static_cast<std::uint64_t>(acc) + ext;
            result = static_cast<std::uint32_t>(wide);
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            if ((wide >> 32U) != 0U) {
                st_ |= flag_l;
            }
            if (((~(acc ^ ext) & (acc ^ result)) & 0x80000000U) != 0U) {
                st_ |= flag_ov;
            }
            set_nz(result);
            return result;
        }
        case alu_sub:
        case alu_cmp: {
            const std::uint64_t wide = static_cast<std::uint64_t>(acc) - ext;
            result = static_cast<std::uint32_t>(wide);
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            if ((wide >> 32U) != 0U) {
                st_ |= flag_l; // borrow
            }
            if ((((acc ^ ext) & (acc ^ result)) & 0x80000000U) != 0U) {
                st_ |= flag_ov;
            }
            set_nz(result);
            return (op == alu_cmp) ? acc : result; // CMP keeps A
        }
        case alu_and:
            result = acc & ext;
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            set_nz(result);
            return result;
        case alu_or:
            result = acc | ext;
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            set_nz(result);
            return result;
        case alu_eor:
            result = acc ^ ext;
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            set_nz(result);
            return result;
        default: // alu_ld
            result = ext;
            st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
            set_nz(result);
            return result;
        }
    }

    bool ssp1601::test_cond(int cond) const noexcept {
        switch (cond) {
        case cond_always:
            return true;
        case cond_z:
            return (st_ & flag_z) != 0U;
        case cond_nz:
            return (st_ & flag_z) == 0U;
        case cond_n:
            return (st_ & flag_n) != 0U;
        case cond_nn:
            return (st_ & flag_n) == 0U;
        case cond_c:
            return (st_ & flag_l) != 0U;
        default:
            return false;
        }
    }

    // ---- decode / execute ----

    int ssp1601::step_instruction() {
        step_cycles_ = 0;

        if (halted_) {
            elapsed_ += 1U;
            return 1;
        }

        if (trace_callback_) {
            trace_callback_(pc_);
        }

        const std::uint16_t op = fetch();

        switch (group(op)) {
        case 0x0: // NOP family
            if (op == 0x0000U) {
                step_cycles_ = 1;
            } else {
                last_undecoded_ = op;
                step_cycles_ = 1;
            }
            break;

        case 0x1: { // LD gr_dst, gr_src   (0001 0000 0ddd 0sss)
            const int dst = (op >> 4U) & 7;
            const int src = op & 7;
            write_gr(dst, read_gr(src));
            step_cycles_ = 1;
            break;
        }

        case 0x2: { // LD gr_dst, imm16    (0010 ...0ddd) + next program word
            const int dst = op & 7;
            const std::uint16_t imm = fetch();
            write_gr(dst, imm);
            step_cycles_ = 2;
            break;
        }

        case 0x3: { // ALU A, gr           (0011 0aaa 0000 0sss)
            const int aop = (op >> 8U) & 7;
            const int src = op & 7;
            a_ = alu(aop, a_, read_gr(src));
            step_cycles_ = 1;
            break;
        }

        case 0x4: { // ALU A, imm16        (0100 0aaa) + next program word
            const int aop = (op >> 8U) & 7;
            const std::uint16_t imm = fetch();
            a_ = alu(aop, a_, imm);
            step_cycles_ = 2;
            break;
        }

        case 0x5: // multiply unit
            if ((op & 0x0FFFU) == 0x000U) {
                // P = X * Y (signed 16x16 -> 32).
                const auto sx = static_cast<std::int32_t>(static_cast<std::int16_t>(x_));
                const auto sy = static_cast<std::int32_t>(static_cast<std::int16_t>(y_));
                p_ = static_cast<std::uint32_t>(sx * sy);
                step_cycles_ = 1;
            } else if ((op & 0x0FFFU) == 0x001U) {
                // A = P (move the product to the accumulator, set N/Z).
                a_ = p_;
                set_nz(a_);
                step_cycles_ = 1;
            } else if ((op & 0x0FFFU) == 0x002U) {
                // A += P  (multiply-accumulate step).
                const std::uint64_t wide = static_cast<std::uint64_t>(a_) + p_;
                st_ &= static_cast<std::uint16_t>(~(flag_l | flag_ov));
                if ((wide >> 32U) != 0U) {
                    st_ |= flag_l;
                }
                a_ = static_cast<std::uint32_t>(wide);
                set_nz(a_);
                step_cycles_ = 1;
            } else {
                last_undecoded_ = op;
                step_cycles_ = 1;
            }
            break;

        case 0x6: { // control transfer    (0110 cccc ...)
            const int c = (op >> 8U) & 0xF;
            if (c == cond_ret) { // RET
                pc_ = pop_pc();
                step_cycles_ = 2;
            } else {
                const std::uint16_t target = fetch();
                const bool is_call = (op & 0x0010U) != 0U; // bit 4 distinguishes CALL
                if (test_cond(c)) {
                    if (is_call) {
                        push_pc(pc_);
                    }
                    pc_ = target;
                    step_cycles_ = 3;
                } else {
                    step_cycles_ = 2;
                }
            }
            break;
        }

        case 0x7: { // LD A, (ri) / LD (ri), A   (0111 w000 0000 0rrr)
            const int reg = op & 7;
            const bool is_store = (op & 0x0800U) != 0U;
            const std::uint16_t ptr = r_[static_cast<std::size_t>(reg)];
            if (is_store) {
                write_word(ptr, a_hi());
            } else {
                set_a_hi(read_word(ptr));
                set_nz(a_);
            }
            step_cycles_ = 2;
            break;
        }

        case 0xF: // HALT / stop
            if (op == 0xFFFFU) {
                halted_ = true;
                step_cycles_ = 1;
            } else {
                last_undecoded_ = op;
                step_cycles_ = 1;
            }
            break;

        default:
            last_undecoded_ = op;
            step_cycles_ = 1;
            break;
        }

        if (step_cycles_ < 1) {
            step_cycles_ = 1;
        }
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void ssp1601::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void ssp1601::reset(reset_kind /*kind*/) {
        x_ = y_ = 0U;
        a_ = p_ = 0U;
        st_ = 0U;
        pc_ = 0U;
        stack_.fill(0U);
        sp_ = 0U;
        ext_.fill(0U);
        r_.fill(0U);
        // Per the canonical SVP boot trace the internal ROM dispatcher expects
        // r6 to hold 0x00FC at reset release as a return-address sentinel.
        r_[6] = 0x00FCU;
        ij_ = ik_ = 0U;
        halted_ = false;
        last_undecoded_ = 0U;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
        // IRAM/IROM contents survive reset (host-loaded before reset release).
    }

    ssp1601::registers ssp1601::cpu_registers() const noexcept {
        registers v;
        v.x = x_;
        v.y = y_;
        v.a = a_;
        v.p = p_;
        v.st = st_;
        v.pc = pc_;
        v.stack = stack_;
        v.sp = sp_;
        v.ext = ext_;
        v.r = r_;
        v.ij = ij_;
        v.ik = ik_;
        v.halted = halted_;
        return v;
    }

    void ssp1601::set_registers(const registers& v) noexcept {
        x_ = v.x;
        y_ = v.y;
        a_ = v.a;
        p_ = v.p;
        st_ = v.st;
        pc_ = v.pc;
        stack_ = v.stack;
        sp_ = v.sp;
        ext_ = v.ext;
        r_ = v.r;
        ij_ = v.ij;
        ik_ = v.ik;
        halted_ = v.halted;
    }

    void ssp1601::save_state(state_writer& writer) const {
        writer.u16(x_);
        writer.u16(y_);
        writer.u32(a_);
        writer.u32(p_);
        writer.u16(st_);
        writer.u16(pc_);
        for (const std::uint16_t s : stack_) {
            writer.u16(s);
        }
        writer.u8(sp_);
        for (const std::uint16_t e : ext_) {
            writer.u16(e);
        }
        for (const std::uint16_t g : r_) {
            writer.u16(g);
        }
        writer.u16(ij_);
        writer.u16(ik_);
        writer.boolean(halted_);
        writer.u16(last_undecoded_);
        for (const std::uint16_t w : iram_) {
            writer.u16(w);
        }
        for (const std::uint16_t w : irom_) {
            writer.u16(w);
        }
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void ssp1601::load_state(state_reader& reader) {
        x_ = reader.u16();
        y_ = reader.u16();
        a_ = reader.u32();
        p_ = reader.u32();
        st_ = reader.u16();
        pc_ = reader.u16();
        for (std::uint16_t& s : stack_) {
            s = reader.u16();
        }
        sp_ = reader.u8();
        for (std::uint16_t& e : ext_) {
            e = reader.u16();
        }
        for (std::uint16_t& g : r_) {
            g = reader.u16();
        }
        ij_ = reader.u16();
        ik_ = reader.u16();
        halted_ = reader.boolean();
        last_undecoded_ = reader.u16();
        for (std::uint16_t& w : iram_) {
            w = reader.u16();
        }
        for (std::uint16_t& w : irom_) {
            w = reader.u16();
        }
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& ssp1601::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ssp1601::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"X", x_, 16U, fmt::unsigned_integer};
        register_view_[1] = {"Y", y_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"A", a_, 32U, fmt::unsigned_integer};
        register_view_[3] = {"P", p_, 32U, fmt::unsigned_integer};
        register_view_[4] = {"ST", st_, 16U, fmt::flags};
        register_view_[5] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"SP", sp_, 8U, fmt::unsigned_integer};
        register_view_[7] = {"IJ", ij_, 16U, fmt::unsigned_integer};
        register_view_[8] = {"IK", ik_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"R6", r_[6], 16U, fmt::unsigned_integer};
        register_view_[10] = {"R7", r_[7], 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ssp1601_registration =
            register_factory("samsung.ssp1601", chip_class::cpu, []() -> std::unique_ptr<ichip> {
                return std::make_unique<ssp1601>();
            });
    } // namespace

} // namespace mnemos::chips::cpu
