#include "sh2.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <limits>
#include <memory>

namespace mnemos::chips::cpu {

    namespace {
        constexpr std::uint32_t onchip_high_base = 0xFFFFFF00U;

        [[nodiscard]] constexpr bool in_low_onchip_space(std::uint32_t address) noexcept {
            return address >= sh2_peripherals::window_base && address < onchip_high_base;
        }

        [[nodiscard]] constexpr bool in_high_onchip_space(std::uint32_t address) noexcept {
            return address >= onchip_high_base;
        }

        [[nodiscard]] int add_bounded_wait_cycles(int cycles, std::uint64_t wait) noexcept {
            const auto room =
                static_cast<std::uint64_t>(std::numeric_limits<int>::max() - cycles);
            return wait > room ? std::numeric_limits<int>::max()
                               : cycles + static_cast<int>(wait);
        }
    } // namespace

    chip_metadata sh2::metadata() const noexcept {
        return {
            .manufacturer = "Hitachi",
            .part_number = "SH7604",
            .family = "SH-2",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void sh2::attach_bus(ibus& bus) noexcept {
        bus_ = &bus;
        peripherals_.set_bus(&bus);
        fetch_len_ = 0U;
        bus.add_invalidation_listener([this]() noexcept { fetch_len_ = 0U; });
    }

    // ---- raw memory: big-endian assembly over the byte bus ----
    // The on-chip peripheral window ($FFFFFE00..) is CPU-internal: intercept it
    // before the external bus so each core keeps its own peripheral state.
    std::uint8_t sh2::rd8(std::uint32_t a) noexcept {
        if (sh2_peripherals::in_window(a)) {
            account_onchip_access_wait(a);
            return peripherals_.read8(a);
        }
        return bus_ != nullptr ? bus_->read8(a) : 0xFFU;
    }
    void sh2::wr8(std::uint32_t a, std::uint8_t v) noexcept {
        if (sh2_peripherals::in_window(a)) {
            peripherals_.write8(a, v);
            return;
        }
        if (bus_ != nullptr) {
            bus_->write8(a, v);
        }
    }
    std::uint16_t sh2::rd16(std::uint32_t a) noexcept {
        const bool onchip =
            sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 1U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a);
            }
            const auto read_byte = [this](std::uint32_t addr) noexcept {
                return sh2_peripherals::in_window(addr)
                           ? peripherals_.read8(addr)
                           : (bus_ != nullptr ? bus_->read8(addr) : 0xFFU);
            };
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(read_byte(a)) << 8U) | read_byte(a + 1U));
        }
        return bus_->read16_be(a);
    }
    void sh2::wr16(std::uint32_t a, std::uint16_t v) noexcept {
        const bool onchip =
            sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 1U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a);
            }
            const auto write_byte = [this](std::uint32_t addr, std::uint8_t value) noexcept {
                if (sh2_peripherals::in_window(addr)) {
                    peripherals_.write8(addr, value);
                    return;
                }
                if (bus_ != nullptr) {
                    bus_->write8(addr, value);
                }
            };
            write_byte(a, static_cast<std::uint8_t>(v >> 8U));
            write_byte(a + 1U, static_cast<std::uint8_t>(v));
            return;
        }
        bus_->write16_be(a, v);
    }
    std::uint32_t sh2::rd32(std::uint32_t a) noexcept {
        const bool onchip =
            sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 3U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a);
            }
            const auto read_byte = [this](std::uint32_t addr) noexcept {
                return sh2_peripherals::in_window(addr)
                           ? peripherals_.read8(addr)
                           : (bus_ != nullptr ? bus_->read8(addr) : 0xFFU);
            };
            return (static_cast<std::uint32_t>(read_byte(a)) << 24U) |
                   (static_cast<std::uint32_t>(read_byte(a + 1U)) << 16U) |
                   (static_cast<std::uint32_t>(read_byte(a + 2U)) << 8U) | read_byte(a + 3U);
        }
        return bus_->read32_be(a);
    }
    void sh2::wr32(std::uint32_t a, std::uint32_t v) noexcept {
        const bool onchip =
            sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 3U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a);
            }
            const auto write_byte = [this](std::uint32_t addr, std::uint8_t value) noexcept {
                if (sh2_peripherals::in_window(addr)) {
                    peripherals_.write8(addr, value);
                    return;
                }
                if (bus_ != nullptr) {
                    bus_->write8(addr, value);
                }
            };
            write_byte(a, static_cast<std::uint8_t>(v >> 24U));
            write_byte(a + 1U, static_cast<std::uint8_t>(v >> 16U));
            write_byte(a + 2U, static_cast<std::uint8_t>(v >> 8U));
            write_byte(a + 3U, static_cast<std::uint8_t>(v));
            return;
        }
        bus_->write32_be(a, v);
    }

    bool sh2::raise_address_error(std::uint32_t saved_pc) {
        raise_exception(9U, saved_pc);
        return false;
    }

    bool sh2::signal_address_error() {
        if (in_delay_slot_) {
            deferred_address_error_ = true;
            return false;
        }
        return raise_address_error(pc_);
    }

    bool sh2::require_fetch_access(std::uint32_t address) {
        if ((address & 1U) != 0U || sh2_peripherals::in_window(address) ||
            sh2_peripherals::in_window(address + 1U)) {
            if (in_delay_slot_) {
                return signal_address_error();
            }
            return raise_address_error(address);
        }
        return true;
    }

    bool sh2::require_byte_data_access(std::uint32_t address, bool tas) {
        if (in_high_onchip_space(address) || (tas && sh2_peripherals::in_window(address))) {
            return signal_address_error();
        }
        return true;
    }

    bool sh2::require_word_data_access(std::uint32_t address, bool pc_relative) {
        if ((address & 1U) != 0U ||
            (pc_relative && (sh2_peripherals::in_window(address) ||
                             sh2_peripherals::in_window(address + 1U)))) {
            return signal_address_error();
        }
        return true;
    }

    bool sh2::require_long_data_access(std::uint32_t address, bool pc_relative) {
        if ((address & 3U) != 0U ||
            (pc_relative && (sh2_peripherals::in_window(address) ||
                             sh2_peripherals::in_window(address + 3U))) ||
            in_low_onchip_space(address) || in_low_onchip_space(address + 3U)) {
            return signal_address_error();
        }
        return true;
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
        // SH-2 instruction decode per the Hitachi SH7604 hardware manual. The
        // full SH7604 instruction set is implemented: data
        // transfer, ALU, logical, shift/rotate, multiply, divide-step, control
        // flow (with delay slots), system-register ops (LDS/STS/LDC/STC), all
        // addressing modes, SLEEP, and the TRAPA/RTE + external-interrupt path
        // (see step_instruction). Any undecoded encoding (including the FPU
        // opcodes, absent on the SH7604) raises the illegal-instruction
        // exception via illegal(). Still deferred: delayed-slot/peripheral-class
        // address-error tails, cache/load-use penalties, and bus contention.
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
                if (!require_long_data_access(r_[15]) || !require_long_data_access(r_[15] + 4U)) {
                    return;
                }
                const std::uint32_t new_pc = rd32(r_[15]);
                const std::uint32_t new_sr = rd32(r_[15] + 4U) & sr_mask;
                r_[15] += 8U;
                sr_ = new_sr;
                branch_delayed(new_pc, 4);
                return;
            }
            if (op == 0x0009U) { // NOP
                return;
            }
            if (op == 0x001BU) { // SLEEP -- halt until an interrupt arrives
                sleeping_ = true;
                account_cycles(3);
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
            case 0x4: {
                const std::uint32_t addr = r_[rn] + r_[0];
                if (!require_byte_data_access(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@(R0,Rn)
            }
            case 0x5: {
                const std::uint32_t addr = r_[rn] + r_[0];
                if (!require_word_data_access(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@(R0,Rn)
            }
            case 0x6: {
                const std::uint32_t addr = r_[rn] + r_[0];
                if (!require_long_data_access(addr)) {
                    return;
                }
                wr32(addr, r_[rm]);
                return; // MOV.L Rm,@(R0,Rn)
            }
            case 0x7:   // MUL.L Rm,Rn -> MACL
                macl_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(r_[rn]) *
                                                   static_cast<std::uint64_t>(r_[rm]));
                account_cycles(2);
                return;
            case 0xC: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_byte_data_access(addr)) {
                    return;
                }
                r_[rn] = sx_b(rd8(addr));
                return; // MOV.B @(R0,Rm),Rn
            }
            case 0xD: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_word_data_access(addr)) {
                    return;
                }
                r_[rn] = sx_w(rd16(addr));
                return; // MOV.W @(R0,Rm),Rn
            }
            case 0xE: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = rd32(addr);
                return; // MOV.L @(R0,Rm),Rn
            }
            case 0xF:
                mac_long(rn, rm);
                return; // MAC.L @Rm+,@Rn+
            default:
                illegal(op);
                return;
            }
        case 0x1: { // MOV.L Rm,@(disp,Rn) -- disp*4 store
            const std::uint32_t addr = r_[rn] + (static_cast<std::uint32_t>(op & 0xFU) * 4U);
            if (!require_long_data_access(addr)) {
                return;
            }
            wr32(addr, r_[rm]);
            return;
        }
        case 0x2:
            switch (lo) {
            case 0x0:
                if (!require_byte_data_access(r_[rn])) {
                    return;
                }
                wr8(r_[rn], static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@Rn
            case 0x1:
                if (!require_word_data_access(r_[rn])) {
                    return;
                }
                wr16(r_[rn], static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@Rn
            case 0x2:
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                wr32(r_[rn], r_[rm]);
                return; // MOV.L Rm,@Rn
            case 0x4: { // MOV.B Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 1U;
                if (!require_byte_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr8(addr, static_cast<std::uint8_t>(r_[rm]));
                return;
            }
            case 0x5: { // MOV.W Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 2U;
                if (!require_word_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr16(addr, static_cast<std::uint16_t>(r_[rm]));
                return;
            }
            case 0x6: { // MOV.L Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, r_[rm]);
                return;
            }
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
                illegal(op);
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
                account_cycles(2);
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
                account_cycles(2);
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
                illegal(op);
                return;
            }
        case 0x4: {
            if (lo == 0xFU) {
                account_cycles(3);
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
            case 0x1B: { // TAS.B @Rn -- locked byte RMW; board glue owns wait states
                const std::uint32_t addr = r_[rn];
                if (!require_byte_data_access(addr, true)) {
                    return;
                }
                const std::uint8_t v = rd8(addr);
                set_t(v == 0U);
                wr8(addr, static_cast<std::uint8_t>(v | 0x80U));
                account_cycles(4);
                add_external_wait_cycles(addr, 1U, true);
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
            case 0x02: { // STS.L MACH,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, mach_);
                return;
            }
            case 0x12: { // STS.L MACL,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, macl_);
                return;
            }
            case 0x22: { // STS.L PR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, pr_);
                return;
            }
            case 0x03: { // STC.L SR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, sr_);
                account_cycles(2);
                return;
            }
            case 0x13: { // STC.L GBR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, gbr_);
                account_cycles(2);
                return;
            }
            case 0x23: { // STC.L VBR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, vbr_);
                account_cycles(2);
                return;
            }
            case 0x06: // LDS.L @Rn+,MACH
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                mach_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x16: // LDS.L @Rn+,MACL
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                macl_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x26: // LDS.L @Rn+,PR
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                pr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x07: // LDC.L @Rn+,SR
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                sr_ = rd32(r_[rn]) & sr_mask;
                r_[rn] += 4U;
                interrupt_inhibit_ = 1; // SR write: defer IRQ acceptance one instruction
                account_cycles(3);
                return;
            case 0x17: // LDC.L @Rn+,GBR
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                gbr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                account_cycles(3);
                return;
            case 0x27: // LDC.L @Rn+,VBR
                if (!require_long_data_access(r_[rn])) {
                    return;
                }
                vbr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                account_cycles(3);
                return;
            default:
                illegal(op); // undefined 0x4nxx encoding
                return;
            }
        }
        case 0x5: { // MOV.L @(disp,Rm),Rn -- disp*4 load
            const std::uint32_t addr = r_[rm] + (static_cast<std::uint32_t>(op & 0xFU) * 4U);
            if (!require_long_data_access(addr)) {
                return;
            }
            r_[rn] = rd32(addr);
            return;
        }
        case 0x6:
            switch (lo) {
            case 0x0:
                if (!require_byte_data_access(r_[rm])) {
                    return;
                }
                r_[rn] = sx_b(rd8(r_[rm]));
                return; // MOV.B @Rm,Rn
            case 0x1:
                if (!require_word_data_access(r_[rm])) {
                    return;
                }
                r_[rn] = sx_w(rd16(r_[rm]));
                return; // MOV.W @Rm,Rn
            case 0x2:
                if (!require_long_data_access(r_[rm])) {
                    return;
                }
                r_[rn] = rd32(r_[rm]);
                return; // MOV.L @Rm,Rn
            case 0x3:
                r_[rn] = r_[rm];
                return; // MOV Rm,Rn
            case 0x4: { // MOV.B @Rm+,Rn (load wins when Rm == Rn)
                if (!require_byte_data_access(r_[rm])) {
                    return;
                }
                const std::uint32_t v = sx_b(rd8(r_[rm]));
                r_[rm] += 1U;
                r_[rn] = v;
                return;
            }
            case 0x5: { // MOV.W @Rm+,Rn
                if (!require_word_data_access(r_[rm])) {
                    return;
                }
                const std::uint32_t v = sx_w(rd16(r_[rm]));
                r_[rm] += 2U;
                r_[rn] = v;
                return;
            }
            case 0x6: { // MOV.L @Rm+,Rn
                if (!require_long_data_access(r_[rm])) {
                    return;
                }
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
                illegal(op);
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
            case 0x0: {
                const std::uint32_t addr = r_[rm] + d4;
                if (!require_byte_data_access(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,Rn)
            }
            case 0x1:   // MOV.W R0,@(disp,Rn)
            {
                const std::uint32_t addr = r_[rm] + d4 * 2U;
                if (!require_word_data_access(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[0]));
                return;
            }
            case 0x4: {
                const std::uint32_t addr = r_[rm] + d4;
                if (!require_byte_data_access(addr)) {
                    return;
                }
                r_[0] = sx_b(rd8(addr));
                return; // MOV.B @(disp,Rm),R0
            }
            case 0x5: {
                const std::uint32_t addr = r_[rm] + d4 * 2U;
                if (!require_word_data_access(addr)) {
                    return;
                }
                r_[0] = sx_w(rd16(addr));
                return; // MOV.W @(disp,Rm),R0
            }
            case 0x8:
                set_t(r_[0] == sx8);
                return; // CMP/EQ #imm,R0
            case 0x9:   // BT disp (no delay slot)
                if (t_in() != 0U) {
                    pc_ = pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U;
                    account_cycles(3);
                }
                return;
            case 0xB: // BF disp (no delay slot)
                if (t_in() == 0U) {
                    pc_ = pc_ + static_cast<std::uint32_t>(d8 * 2) + 2U;
                    account_cycles(3);
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
                illegal(op); // undefined 0x8nxx sub-op (2/3/6/7/A/C/E)
                return;
            }
        }
        case 0x9: { // MOV.W @(disp,PC),Rn -- PC-relative word load (sign-extended)
            const std::uint32_t addr = pc_ + (static_cast<std::uint32_t>(op & 0xFFU) * 2U) + 2U;
            if (!require_word_data_access(addr, true)) {
                return;
            }
            r_[rn] = sx_w(rd16(addr));
            return;
        }
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
            case 0x0: {
                const std::uint32_t addr = gbr_ + imm;
                if (!require_byte_data_access(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,GBR)
            }
            case 0x1:   // MOV.W R0,@(disp,GBR)
            {
                const std::uint32_t addr = gbr_ + imm * 2U;
                if (!require_word_data_access(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[0]));
                return;
            }
            case 0x2: {
                const std::uint32_t addr = gbr_ + imm * 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                wr32(addr, r_[0]);
                return; // MOV.L R0,@(disp,GBR)
            }
            case 0x4: {
                const std::uint32_t addr = gbr_ + imm;
                if (!require_byte_data_access(addr)) {
                    return;
                }
                r_[0] = sx_b(rd8(addr));
                return; // MOV.B @(disp,GBR),R0
            }
            case 0x5: {
                const std::uint32_t addr = gbr_ + imm * 2U;
                if (!require_word_data_access(addr)) {
                    return;
                }
                r_[0] = sx_w(rd16(addr));
                return; // MOV.W @(disp,GBR),R0
            }
            case 0x6: {
                const std::uint32_t addr = gbr_ + imm * 4U;
                if (!require_long_data_access(addr)) {
                    return;
                }
                r_[0] = rd32(addr);
                return; // MOV.L @(disp,GBR),R0
            }
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
                if (!require_byte_data_access(gbr_ + r_[0])) {
                    return;
                }
                set_t((rd8(gbr_ + r_[0]) & imm) == 0U);
                account_cycles(3);
                return; // TST.B #imm,@(R0,GBR)
            case 0xD: { // AND.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access(a)) {
                    return;
                }
                wr8(a, static_cast<std::uint8_t>(rd8(a) & imm));
                account_cycles(3);
                return;
            }
            case 0xE: { // XOR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access(a)) {
                    return;
                }
                wr8(a, static_cast<std::uint8_t>(rd8(a) ^ imm));
                account_cycles(3);
                return;
            }
            case 0xF: { // OR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access(a)) {
                    return;
                }
                wr8(a, static_cast<std::uint8_t>(rd8(a) | imm));
                account_cycles(3);
                return;
            }
            case 0x3: // TRAPA #imm -- vector through VBR + imm*4; saved PC = next
                raise_exception(static_cast<std::uint8_t>(imm), pc_);
                account_cycles(8);
                return;
            default:
                illegal(op);
                return;
            }
        }
        case 0xD: { // MOV.L @(disp,PC),Rn -- PC-relative long load
            const std::uint32_t addr =
                ((pc_ + 2U) & ~3U) + (static_cast<std::uint32_t>(op & 0xFFU) * 4U);
            if (!require_long_data_access(addr, true)) {
                return;
            }
            r_[rn] = rd32(addr);
            return;
        }
        case 0xE:
            r_[rn] = sx8;
            return; // MOV #imm,Rn (sign-extended)
        default:
            illegal(op); // 0xF: FPU opcodes are illegal on the SH7604
            return;
        }
    }

    void sh2::branch_delayed(std::uint32_t target, int minimum_cycles) {
        // SH-2 delayed control transfer: the instruction in the delay slot (the
        // one immediately after the branch) executes before control moves to the
        // target. A branch in a delay slot raises the slot-illegal exception
        // (vector 6) on hardware -- vector it like any other slot-illegal
        // encoding instead of silently ignoring the inner branch.
        if (in_delay_slot_) {
            raise_exception(6U, delay_resume_target_);
            return;
        }
        in_delay_slot_ = true;
        deferred_address_error_ = false;
        delay_resume_target_ = target;
        if (!require_fetch_access(pc_)) {
            in_delay_slot_ = false;
            pc_ = target;
            account_cycles(minimum_cycles);
            if (deferred_address_error_) {
                deferred_address_error_ = false;
                raise_address_error(pc_);
            }
            return;
        }
        const std::uint16_t slot = rd16(pc_);
        pc_ += 2U;
        exec(slot);
        in_delay_slot_ = false;
        if (exception_taken_) {
            // The delay-slot instruction vectored (slot-illegal); it owns PC now.
            return;
        }
        pc_ = target;
        account_cycles(minimum_cycles);
        if (deferred_address_error_) {
            deferred_address_error_ = false;
            raise_address_error(pc_);
        }
    }

    void sh2::add_external_wait_cycles(std::uint32_t address, std::uint8_t bytes, bool locked) {
        if (!bus_wait_) {
            return;
        }
        const int wait = bus_wait_(address, bytes, locked);
        if (wait > 0) {
            cycles_ = add_bounded_wait_cycles(cycles_, static_cast<std::uint64_t>(wait));
        }
    }

    void sh2::account_onchip_access_wait(std::uint32_t address) noexcept {
        const std::uint64_t wait = peripherals_.consume_divu_access_wait(address);
        if (wait != 0U) {
            cycles_ = add_bounded_wait_cycles(cycles_, wait);
        }
    }

    void sh2::mac_long(std::size_t rn, std::size_t rm) noexcept {
        // MAC.L @Rm+,@Rn+ : signed 32x32 multiply, accumulate into MACH:MACL.
        // SR.S saturates the accumulator to the signed 48-bit range.
        if (!require_long_data_access(r_[rm]) || !require_long_data_access(r_[rn])) {
            return;
        }
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
        account_cycles(3);
    }

    void sh2::mac_word(std::size_t rn, std::size_t rm) noexcept {
        // MAC.W @Rm+,@Rn+ : signed 16x16 multiply-accumulate. SR.S clamps to a
        // 32-bit MACL (MACH bit 0 latches overflow); otherwise the 64-bit
        // MACH:MACL accumulates.
        if (!require_word_data_access(r_[rm]) || !require_word_data_access(r_[rn])) {
            return;
        }
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
        exception_taken_ = true;
    }

    void sh2::illegal(std::uint16_t /*op*/) {
        // Undecoded encoding: vector through the illegal-instruction handler. In
        // a delay slot it is slot-illegal (vector 6), saving the branch's resume
        // target; otherwise general-illegal (vector 4), saving the faulting PC.
        if (in_delay_slot_) {
            raise_exception(6U, delay_resume_target_);
        } else {
            raise_exception(4U, inst_addr_);
        }
    }

    bool sh2::try_service_irq() {
        // Arbitrate two sources into the single accept path: the EXTERNAL request
        // (edge-latched, presented via set_irq) and the on-chip INTC (level-driven,
        // derived live from the peripherals). The external source wins ties.
        int level = pending_irq_level_;
        std::uint8_t vector = pending_irq_vector_;
        bool external = level > 0;

        const sh2_peripherals::onchip_irq onchip = peripherals_.pending_onchip_irq();
        if (onchip.level > level) {
            level = onchip.level;
            vector = onchip.vector;
            external = false;
        }
        if (level == 0) {
            return false;
        }
        const auto imask = static_cast<int>((sr_ & sr_imask) >> 4U);
        if (level <= imask) {
            return false;
        }

        raise_exception(vector, pc_); // saved PC = the interrupted boundary
        // Raise IMASK to the accepted level (clamped to the 4-bit field) so the
        // handler is not immediately re-entered.
        const std::uint32_t lv = level > 15 ? 15U : static_cast<std::uint32_t>(level);
        sr_ = (sr_ & ~sr_imask) | ((lv << 4U) & sr_imask);
        // Only the external (edge) source is consumed on accept; the on-chip
        // (level) source stays asserted until its handler clears the flag -- the
        // raised IMASK is what prevents an immediate re-accept.
        if (external) {
            pending_irq_level_ = 0;
            pending_irq_vector_ = 0;
            if (irq_accept_) {
                irq_accept_(level, vector);
            }
        }
        return true;
    }

    int sh2::step_instruction() {
        cycles_ = 1; // SH-2 base: one instruction per cycle on a hit

        // Interrupt acceptance happens at the instruction boundary, unless a
        // preceding control-register load inhibits it for one instruction (code
        // that unmasks SR expects the next instruction to run before any newly
        // enabled IRQ fires). An accepted interrupt also resumes a CPU halted by
        // SLEEP.
        if (interrupt_inhibit_ > 0) {
            --interrupt_inhibit_;
        } else if (try_service_irq()) {
            sleeping_ = false;
        }

        if (sleeping_) {
            const std::uint64_t wait =
                peripherals_.tick(static_cast<std::uint64_t>(cycles_)); // FRT/WDT keep running
            cycles_ = add_bounded_wait_cycles(cycles_, wait);
            const int consumed = cycles_;
            elapsed_ += static_cast<std::uint64_t>(consumed);
            service_watchdog_reset();
            return consumed; // halted: no fetch/execute until an interrupt arrives
        }

        exception_taken_ = false; // only a slot exception (set inside exec) matters
        inst_addr_ = pc_;
        if (trace_callback_) {
            trace_callback_(pc_);
        }
        if (!require_fetch_access(pc_)) {
            const std::uint64_t wait = peripherals_.tick(static_cast<std::uint64_t>(cycles_));
            cycles_ = add_bounded_wait_cycles(cycles_, wait);
            const int consumed = cycles_;
            elapsed_ += static_cast<std::uint64_t>(consumed);
            service_watchdog_reset();
            return consumed;
        }
        // Fetch through the cached direct span when the PC is inside it (the
        // span length is region-sized, far below 2^31, so the underflowing
        // subtraction of an out-of-span PC always fails the compare).
        const std::uint32_t fetch_off = pc_ - fetch_lo_;
        const std::uint16_t op =
            (fetch_off < fetch_len_ && fetch_off + 2U <= fetch_len_)
                ? static_cast<std::uint16_t>(
                      (static_cast<std::uint16_t>(fetch_data_[fetch_off]) << 8U) |
                      fetch_data_[fetch_off + 1U])
                : fetch_slow(pc_);
        pc_ += 2U;
        exec(op);

        const std::uint64_t wait = peripherals_.tick(static_cast<std::uint64_t>(cycles_));
        cycles_ = add_bounded_wait_cycles(cycles_, wait);
        const int consumed = cycles_;
        elapsed_ += static_cast<std::uint64_t>(consumed);
        service_watchdog_reset();
        return consumed;
    }

    std::uint16_t sh2::fetch_slow(std::uint32_t a) {
        // Refill the fetch span from the bus, then read through it. MMIO / the
        // on-chip window / observer-guarded buses give no span -- plain rd16.
        if (bus_ != nullptr && !sh2_peripherals::in_window(a) &&
            !sh2_peripherals::in_window(a + 1U)) {
            chips::ibus::direct_span span;
            if (bus_->direct_read_span(a, span)) {
                const std::uint32_t len = span.end - span.start;
                fetch_data_ = span.data;
                fetch_lo_ = span.start;
                fetch_len_ = len == 0xFFFFFFFFU ? len : len + 1U;
                const std::uint32_t off = a - span.start;
                if (off + 2U <= fetch_len_) {
                    return static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(span.data[off]) << 8U) | span.data[off + 1U]);
                }
            }
        }
        return rd16(a);
    }

    void sh2::tick(std::uint64_t cycles) {
        grant_cycles(cycles);
        while (has_cycle_credit()) {
            step_credited_instruction();
        }
    }

    void sh2::grant_cycles(std::uint64_t cycles) noexcept {
        cycle_debt_ += static_cast<std::int64_t>(cycles);
    }

    bool sh2::has_cycle_credit() const noexcept { return cycle_debt_ > 0; }

    int sh2::step_credited_instruction() {
        const int consumed = step_instruction();
        cycle_debt_ -= consumed;
        return consumed;
    }

    bool sh2::service_watchdog_reset() {
        const auto request = peripherals_.consume_watchdog_reset();
        if (!request.asserted) {
            return false;
        }
        if (self_reset_) {
            self_reset_(); // let the board fold elapsed_ before reset_core zeroes it
        }
        reset_core(request.kind, true);
        return true;
    }

    void sh2::reset(reset_kind kind) { reset_core(kind, false); }

    void sh2::reset_core(reset_kind /*kind*/, bool preserve_watchdog_status) {
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
        sleeping_ = false;
        exception_taken_ = false;
        deferred_address_error_ = false;
        delay_resume_target_ = 0U;
        pending_irq_level_ = 0;
        pending_irq_vector_ = 0;
        interrupt_inhibit_ = 0;
        if (preserve_watchdog_status) {
            peripherals_.reset_preserving_watchdog_status();
        } else {
            peripherals_.reset();
        }
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
        writer.u32(sleeping_ ? 1U : 0U);
        peripherals_.save_state(writer);
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
        sleeping_ = reader.u32() != 0U;
        peripherals_.load_state(reader);
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
