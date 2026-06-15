#include "scu_dsp.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <array>
#include <cstdint>
#include <memory>

// Ported from the Emu reference (chips/scu_dsp); clean-room per the Sega SCU DSP
// datasheet. The instruction decode/execute is integer-exact with the reference.
//
// Instruction format (operation/control sub-decode):
//   bits 31-30 = 00: Operation command (ALU + X + Y + D1 run in parallel)
//     bits 29-26  ALU op (4-bit)
//     bits 25-20  X-Bus control (3-bit process + 3-bit source)
//     bits 19-14  Y-Bus control (3-bit process + 3-bit source)
//     bits 13-0   D1-Bus control (mode + 4-bit dest + src / 8-bit immediate)
//   bits 31-30 = 10: MVI immediate to destination
//   bits 31-28 = 1100: DMA (D0-bus <-> data RAM)
//   bits 31-28 = 1101: JMP (conditional / unconditional)
//   bits 31-28 = 1110: LPS / BTM (loop control)
//   bits 31-28 = 1111: END / ENDI (stop execution, optional IRQ)

namespace mnemos::chips::cpu {
    namespace {

        // ALU opcodes (4-bit, bits 29-26).
        enum alu_op : std::uint32_t {
            alu_nop = 0x0U,
            alu_and = 0x1U,
            alu_or = 0x2U,
            alu_xor = 0x3U,
            alu_add = 0x4U,
            alu_sub = 0x5U,
            alu_ad2 = 0x6U,
            alu_sr = 0x8U,
            alu_rr = 0x9U,
            alu_sl = 0xAU,
            alu_rl = 0xBU,
            alu_rl8 = 0xEU,
        };

        // D0-bus address increment per transferred longword, in bytes. The
        // DSP->external direction uses the full 3-bit stride as (1<<stride)&~1;
        // the external->DSP direction honours only stride bit 1, giving 0 or 4.
        std::uint32_t dma_address_inc_bytes(std::uint32_t op, bool dsp_to_ext) noexcept {
            const std::uint32_t stride = (op >> 15U) & 0x7U;
            if (dsp_to_ext) {
                return (1U << stride) & ~1U;
            }
            return (1U << (stride & 0x2U)) & ~1U;
        }

        // The peripheral-bus sub-window of the external address space, where
        // external->DSP reads advance the source address by four bytes per DSP
        // longword regardless of the encoded add mode.
        bool dma_addr_is_peripheral_bus(std::uint32_t addr) noexcept {
            const std::uint32_t a = addr & 0x07FFFFFFU;
            return a >= 0x05A00000U && a < 0x05FC0000U;
        }

    } // namespace

    chip_metadata scu_dsp::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "SCU-DSP",
            .family = "SCU",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- data-RAM access ----

    std::uint32_t scu_dsp::ram_read(std::uint8_t bank, bool post_increment) noexcept {
        const std::uint8_t off = ct_[bank & 0x3U] & 0x3FU;
        const std::uint32_t value = ram_[bank & 0x3U][off];
        if (post_increment) {
            ct_[bank & 0x3U] = static_cast<std::uint8_t>((off + 1U) & 0x3FU);
        }
        return value;
    }

    void scu_dsp::ram_write_postincrement(std::uint8_t bank, std::uint32_t value) noexcept {
        const std::uint8_t off = ct_[bank & 0x3U] & 0x3FU;
        ram_[bank & 0x3U][off] = value;
        ct_[bank & 0x3U] = static_cast<std::uint8_t>((off + 1U) & 0x3FU);
    }

    // ---- D0-bus access ----

    std::uint32_t scu_dsp::bus_read32(std::uint32_t addr) noexcept {
        return bus_ != nullptr ? bus_->read32_be(addr) : 0U;
    }
    void scu_dsp::bus_write32(std::uint32_t addr, std::uint32_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write32_be(addr, value);
        }
    }

    // ---- bus source / dest decoders ----

    // D1-Bus source selector:
    //   0-3 = M0-M3 (RAM read, no CT increment)
    //   4-7 = MC0-MC3 (RAM read, post-increment CT)
    //   9   = ALU.L (low 32 bits of the ALU OUTPUT register, not AC.L)
    //   10  = ALU.H (high 16 bits of the ALU output, sign-extended)
    // Source 9/10 read the ALU register (not AC) so a computed result written
    // back to data RAM through D1 reflects this instruction's ALU output.
    std::uint32_t scu_dsp::d1_bus_source(std::uint32_t src) noexcept {
        switch (src & 0xFU) {
        case 0U:
        case 1U:
        case 2U:
        case 3U:
            return ram_read(static_cast<std::uint8_t>(src & 0x3U), false);
        case 4U:
        case 5U:
        case 6U:
        case 7U:
            return ram_read(static_cast<std::uint8_t>(src & 0x3U), true);
        case 9U:
            return alu_l_;
        case 10U: {
            // ALU.H sign-extended into a 32-bit lane.
            const auto h = static_cast<std::int16_t>(alu_h_);
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(h));
        }
        default:
            return 0U;
        }
    }

    // D1 operation-command destination selector. PC is intentionally absent
    // here: code 0xC writes CT0. MVI uses a separate map where 0xC is PC.
    void scu_dsp::d1_op_dest_write(std::uint32_t dst, std::uint32_t v) noexcept {
        switch (dst & 0xFU) {
        case 0U:
        case 1U:
        case 2U:
        case 3U:
            ram_write_postincrement(static_cast<std::uint8_t>(dst & 0x3U), v);
            break;
        case 4U:
            rx_ = v;
            break;
        case 5U:
            pl_ = v;
            break;
        // RA0/WA0 hold the D0-bus DMA address in longword units; the stored
        // value is shifted left by two to the byte address and masked to 25 bits.
        case 6U:
            ra0_ = (v << 2U) & 0x07FFFFFCU;
            break;
        case 7U:
            wa0_ = (v << 2U) & 0x07FFFFFCU;
            break;
        case 0xAU:
            lop_ = static_cast<std::uint16_t>(v & 0xFFFU);
            break;
        case 0xBU:
            top_ = static_cast<std::uint8_t>(v & 0xFFU);
            break;
        case 0xCU:
        case 0xDU:
        case 0xEU:
        case 0xFU:
            ct_[dst & 0x3U] = static_cast<std::uint8_t>(v & 0x3FU);
            break;
        default:
            break;
        }
    }

    void scu_dsp::mvi_dest_write(std::uint32_t dst, std::uint32_t v) noexcept {
        switch (dst & 0xFU) {
        case 0U:
        case 1U:
        case 2U:
        case 3U:
            ram_write_postincrement(static_cast<std::uint8_t>(dst & 0x3U), v);
            break;
        case 4U:
            rx_ = v;
            break;
        case 5U:
            pl_ = v;
            break;
        case 6U:
            ra0_ = (v << 2U) & 0x07FFFFFCU;
            break;
        case 7U:
            wa0_ = (v << 2U) & 0x07FFFFFCU;
            break;
        case 0xAU:
            lop_ = static_cast<std::uint16_t>(v & 0xFFFU);
            break;
        case 0xCU:
            top_ = pc_;
            pc_ = static_cast<std::uint8_t>(v & 0xFFU);
            break;
        default:
            break;
        }
    }

    // ---- ALU ----
    // ALU operations write to the separate ALU register (alu_l_/alu_h_); AC
    // (acl_/ach_) is the ALU input and stays unchanged unless the Y-bus
    // "MOV ALU,A" path copies the output back. X/Y/D1 buses therefore see the
    // PREVIOUS AC inside the same instruction.
    void scu_dsp::do_alu(std::uint32_t opcode) noexcept {
        // Seed the ALU register with AC so non-NOP paths that compute "result =
        // a" still produce sensible output and the 48-bit AD2 path starts from AC.
        alu_l_ = acl_;
        alu_h_ = ach_;

        const std::uint32_t a = acl_;
        const std::uint32_t b = pl_;
        std::uint32_t result = a;
        bool carry = c_flag_;

        switch (opcode) {
        case alu_nop:
            return;
        case alu_and:
            result = a & b;
            carry = false;
            break;
        case alu_or:
            result = a | b;
            carry = false;
            break;
        case alu_xor:
            result = a ^ b;
            carry = false;
            break;
        case alu_add: {
            const std::uint64_t s = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b);
            result = static_cast<std::uint32_t>(s);
            carry = (s >> 32U) != 0U;
            break;
        }
        case alu_sub: {
            const std::uint64_t s = static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b);
            result = static_cast<std::uint32_t>(s);
            carry = (s >> 32U) != 0U;
            break;
        }
        case alu_ad2: {
            // 48-bit add: ALU = AC + P.
            const std::uint64_t acc = (static_cast<std::uint64_t>(ach_) << 32U) | acl_;
            const std::uint64_t prd = (static_cast<std::uint64_t>(ph_) << 32U) | pl_;
            const std::uint64_t s = (acc + prd) & 0xFFFFFFFFFFFFULL;
            alu_h_ = static_cast<std::uint16_t>((s >> 32U) & 0xFFFFU);
            alu_l_ = static_cast<std::uint32_t>(s & 0xFFFFFFFFU);
            s_flag_ = (alu_h_ & 0x8000U) != 0U;
            z_flag_ = (s == 0U);
            c_flag_ = (acc + prd) > 0xFFFFFFFFFFFFULL;
            return;
        }
        case alu_sr:
            carry = (a & 1U) != 0U;
            result = a >> 1U;
            break;
        case alu_rr:
            carry = (a & 1U) != 0U;
            result = (a >> 1U) | (carry ? 0x80000000U : 0U);
            break;
        case alu_sl:
            carry = (a & 0x80000000U) != 0U;
            result = a << 1U;
            break;
        case alu_rl:
            carry = (a & 0x80000000U) != 0U;
            result = (a << 1U) | (carry ? 1U : 0U);
            break;
        case alu_rl8:
            carry = (a & 0x01000000U) != 0U;
            result = (a << 8U) | (a >> 24U);
            break;
        default:
            return;
        }

        alu_l_ = result;
        alu_h_ = (result & 0x80000000U) != 0U ? 0xFFFFU : 0U;
        s_flag_ = (result & 0x80000000U) != 0U;
        z_flag_ = (result == 0U);
        c_flag_ = carry;
    }

    // ---- Operation command (bits 31-30 = 00) ----
    void scu_dsp::do_op_command(std::uint32_t op) noexcept {
        const std::uint32_t alu_code = (op >> 26U) & 0xFU;
        const std::uint32_t x_ctrl = (op >> 20U) & 0x3FU;
        const std::uint32_t y_ctrl = (op >> 14U) & 0x3FU;
        const std::uint32_t d1_ctrl = op & 0x3FFFU;

        // Order: ALU -> X-bus -> Y-bus -> D1-bus. ALU writes the separate ALU
        // register so it does not clobber AC mid-instruction.
        do_alu(alu_code);

        const std::uint32_t x_process = x_ctrl >> 3U;
        const std::uint32_t x_src = x_ctrl & 0x7U;
        // X-Bus combined-op semantics:
        //   process & 0b11 == 0b10 -> MOV MUL,P (always runs)
        //   process >= 0b011       -> read [s], then maybe MOV [s],P / MOV [s],X
        //     process & 0b11 == 0b11 -> MOV [s],P
        //     bit 2 set              -> MOV [s],X
        if ((x_process & 0x3U) == 0x2U) {
            const auto prod = static_cast<std::int64_t>(static_cast<std::int32_t>(rx_)) *
                              static_cast<std::int64_t>(static_cast<std::int32_t>(ry_));
            const auto uprod = static_cast<std::uint64_t>(prod);
            ph_ = static_cast<std::uint16_t>((uprod >> 32U) & 0xFFFFU);
            pl_ = static_cast<std::uint32_t>(uprod & 0xFFFFFFFFU);
        }
        if (x_process >= 0x3U) {
            const std::uint32_t v =
                ram_read(static_cast<std::uint8_t>(x_src & 0x3U), (x_src & 0x4U) != 0U);
            if ((x_process & 0x3U) == 0x3U) {
                pl_ = v;
                ph_ = (v & 0x80000000U) != 0U ? 0xFFFFU : 0U;
            }
            if ((x_process & 0x4U) != 0U) {
                rx_ = v;
            }
        }

        const std::uint32_t y_src = y_ctrl & 0x7U;
        const std::uint32_t y_process = y_ctrl >> 3U;
        // Y-Bus combined-op semantics:
        //   process & 0b11 == 0b01 -> CLR A
        //   process & 0b11 == 0b10 -> MOV ALU,A
        //   process >= 0b011       -> read [s], then maybe MOV [s],A / MOV [s],Y
        //     process & 0b11 == 0b11 -> MOV [s],A
        //     bit 2 set              -> MOV [s],Y
        if ((y_process & 0x3U) == 0x1U) {
            ach_ = 0U;
            acl_ = 0U;
        } else if ((y_process & 0x3U) == 0x2U) {
            ach_ = alu_h_;
            acl_ = alu_l_;
        }
        if (y_process >= 0x3U) {
            const std::uint32_t v =
                ram_read(static_cast<std::uint8_t>(y_src & 0x3U), (y_src & 0x4U) != 0U);
            if ((y_process & 0x3U) == 0x3U) {
                acl_ = v;
                ach_ = (v & 0x80000000U) != 0U ? 0xFFFFU : 0U;
            }
            if ((y_process & 0x4U) != 0U) {
                ry_ = v;
            }
        }

        // D1-Bus. Bits 13-12 select mode; bits 11-8 = dest; bits 7-0 = immediate
        // or source code.
        const std::uint32_t d1_mode = (d1_ctrl >> 12U) & 0x3U;
        const std::uint32_t d1_dst = (d1_ctrl >> 8U) & 0xFU;
        const std::uint32_t d1_imm_or_src = d1_ctrl & 0xFFU;
        if (d1_mode == 1U) {
            const auto simm = static_cast<std::int32_t>(static_cast<std::int8_t>(d1_imm_or_src));
            d1_op_dest_write(d1_dst, static_cast<std::uint32_t>(simm));
        } else if (d1_mode == 3U) {
            const std::uint32_t v = d1_bus_source(d1_imm_or_src & 0xFU);
            d1_op_dest_write(d1_dst, v);
        }
    }

    // ---- condition decoder (shared by JMP and conditional MVI) ----
    //   bit 25 = 0 unconditional / 1 conditional
    //   bit 24 = polarity: 1 -> take when ANY selected flag is 1
    //                      0 -> take when ALL selected flags are 0
    //   bits 22-19 = flag selector mask (bit19 Z, bit20 S, bit21 C, bit22 T0)
    bool scu_dsp::eval_cond(std::uint32_t op) const noexcept {
        if ((op & 0x02000000U) == 0U) {
            return true; // unconditional
        }
        const bool polarity = (op & 0x01000000U) != 0U;
        bool any_set = false;
        if ((op & 0x00080000U) != 0U && z_flag_) {
            any_set = true;
        }
        if ((op & 0x00100000U) != 0U && s_flag_) {
            any_set = true;
        }
        if ((op & 0x00200000U) != 0U && c_flag_) {
            any_set = true;
        }
        if ((op & 0x00400000U) != 0U && t0_flag_) {
            any_set = true;
        }
        return polarity == any_set;
    }

    // ---- MVI (bits 31-30 = 10) ----
    void scu_dsp::do_mvi(std::uint32_t op) noexcept {
        if (!eval_cond(op)) {
            return;
        }
        const std::uint32_t dst = (op >> 26U) & 0xFU;
        const bool conditional = (op & 0x02000000U) != 0U;
        std::int32_t imm = 0;
        if (conditional) {
            // sign-extend a 19-bit immediate.
            imm = static_cast<std::int32_t>(static_cast<std::uint32_t>(op & 0x0007FFFFU) << 13U) >>
                  13;
        } else {
            // sign-extend a 25-bit immediate.
            imm =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(op & 0x01FFFFFFU) << 7U) >> 7;
        }
        mvi_dest_write(dst, static_cast<std::uint32_t>(imm));
    }

    // ---- JMP (bits 31-28 = 1101) ----
    void scu_dsp::do_jmp(std::uint32_t op) noexcept {
        // JMP updates pc immediately; the two-stage fetch pipeline in
        // step_instruction lets the already-prefetched slot execute once more
        // (the delay slot) before the fetch lands on the target.
        if (eval_cond(op)) {
            pc_ = static_cast<std::uint8_t>(op & 0xFFU);
        }
    }

    // ---- LPS / BTM (bits 31-28 = 1110) ----
    void scu_dsp::do_loop(std::uint32_t op) noexcept {
        // bit 27 = 0 -> LPS (1-instruction loop, armed + post-step rewind),
        // bit 27 = 1 -> BTM (loop bottom: rewind PC to TOP while LOP != 0).
        const bool is_btm = (op & 0x08000000U) != 0U;
        if (is_btm) {
            if (lop_ > 0U) {
                pc_ = top_;
                --lop_;
            }
        } else {
            if (lop_ > 0U) {
                top_ = pc_;
                loop_active_ = true;
            }
        }
    }

    // ---- END / ENDI (bits 31-28 = 1111) ----
    void scu_dsp::do_end(std::uint32_t op) noexcept {
        if ((op & 0x08000000U) != 0U) {
            end_irq_ = true; // ENDI raises the E flag for the host IRQ
        }
        ex_flag_ = false;
    }

    std::uint32_t scu_dsp::dma_count_source(std::uint32_t source) noexcept {
        const std::uint8_t bank = static_cast<std::uint8_t>(source & 0x3U);
        const std::uint8_t off = ct_[bank] & 0x3FU;
        const std::uint32_t value = ram_[bank][off] & 0x3FFU;
        if ((source & 0x4U) != 0U) {
            ct_[bank] = static_cast<std::uint8_t>((off + 1U) & 0x3FU);
        }
        return value;
    }

    // ---- DMA (bits 31-28 = 1100) ----
    // Transfer between external work RAM (via the D0-bus / attached ibus) and
    // DSP internal data RAM (the CT-addressed banks).
    //   bits 17-15  address-add mode
    //   bit 14      hold address when set
    //   bit 13      count source: 0 = immediate bits 7-0, 1 = RAM selector
    //   bit 12      direction: 0 = external->DSP, 1 = DSP->external
    //   bits 10-8   DSP RAM bank select
    void scu_dsp::do_dma(std::uint32_t op) noexcept {
        const bool register_count = (op & 0x2000U) != 0U;
        const std::uint32_t count = register_count ? dma_count_source(op & 0x7U) : (op & 0xFFU);
        const bool dsp_to_ext = (op & 0x1000U) != 0U;
        const bool hold = (op & 0x4000U) != 0U;
        const std::uint32_t add_bytes = dma_address_inc_bytes(op, dsp_to_ext);
        const auto bank = static_cast<std::uint8_t>((op >> 8U) & 0x3U);

        t0_flag_ = true;

        if (dsp_to_ext) {
            std::uint32_t waddr = wa0_;
            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t v = ram_[bank][ct_[bank] & 0x3FU];
                bus_write32(waddr, v);
                ct_[bank] = static_cast<std::uint8_t>((ct_[bank] + 1U) & 0x3FU);
                if (!hold) {
                    waddr += add_bytes;
                }
            }
            if (!hold) {
                wa0_ = waddr;
            }
        } else {
            std::uint32_t raddr = ra0_;
            for (std::uint32_t i = 0U; i < count; ++i) {
                const std::uint32_t v = bus_read32(raddr);
                ram_[bank][ct_[bank] & 0x3FU] = v;
                ct_[bank] = static_cast<std::uint8_t>((ct_[bank] + 1U) & 0x3FU);
                if (!hold) {
                    raddr += dma_addr_is_peripheral_bus(raddr) ? 4U : add_bytes;
                }
            }
            if (!hold) {
                ra0_ = raddr;
            }
        }

        t0_flag_ = false;
    }

    // ---- step / catch-up ----

    int scu_dsp::step_instruction() {
        // Idle when not executing: report a unit cost so tick()'s catch-up loop
        // terminates instead of spinning. A pause (EP without single-step) also
        // idles.
        const bool runnable = (ex_flag_ || es_flag_) && !(ep_flag_ && !es_flag_);
        if (!runnable) {
            step_cycles_ = 1;
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        // Two-stage pipeline. On the first step after reset / a port PC load,
        // prime by fetching program[pc] without consuming a cycle so the natural
        // "step N runs instruction N" contract holds. exec_pc_ records the
        // address the executing op was fetched from, so the trace reports the
        // instruction actually running rather than the look-ahead pointer.
        if (!pipeline_primed_) {
            next_pc_ = pc_;
            next_instr_ = program_[pc_];
            pc_ = static_cast<std::uint8_t>((pc_ + 1U) & 0xFFU);
            pipeline_primed_ = true;
        }
        const std::uint32_t op = next_instr_;
        const std::uint8_t exec_pc = next_pc_;
        next_pc_ = pc_;
        next_instr_ = program_[pc_];
        pc_ = static_cast<std::uint8_t>((pc_ + 1U) & 0xFFU);
        const bool step_only = es_flag_;

        if (trace_callback_) {
            trace_callback_(exec_pc);
        }

        // An LPS op must not trigger the post-step rewind on its own step: the
        // body gets one natural execution first.
        const bool was_lps = ((op >> 28U) & 0xFU) == 0xEU && (op & 0x08000000U) == 0U;

        const std::uint32_t top2 = (op >> 30U) & 0x3U;
        if (top2 == 0U) {
            do_op_command(op);
        } else if (top2 == 0x2U) {
            do_mvi(op);
        } else if (top2 == 0x3U) {
            const std::uint32_t sub = (op >> 28U) & 0x3U;
            if (sub == 0x0U) {
                do_dma(op);
            } else if (sub == 0x1U) {
                do_jmp(op);
            } else if (sub == 0x2U) {
                do_loop(op);
            } else {
                do_end(op);
            }
        }

        // LPS post-step rewind: the body just executed (PC advanced past it). If
        // iterations remain, rewind PC to TOP and decrement LOP; at zero, clear.
        if (loop_active_ && !was_lps) {
            if (lop_ > 0U) {
                pc_ = top_;
                --lop_;
            } else {
                loop_active_ = false;
            }
        }

        if (step_only) {
            es_flag_ = false;
        }

        step_cycles_ = cycles_per_instruction;
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    int scu_dsp::run(int max_steps) {
        int n = 0;
        while (n < max_steps && (ex_flag_ || es_flag_)) {
            const bool was_executing = ex_flag_;
            step_instruction();
            ++n;
            if (was_executing && !ex_flag_ && !es_flag_) {
                break; // execution ended this step (END / ENDI)
            }
        }
        return n;
    }

    void scu_dsp::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void scu_dsp::reset(reset_kind /*kind*/) {
        program_ = {};
        ram_ = {};
        rx_ = ry_ = 0U;
        ph_ = 0U;
        pl_ = 0U;
        ach_ = 0U;
        acl_ = 0U;
        alu_h_ = 0U;
        alu_l_ = 0U;
        ct_ = {};
        lop_ = 0U;
        top_ = 0U;
        pc_ = 0U;
        ra0_ = wa0_ = 0U;
        s_flag_ = z_flag_ = c_flag_ = t0_flag_ = false;
        loop_active_ = false;
        next_instr_ = 0U;
        next_pc_ = 0U;
        pipeline_primed_ = false;
        ex_flag_ = ep_flag_ = es_flag_ = end_irq_ = false;
        prog_write_addr_ = 0U;
        data_addr_reg_ = 0U;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    scu_dsp::registers scu_dsp::cpu_registers() const noexcept {
        return {
            .rx = rx_,
            .ry = ry_,
            .ph = ph_,
            .pl = pl_,
            .ach = ach_,
            .acl = acl_,
            .alu_h = alu_h_,
            .alu_l = alu_l_,
            .ct = ct_,
            .lop = lop_,
            .top = top_,
            .pc = pc_,
            .ra0 = ra0_,
            .wa0 = wa0_,
            .s_flag = s_flag_,
            .z_flag = z_flag_,
            .c_flag = c_flag_,
            .t0_flag = t0_flag_,
            .loop_active = loop_active_,
            .ex_flag = ex_flag_,
            .ep_flag = ep_flag_,
            .es_flag = es_flag_,
            .end_irq = end_irq_,
        };
    }

    void scu_dsp::set_registers(const registers& v) noexcept {
        rx_ = v.rx;
        ry_ = v.ry;
        ph_ = v.ph;
        pl_ = v.pl;
        ach_ = v.ach;
        acl_ = v.acl;
        alu_h_ = v.alu_h;
        alu_l_ = v.alu_l;
        ct_ = v.ct;
        lop_ = v.lop;
        top_ = v.top;
        pc_ = v.pc;
        ra0_ = v.ra0;
        wa0_ = v.wa0;
        s_flag_ = v.s_flag;
        z_flag_ = v.z_flag;
        c_flag_ = v.c_flag;
        t0_flag_ = v.t0_flag;
        loop_active_ = v.loop_active;
        ex_flag_ = v.ex_flag;
        ep_flag_ = v.ep_flag;
        es_flag_ = v.es_flag;
        end_irq_ = v.end_irq;
    }

    // ---- port-control window ----

    std::uint32_t scu_dsp::read_reg(std::uint32_t address) noexcept {
        switch (address & 0xCU) {
        case 0x0U: {
            // PPAF status: PC in bits 7-0, flags in the upper bits.
            std::uint32_t v = pc_;
            if (t0_flag_) {
                v |= 0x00800000U; // bit 23
            }
            if (s_flag_) {
                v |= 0x00400000U; // bit 22
            }
            if (z_flag_) {
                v |= 0x00200000U; // bit 21
            }
            if (c_flag_) {
                v |= 0x00100000U; // bit 20
            }
            if (end_irq_) {
                v |= 0x00040000U; // bit 18
            }
            if (ex_flag_) {
                v |= 0x00010000U; // bit 16
            }
            return v;
        }
        case 0x4U: // PPD: write-only program port, returns 0 on read.
            return 0U;
        case 0x8U: // PDA: data-RAM address.
            return data_addr_reg_;
        case 0xCU: {
            // PDD: read from ram[bank][offset], auto-increment offset.
            const auto bank = static_cast<std::uint8_t>((data_addr_reg_ >> 6U) & 0x3U);
            std::uint8_t off = static_cast<std::uint8_t>(data_addr_reg_ & 0x3FU);
            const std::uint32_t v = ram_[bank][off];
            off = static_cast<std::uint8_t>((off + 1U) & 0x3FU);
            data_addr_reg_ = (data_addr_reg_ & ~0x3FU) | off;
            return v;
        }
        default:
            return 0U;
        }
    }

    void scu_dsp::write_reg(std::uint32_t address, std::uint32_t val) noexcept {
        switch (address & 0xCU) {
        case 0x0U: {
            // PPAF control bits:
            //   bit 15: load PC from bits 7-0
            //   bit 16: execute, bit 17: single-step
            //   bit 25: pause, bit 26: resume
            if ((val & 0x00008000U) != 0U) {
                pc_ = static_cast<std::uint8_t>(val & 0xFFU);
                prog_write_addr_ = pc_;
                // Reset the pipeline so the next step fetches program[pc].
                next_instr_ = 0U;
                pipeline_primed_ = false;
            }
            if ((val & 0x02000000U) != 0U) {
                ep_flag_ = true;
            } else if ((val & 0x04000000U) != 0U) {
                ep_flag_ = false;
            } else {
                ex_flag_ = (val & 0x00010000U) != 0U;
                es_flag_ = (val & 0x00020000U) != 0U;
            }
            break;
        }
        case 0x4U: {
            // PPD: program-RAM data. Writes are ignored while executing unless
            // paused.
            if (ex_flag_ && !ep_flag_) {
                break;
            }
            program_[pc_] = val;
            pc_ = static_cast<std::uint8_t>((pc_ + 1U) & 0xFFU);
            prog_write_addr_ = pc_;
            break;
        }
        case 0x8U: {
            // PDA: data-RAM address. Bits 7-6 = bank, 5-0 = offset.
            data_addr_reg_ = val & 0xFFU;
            break;
        }
        case 0xCU: {
            // PDD: write to ram[bank][offset], auto-increment offset.
            const auto bank = static_cast<std::uint8_t>((data_addr_reg_ >> 6U) & 0x3U);
            std::uint8_t off = static_cast<std::uint8_t>(data_addr_reg_ & 0x3FU);
            ram_[bank][off] = val;
            off = static_cast<std::uint8_t>((off + 1U) & 0x3FU);
            data_addr_reg_ = (data_addr_reg_ & ~0x3FU) | off;
            break;
        }
        default:
            break;
        }
    }

    // ---- state ----

    void scu_dsp::save_state(state_writer& writer) const {
        for (const std::uint32_t word : program_) {
            writer.u32(word);
        }
        for (const auto& bank : ram_) {
            for (const std::uint32_t word : bank) {
                writer.u32(word);
            }
        }
        writer.u32(rx_);
        writer.u32(ry_);
        writer.u16(ph_);
        writer.u32(pl_);
        writer.u16(ach_);
        writer.u32(acl_);
        writer.u16(alu_h_);
        writer.u32(alu_l_);
        for (const std::uint8_t counter : ct_) {
            writer.u8(counter);
        }
        writer.u16(lop_);
        writer.u8(top_);
        writer.u8(pc_);
        writer.u32(ra0_);
        writer.u32(wa0_);
        writer.boolean(s_flag_);
        writer.boolean(z_flag_);
        writer.boolean(c_flag_);
        writer.boolean(t0_flag_);
        writer.boolean(loop_active_);
        writer.u32(next_instr_);
        writer.u8(next_pc_);
        writer.boolean(pipeline_primed_);
        writer.boolean(ex_flag_);
        writer.boolean(ep_flag_);
        writer.boolean(es_flag_);
        writer.boolean(end_irq_);
        writer.u8(prog_write_addr_);
        writer.u32(data_addr_reg_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void scu_dsp::load_state(state_reader& reader) {
        for (std::uint32_t& word : program_) {
            word = reader.u32();
        }
        for (auto& bank : ram_) {
            for (std::uint32_t& word : bank) {
                word = reader.u32();
            }
        }
        rx_ = reader.u32();
        ry_ = reader.u32();
        ph_ = reader.u16();
        pl_ = reader.u32();
        ach_ = reader.u16();
        acl_ = reader.u32();
        alu_h_ = reader.u16();
        alu_l_ = reader.u32();
        for (std::uint8_t& counter : ct_) {
            counter = reader.u8();
        }
        lop_ = reader.u16();
        top_ = reader.u8();
        pc_ = reader.u8();
        ra0_ = reader.u32();
        wa0_ = reader.u32();
        s_flag_ = reader.boolean();
        z_flag_ = reader.boolean();
        c_flag_ = reader.boolean();
        t0_flag_ = reader.boolean();
        loop_active_ = reader.boolean();
        next_instr_ = reader.u32();
        next_pc_ = reader.u8();
        pipeline_primed_ = reader.boolean();
        ex_flag_ = reader.boolean();
        ep_flag_ = reader.boolean();
        es_flag_ = reader.boolean();
        end_irq_ = reader.boolean();
        prog_write_addr_ = reader.u8();
        data_addr_reg_ = reader.u32();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& scu_dsp::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> scu_dsp::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"PC", pc_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"RX", rx_, 32U, fmt::unsigned_integer};
        register_view_[2] = {"RY", ry_, 32U, fmt::unsigned_integer};
        register_view_[3] = {"PH", ph_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"PL", pl_, 32U, fmt::unsigned_integer};
        register_view_[5] = {"ACH", ach_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"ACL", acl_, 32U, fmt::unsigned_integer};
        register_view_[7] = {"ALUH", alu_h_, 16U, fmt::unsigned_integer};
        register_view_[8] = {"ALUL", alu_l_, 32U, fmt::unsigned_integer};
        register_view_[9] = {"CT0", ct_[0], 8U, fmt::unsigned_integer};
        register_view_[10] = {"CT1", ct_[1], 8U, fmt::unsigned_integer};
        register_view_[11] = {"CT2", ct_[2], 8U, fmt::unsigned_integer};
        register_view_[12] = {"CT3", ct_[3], 8U, fmt::unsigned_integer};
        register_view_[13] = {"LOP", lop_, 16U, fmt::unsigned_integer};
        register_view_[14] = {"TOP", top_, 8U, fmt::unsigned_integer};
        register_view_[15] = {"RA0", ra0_, 32U, fmt::unsigned_integer};
        register_view_[16] = {"WA0", wa0_, 32U, fmt::unsigned_integer};
        const std::uint64_t flags = (s_flag_ ? 0x8U : 0U) | (z_flag_ ? 0x4U : 0U) |
                                    (c_flag_ ? 0x2U : 0U) | (t0_flag_ ? 0x1U : 0U);
        register_view_[17] = {"FLAGS", flags, 4U, fmt::flags};
        register_view_[18] = {"EX", ex_flag_ ? 1U : 0U, 1U, fmt::unsigned_integer};
        register_view_[19] = {"E", end_irq_ ? 1U : 0U, 1U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto scu_dsp_registration =
            register_factory("sega.scu_dsp", chip_class::cpu, []() -> std::unique_ptr<ichip> {
                return std::make_unique<scu_dsp>();
            });
    } // namespace

} // namespace mnemos::chips::cpu
