#include "sh2.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::cpu {

    chip_metadata sh2::metadata() const noexcept {
        return {
            .manufacturer = "Hitachi",
            .part_number = "SH7604",
            .family = "SH-2",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- raw memory: big-endian assembly over the byte bus ----
    std::uint8_t sh2::rd8(std::uint32_t a) const noexcept {
        return bus_ != nullptr ? bus_->read8(a) : 0xFFU;
    }
    void sh2::wr8(std::uint32_t a, std::uint8_t v) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(a, v);
        }
    }
    std::uint16_t sh2::rd16(std::uint32_t a) const noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(rd8(a)) << 8U) | rd8(a + 1U));
    }
    void sh2::wr16(std::uint32_t a, std::uint16_t v) noexcept {
        wr8(a, static_cast<std::uint8_t>(v >> 8U));
        wr8(a + 1U, static_cast<std::uint8_t>(v));
    }
    std::uint32_t sh2::rd32(std::uint32_t a) const noexcept {
        return (static_cast<std::uint32_t>(rd16(a)) << 16U) | rd16(a + 2U);
    }
    void sh2::wr32(std::uint32_t a, std::uint32_t v) noexcept {
        wr16(a, static_cast<std::uint16_t>(v >> 16U));
        wr16(a + 2U, static_cast<std::uint16_t>(v));
    }

    // Signed widening helpers (byte/word -> 32-bit) used by the loads + EXT ops.
    namespace {
        [[nodiscard]] std::uint32_t sx_b(std::uint32_t v) noexcept {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(v)));
        }
        [[nodiscard]] std::uint32_t sx_w(std::uint32_t v) noexcept {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(v)));
        }
    } // namespace

    void sh2::exec(std::uint16_t op) {
        // SH-2 instruction decode. Behaviour ported from the Emu reference
        // (chips/sh2). Implemented: data transfer, ALU, logical, shift/rotate,
        // multiply, divide-step, control flow (with delay slots),
        // system-register ops (LDS/STS/LDC/STC), all addressing modes, and the
        // TRAPA/RTE + external-interrupt path (see step_instruction). Still
        // deferred: SLEEP, the illegal-instruction / slot-illegal / address-error
        // exceptions (need complete-decode + bus-fault reporting), and the FPU
        // (absent on the SH7604) -- these undecoded encodings stay 1-cycle
        // no-ops. Instruction timing beyond the 1-cycle base is deferred
        // (ADR-0011).
        const auto n0 = static_cast<unsigned>(op >> 12U);
        const auto rn = static_cast<std::size_t>((op >> 8U) & 0xFU);
        const auto rm = static_cast<std::size_t>((op >> 4U) & 0xFU);
        const auto lo = static_cast<unsigned>(op & 0xFU);
        const std::uint32_t sx8 = sx_b(op & 0xFFU);

        switch (n0) {
        case 0x0:
            if (op == 0x0008U) {
                sr_ &= ~sr_t;
                return;
            } // CLRT
            if (op == 0x0018U) {
                sr_ |= sr_t;
                return;
            } // SETT
            if (op == 0x0028U) {
                mach_ = macl_ = 0U;
                return;
            } // CLRMAC
            if (op == 0x0019U) {
                sr_ &= ~(sr_m | sr_q | sr_t);
                return;
            } // DIV0U
            if ((op & 0xFFU) == 0x29U) {
                r_[rn] = t_in();
                return;
            } // MOVT Rn
            if (op == 0x000BU) { // RTS -- PC := PR (delayed)
                branch_delayed(pr_);
                return;
            }
            if (op == 0x002BU) { // RTE -- pop PC + SR, then a delayed branch
                const std::uint32_t new_pc = rd32(r_[15]);
                const std::uint32_t new_sr = rd32(r_[15] + 4U) & sr_mask;
                r_[15] += 8U;
                sr_ = new_sr;
                branch_delayed(new_pc);
                return;
            }
            switch (static_cast<unsigned>(op & 0xFFU)) {
            case 0x02:
                r_[rn] = sr_;
                return; // STC SR,Rn
            case 0x12:
                r_[rn] = gbr_;
                return; // STC GBR,Rn
            case 0x22:
                r_[rn] = vbr_;
                return; // STC VBR,Rn
            case 0x0A:
                r_[rn] = mach_;
                return; // STS MACH,Rn
            case 0x1A:
                r_[rn] = macl_;
                return; // STS MACL,Rn
            case 0x2A:
                r_[rn] = pr_;
                return; // STS PR,Rn
            case 0x03:  // BSRF Rn -- PC-relative call (delayed)
                pr_ = pc_ + 2U;
                branch_delayed(pc_ + 2U + r_[rn]);
                return;
            case 0x23:
                branch_delayed(pc_ + 2U + r_[rn]);
                return; // BRAF Rn (delayed)
            default:
                break;
            }
            switch (lo) {
            case 0x4:
                wr8(r_[rn] + r_[0], static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@(R0,Rn)
            case 0x5:
                wr16(r_[rn] + r_[0], static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@(R0,Rn)
            case 0x6:
                wr32(r_[rn] + r_[0], r_[rm]);
                return; // MOV.L Rm,@(R0,Rn)
            case 0x7:   // MUL.L Rm,Rn -> MACL
                macl_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(r_[rn]) *
                                                   static_cast<std::uint64_t>(r_[rm]));
                return;
            case 0xC:
                r_[rn] = sx_b(rd8(r_[rm] + r_[0]));
                return; // MOV.B @(R0,Rm),Rn
            case 0xD:
                r_[rn] = sx_w(rd16(r_[rm] + r_[0]));
                return; // MOV.W @(R0,Rm),Rn
            case 0xE:
                r_[rn] = rd32(r_[rm] + r_[0]);
                return; // MOV.L @(R0,Rm),Rn
            case 0xF:
                mac_long(rn, rm);
                return; // MAC.L @Rm+,@Rn+
            default:
                return; // NOP (0x0009); RTE/SLEEP deferred
            }
        case 0x1: // MOV.L Rm,@(disp,Rn) -- disp*4 store
            wr32(r_[rn] + (static_cast<std::uint32_t>(op & 0xFU) * 4U), r_[rm]);
            return;
        case 0x2:
            switch (lo) {
            case 0x0:
                wr8(r_[rn], static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@Rn
            case 0x1:
                wr16(r_[rn], static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@Rn
            case 0x2:
                wr32(r_[rn], r_[rm]);
                return; // MOV.L Rm,@Rn
            case 0x4:   // MOV.B Rm,@-Rn
                r_[rn] -= 1U;
                wr8(r_[rn], static_cast<std::uint8_t>(r_[rm]));
                return;
            case 0x5: // MOV.W Rm,@-Rn
                r_[rn] -= 2U;
                wr16(r_[rn], static_cast<std::uint16_t>(r_[rm]));
                return;
            case 0x6: // MOV.L Rm,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], r_[rm]);
                return;
            case 0x7: { // DIV0S Rm,Rn -- signed divide setup
                const std::uint32_t m = (r_[rm] >> 31U) & 1U;
                const std::uint32_t q = (r_[rn] >> 31U) & 1U;
                sr_ = (sr_ & ~(sr_m | sr_q | sr_t)) | (m != 0U ? sr_m : 0U) |
                      (q != 0U ? sr_q : 0U) | ((m ^ q) != 0U ? sr_t : 0U);
                return;
            }
            case 0x8:
                set_t((r_[rn] & r_[rm]) == 0U);
                return; // TST Rm,Rn
            case 0x9:
                r_[rn] &= r_[rm];
                return; // AND Rm,Rn
            case 0xA:
                r_[rn] ^= r_[rm];
                return; // XOR Rm,Rn
            case 0xB:
                r_[rn] |= r_[rm];
                return; // OR Rm,Rn
            case 0xC: { // CMP/STR Rm,Rn -- T=1 if any byte lane matches
                const std::uint32_t x = r_[rn] ^ r_[rm];
                set_t(((x & 0xFF000000U) == 0U) || ((x & 0x00FF0000U) == 0U) ||
                      ((x & 0x0000FF00U) == 0U) || ((x & 0x000000FFU) == 0U));
                return;
            }
            case 0xD:
                r_[rn] = (r_[rn] >> 16U) | (r_[rm] << 16U);
                return; // XTRCT Rm,Rn
            case 0xE:
                macl_ = (r_[rn] & 0xFFFFU) * (r_[rm] & 0xFFFFU);
                return; // MULU.W
            case 0xF:   // MULS.W Rm,Rn -> MACL (signed 16x16)
                macl_ = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(r_[rn] & 0xFFFFU)) *
                    static_cast<std::int32_t>(static_cast<std::int16_t>(r_[rm] & 0xFFFFU)));
                return;
            default:
                return;
            }
        case 0x3:
            switch (lo) {
            case 0x0:
                set_t(r_[rn] == r_[rm]);
                return; // CMP/EQ
            case 0x2:
                set_t(r_[rn] >= r_[rm]);
                return; // CMP/HS (unsigned)
            case 0x3:   // CMP/GE (signed)
                set_t(static_cast<std::int32_t>(r_[rn]) >= static_cast<std::int32_t>(r_[rm]));
                return;
            case 0x4: { // DIV1 Rm,Rn -- one non-restoring divide step
                const std::uint32_t m = (sr_ & sr_m) != 0U ? 1U : 0U;
                const std::uint32_t old_q = (sr_ & sr_q) != 0U ? 1U : 0U;
                const std::uint32_t t = t_in();
                const std::uint32_t q_int = (r_[rn] >> 31U) & 1U;
                r_[rn] = (r_[rn] << 1U) | t;
                const std::uint32_t pre = r_[rn];
                std::uint32_t tmp = 0U;
                std::uint32_t new_q = 0U;
                if (old_q == 0U) {
                    if (m == 0U) {
                        r_[rn] = pre - r_[rm];
                        tmp = (r_[rn] > pre) ? 1U : 0U;
                        new_q = (q_int == 0U) ? tmp : (tmp ^ 1U);
                    } else {
                        r_[rn] = pre + r_[rm];
                        tmp = (r_[rn] < pre) ? 1U : 0U;
                        new_q = (q_int == 0U) ? (tmp ^ 1U) : tmp;
                    }
                } else {
                    if (m == 0U) {
                        r_[rn] = pre + r_[rm];
                        tmp = (r_[rn] < pre) ? 1U : 0U;
                        new_q = (q_int == 0U) ? tmp : (tmp ^ 1U);
                    } else {
                        r_[rn] = pre - r_[rm];
                        tmp = (r_[rn] > pre) ? 1U : 0U;
                        new_q = (q_int == 0U) ? (tmp ^ 1U) : tmp;
                    }
                }
                sr_ = (sr_ & ~(sr_q | sr_t)) | (new_q != 0U ? sr_q : 0U) | (new_q == m ? sr_t : 0U);
                return;
            }
            case 0x5: { // DMULU.L Rm,Rn -> MACH:MACL (unsigned 32x32)
                const std::uint64_t p =
                    static_cast<std::uint64_t>(r_[rn]) * static_cast<std::uint64_t>(r_[rm]);
                macl_ = static_cast<std::uint32_t>(p);
                mach_ = static_cast<std::uint32_t>(p >> 32U);
                return;
            }
            case 0x6:
                set_t(r_[rn] > r_[rm]);
                return; // CMP/HI (unsigned)
            case 0x7:   // CMP/GT (signed)
                set_t(static_cast<std::int32_t>(r_[rn]) > static_cast<std::int32_t>(r_[rm]));
                return;
            case 0x8:
                r_[rn] -= r_[rm];
                return; // SUB
            case 0xA: { // SUBC -- subtract with T borrow-in; update T
                const std::uint64_t d = static_cast<std::uint64_t>(r_[rn]) -
                                        static_cast<std::uint64_t>(r_[rm]) - t_in();
                r_[rn] = static_cast<std::uint32_t>(d);
                set_t(((d >> 32U) & 1U) != 0U);
                return;
            }
            case 0xB: { // SUBV -- signed subtract; T = signed overflow
                const std::uint32_t a = r_[rn];
                const std::uint32_t b = r_[rm];
                const std::uint32_t diff = a - b;
                r_[rn] = diff;
                set_t((((a ^ b) & (a ^ diff)) & 0x80000000U) != 0U);
                return;
            }
            case 0xC:
                r_[rn] += r_[rm];
                return; // ADD
            case 0xD: { // DMULS.L Rm,Rn -> MACH:MACL (signed 32x32)
                const std::int64_t p =
                    static_cast<std::int64_t>(static_cast<std::int32_t>(r_[rn])) *
                    static_cast<std::int64_t>(static_cast<std::int32_t>(r_[rm]));
                const auto u = static_cast<std::uint64_t>(p);
                macl_ = static_cast<std::uint32_t>(u);
                mach_ = static_cast<std::uint32_t>(u >> 32U);
                return;
            }
            case 0xE: { // ADDC -- add with T carry-in; update T
                const std::uint64_t s = static_cast<std::uint64_t>(r_[rn]) +
                                        static_cast<std::uint64_t>(r_[rm]) + t_in();
                r_[rn] = static_cast<std::uint32_t>(s);
                set_t(s > 0xFFFFFFFFU);
                return;
            }
            case 0xF: { // ADDV -- signed add; T = signed overflow
                const std::uint32_t a = r_[rn];
                const std::uint32_t b = r_[rm];
                const std::uint32_t sum = a + b;
                r_[rn] = sum;
                set_t(((~(a ^ b) & (a ^ sum)) & 0x80000000U) != 0U);
                return;
            }
            default:
                return;
            }
        case 0x4: {
            if (lo == 0xFU) {
                mac_word(rn, rm);
                return;
            } // MAC.W @Rm+,@Rn+
            switch (static_cast<unsigned>(op & 0xFFU)) {
            case 0x00:
                set_t((r_[rn] & 0x80000000U) != 0U);
                r_[rn] <<= 1U;
                return; // SHLL
            case 0x01:
                set_t((r_[rn] & 1U) != 0U);
                r_[rn] >>= 1U;
                return; // SHLR
            case 0x20:
                set_t((r_[rn] & 0x80000000U) != 0U);
                r_[rn] <<= 1U;
                return; // SHAL
            case 0x21:  // SHAR (arithmetic right)
                set_t((r_[rn] & 1U) != 0U);
                r_[rn] = static_cast<std::uint32_t>(static_cast<std::int32_t>(r_[rn]) >> 1);
                return;
            case 0x04: { // ROTL
                const std::uint32_t msb = (r_[rn] >> 31U) & 1U;
                r_[rn] = (r_[rn] << 1U) | msb;
                set_t(msb != 0U);
                return;
            }
            case 0x05: { // ROTR
                const std::uint32_t lsb = r_[rn] & 1U;
                r_[rn] = (r_[rn] >> 1U) | (lsb << 31U);
                set_t(lsb != 0U);
                return;
            }
            case 0x24: { // ROTCL -- rotate left through T
                const std::uint32_t msb = (r_[rn] >> 31U) & 1U;
                r_[rn] = (r_[rn] << 1U) | t_in();
                set_t(msb != 0U);
                return;
            }
            case 0x25: { // ROTCR -- rotate right through T
                const std::uint32_t lsb = r_[rn] & 1U;
                r_[rn] = (r_[rn] >> 1U) | (t_in() << 31U);
                set_t(lsb != 0U);
                return;
            }
            case 0x08:
                r_[rn] <<= 2U;
                return; // SHLL2
            case 0x09:
                r_[rn] >>= 2U;
                return; // SHLR2
            case 0x18:
                r_[rn] <<= 8U;
                return; // SHLL8
            case 0x19:
                r_[rn] >>= 8U;
                return; // SHLR8
            case 0x28:
                r_[rn] <<= 16U;
                return; // SHLL16
            case 0x29:
                r_[rn] >>= 16U;
                return; // SHLR16
            case 0x10:
                r_[rn] -= 1U;
                set_t(r_[rn] == 0U);
                return; // DT
            case 0x11:
                set_t(static_cast<std::int32_t>(r_[rn]) >= 0);
                return; // CMP/PZ
            case 0x15:
                set_t(static_cast<std::int32_t>(r_[rn]) > 0);
                return;  // CMP/PL
            case 0x1B: { // TAS.B @Rn -- non-atomic RMW (dual-CPU bus lock deferred)
                const std::uint8_t v = rd8(r_[rn]);
                set_t(v == 0U);
                wr8(r_[rn], static_cast<std::uint8_t>(v | 0x80U));
                return;
            }
            case 0x0B: // JSR @Rn -- call (delayed)
                pr_ = pc_ + 2U;
                branch_delayed(r_[rn]);
                return;
            case 0x2B:
                branch_delayed(r_[rn]);
                return; // JMP @Rn (delayed)
            case 0x0E:
                sr_ = r_[rn] & sr_mask;
                interrupt_inhibit_ = 1; // SR write: defer IRQ acceptance one instruction
                return;                 // LDC Rn,SR
            case 0x1E:
                gbr_ = r_[rn];
                return; // LDC Rn,GBR
            case 0x2E:
                vbr_ = r_[rn];
                return; // LDC Rn,VBR
            case 0x0A:
                mach_ = r_[rn];
                return; // LDS Rn,MACH
            case 0x1A:
                macl_ = r_[rn];
                return; // LDS Rn,MACL
            case 0x2A:
                pr_ = r_[rn];
                return; // LDS Rn,PR
            case 0x02:  // STS.L MACH,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], mach_);
                return;
            case 0x12: // STS.L MACL,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], macl_);
                return;
            case 0x22: // STS.L PR,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], pr_);
                return;
            case 0x03: // STC.L SR,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], sr_);
                return;
            case 0x13: // STC.L GBR,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], gbr_);
                return;
            case 0x23: // STC.L VBR,@-Rn
                r_[rn] -= 4U;
                wr32(r_[rn], vbr_);
                return;
            case 0x06: // LDS.L @Rn+,MACH
                mach_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x16: // LDS.L @Rn+,MACL
                macl_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x26: // LDS.L @Rn+,PR
                pr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x07: // LDC.L @Rn+,SR
                sr_ = rd32(r_[rn]) & sr_mask;
                r_[rn] += 4U;
                interrupt_inhibit_ = 1; // SR write: defer IRQ acceptance one instruction
                return;
            case 0x17: // LDC.L @Rn+,GBR
                gbr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x27: // LDC.L @Rn+,VBR
                vbr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            default:
                return; // remaining 0x4nxx (UBC/exception entry): deferred
            }
        }
        case 0x5: // MOV.L @(disp,Rm),Rn -- disp*4 load
            r_[rn] = rd32(r_[rm] + (static_cast<std::uint32_t>(op & 0xFU) * 4U));
            return;
        case 0x6:
            switch (lo) {
            case 0x0:
                r_[rn] = sx_b(rd8(r_[rm]));
                return; // MOV.B @Rm,Rn
            case 0x1:
                r_[rn] = sx_w(rd16(r_[rm]));
                return; // MOV.W @Rm,Rn
            case 0x2:
                r_[rn] = rd32(r_[rm]);
                return; // MOV.L @Rm,Rn
            case 0x3:
                r_[rn] = r_[rm];
                return; // MOV Rm,Rn
            case 0x4: { // MOV.B @Rm+,Rn (load wins when Rm == Rn)
                const std::uint32_t v = sx_b(rd8(r_[rm]));
                r_[rm] += 1U;
                r_[rn] = v;
                return;
            }
            case 0x5: { // MOV.W @Rm+,Rn
                const std::uint32_t v = sx_w(rd16(r_[rm]));
                r_[rm] += 2U;
                r_[rn] = v;
                return;
            }
            case 0x6: { // MOV.L @Rm+,Rn
                const std::uint32_t v = rd32(r_[rm]);
                r_[rm] += 4U;
                r_[rn] = v;
                return;
            }
            case 0x7:
                r_[rn] = ~r_[rm];
                return; // NOT
            case 0x8: { // SWAP.B -- swap low two bytes
                const std::uint32_t v = r_[rm];
                r_[rn] = (v & 0xFFFF0000U) | ((v & 0x00FFU) << 8U) | ((v & 0xFF00U) >> 8U);
                return;
            }
            case 0x9:
                r_[rn] = (r_[rm] << 16U) | (r_[rm] >> 16U);
                return; // SWAP.W
            case 0xA: { // NEGC -- negate with T borrow-in
                const std::uint64_t res = 0ULL - static_cast<std::uint64_t>(r_[rm]) - t_in();
                r_[rn] = static_cast<std::uint32_t>(res);
                set_t((res >> 32U) != 0U);
                return;
            }
            case 0xB:
                r_[rn] = 0U - r_[rm];
                return; // NEG
            case 0xC:
                r_[rn] = r_[rm] & 0x000000FFU;
                return; // EXTU.B
            case 0xD:
                r_[rn] = r_[rm] & 0x0000FFFFU;
                return; // EXTU.W
            case 0xE:
                r_[rn] = sx_b(r_[rm] & 0xFFU);
                return; // EXTS.B
            case 0xF:
                r_[rn] = sx_w(r_[rm] & 0xFFFFU);
                return; // EXTS.W
            default:
                return;
            }
        case 0x7:
            r_[rn] += sx8;
            return; // ADD #imm,Rn
        case 0x8: {
            // Sub-opcode is in bits 8-11 (rn); the register operand is in bits 4-7
            // (rm), the displacement in the low nibble, and the branch displacement
            // in the low byte.
            const auto d4 = static_cast<std::uint32_t>(op & 0xFU);
            const auto d8 = static_cast<std::int32_t>(static_cast<std::int8_t>(op & 0xFFU));
            switch (rn) {
            case 0x0:
                wr8(r_[rm] + d4, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,Rn)
            case 0x1:   // MOV.W R0,@(disp,Rn)
                wr16(r_[rm] + d4 * 2U, static_cast<std::uint16_t>(r_[0]));
                return;
            case 0x4:
                r_[0] = sx_b(rd8(r_[rm] + d4));
                return; // MOV.B @(disp,Rm),R0
            case 0x5:
                r_[0] = sx_w(rd16(r_[rm] + d4 * 2U));
                return; // MOV.W @(disp,Rm),R0
            case 0x8:
                set_t(r_[0] == sx8);
                return; // CMP/EQ #imm,R0
            case 0x9:   // BT disp (no delay slot)
                if (t_in() != 0U) {
                    pc_ = pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U;
                    cycles_ += 2;
                }
                return;
            case 0xB: // BF disp (no delay slot)
                if (t_in() == 0U) {
                    pc_ = pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U;
                    cycles_ += 2;
                }
                return;
            case 0xD: // BT/S disp (delayed)
                if (t_in() != 0U) {
                    branch_delayed(pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U);
                }
                return;
            case 0xF: // BF/S disp (delayed)
                if (t_in() == 0U) {
                    branch_delayed(pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U);
                }
                return;
            default:
                return;
            }
        }
        case 0x9: // MOV.W @(disp,PC),Rn -- PC-relative word load (sign-extended)
            r_[rn] = sx_w(rd16(pc_ + (static_cast<std::uint32_t>(op & 0xFFU) * 2U) + 2U));
            return;
        case 0xA: { // BRA disp12 (delayed)
            const std::int32_t d =
                static_cast<std::int32_t>(static_cast<std::int16_t>((op & 0x0FFFU) << 4U)) >> 4;
            branch_delayed(pc_ + static_cast<std::uint32_t>(d * 2) + 2U);
            return;
        }
        case 0xB: { // BSR disp12 (delayed call)
            const std::int32_t d =
                static_cast<std::int32_t>(static_cast<std::int16_t>((op & 0x0FFFU) << 4U)) >> 4;
            const std::uint32_t target = pc_ + static_cast<std::uint32_t>(d * 2) + 2U;
            pr_ = pc_ + 2U;
            branch_delayed(target);
            return;
        }
        case 0xC: {
            const auto imm = static_cast<std::uint32_t>(op & 0xFFU);
            switch (rn) { // sub-opcode in bits 8-11
            case 0x0:
                wr8(gbr_ + imm, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,GBR)
            case 0x1:   // MOV.W R0,@(disp,GBR)
                wr16(gbr_ + imm * 2U, static_cast<std::uint16_t>(r_[0]));
                return;
            case 0x2:
                wr32(gbr_ + imm * 4U, r_[0]);
                return; // MOV.L R0,@(disp,GBR)
            case 0x4:
                r_[0] = sx_b(rd8(gbr_ + imm));
                return; // MOV.B @(disp,GBR),R0
            case 0x5:
                r_[0] = sx_w(rd16(gbr_ + imm * 2U));
                return; // MOV.W @(disp,GBR),R0
            case 0x6:
                r_[0] = rd32(gbr_ + imm * 4U);
                return; // MOV.L @(disp,GBR),R0
            case 0x7:
                r_[0] = ((pc_ + 2U) & ~3U) + imm * 4U;
                return; // MOVA @(disp,PC),R0
            case 0x8:
                set_t((r_[0] & imm) == 0U);
                return; // TST #imm,R0
            case 0x9:
                r_[0] &= imm;
                return; // AND #imm,R0
            case 0xA:
                r_[0] ^= imm;
                return; // XOR #imm,R0
            case 0xB:
                r_[0] |= imm;
                return; // OR #imm,R0
            case 0xC:
                set_t((rd8(gbr_ + r_[0]) & imm) == 0U);
                return; // TST.B #imm,@(R0,GBR)
            case 0xD: { // AND.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                wr8(a, static_cast<std::uint8_t>(rd8(a) & imm));
                return;
            }
            case 0xE: { // XOR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                wr8(a, static_cast<std::uint8_t>(rd8(a) ^ imm));
                return;
            }
            case 0xF: { // OR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                wr8(a, static_cast<std::uint8_t>(rd8(a) | imm));
                return;
            }
            case 0x3: // TRAPA #imm -- vector through VBR + imm*4; saved PC = next
                raise_exception(static_cast<std::uint8_t>(imm), pc_);
                return;
            default:
                return;
            }
        }
        case 0xD: // MOV.L @(disp,PC),Rn -- PC-relative long load
            r_[rn] = rd32(((pc_ + 2U) & ~3U) + (static_cast<std::uint32_t>(op & 0xFFU) * 4U));
            return;
        case 0xE:
            r_[rn] = sx8;
            return; // MOV #imm,Rn (sign-extended)
        default:
            return; // 0xF (no FPU on the SH7604)
        }
    }

    void sh2::branch_delayed(std::uint32_t target) {
        // SH-2 delayed control transfer: the instruction in the delay slot (the
        // one immediately after the branch) executes before control moves to the
        // target. A branch inside a delay slot is illegal on hardware (a
        // slot-illegal exception, deferred to a later phase); the re-entry guard
        // makes that case degrade safely instead of recursing.
        if (in_delay_slot_) {
            return;
        }
        in_delay_slot_ = true;
        const std::uint16_t slot = rd16(pc_);
        pc_ += 2U;
        exec(slot);
        in_delay_slot_ = false;
        pc_ = target;
        cycles_ += 2; // delay-slot op + branch-taken penalty (timing refined later)
    }

    void sh2::mac_long(std::size_t rn, std::size_t rm) noexcept {
        // MAC.L @Rm+,@Rn+ : signed 32x32 multiply, accumulate into MACH:MACL.
        // SR.S saturates the accumulator to the signed 48-bit range.
        const std::uint32_t a = rd32(r_[rm]);
        r_[rm] += 4U;
        const std::uint32_t b = rd32(r_[rn]);
        r_[rn] += 4U;
        const std::int64_t prod = static_cast<std::int64_t>(static_cast<std::int32_t>(a)) *
                                  static_cast<std::int64_t>(static_cast<std::int32_t>(b));
        const auto acc =
            static_cast<std::int64_t>((static_cast<std::uint64_t>(mach_) << 32U) | macl_);
        std::uint64_t result = 0U;
        if ((sr_ & sr_s) != 0U) {
            constexpr std::int64_t sat_max = 0x00007FFFFFFFFFFFLL;
            constexpr std::int64_t sat_min = -0x0000800000000000LL;
            const auto usum = static_cast<std::uint64_t>(acc) + static_cast<std::uint64_t>(prod);
            auto sum = static_cast<std::int64_t>(usum);
            const bool same_sign = (acc < 0) == (prod < 0);
            if (same_sign && ((sum < 0) != (acc < 0))) {
                sum = (acc < 0) ? sat_min : sat_max; // 64-bit overflow -> clamp by sign
            } else if (sum > sat_max) {
                sum = sat_max;
            } else if (sum < sat_min) {
                sum = sat_min;
            }
            result = static_cast<std::uint64_t>(sum);
        } else {
            result = static_cast<std::uint64_t>(acc) + static_cast<std::uint64_t>(prod);
        }
        macl_ = static_cast<std::uint32_t>(result);
        mach_ = static_cast<std::uint32_t>(result >> 32U);
    }

    void sh2::mac_word(std::size_t rn, std::size_t rm) noexcept {
        // MAC.W @Rm+,@Rn+ : signed 16x16 multiply-accumulate. SR.S clamps to a
        // 32-bit MACL (MACH bit 0 latches overflow); otherwise the 64-bit
        // MACH:MACL accumulates.
        const auto a = static_cast<std::int16_t>(rd16(r_[rm]));
        r_[rm] += 2U;
        const auto b = static_cast<std::int16_t>(rd16(r_[rn]));
        r_[rn] += 2U;
        const std::int64_t prod = static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);
        if ((sr_ & sr_s) != 0U) {
            std::int64_t sum = static_cast<std::int64_t>(static_cast<std::int32_t>(macl_)) + prod;
            constexpr std::int64_t s32_max = 0x7FFFFFFFLL;
            constexpr std::int64_t s32_min = -0x80000000LL;
            bool overflow = false;
            if (sum > s32_max) {
                sum = s32_max;
                overflow = true;
            } else if (sum < s32_min) {
                sum = s32_min;
                overflow = true;
            }
            macl_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum));
            if (overflow) {
                mach_ |= 1U;
            }
        } else {
            const std::uint64_t acc = (static_cast<std::uint64_t>(mach_) << 32U) | macl_;
            const std::uint64_t sum = acc + static_cast<std::uint64_t>(prod);
            macl_ = static_cast<std::uint32_t>(sum);
            mach_ = static_cast<std::uint32_t>(sum >> 32U);
        }
    }

    void sh2::raise_exception(std::uint8_t vector, std::uint32_t saved_pc) {
        // Architectural exception entry: push SR then PC to @-R15 (frame is
        // [R15]=PC, [R15+4]=SR) and vector through the table at VBR.
        const std::uint32_t saved_sr = sr_ & sr_mask;
        r_[15] -= 4U;
        wr32(r_[15], saved_sr);
        r_[15] -= 4U;
        wr32(r_[15], saved_pc);
        pc_ = rd32(vbr_ + (static_cast<std::uint32_t>(vector) << 2U));
    }

    void sh2::try_service_irq() {
        // Accept the presented external IRQ only when its level outranks SR.IMASK.
        if (pending_irq_level_ == 0) {
            return;
        }
        const auto imask = static_cast<int>((sr_ & sr_imask) >> 4U);
        if (pending_irq_level_ <= imask) {
            return;
        }
        const int level = pending_irq_level_;
        const std::uint8_t vector = pending_irq_vector_;
        raise_exception(vector, pc_); // saved PC = the interrupted boundary
        // Raise IMASK to the accepted level (clamped to the 4-bit field) so the
        // handler is not immediately re-entered.
        const std::uint32_t lv = level > 15 ? 15U : static_cast<std::uint32_t>(level);
        sr_ = (sr_ & ~sr_imask) | ((lv << 4U) & sr_imask);
        // Consume the request; the system re-presents the next source (if any)
        // from the accept callback.
        pending_irq_level_ = 0;
        pending_irq_vector_ = 0;
        if (irq_accept_) {
            irq_accept_(level, vector);
        }
    }

    int sh2::step_instruction() {
        cycles_ = 1; // SH-2 base: one instruction per cycle on a hit

        // Interrupt acceptance happens at the instruction boundary, unless a
        // preceding control-register load inhibits it for one instruction (code
        // that unmasks SR expects the next instruction to run before any newly
        // enabled IRQ fires).
        if (interrupt_inhibit_ > 0) {
            --interrupt_inhibit_;
        } else {
            try_service_irq();
        }

        inst_addr_ = pc_;
        if (trace_callback_) {
            trace_callback_(pc_);
        }
        const std::uint16_t op = rd16(pc_);
        pc_ += 2U;
        exec(op);

        elapsed_ += static_cast<std::uint64_t>(cycles_);
        return cycles_;
    }

    void sh2::tick(std::uint64_t cycles) {
        cycle_debt_ += static_cast<std::int64_t>(cycles);
        while (cycle_debt_ > 0) {
            cycle_debt_ -= step_instruction();
        }
    }

    void sh2::reset(reset_kind /*kind*/) {
        r_.fill(0U);
        pc_ = 0U;
        pr_ = 0U;
        gbr_ = 0U;
        vbr_ = 0U;
        mach_ = 0U;
        macl_ = 0U;
        inst_addr_ = 0U;
        cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
        in_delay_slot_ = false;
        pending_irq_level_ = 0;
        pending_irq_vector_ = 0;
        interrupt_inhibit_ = 0;
        // Reset: interrupts fully masked (I0-I3 = 1111), S/T cleared.
        sr_ = sr_imask;
        // The power-on reset vector lives at VBR (=0): PC at $00000000, the
        // initial R15 (SP) at $00000004, both read big-endian off the bus.
        if (bus_ != nullptr) {
            pc_ = rd32(0U);
            r_[15] = rd32(4U);
        }
    }

    sh2::registers sh2::cpu_registers() const noexcept {
        registers v;
        v.r = r_;
        v.pc = pc_;
        v.pr = pr_;
        v.sr = sr_;
        v.gbr = gbr_;
        v.vbr = vbr_;
        v.mach = mach_;
        v.macl = macl_;
        return v;
    }

    void sh2::set_registers(const registers& values) noexcept {
        r_ = values.r;
        pc_ = values.pc;
        pr_ = values.pr;
        sr_ = values.sr & sr_mask;
        gbr_ = values.gbr;
        vbr_ = values.vbr;
        mach_ = values.mach;
        macl_ = values.macl;
    }

    void sh2::save_state(state_writer& writer) const {
        for (const std::uint32_t v : r_) {
            writer.u32(v);
        }
        writer.u32(pc_);
        writer.u32(pr_);
        writer.u32(sr_);
        writer.u32(gbr_);
        writer.u32(vbr_);
        writer.u32(mach_);
        writer.u32(macl_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
        writer.u32(static_cast<std::uint32_t>(pending_irq_level_));
        writer.u32(static_cast<std::uint32_t>(pending_irq_vector_));
        writer.u32(static_cast<std::uint32_t>(interrupt_inhibit_));
    }

    void sh2::load_state(state_reader& reader) {
        for (std::uint32_t& v : r_) {
            v = reader.u32();
        }
        pc_ = reader.u32();
        pr_ = reader.u32();
        sr_ = reader.u32();
        gbr_ = reader.u32();
        vbr_ = reader.u32();
        mach_ = reader.u32();
        macl_ = reader.u32();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
        pending_irq_level_ = static_cast<int>(reader.u32());
        pending_irq_vector_ = static_cast<std::uint8_t>(reader.u32());
        interrupt_inhibit_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& sh2::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> sh2::register_snapshot() noexcept {
        using fmt = register_value_format;
        static constexpr std::array<std::string_view, 16> rn = {
            "R0", "R1", "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
            "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"};
        for (std::size_t i = 0; i < 16; ++i) {
            register_view_[i] = {rn[i], r_[i], 32U, fmt::unsigned_integer};
        }
        register_view_[16] = {"PC", pc_, 32U, fmt::unsigned_integer};
        register_view_[17] = {"PR", pr_, 32U, fmt::unsigned_integer};
        register_view_[18] = {"SR", sr_, 32U, fmt::flags};
        register_view_[19] = {"GBR", gbr_, 32U, fmt::unsigned_integer};
        register_view_[20] = {"VBR", vbr_, 32U, fmt::unsigned_integer};
        register_view_[21] = {"MACH", mach_, 32U, fmt::unsigned_integer};
        register_view_[22] = {"MACL", macl_, 32U, fmt::unsigned_integer};
        return register_view_;
    }

    sh2::introspection_surface::introspection_surface(sh2& owner) noexcept
        : trace_impl_(owner), registers_impl_(owner) {}

    void sh2::introspection_surface::trace_impl::install(callback cb) {
        if (cb) {
            sh2* cpu = owner_;
            owner_->trace_callback_ = [cpu, cb = std::move(cb)](std::uint32_t pc) {
                cb({.pc = pc, .cycles = cpu->elapsed_cycles()});
            };
        } else {
            owner_->trace_callback_ = {};
        }
    }

    std::span<const register_descriptor> sh2::introspection_surface::registers_impl::registers() {
        return owner_->register_snapshot();
    }

    namespace {
        [[maybe_unused]] const auto sh2_registration =
            register_factory("hitachi.sh7604", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<sh2>(); });
    } // namespace

} // namespace mnemos::chips::cpu
