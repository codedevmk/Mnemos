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

        // SH7604 cache-control address spaces: the associative purge area
        // ($40000000-$5FFFFFFF) and the address-array area ($60000000-$7FFFFFFF,
        // longword-only). Byte/word access (and any PC-relative or TAS access)
        // to these is an address error. The data-array area ($C0000000) is NOT
        // included: the 32X locks the cache and uses it as scratch RAM there, so
        // an access is valid RAM, not a fault.
        [[nodiscard]] constexpr bool in_cache_control_space(std::uint32_t address) noexcept {
            return address >= 0x40000000U && address < 0x80000000U;
        }

        [[nodiscard]] int add_bounded_wait_cycles(int cycles, std::uint64_t wait) noexcept {
            const auto room = static_cast<std::uint64_t>(std::numeric_limits<int>::max() - cycles);
            return wait > room ? std::numeric_limits<int>::max() : cycles + static_cast<int>(wait);
        }

        // ---- X2 load-use interlock classifiers (pure; mirror sh2::exec decode) ----
        // SH-2 register fields: Rn = bits 8-11, Rm = bits 4-7; R0 / R15 are
        // implicit operands. Bit i in a mask = general register Ri.
        [[nodiscard]] constexpr std::uint32_t rn_bit(std::uint16_t op) noexcept {
            return 1U << ((op >> 8U) & 0xFU);
        }
        [[nodiscard]] constexpr std::uint32_t rm_bit(std::uint16_t op) noexcept {
            return 1U << ((op >> 4U) & 0xFU);
        }
        constexpr std::uint32_t r0_bit = 1U << 0U;
        constexpr std::uint32_t r15_bit = 1U << 15U;

        // GPR a memory LOAD writes its loaded value into, or -1 if the opcode is
        // not a register-target memory load (so it produces no load-use latency).
        // Register-register MOV and the @Rm+ pointer write-back are NOT loads.
        [[nodiscard]] constexpr int load_destination(std::uint16_t op) noexcept {
            const unsigned n0 = (op >> 12U) & 0xFU;
            const unsigned lo = op & 0xFU;
            const unsigned sub = (op >> 8U) & 0xFU;
            const int rn = static_cast<int>((op >> 8U) & 0xFU);
            switch (n0) {
            case 0x0: // MOV.B/W/L @(R0,Rm),Rn
                return (lo == 0xCU || lo == 0xDU || lo == 0xEU) ? rn : -1;
            case 0x5: // MOV.L @(disp,Rm),Rn
                return rn;
            case 0x6: // MOV.B/W/L @Rm,Rn (0/1/2) and @Rm+,Rn (4/5/6); lo 3 = MOV Rm,Rn
                return (lo <= 0x2U || lo == 0x4U || lo == 0x5U || lo == 0x6U) ? rn : -1;
            case 0x8: // MOV.B/W @(disp,Rm),R0
                return (sub == 0x4U || sub == 0x5U) ? 0 : -1;
            case 0x9: // MOV.W @(disp,PC),Rn
                return rn;
            case 0xC: // MOV.B/W/L @(disp,GBR),R0
                return (sub == 0x4U || sub == 0x5U || sub == 0x6U) ? 0 : -1;
            case 0xD: // MOV.L @(disp,PC),Rn
                return rn;
            default:
                return -1;
            }
        }

        // LDC.L @Rm+,SR loads SR from memory; the loaded bit 0 is T, which can
        // feed the very next instruction just like a GPR memory-load result.
        [[nodiscard]] constexpr bool loads_t_from_memory(std::uint16_t op) noexcept {
            return (op & 0xF0FFU) == 0x4007U;
        }

        // Set of GPRs an opcode READS as a source consumed in EX/MA (addresses,
        // ALU operands, store data, index regs). Write-only destinations (e.g.
        // MOV #imm,Rn, the value half of a load) are excluded.
        [[nodiscard]] constexpr std::uint32_t source_reg_mask(std::uint16_t op) noexcept {
            const unsigned n0 = (op >> 12U) & 0xFU;
            const unsigned lo = op & 0xFU;
            const unsigned sub = (op >> 8U) & 0xFU;
            switch (n0) {
            case 0x0:
                switch (op & 0xFFU) {
                case 0x02U: // STC SR,Rn
                case 0x12U: // STC GBR,Rn
                case 0x22U: // STC VBR,Rn
                case 0x0AU: // STS MACH,Rn
                case 0x1AU: // STS MACL,Rn
                case 0x2AU: // STS PR,Rn
                case 0x29U: // MOVT Rn
                    return 0U;
                default:
                    break;
                }
                if (op == 0x002BU) {
                    return r15_bit; // RTE pops PC/SR from the stack
                }
                switch (lo) {
                case 0x3U: // BSRF Rn / BRAF Rn
                    return rn_bit(op);
                case 0x4U: // MOV.B/W/L Rm,@(R0,Rn): base Rn, data Rm, index R0
                case 0x5U:
                case 0x6U:
                    return rn_bit(op) | rm_bit(op) | r0_bit;
                case 0x7U: // MUL.L Rm,Rn
                    return rn_bit(op) | rm_bit(op);
                case 0xCU: // MOV.B/W/L @(R0,Rm),Rn: base Rm, index R0
                case 0xDU:
                case 0xEU:
                    return rm_bit(op) | r0_bit;
                case 0xFU: // MAC.L @Rm+,@Rn+
                    return rn_bit(op) | rm_bit(op);
                default:
                    return 0U;
                }
            case 0x1: // MOV.L Rm,@(disp,Rn): base Rn, data Rm
                return rn_bit(op) | rm_bit(op);
            case 0x2: // stores @Rn/@-Rn read Rn(addr)+Rm(data); ALU ops read both
            case 0x3:
                return rn_bit(op) | rm_bit(op);
            case 0x4: { // group-4 single-operand ops read Rn(8-11); MAC.W also Rm
                std::uint32_t m = rn_bit(op);
                if (lo == 0xFU) { // MAC.W @Rm+,@Rn+
                    m |= rm_bit(op);
                }
                return m;
            }
            case 0x5: // MOV.L @(disp,Rm),Rn
                return rm_bit(op);
            case 0x6: // @Rm/@Rm+ loads and the unary ALU ops all read Rm
                return rm_bit(op);
            case 0x7: // ADD #imm,Rn
                return rn_bit(op);
            case 0x8:
                switch (sub) {
                case 0x0U: // MOV.B/W R0,@(disp,Rn): base Rn(bits 4-7), data R0
                case 0x1U:
                    return rm_bit(op) | r0_bit;
                case 0x4U: // MOV.B/W @(disp,Rm),R0
                case 0x5U:
                    return rm_bit(op);
                case 0x8U: // CMP/EQ #imm,R0
                    return r0_bit;
                default: // BT/BF/BT.S/BF.S
                    return 0U;
                }
            case 0xC:
                switch (sub) {
                case 0x0U: // MOV.B/W/L R0,@(disp,GBR)
                case 0x1U:
                case 0x2U:
                    return r0_bit;
                case 0x3U: // TRAPA #imm
                case 0x4U: // MOV.B/W/L @(disp,GBR),R0 (loads to R0)
                case 0x5U:
                case 0x6U:
                case 0x7U: // MOVA @(disp,PC),R0 (writes R0)
                    return 0U;
                default: // TST/AND/XOR/OR #imm,R0 and the .B RMW @(R0,GBR) forms
                    return r0_bit;
                }
            default:
                // 0x9 MOV.W @(disp,PC),Rn / 0xA BRA / 0xB BSR / 0xD MOV.L @(disp,PC),Rn /
                // 0xE MOV #imm,Rn / 0xF FPU: no GPR source consumed in EX.
                return 0U;
            }
        }

        // True for instructions that consume the current T bit as input. T
        // producers such as CMP/TST/DT/TAS/SETT/CLRT are intentionally excluded.
        [[nodiscard]] constexpr bool consumes_t_bit(std::uint16_t op) noexcept {
            const unsigned n0 = (op >> 12U) & 0xFU;
            const unsigned lo = op & 0xFU;
            const unsigned sub = (op >> 8U) & 0xFU;
            switch (n0) {
            case 0x0:
                return (op & 0xFFU) == 0x29U; // MOVT Rn
            case 0x3:
                return lo == 0x4U || lo == 0xAU || lo == 0xEU; // DIV1/SUBC/ADDC
            case 0x4:
                return (op & 0xFFU) == 0x24U || (op & 0xFFU) == 0x25U; // ROTCL/ROTCR
            case 0x6:
                return lo == 0xAU; // NEGC
            case 0x8:
                return sub == 0x9U || sub == 0xBU || sub == 0xDU || sub == 0xFU; // BT/BF
            default:
                return false;
            }
        }

        // MAC.L (0x0nmF) / MAC.W (0x4nmF): the multiply-accumulate absorbs a
        // preceding load's latency, so it is exempt from the load-use stall.
        [[nodiscard]] constexpr bool is_mac(std::uint16_t op) noexcept {
            const unsigned n0 = (op >> 12U) & 0xFU;
            return (n0 == 0x0U || n0 == 0x4U) && (op & 0xFU) == 0xFU;
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
        install_fetch_invalidation(bus);
    }

    bool sh2::read_onchip_extension8(std::uint32_t a, std::uint8_t& value) noexcept {
        return onchip_extension_read_ && onchip_extension_read_(a, value);
    }

    bool sh2::write_onchip_extension8(std::uint32_t a, std::uint8_t value) noexcept {
        return onchip_extension_write_ && onchip_extension_write_(a, value);
    }

    // ---- raw memory: big-endian assembly over the byte bus ----
    // The on-chip peripheral window ($FFFFFE00..) is CPU-internal: intercept it
    // before the external bus so each core keeps its own peripheral state.
    std::uint8_t sh2::rd8(std::uint32_t a) noexcept {
        // X3 cache shadow owns CCR (the SH-2's own register) while metering; the
        // default path leaves it in the peripheral register file (bit-identical).
        if (meter_shared_contention_ && a == cache_ccr_address) {
            return ccr_;
        }
        if (sh2_peripherals::in_window(a)) {
            account_onchip_access_wait(a, true);
            std::uint8_t value = 0xFFU;
            if (read_onchip_extension8(a, value)) {
                return value;
            }
            return peripherals_.read8(a);
        }
        return bus_ != nullptr ? bus_->read8(a) : 0xFFU;
    }
    void sh2::wr8(std::uint32_t a, std::uint8_t v) noexcept {
        note_write_access(a);
        if (meter_shared_contention_ && a == cache_ccr_address) {
            cache_write_ccr(v);
            return;
        }
        if (sh2_peripherals::in_window(a)) {
            if (write_onchip_extension8(a, v)) {
                return;
            }
            peripherals_.write8(a, v);
            return;
        }
        if (bus_ != nullptr) {
            bus_->write8(a, v);
        }
    }
    std::uint16_t sh2::rd16(std::uint32_t a) noexcept {
        const bool onchip = sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 1U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a, true);
            }
            const auto read_byte = [this](std::uint32_t addr) noexcept -> std::uint8_t {
                if (sh2_peripherals::in_window(addr)) {
                    std::uint8_t value = 0xFFU;
                    return read_onchip_extension8(addr, value) ? value : peripherals_.read8(addr);
                }
                return bus_ != nullptr ? bus_->read8(addr) : 0xFFU;
            };
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(read_byte(a)) << 8U) |
                                              read_byte(a + 1U));
        }
        return bus_->read16_be(a);
    }
    void sh2::wr16(std::uint32_t a, std::uint16_t v) noexcept {
        note_write_access(a);
        const bool onchip = sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 1U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a, false);
            }
            const auto write_byte = [this](std::uint32_t addr, std::uint8_t value) noexcept {
                if (sh2_peripherals::in_window(addr)) {
                    if (write_onchip_extension8(addr, value)) {
                        return;
                    }
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
        const bool onchip = sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 3U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a, true);
            }
            const auto read_byte = [this](std::uint32_t addr) noexcept -> std::uint8_t {
                if (sh2_peripherals::in_window(addr)) {
                    std::uint8_t value = 0xFFU;
                    return read_onchip_extension8(addr, value) ? value : peripherals_.read8(addr);
                }
                return bus_ != nullptr ? bus_->read8(addr) : 0xFFU;
            };
            return (static_cast<std::uint32_t>(read_byte(a)) << 24U) |
                   (static_cast<std::uint32_t>(read_byte(a + 1U)) << 16U) |
                   (static_cast<std::uint32_t>(read_byte(a + 2U)) << 8U) | read_byte(a + 3U);
        }
        return bus_->read32_be(a);
    }
    void sh2::wr32(std::uint32_t a, std::uint32_t v) noexcept {
        note_write_access(a);
        const bool onchip = sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 3U);
        if (onchip || bus_ == nullptr) {
            if (onchip) {
                account_onchip_access_wait(a, false);
            }
            const auto write_byte = [this](std::uint32_t addr, std::uint8_t value) noexcept {
                if (sh2_peripherals::in_window(addr)) {
                    if (write_onchip_extension8(addr, value)) {
                        return;
                    }
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

    bool sh2::require_byte_data_access(std::uint32_t address, data_access_kind kind) {
        // Byte access to high on-chip space or a cache-control space faults; TAS
        // additionally faults anywhere in the on-chip window.
        if (in_high_onchip_space(address) || in_cache_control_space(address) ||
            (kind == data_access_kind::tas && sh2_peripherals::in_window(address))) {
            return signal_address_error();
        }
        record_data_access(address, 1U, kind);
        return true;
    }

    bool sh2::require_word_data_access(std::uint32_t address, bool pc_relative,
                                       data_access_kind kind) {
        // Word access faults when misaligned, in a cache-control space (those are
        // longword-only), or PC-relative into the on-chip window.
        if ((address & 1U) != 0U || in_cache_control_space(address) ||
            (pc_relative &&
             (sh2_peripherals::in_window(address) || sh2_peripherals::in_window(address + 1U)))) {
            return signal_address_error();
        }
        record_data_access(address, 2U, kind);
        return true;
    }

    bool sh2::require_long_data_access(std::uint32_t address, bool pc_relative,
                                       data_access_kind kind) {
        // Longword access is the valid size for the address-array, so a normal
        // long access there is fine; only a PC-relative long into a cache-control
        // space or the on-chip window faults (alongside misalignment and the
        // low on-chip space).
        if ((address & 3U) != 0U ||
            (pc_relative &&
             (sh2_peripherals::in_window(address) || sh2_peripherals::in_window(address + 3U) ||
              in_cache_control_space(address))) ||
            in_low_onchip_space(address) || in_low_onchip_space(address + 3U)) {
            return signal_address_error();
        }
        record_data_access(address, 4U, kind);
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
        // exception via illegal(). Opt-in timing models add the load-use
        // interlock and shared-bus contention; cache-miss timing stays deferred.
        last_exec_op_ = op; // X2: producer for the next instruction's load-use check
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
                if (!require_long_data_access_fast(r_[15]) ||
                    !require_long_data_access_fast(r_[15] + 4U)) {
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
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@(R0,Rn)
            }
            case 0x5: {
                const std::uint32_t addr = r_[rn] + r_[0];
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@(R0,Rn)
            }
            case 0x6: {
                const std::uint32_t addr = r_[rn] + r_[0];
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                wr32(addr, r_[rm]);
                return; // MOV.L Rm,@(R0,Rn)
            }
            case 0x7: // MUL.L Rm,Rn -> MACL
                macl_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(r_[rn]) *
                                                   static_cast<std::uint64_t>(r_[rm]));
                account_cycles(2);
                return;
            case 0xC: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = sx_b(rd8(addr));
                return; // MOV.B @(R0,Rm),Rn
            }
            case 0xD: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = sx_w(rd16(addr));
                return; // MOV.W @(R0,Rm),Rn
            }
            case 0xE: {
                const std::uint32_t addr = r_[rm] + r_[0];
                if (!require_long_data_access_fast(addr)) {
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
            if (!require_long_data_access_fast(addr)) {
                return;
            }
            wr32(addr, r_[rm]);
            return;
        }
        case 0x2:
            switch (lo) {
            case 0x0:
                if (!require_byte_data_access_fast(r_[rn])) {
                    return;
                }
                wr8(r_[rn], static_cast<std::uint8_t>(r_[rm]));
                return; // MOV.B Rm,@Rn
            case 0x1:
                if (!require_word_data_access_fast(r_[rn])) {
                    return;
                }
                wr16(r_[rn], static_cast<std::uint16_t>(r_[rm]));
                return; // MOV.W Rm,@Rn
            case 0x2:
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                wr32(r_[rn], r_[rm]);
                return; // MOV.L Rm,@Rn
            case 0x4: { // MOV.B Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 1U;
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr8(addr, static_cast<std::uint8_t>(r_[rm]));
                return;
            }
            case 0x5: { // MOV.W Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 2U;
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr16(addr, static_cast<std::uint16_t>(r_[rm]));
                return;
            }
            case 0x6: { // MOV.L Rm,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
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
                if (!require_byte_data_access_fast(addr, data_access_kind::tas)) {
                    return;
                }
                const std::uint8_t v = rd8(addr);
                set_t(v == 0U);
                wr8(addr, static_cast<std::uint8_t>(v | 0x80U));
                account_cycles(4);
                add_external_wait_cycles(addr, 1U, data_access_kind::tas); // locked RMW reservation
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
                return;  // LDS Rn,PR
            case 0x02: { // STS.L MACH,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, mach_);
                return;
            }
            case 0x12: { // STS.L MACL,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, macl_);
                return;
            }
            case 0x22: { // STS.L PR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, pr_);
                return;
            }
            case 0x03: { // STC.L SR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, sr_);
                account_cycles(2);
                return;
            }
            case 0x13: { // STC.L GBR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, gbr_);
                account_cycles(2);
                return;
            }
            case 0x23: { // STC.L VBR,@-Rn
                const std::uint32_t addr = r_[rn] - 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                r_[rn] = addr;
                wr32(addr, vbr_);
                account_cycles(2);
                return;
            }
            case 0x06: // LDS.L @Rn+,MACH
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                mach_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x16: // LDS.L @Rn+,MACL
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                macl_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x26: // LDS.L @Rn+,PR
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                pr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                return;
            case 0x07: // LDC.L @Rn+,SR
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                sr_ = rd32(r_[rn]) & sr_mask;
                r_[rn] += 4U;
                interrupt_inhibit_ = 1; // SR write: defer IRQ acceptance one instruction
                account_cycles(3);
                return;
            case 0x17: // LDC.L @Rn+,GBR
                if (!require_long_data_access_fast(r_[rn])) {
                    return;
                }
                gbr_ = rd32(r_[rn]);
                r_[rn] += 4U;
                account_cycles(3);
                return;
            case 0x27: // LDC.L @Rn+,VBR
                if (!require_long_data_access_fast(r_[rn])) {
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
            if (!require_long_data_access_fast(addr)) {
                return;
            }
            r_[rn] = rd32(addr);
            return;
        }
        case 0x6:
            switch (lo) {
            case 0x0:
                if (!require_byte_data_access_fast(r_[rm])) {
                    return;
                }
                r_[rn] = sx_b(rd8(r_[rm]));
                return; // MOV.B @Rm,Rn
            case 0x1:
                if (!require_word_data_access_fast(r_[rm])) {
                    return;
                }
                r_[rn] = sx_w(rd16(r_[rm]));
                return; // MOV.W @Rm,Rn
            case 0x2:
                if (!require_long_data_access_fast(r_[rm])) {
                    return;
                }
                r_[rn] = rd32(r_[rm]);
                return; // MOV.L @Rm,Rn
            case 0x3:
                r_[rn] = r_[rm];
                return; // MOV Rm,Rn
            case 0x4: { // MOV.B @Rm+,Rn (load wins when Rm == Rn)
                if (!require_byte_data_access_fast(r_[rm])) {
                    return;
                }
                const std::uint32_t v = sx_b(rd8(r_[rm]));
                r_[rm] += 1U;
                r_[rn] = v;
                return;
            }
            case 0x5: { // MOV.W @Rm+,Rn
                if (!require_word_data_access_fast(r_[rm])) {
                    return;
                }
                const std::uint32_t v = sx_w(rd16(r_[rm]));
                r_[rm] += 2U;
                r_[rn] = v;
                return;
            }
            case 0x6: { // MOV.L @Rm+,Rn
                if (!require_long_data_access_fast(r_[rm])) {
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
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,Rn)
            }
            case 0x1: // MOV.W R0,@(disp,Rn)
            {
                const std::uint32_t addr = r_[rm] + d4 * 2U;
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[0]));
                return;
            }
            case 0x4: {
                const std::uint32_t addr = r_[rm] + d4;
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                r_[0] = sx_b(rd8(addr));
                return; // MOV.B @(disp,Rm),R0
            }
            case 0x5: {
                const std::uint32_t addr = r_[rm] + d4 * 2U;
                if (!require_word_data_access_fast(addr)) {
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
            if (!require_word_data_access_fast(addr, true)) {
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
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                wr8(addr, static_cast<std::uint8_t>(r_[0]));
                return; // MOV.B R0,@(disp,GBR)
            }
            case 0x1: // MOV.W R0,@(disp,GBR)
            {
                const std::uint32_t addr = gbr_ + imm * 2U;
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                wr16(addr, static_cast<std::uint16_t>(r_[0]));
                return;
            }
            case 0x2: {
                const std::uint32_t addr = gbr_ + imm * 4U;
                if (!require_long_data_access_fast(addr)) {
                    return;
                }
                wr32(addr, r_[0]);
                return; // MOV.L R0,@(disp,GBR)
            }
            case 0x4: {
                const std::uint32_t addr = gbr_ + imm;
                if (!require_byte_data_access_fast(addr)) {
                    return;
                }
                r_[0] = sx_b(rd8(addr));
                return; // MOV.B @(disp,GBR),R0
            }
            case 0x5: {
                const std::uint32_t addr = gbr_ + imm * 2U;
                if (!require_word_data_access_fast(addr)) {
                    return;
                }
                r_[0] = sx_w(rd16(addr));
                return; // MOV.W @(disp,GBR),R0
            }
            case 0x6: {
                const std::uint32_t addr = gbr_ + imm * 4U;
                if (!require_long_data_access_fast(addr)) {
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
                if (!require_byte_data_access_fast(gbr_ + r_[0])) {
                    return;
                }
                set_t((rd8(gbr_ + r_[0]) & imm) == 0U);
                account_cycles(3);
                return; // TST.B #imm,@(R0,GBR)
            case 0xD: { // AND.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access_fast(a)) {
                    return;
                }
                wr8(a, static_cast<std::uint8_t>(rd8(a) & imm));
                account_cycles(3);
                return;
            }
            case 0xE: { // XOR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access_fast(a)) {
                    return;
                }
                wr8(a, static_cast<std::uint8_t>(rd8(a) ^ imm));
                account_cycles(3);
                return;
            }
            case 0xF: { // OR.B #imm,@(R0,GBR)
                const std::uint32_t a = gbr_ + r_[0];
                if (!require_byte_data_access_fast(a)) {
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
            if (!require_long_data_access_fast(addr, true)) {
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
        const std::uint32_t fetch_addr = pc_;
        const bool fetch_access_ok = (fetch_addr & 1U) == 0U &&
                                     !sh2_peripherals::in_window(fetch_addr) &&
                                     !sh2_peripherals::in_window(fetch_addr + 1U);
        if (!fetch_access_ok && !require_fetch_access(fetch_addr)) {
            in_delay_slot_ = false;
            pc_ = target;
            account_cycles(minimum_cycles);
            if (deferred_address_error_) {
                deferred_address_error_ = false;
                raise_address_error(pc_);
            }
            return;
        }
        // X3 (Z7b): the delay-slot fetch is the step's second fetch; look it up in
        // program order (after the branch fetch, before the slot's operand MA).
        record_fetch_access(pc_);
        const std::uint16_t slot = rd16(pc_);
        pc_ += 2U;
        exec(slot);
        in_delay_slot_ = false;
        if (exception_taken_) {
            // The delay-slot instruction vectored (slot-illegal); it owns PC now.
            return;
        }
        pc_ = target;
        if (model_load_use_) {
            // X2 accurate timing (ADR-0026): a delayed branch and its delay slot
            // are separate instructions whose execution states SUM (SH-1/SH-2 PM
            // 7.3). cycles_ holds the delay-slot states (exec(slot) floored it
            // from the base); add the branch's. Default path keeps the pre-X2
            // max-floor so the 32X stays bit-identical until the Z8 gate.
            cycles_ = add_bounded_wait_cycles(cycles_, static_cast<std::uint64_t>(minimum_cycles));
        } else {
            account_cycles(minimum_cycles);
        }
        if (deferred_address_error_) {
            deferred_address_error_ = false;
            raise_address_error(pc_);
        }
    }

    void sh2::add_external_wait_cycles(std::uint32_t address, std::uint8_t bytes,
                                       data_access_kind kind) {
        if (!bus_wait_) {
            return;
        }
        const int wait = bus_wait_(address, bytes, kind);
        if (wait > 0) {
            cycles_ = add_bounded_wait_cycles(cycles_, static_cast<std::uint64_t>(wait));
        }
    }

    void sh2::account_onchip_access_wait(std::uint32_t address, bool is_read) noexcept {
        const std::uint64_t wait = peripherals_.consume_divu_access_wait(address, is_read);
        if (wait != 0U) {
            cycles_ = add_bounded_wait_cycles(cycles_, wait);
        }
    }

    bool sh2::cache_lookup(std::uint32_t address, bool is_instruction) noexcept {
        // SH7604 cache geometry: 64 sets x 4 ways x 16-byte lines. The set index
        // is A9-A4 and the tag is A28-A10 (A31-29 = 0 selects the cacheable area;
        // the caller already gated on that). LRU is a per-set MRU->LRU way order.
        const std::size_t set = (address >> 4U) & (cache_sets - 1U);
        const std::uint32_t tag = (address >> 10U) & 0x7FFFFU;
        auto& order = cache_order_[set];
        const std::size_t base = set * cache_ways;
        const auto promote = [&order](std::size_t rank) noexcept {
            const std::uint8_t way = order[rank];
            for (std::size_t i = rank; i > 0U; --i) {
                order[i] = order[i - 1U];
            }
            order[0] = way; // move-to-front: most-recently-used
        };
        for (std::size_t rank = 0U; rank < cache_ways; ++rank) {
            const std::size_t way = order[rank];
            if (cache_valid_[base + way] && cache_tag_[base + way] == tag) {
                promote(rank);
                return true; // hit: the line is resident, no SDRAM burst
            }
        }
        // Miss: fill the least-recently-used way (the back of the order) unless
        // replacement is disabled -- ID for an instruction fetch, OD for an operand
        // (the unified cache still HITS resident lines under either disable bit).
        const std::uint8_t replace_disable = is_instruction ? ccr_id : ccr_od;
        if ((ccr_ & replace_disable) == 0U) {
            const std::size_t victim = order[cache_ways - 1U];
            cache_tag_[base + victim] = tag;
            cache_valid_[base + victim] = true;
            promote(cache_ways - 1U);
        }
        return false;
    }

    void sh2::cache_purge() noexcept {
        cache_valid_.fill(false);
        for (auto& order : cache_order_) {
            for (std::size_t way = 0U; way < cache_ways; ++way) {
                order[way] = static_cast<std::uint8_t>(way);
            }
        }
    }

    void sh2::cache_write_ccr(std::uint8_t value) noexcept {
        // CP (cache purge) is write-only and self-clearing: writing 1 invalidates
        // the array and reads back 0. The other bits latch.
        ccr_ = static_cast<std::uint8_t>(value & ~ccr_cp);
        if ((value & ccr_cp) != 0U) {
            cache_purge();
        }
    }

    void sh2::mac_long(std::size_t rn, std::size_t rm) noexcept {
        // MAC.L @Rm+,@Rn+ : signed 32x32 multiply, accumulate into MACH:MACL.
        // SR.S saturates the accumulator to the signed 48-bit range.
        if (!require_long_data_access_fast(r_[rm]) || !require_long_data_access_fast(r_[rn])) {
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
        if (!require_word_data_access_fast(r_[rm]) || !require_word_data_access_fast(r_[rn])) {
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
        //
        // Stacking fault: a misaligned SP makes the SR/PC push itself
        // address-error. The SH7604 still completes the (now-undefined) stack
        // writes, then takes the address-error exception (vector 9) WITHOUT
        // recursing or resetting -- the stacking fault is suppressed for that
        // nested entry (so the vector-9 frame can stack onto the same bad SP).
        const bool stacking_fault = (r_[15] & 3U) != 0U && vector != 9U;
        const std::uint32_t saved_sr = sr_ & sr_mask;
        r_[15] -= 4U;
        wr32(r_[15], saved_sr);
        r_[15] -= 4U;
        wr32(r_[15], saved_pc);
        if (stacking_fault) {
            raise_exception(9U, saved_pc); // one nested entry; vector 9 won't re-fault
            return;
        }
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

        const auto imask = static_cast<int>((sr_ & sr_imask) >> 4U);
        const int scan_threshold = level > imask ? level : imask;
        if (!peripherals_.onchip_irq_priority_can_exceed(scan_threshold)) {
            if (level <= imask) {
                return false;
            }
        } else {
            const sh2_peripherals::onchip_irq onchip = peripherals_.pending_onchip_irq();
            if (onchip.level > level) {
                level = onchip.level;
                vector = onchip.vector;
                external = false;
            }
        }
        if (level == 0) {
            return false;
        }
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
        cycles_ = 1;              // SH-2 base: one instruction per cycle on a hit
        shared_access_count_ = 0; // X3: per-instruction shared-access log
        fetch_access_count_ = 0;  // X3 (Z7b): per-instruction fetch-miss log

        // Interrupt acceptance happens at the instruction boundary, unless a
        // preceding control-register load inhibits it for one instruction (code
        // that unmasks SR expects the next instruction to run before any newly
        // enabled IRQ fires). An accepted interrupt also resumes a CPU halted by
        // SLEEP.
        if (interrupt_inhibit_ > 0) {
            --interrupt_inhibit_;
        } else {
            const auto imask = static_cast<int>((sr_ & sr_imask) >> 4U);
            if ((pending_irq_level_ > imask ||
                 peripherals_.onchip_irq_priority_can_exceed(imask)) &&
                try_service_irq()) {
                sleeping_ = false;
                pending_load_reg_ = -1; // the exception sequence absorbs any load latency
                pending_load_t_ = false;
            }
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
        const std::uint32_t fetch_addr = pc_;
        const bool fetch_access_ok = (fetch_addr & 1U) == 0U &&
                                     !sh2_peripherals::in_window(fetch_addr) &&
                                     !sh2_peripherals::in_window(fetch_addr + 1U);
        if (!fetch_access_ok && !require_fetch_access(fetch_addr)) {
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
                : fetch_span_refill(pc_);
        // X3 (Z7b): the instruction fetch hits/misses the unified cache shadow
        // (independent of the host-side fetch_data_ span). Lookup runs now, in
        // program order; a miss's burst is charged at end-of-step.
        record_fetch_access(fetch_addr);
        // X2: decide the load-use stall from the instruction about to run and the
        // previous instruction's load destination, BEFORE exec overwrites either.
        // The stall is charged after exec (an add, so a base-cycle floor cannot
        // swallow it). Exempt load->load to the same destination and load->MAC.
        bool load_use_stall = false;
        if (model_load_use_ && pending_load_reg_ >= 0 &&
            (source_reg_mask(op) & (1U << static_cast<unsigned>(pending_load_reg_))) != 0U &&
            load_destination(op) != pending_load_reg_ && !is_mac(op)) {
            load_use_stall = true;
        }
        if (model_load_use_ && pending_load_t_ && consumes_t_bit(op)) {
            load_use_stall = true;
        }

        pc_ += 2U;
        exec(op);

        // X3: charge shared-bus contention now that all base-cycle floors are
        // applied, so the board's wait adds on top (charging inside the access
        // would be swallowed by a later account_cycles floor). Fetch misses are
        // charged FIRST -- IF precedes MA on the bus, so a fetch fill reserves the
        // shared bus ahead of this instruction's operand accesses.
        for (int i = 0; i < fetch_access_count_; ++i) {
            add_external_wait_cycles(fetch_accesses_[static_cast<std::size_t>(i)], 2U,
                                     data_access_kind::read);
        }
        for (int i = 0; i < shared_access_count_; ++i) {
            const shared_access& a = shared_accesses_[static_cast<std::size_t>(i)];
            // Operand-cache shadow: a cacheable read that hits costs no bus cycles
            // (the line is on-chip); only a miss fills a line and pays the SDRAM
            // line-fill burst the board charges. Writes are write-through /
            // no-allocate, and non-cacheable space (A31-29 != 0) bypasses the cache.
            if (a.kind == data_access_kind::read && (ccr_ & ccr_ce) != 0U &&
                a.address < cache_area_limit && cache_lookup(a.address, /*is_instruction=*/false)) {
                continue; // hit: no external SDRAM access this access
            }
            add_external_wait_cycles(a.address, a.bytes, a.kind);
        }

        // X2: charge the +1 interlock and arm the producer for the next step (the
        // last executed op = the delay slot for a taken delayed branch). An
        // exception this step vectors away and clears pending via try_service_irq
        // next boundary; the rare faulting-load case self-corrects within a step.
        if (model_load_use_) {
            if (load_use_stall) {
                cycles_ = add_bounded_wait_cycles(cycles_, 1U);
            }
            pending_load_reg_ = load_destination(last_exec_op_);
            pending_load_t_ = loads_t_from_memory(last_exec_op_);
        }

        const std::uint64_t wait = peripherals_.tick(static_cast<std::uint64_t>(cycles_));
        cycles_ = add_bounded_wait_cycles(cycles_, wait);
        const int consumed = cycles_;
        elapsed_ += static_cast<std::uint64_t>(consumed);
        service_watchdog_reset();
        return consumed;
    }

    void sh2::tick(std::uint64_t cycles) {
        grant_cycles(cycles);
        while (has_cycle_credit()) {
            step_credited_instruction();
        }
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
        last_exec_op_ = 0U;
        pending_load_reg_ = -1;
        pending_load_t_ = false;
        ccr_ = 0U; // reset disables the cache; CP-style purge inits valid + LRU
        cache_tag_.fill(0U);
        cache_purge();
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

    // sh2 save-state format version. v1 = the original flat layout (no marker);
    // v2 prepends this marker and appends the X2 load-use interlock state.
    constexpr std::uint32_t sh2_save_state_version = 3U;

    void sh2::save_state(state_writer& writer) const {
        // sh2 state-format version. v2 adds the X2 load-use interlock state so a
        // snapshot taken between a load and its consumer reloads with identical
        // timing; v3 adds the X3 operand-cache timing shadow (CCR + tags + valid +
        // LRU) so a snapshot under the cache model reloads with identical hit/miss
        // history (the timing models are opt-in; under them this state is live).
        writer.u32(sh2_save_state_version);
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
        // X2 load-use interlock: pending_load_reg_ (-1 = none) + the SR/T variant,
        // the only cross-instruction timing state. last_exec_op_ is recomputed each
        // step (overwritten before it is read), so it needs no serialization.
        writer.u32(static_cast<std::uint32_t>(pending_load_reg_));
        writer.u32(pending_load_t_ ? 1U : 0U);
        // X3 operand-cache timing shadow (v3): CCR, then per-line tag + valid, then
        // the per-set MRU->LRU way order. History-dependent, so it must round-trip.
        writer.u8(ccr_);
        for (const std::uint32_t tag : cache_tag_) {
            writer.u32(tag);
        }
        for (const bool valid : cache_valid_) {
            writer.boolean(valid);
        }
        for (const auto& order : cache_order_) {
            for (const std::uint8_t way : order) {
                writer.u8(way);
            }
        }
        peripherals_.save_state(writer);
    }

    void sh2::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
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
        if (version >= 2U) {
            pending_load_reg_ = static_cast<int>(reader.u32());
            pending_load_t_ = reader.u32() != 0U;
        }
        if (version >= 3U) {
            ccr_ = reader.u8();
            for (std::uint32_t& tag : cache_tag_) {
                tag = reader.u32();
            }
            for (bool& valid : cache_valid_) {
                valid = reader.boolean();
            }
            for (auto& order : cache_order_) {
                for (std::uint8_t& way : order) {
                    way = reader.u8();
                }
            }
        }
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

    namespace {
        [[maybe_unused]] const auto sh2_registration =
            register_factory("hitachi.sh7604", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<sh2>(); });
    } // namespace

} // namespace mnemos::chips::cpu
