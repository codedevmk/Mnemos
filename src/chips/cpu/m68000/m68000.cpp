#include "m68000.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace mnemos::chips::cpu {

    void force_link_m68000_registration() noexcept;

    namespace {
        [[maybe_unused]] const auto m68000_registration_anchor =
            (force_link_m68000_registration(), 0);

        // Exception vector numbers (the address is vector * 4).
        constexpr int vec_bus_error = 2;
        constexpr int vec_address_error = 3;
        constexpr int vec_illegal = 4; // illegal instruction ($4AFC)
        constexpr int vec_divzero = 5;
        constexpr int vec_chk = 6;
        constexpr int vec_trapv = 7;
        constexpr int vec_privilege = 8;
        constexpr int vec_trace = 9;
        constexpr int vec_line_a = 10; // 1010 (line-A) emulator exception -> $28
        constexpr int vec_line_f = 11; // 1111 (line-F) emulator exception -> $2C
        constexpr int vec_trap0 = 32;
        constexpr int vec_autovector_base = 24; // autovector level n is 24 + n

        // MOVEM register-list mask. The register index i maps 0-7 -> D0-D7, 8-15 ->
        // A0-A7. In predecrement mode the mask bit order is reversed (bit 0 = A7).
        constexpr bool movem_has(std::uint16_t mask, int i, bool predec) {
            return ((mask >> (predec ? (15 - i) : i)) & 1U) != 0U;
        }
        constexpr bool reg_is_addr(int i) { return i >= 8; }
        constexpr std::size_t reg_num(int i) { return static_cast<std::size_t>(i & 7); }

        // Data-dependent DIVU/DIVS internal cycle counts (Jorge Cwik's algorithm, as
        // used by WinUAE) -- the idle clocks the handler adds on top of the prefetch.
        int divu_cycles(std::uint32_t dividend, std::uint16_t divisor) {
            if ((dividend >> 16U) >= divisor) {
                return 5 * 2 - 4;
            }
            int mcycles = 38;
            const std::uint32_t hdivisor = static_cast<std::uint32_t>(divisor) << 16U;
            for (int i = 0; i < 15; ++i) {
                const std::uint32_t temp = dividend;
                dividend <<= 1U;
                if (static_cast<std::int32_t>(temp) < 0) {
                    dividend -= hdivisor;
                } else {
                    mcycles += 2;
                    if (dividend >= hdivisor) {
                        dividend -= hdivisor;
                        --mcycles;
                    }
                }
            }
            return mcycles * 2 - 4;
        }

        int divs_cycles(std::int32_t dividend, std::int16_t divisor) {
            int mcycles = 6;
            if (dividend < 0) {
                ++mcycles;
            }
            const std::uint32_t adividend = dividend >= 0
                                                ? static_cast<std::uint32_t>(dividend)
                                                : (0U - static_cast<std::uint32_t>(dividend));
            const std::uint16_t adivisor =
                divisor >= 0 ? static_cast<std::uint16_t>(divisor)
                             : static_cast<std::uint16_t>(-static_cast<int>(divisor));
            if ((adividend >> 16U) >= adivisor) {
                return (mcycles + 2) * 2 - 4;
            }
            std::uint32_t aquot = adividend / adivisor;
            mcycles += 55;
            if (divisor >= 0) {
                mcycles += dividend >= 0 ? -1 : 1;
            }
            for (int i = 0; i < 15; ++i) {
                if (static_cast<std::int16_t>(static_cast<std::uint16_t>(aquot)) >= 0) {
                    ++mcycles;
                }
                aquot <<= 1U;
            }
            return mcycles * 2 - 4;
        }
    } // namespace

    chip_metadata m68000::metadata() const noexcept {
        return {
            .manufacturer = "Motorola",
            .part_number = "MC68000",
            .family = "68000",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- size helpers ----

    std::uint32_t m68000::size_mask(op_size s) noexcept {
        switch (s) {
        case op_size::byte:
            return 0x000000FFU;
        case op_size::word:
            return 0x0000FFFFU;
        case op_size::longword:
            break;
        }
        return 0xFFFFFFFFU;
    }

    std::uint32_t m68000::size_sign_bit(op_size s) noexcept {
        switch (s) {
        case op_size::byte:
            return 0x00000080U;
        case op_size::word:
            return 0x00008000U;
        case op_size::longword:
            break;
        }
        return 0x80000000U;
    }

    int m68000::size_bytes(op_size s) noexcept {
        switch (s) {
        case op_size::byte:
            return 1;
        case op_size::word:
            return 2;
        case op_size::longword:
            break;
        }
        return 4;
    }

    std::int32_t m68000::sign_extend(std::uint32_t v, op_size s) noexcept {
        switch (s) {
        case op_size::byte:
            return static_cast<std::int32_t>(
                static_cast<std::int8_t>(static_cast<std::uint8_t>(v)));
        case op_size::word:
            return static_cast<std::int32_t>(
                static_cast<std::int16_t>(static_cast<std::uint16_t>(v)));
        case op_size::longword:
            break;
        }
        return static_cast<std::int32_t>(v);
    }

    // ---- raw memory (24-bit masked, big-endian, no cycle accounting) ----

    std::uint8_t m68000::rd8(std::uint32_t a) const noexcept {
        return bus_ != nullptr ? bus_->read8(a & address_mask) : 0xFFU;
    }
    void m68000::wr8(std::uint32_t a, std::uint8_t v) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(a & address_mask, v);
        }
    }
    std::uint16_t m68000::rd16(std::uint32_t a) const noexcept {
        const std::uint32_t am = a & address_mask;
        if (bus_ != nullptr && am != address_mask) {
            return bus_->read16_be(am);
        }
        // Word wraps the address mask (or no bus): per-byte, each re-masked.
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(rd8(a)) << 8U) | rd8(a + 1U));
    }
    void m68000::wr16(std::uint32_t a, std::uint16_t v) noexcept {
        const std::uint32_t am = a & address_mask;
        if (bus_ != nullptr && am != address_mask) {
            bus_->write16_be(am, v);
            return;
        }
        wr8(a, static_cast<std::uint8_t>(v >> 8U));
        wr8(a + 1U, static_cast<std::uint8_t>(v));
    }
    std::uint32_t m68000::rd32(std::uint32_t a) const noexcept {
        const std::uint32_t am = a & address_mask;
        if (bus_ != nullptr && am + 3U <= address_mask) {
            return bus_->read32_be(am);
        }
        // Longword wraps the address mask (or no bus): per-word, each re-masked.
        return (static_cast<std::uint32_t>(rd16(a)) << 16U) | rd16(a + 2U);
    }

    // ---- cycle-accounted accesses (one bus cycle = 4 clocks) ----

    // Genesis Z80-bus access latency. When enabled, +1 CPU cycle (= 7
    // master clocks) per access into $A00000-$A0FFFF (Z80 RAM, YM2612,
    // VDP-via-Z80-bus). See set_z80_bus_latency_enabled().
    static constexpr bool is_z80_bus_addr(std::uint32_t a) noexcept {
        return (a & 0xFF0000U) == 0xA00000U;
    }

    void m68000::charge_bus_cycle(std::uint32_t a, bool program, bool write) noexcept {
        const std::uint32_t instruction_cycles_before_access =
            cycles_ > 0 ? static_cast<std::uint32_t>(cycles_) : 0U;
        cycles_ += 4;
        if (z80_bus_latency_enabled_ && is_z80_bus_addr(a)) {
            cycles_ += 1;
            ++cycle_sources_.z80_bus_accesses;
        }
        if (bus_wait_callback_) {
            const std::uint32_t wait = bus_wait_callback_(a & address_mask, program, write,
                                                          instruction_cycles_before_access,
                                                          cycle_sources_.external_wait_cycles);
            const int room = std::numeric_limits<int>::max() - cycles_;
            const auto clamped = static_cast<int>(
                std::min<std::uint32_t>(wait, room > 0 ? static_cast<std::uint32_t>(room) : 0U));
            cycles_ += clamped;
            cycle_sources_.external_wait_cycles += static_cast<std::uint32_t>(clamped);
        }
    }

    std::uint8_t m68000::read8(std::uint32_t a, bool program) noexcept {
        if (group0_fault_.pending) {
            return 0U;
        }
        charge_bus_cycle(a, program, false);
        // PC-relative byte read on a split board: take the byte from the decrypted
        // word (the opcode bus has no byte strobe; the lane is selected by A0).
        const std::uint8_t value =
            (program && bus_ != nullptr)
                ? static_cast<std::uint8_t>(bus_->fetch16_be_opcode(a & ~1U) >>
                                            ((a & 1U) != 0U ? 0U : 8U))
                : rd8(a);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
            return 0U;
        }
        return value;
    }
    std::uint16_t m68000::read16(std::uint32_t a, bool program) noexcept {
        if (group0_fault_.pending) {
            return 0U;
        }
        if (!exception_entry_ && (a & 1U) != 0U) {
            queue_address_error(a, inst_addr_, false, true);
            return 0U;
        }
        charge_bus_cycle(a, program, false);
        const std::uint16_t value =
            (program && bus_ != nullptr) ? bus_->fetch16_be_opcode(a) : rd16(a);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
            return 0U;
        }
        return value;
    }
    std::uint32_t m68000::read32(std::uint32_t a, bool program) noexcept {
        if (group0_fault_.pending) {
            return 0U;
        }
        if (!exception_entry_ && (a & 1U) != 0U) {
            queue_address_error(a, inst_addr_, false, true);
            return 0U;
        }
        charge_bus_cycle(a, program, false);
        charge_bus_cycle(a + 2U, program, false);
        const std::uint32_t value =
            (program && bus_ != nullptr)
                ? ((static_cast<std::uint32_t>(bus_->fetch16_be_opcode(a)) << 16U) |
                   bus_->fetch16_be_opcode(a + 2U))
                : rd32(a);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
            return 0U;
        }
        return value;
    }
    void m68000::write8(std::uint32_t a, std::uint8_t v) noexcept {
        if (group0_fault_.pending) {
            return;
        }
        charge_bus_cycle(a, false, true);
        wr8(a, v);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
        }
    }
    void m68000::write16(std::uint32_t a, std::uint16_t v) noexcept {
        if (group0_fault_.pending) {
            return;
        }
        if (!exception_entry_ && (a & 1U) != 0U) {
            queue_address_error(a, inst_addr_, false, false);
            return;
        }
        charge_bus_cycle(a, false, true);
        wr16(a, v);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
        }
    }
    void m68000::write32(std::uint32_t a, std::uint32_t v) noexcept {
        if (group0_fault_.pending) {
            return;
        }
        if (!exception_entry_ && (a & 1U) != 0U) {
            queue_address_error(a, inst_addr_, false, false);
            return;
        }
        // A longword is two word bus transfers of 4 cycles each. Charge each word's
        // cost BEFORE its own wr16 so a mid-instruction observer (e.g. the VDP
        // write-timing trace) sees the correct cumulative cycles for each halfword
        // -- the first word must read +4, not the full +8. Total cost (+8 plus any
        // Z80-bus latency) and the per-access latency attribution are unchanged.
        charge_bus_cycle(a, false, true);
        wr16(a, static_cast<std::uint16_t>(v >> 16U));
        ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
            return;
        }
        charge_bus_cycle(a + 2U, false, true);
        wr16(a + 2U, static_cast<std::uint16_t>(v));
        fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, inst_addr_, false, !fault.write);
        }
    }

    std::uint32_t m68000::read_sized(std::uint32_t a, op_size s, bool program) noexcept {
        switch (s) {
        case op_size::byte:
            return read8(a, program);
        case op_size::word:
            return read16(a, program);
        case op_size::longword:
            break;
        }
        return read32(a, program);
    }
    void m68000::write_sized(std::uint32_t a, op_size s, std::uint32_t v) noexcept {
        switch (s) {
        case op_size::byte:
            write8(a, static_cast<std::uint8_t>(v));
            return;
        case op_size::word:
            write16(a, static_cast<std::uint16_t>(v));
            return;
        case op_size::longword:
            break;
        }
        write32(a, v);
    }

    // ---- instruction stream ----

    void m68000::attach_bus(ibus& bus) noexcept {
        bus_ = &bus;
        install_fetch_invalidation(bus);
    }

    std::uint16_t m68000::fetch16() noexcept {
        if (group0_fault_.pending) {
            return 0U;
        }
        const std::uint32_t a = pc_ & address_mask;
        charge_bus_cycle(a, true, false);
        if (!exception_entry_ && (a & 1U) != 0U) {
            queue_address_error(a, a, true, true);
            return 0U;
        }
        pc_ += 2U;
        // Fetch through the cached direct span when the PC is inside it (span
        // lengths sit far below 2^31, so an out-of-span subtraction always
        // fails the compare).
        const std::uint32_t off = a - fetch_lo_;
        const std::uint16_t word =
            (off < fetch_len_ && off + 2U <= fetch_len_)
                ? static_cast<std::uint16_t>((static_cast<std::uint16_t>(fetch_data_[off]) << 8U) |
                                             fetch_data_[off + 1U])
                : fetch_span_refill(a);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, a, true, !fault.write);
            return 0U;
        }
        return word;
    }
    std::uint32_t m68000::fetch32() noexcept {
        const std::uint32_t hi = fetch16();
        if (group0_fault_.pending) {
            return 0U;
        }
        const std::uint32_t a = pc_ & address_mask;
        charge_bus_cycle(a, true, false);
        pc_ += 2U;
        const std::uint32_t off = a - fetch_lo_;
        const std::uint32_t lo =
            (off < fetch_len_ && off + 2U <= fetch_len_)
                ? static_cast<std::uint32_t>((static_cast<std::uint32_t>(fetch_data_[off]) << 8U) |
                                             fetch_data_[off + 1U])
                : fetch_span_refill(a);
        const ibus::bus_fault fault = consume_bus_fault();
        if (!exception_entry_ && fault.asserted) {
            queue_bus_error(fault.address, a, true, !fault.write);
            return 0U;
        }
        return (hi << 16U) | lo;
    }

    // ---- effective-address resolution ----

    std::uint32_t m68000::decode_extension(std::uint32_t base) noexcept {
        const std::uint16_t e = fetch16();
        const int xr = (e >> 12U) & 7;
        const bool address_index = (e & 0x8000U) != 0U;
        const bool long_index = (e & 0x0800U) != 0U;
        const auto d8 = static_cast<std::int8_t>(static_cast<std::uint8_t>(e & 0xFFU));
        auto idx = static_cast<std::int32_t>(address_index ? a_[static_cast<std::size_t>(xr)]
                                                           : d_[static_cast<std::size_t>(xr)]);
        if (!long_index) {
            idx = static_cast<std::int32_t>(
                static_cast<std::int16_t>(static_cast<std::uint16_t>(idx)));
        }
        cycles_ += 2; // index-calculation idle
        return base + static_cast<std::uint32_t>(static_cast<std::int32_t>(d8)) +
               static_cast<std::uint32_t>(idx);
    }

    int m68000::ea_increment(int reg, op_size s) const noexcept {
        return (s == op_size::byte && reg == 7) ? 2 : size_bytes(s);
    }

    std::uint32_t m68000::ea_address(int mode, int reg, op_size s, bool adjust) noexcept {
        const auto r = static_cast<std::size_t>(reg);
        switch (mode) {
        case 2: // (An)
            return a_[r];
        case 3: { // (An)+
            const std::uint32_t addr = a_[r];
            if (adjust) {
                a_[r] += static_cast<std::uint32_t>(ea_increment(reg, s));
            }
            return addr;
        }
        case 4: // -(An)
            if (adjust) {
                a_[r] -= static_cast<std::uint32_t>(ea_increment(reg, s));
            }
            cycles_ += 2;
            return a_[r];
        case 5: { // d16(An)
            const auto d = static_cast<std::int16_t>(fetch16());
            return a_[r] + static_cast<std::uint32_t>(static_cast<std::int32_t>(d));
        }
        case 6: // d8(An,Xn)
            return decode_extension(a_[r]);
        case 7:
            switch (reg) {
            case 0: // abs.W
                return static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(fetch16())));
            case 1: // abs.L
                return fetch32();
            case 2: { // d16(PC)
                const std::uint32_t base = pc_;
                const auto d = static_cast<std::int16_t>(fetch16());
                return base + static_cast<std::uint32_t>(static_cast<std::int32_t>(d));
            }
            case 3: { // d8(PC,Xn)
                const std::uint32_t base = pc_;
                return decode_extension(base);
            }
            default:
                break;
            }
            break;
        default:
            break;
        }
        return 0U;
    }

    std::uint32_t m68000::ea_read(int mode, int reg, op_size s) noexcept {
        const auto r = static_cast<std::size_t>(reg);
        if (mode == 0) {
            return d_[r] & size_mask(s);
        }
        if (mode == 1) {
            return a_[r] & size_mask(s);
        }
        if (mode == 7 && reg == 4) { // #immediate
            if (s == op_size::longword) {
                return fetch32();
            }
            const std::uint16_t w = fetch16();
            return s == op_size::byte ? (w & 0xFFU) : w;
        }
        return read_sized(ea_address(mode, reg, s, true), s, is_pc_relative(mode, reg));
    }

    void m68000::ea_write(int mode, int reg, op_size s, std::uint32_t value) noexcept {
        const auto r = static_cast<std::size_t>(reg);
        const std::uint32_t mask = size_mask(s);
        value &= mask;
        if (mode == 0) {
            d_[r] = (d_[r] & ~mask) | value;
            return;
        }
        if (mode == 1) { // MOVE proper routes An destinations through MOVEA, not here
            a_[r] = value;
            return;
        }
        write_sized(ea_address(mode, reg, s, true), s, value);
    }

    // ---- read-modify-write EA (resolve the address once) ----

    std::uint32_t m68000::ea_rmw_read(int mode, int reg, op_size s, std::uint32_t& addr) noexcept {
        const auto r = static_cast<std::size_t>(reg);
        if (mode == 0) {
            addr = 0;
            return d_[r] & size_mask(s);
        }
        if (mode == 1) {
            addr = 0;
            return a_[r] & size_mask(s);
        }
        addr = ea_address(mode, reg, s, true);
        return read_sized(addr, s, is_pc_relative(mode, reg));
    }

    void m68000::ea_rmw_write(int mode, int reg, op_size s, std::uint32_t value,
                              std::uint32_t addr) noexcept {
        const auto r = static_cast<std::size_t>(reg);
        const std::uint32_t mask = size_mask(s);
        value &= mask;
        if (mode == 0) {
            d_[r] = (d_[r] & ~mask) | value;
            return;
        }
        if (mode == 1) {
            a_[r] = value;
            return;
        }
        write_sized(addr, s, value);
    }

    // ---- flags ----

    void m68000::flags_add(op_size s, std::uint32_t src, std::uint32_t dst,
                           std::uint32_t r) noexcept {
        const std::uint32_t m = size_mask(s);
        const std::uint32_t b = size_sign_bit(s);
        src &= m;
        dst &= m;
        r &= m;
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_x | sr_n | sr_z | sr_v | sr_c));
        if (r == 0U) {
            sr_ |= sr_z;
        }
        if ((r & b) != 0U) {
            sr_ |= sr_n;
        }
        if (((src ^ r) & (dst ^ r) & b) != 0U) {
            sr_ |= sr_v;
        }
        if (static_cast<std::uint64_t>(src) + dst > m) {
            sr_ |= sr_c | sr_x;
        }
    }

    void m68000::flags_sub(op_size s, std::uint32_t src, std::uint32_t dst,
                           std::uint32_t r) noexcept {
        const std::uint32_t m = size_mask(s);
        const std::uint32_t b = size_sign_bit(s);
        src &= m;
        dst &= m;
        r &= m;
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_x | sr_n | sr_z | sr_v | sr_c));
        if (r == 0U) {
            sr_ |= sr_z;
        }
        if ((r & b) != 0U) {
            sr_ |= sr_n;
        }
        if (((src ^ dst) & (r ^ dst) & b) != 0U) {
            sr_ |= sr_v;
        }
        if (src > dst) {
            sr_ |= sr_c | sr_x;
        }
    }

    void m68000::flags_cmp(op_size s, std::uint32_t src, std::uint32_t dst,
                           std::uint32_t r) noexcept {
        const std::uint32_t m = size_mask(s);
        const std::uint32_t b = size_sign_bit(s);
        src &= m;
        dst &= m;
        r &= m;
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c)); // CMP leaves X
        if (r == 0U) {
            sr_ |= sr_z;
        }
        if ((r & b) != 0U) {
            sr_ |= sr_n;
        }
        if (((src ^ dst) & (r ^ dst) & b) != 0U) {
            sr_ |= sr_v;
        }
        if (src > dst) {
            sr_ |= sr_c;
        }
    }

    void m68000::flags_addx(op_size s, std::uint32_t src, std::uint32_t dst,
                            std::uint32_t x) noexcept {
        const std::uint32_t m = size_mask(s);
        const std::uint32_t b = size_sign_bit(s);
        src &= m;
        dst &= m;
        const std::uint64_t f = static_cast<std::uint64_t>(src) + dst + x;
        const auto r = static_cast<std::uint32_t>(f) & m;
        const auto prev_z = static_cast<std::uint16_t>(sr_ & sr_z);
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_x | sr_n | sr_z | sr_v | sr_c));
        if ((r & b) != 0U) {
            sr_ |= sr_n;
        }
        if (((src ^ r) & (dst ^ r) & b) != 0U) {
            sr_ |= sr_v;
        }
        if (f > m) {
            sr_ |= sr_c | sr_x;
        }
        // ADDX only ever clears Z, never sets it (multi-precision semantics).
        if (r != 0U) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
        } else {
            sr_ |= prev_z;
        }
    }

    void m68000::flags_subx(op_size s, std::uint32_t src, std::uint32_t dst,
                            std::uint32_t x) noexcept {
        const std::uint32_t m = size_mask(s);
        const std::uint32_t b = size_sign_bit(s);
        src &= m;
        dst &= m;
        const std::uint64_t borrow = static_cast<std::uint64_t>(src) + x;
        const std::uint32_t r = (dst - src - x) & m;
        const auto prev_z = static_cast<std::uint16_t>(sr_ & sr_z);
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_x | sr_n | sr_z | sr_v | sr_c));
        if ((r & b) != 0U) {
            sr_ |= sr_n;
        }
        if (((src ^ dst) & (r ^ dst) & b) != 0U) {
            sr_ |= sr_v;
        }
        if (borrow > dst) {
            sr_ |= sr_c | sr_x;
        }
        if (r != 0U) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
        } else {
            sr_ |= prev_z;
        }
    }

    int m68000::popcount16(std::uint16_t v) noexcept {
        int n = 0;
        while (v != 0U) {
            v = static_cast<std::uint16_t>(v & (v - 1U));
            ++n;
        }
        return n;
    }

    std::uint8_t m68000::bcd_add(std::uint8_t dst, std::uint8_t src) noexcept {
        const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
        std::uint32_t result = static_cast<std::uint32_t>(src) + dst + x;
        bool carry = false;
        bool overflow = false;
        if (((static_cast<std::uint32_t>(src) ^ dst ^ result) & 0x10U) != 0U ||
            (result & 0x0FU) >= 0x0AU) {
            const std::uint32_t prev = result;
            result += 0x06U;
            if (((~prev & result) & 0x80U) != 0U) {
                overflow = true;
            }
        }
        if (result >= 0xA0U) {
            const std::uint32_t prev = result;
            result += 0x60U;
            carry = true;
            if (((~prev & result) & 0x80U) != 0U) {
                overflow = true;
            }
        }
        const auto r = static_cast<std::uint8_t>(result);
        sr_ = static_cast<std::uint16_t>(carry ? (sr_ | sr_c | sr_x) : (sr_ & ~(sr_c | sr_x)));
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_v));
        if (overflow) {
            sr_ |= sr_v;
        }
        if ((r & 0x80U) != 0U) {
            sr_ |= sr_n;
        }
        if (r != 0U) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
        }
        return r;
    }

    std::uint8_t m68000::bcd_sub(std::uint8_t dst, std::uint8_t src) noexcept {
        const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
        std::uint32_t result = static_cast<std::uint32_t>(dst) - src - x;
        bool carry = false;
        bool overflow = false;
        const bool adjust_lo = ((static_cast<std::uint32_t>(dst) ^ src ^ result) & 0x10U) != 0U;
        const bool adjust_hi = (result & 0x100U) != 0U;
        if (adjust_lo) {
            const std::uint32_t prev = result;
            result -= 0x06U;
            carry = ((~prev & result) & 0x80U) != 0U;
            if (((prev & ~result) & 0x80U) != 0U) {
                overflow = true;
            }
        }
        if (adjust_hi) {
            const std::uint32_t prev = result;
            result -= 0x60U;
            carry = true;
            if (((prev & ~result) & 0x80U) != 0U) {
                overflow = true;
            }
        }
        const auto r = static_cast<std::uint8_t>(result);
        sr_ = static_cast<std::uint16_t>(carry ? (sr_ | sr_c | sr_x) : (sr_ & ~(sr_c | sr_x)));
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_v));
        if (overflow) {
            sr_ |= sr_v;
        }
        if ((r & 0x80U) != 0U) {
            sr_ |= sr_n;
        }
        if (r != 0U) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
        }
        return r;
    }

    void m68000::set_logic_flags(op_size s, std::uint32_t value) noexcept {
        const bool negative = (value & size_sign_bit(s)) != 0U;
        const bool zero = (value & size_mask(s)) == 0U;
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c));
        if (negative) {
            sr_ |= sr_n;
        }
        if (zero) {
            sr_ |= sr_z;
        }
    }

    // ---- instructions ----

    void m68000::op_move(std::uint16_t op) {
        op_size sz{};
        switch ((op >> 12U) & 3U) {
        case 1:
            sz = op_size::byte;
            break;
        case 2:
            sz = op_size::longword;
            break;
        case 3:
            sz = op_size::word;
            break;
        default:
            return; // 0 is not a MOVE encoding
        }

        const int sm = (op >> 3U) & 7;
        const int sreg = op & 7;
        const int dr = (op >> 9U) & 7;
        const int dm = (op >> 6U) & 7;

        const std::uint32_t v = ea_read(sm, sreg, sz);

        if (dm == 1) { // MOVEA: no flags; word source sign-extends to 32 bits
            a_[static_cast<std::size_t>(dr)] =
                sz == op_size::word ? static_cast<std::uint32_t>(sign_extend(v, op_size::word)) : v;
            return;
        }
        set_logic_flags(sz, v);
        ea_write(dm, dr, sz, v);
        if (dm == 4) {
            // MOVE -(An) destination doesn't pay the predec-calc penalty
            // (it overlaps with the source fetch in the bus pipeline); the
            // generic ea_address adds +2 for -(An) that we refund here.
            cycles_ -= 2;
        }
    }

    void m68000::op_moveq(std::uint16_t op) noexcept {
        const int dr = (op >> 9U) & 7;
        const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int8_t>(static_cast<std::uint8_t>(op & 0xFFU))));
        d_[static_cast<std::size_t>(dr)] = value;
        set_logic_flags(op_size::longword, value);
    }

    void m68000::op_add(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7, dn = (op >> 9U) & 7, opm = (op >> 6U) & 7;
        if (opm >= 4 && opm <= 6 && (em == 0 || em == 1)) { // ADDX
            const op_size sz = static_cast<op_size>(opm - 4);
            const std::uint32_t m = size_mask(sz);
            const bool mem = em == 1;
            std::uint32_t src{};
            std::uint32_t dst{};
            if (mem) {
                a_[static_cast<std::size_t>(er)] -=
                    static_cast<std::uint32_t>(ea_increment(er, sz));
                src = read_sized(a_[static_cast<std::size_t>(er)], sz);
                a_[static_cast<std::size_t>(dn)] -=
                    static_cast<std::uint32_t>(ea_increment(dn, sz));
                dst = read_sized(a_[static_cast<std::size_t>(dn)], sz);
                cycles_ += 2;
            } else {
                src = d_[static_cast<std::size_t>(er)] & m;
                dst = d_[static_cast<std::size_t>(dn)] & m;
                if (sz == op_size::longword) {
                    cycles_ += 4;
                }
            }
            const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
            const std::uint32_t r = (dst + src + x) & m;
            flags_addx(sz, src, dst, x);
            if (mem) {
                write_sized(a_[static_cast<std::size_t>(dn)], sz, r);
            } else {
                d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | r;
            }
            return;
        }
        if (opm == 3 || opm == 7) { // ADDA
            const op_size sz = opm == 3 ? op_size::word : op_size::longword;
            a_[static_cast<std::size_t>(dn)] +=
                static_cast<std::uint32_t>(sign_extend(ea_read(em, er, sz), sz));
            cycles_ += opm == 3 ? 4 : ((em <= 1 || (em == 7 && er == 4)) ? 4 : 2);
            return;
        }
        const op_size sz = static_cast<op_size>(opm & 3);
        const std::uint32_t m = size_mask(sz);
        if (opm < 3) { // ADD <ea>,Dn
            const std::uint32_t src = ea_read(em, er, sz);
            const std::uint32_t dst = d_[static_cast<std::size_t>(dn)] & m;
            const std::uint32_t r = dst + src;
            d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | (r & m);
            flags_add(sz, src, dst, r);
            if (sz == op_size::longword) {
                cycles_ += (em <= 1 || (em == 7 && er == 4)) ? 4 : 2;
            }
        } else { // ADD Dn,<ea>
            std::uint32_t addr = 0;
            const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
            const std::uint32_t src = d_[static_cast<std::size_t>(dn)] & m;
            const std::uint32_t r = dst + src;
            ea_rmw_write(em, er, sz, r, addr);
            flags_add(sz, src, dst, r);
        }
    }

    void m68000::op_sub(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7, dn = (op >> 9U) & 7, opm = (op >> 6U) & 7;
        if (opm >= 4 && opm <= 6 && (em == 0 || em == 1)) { // SUBX
            const op_size sz = static_cast<op_size>(opm - 4);
            const std::uint32_t m = size_mask(sz);
            const bool mem = em == 1;
            std::uint32_t src{};
            std::uint32_t dst{};
            if (mem) {
                a_[static_cast<std::size_t>(er)] -=
                    static_cast<std::uint32_t>(ea_increment(er, sz));
                src = read_sized(a_[static_cast<std::size_t>(er)], sz);
                a_[static_cast<std::size_t>(dn)] -=
                    static_cast<std::uint32_t>(ea_increment(dn, sz));
                dst = read_sized(a_[static_cast<std::size_t>(dn)], sz);
                cycles_ += 2;
            } else {
                src = d_[static_cast<std::size_t>(er)] & m;
                dst = d_[static_cast<std::size_t>(dn)] & m;
                if (sz == op_size::longword) {
                    cycles_ += 4;
                }
            }
            const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
            const std::uint32_t r = (dst - src - x) & m;
            flags_subx(sz, src, dst, x);
            if (mem) {
                write_sized(a_[static_cast<std::size_t>(dn)], sz, r);
            } else {
                d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | r;
            }
            return;
        }
        if (opm == 3 || opm == 7) { // SUBA
            const op_size sz = opm == 3 ? op_size::word : op_size::longword;
            a_[static_cast<std::size_t>(dn)] -=
                static_cast<std::uint32_t>(sign_extend(ea_read(em, er, sz), sz));
            cycles_ += opm == 3 ? 4 : ((em <= 1 || (em == 7 && er == 4)) ? 4 : 2);
            return;
        }
        const op_size sz = static_cast<op_size>(opm & 3);
        const std::uint32_t m = size_mask(sz);
        if (opm < 3) { // SUB <ea>,Dn
            const std::uint32_t src = ea_read(em, er, sz);
            const std::uint32_t dst = d_[static_cast<std::size_t>(dn)] & m;
            const std::uint32_t r = dst - src;
            d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | (r & m);
            flags_sub(sz, src, dst, r);
            if (sz == op_size::longword) {
                cycles_ += (em <= 1 || (em == 7 && er == 4)) ? 4 : 2;
            }
        } else { // SUB Dn,<ea>
            std::uint32_t addr = 0;
            const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
            const std::uint32_t src = d_[static_cast<std::size_t>(dn)] & m;
            const std::uint32_t r = dst - src;
            ea_rmw_write(em, er, sz, r, addr);
            flags_sub(sz, src, dst, r);
        }
    }

    void m68000::op_cmp(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7, dn = (op >> 9U) & 7, opm = (op >> 6U) & 7;
        if (opm == 3 || opm == 7) { // CMPA (compares the full 32-bit address register)
            const op_size sz = opm == 3 ? op_size::word : op_size::longword;
            const std::int32_t src = sign_extend(ea_read(em, er, sz), sz);
            // Unsigned subtraction: An - src in int32 overflows (UB) on ordinary
            // guest values like CMPA.L against a high address.
            const std::uint32_t r =
                a_[static_cast<std::size_t>(dn)] - static_cast<std::uint32_t>(src);
            flags_cmp(op_size::longword, static_cast<std::uint32_t>(src),
                      a_[static_cast<std::size_t>(dn)], r);
            cycles_ += 2;
            return;
        }
        if (opm >= 4 && em == 1) { // CMPM (Ay)+,(Ax)+
            const op_size sz = static_cast<op_size>(opm - 4);
            const std::uint32_t src = read_sized(a_[static_cast<std::size_t>(er)], sz);
            a_[static_cast<std::size_t>(er)] += static_cast<std::uint32_t>(ea_increment(er, sz));
            const std::uint32_t dst = read_sized(a_[static_cast<std::size_t>(dn)], sz);
            a_[static_cast<std::size_t>(dn)] += static_cast<std::uint32_t>(ea_increment(dn, sz));
            flags_cmp(sz, src, dst, dst - src);
            return;
        }
        if (opm < 3) { // CMP <ea>,Dn
            const op_size sz = static_cast<op_size>(opm);
            const std::uint32_t src = ea_read(em, er, sz);
            const std::uint32_t dst = d_[static_cast<std::size_t>(dn)] & size_mask(sz);
            flags_cmp(sz, src, dst, dst - src);
            if (sz == op_size::longword) {
                cycles_ += 2;
            }
        } else { // EOR Dn,<ea> (opm 4-6, em != 1; CMPM with em == 1 handled above)
            const op_size sz = static_cast<op_size>(opm - 4);
            std::uint32_t addr = 0;
            const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
            const std::uint32_t res = dst ^ (d_[static_cast<std::size_t>(dn)] & size_mask(sz));
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
            if (em == 0 && sz == op_size::longword) {
                cycles_ += 4;
            }
        }
    }

    void m68000::op_mul(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7, dn = (op >> 9U) & 7, opm = (op >> 6U) & 7;
        const auto dni = static_cast<std::size_t>(dn);
        const auto eri = static_cast<std::size_t>(er);
        if ((op & 0xF1F0U) == 0xC100U) { // ABCD
            const bool mem = (op & 8U) != 0U;
            std::uint8_t src{};
            std::uint8_t dst{};
            if (mem) {
                a_[eri] -= (er == 7) ? 2U : 1U;
                src = read8(a_[eri]);
                a_[dni] -= (dn == 7) ? 2U : 1U;
                dst = read8(a_[dni]);
            } else {
                src = static_cast<std::uint8_t>(d_[eri]);
                dst = static_cast<std::uint8_t>(d_[dni]);
            }
            const std::uint8_t r = bcd_add(dst, src);
            if (mem) {
                write8(a_[dni], r);
            } else {
                d_[dni] = (d_[dni] & 0xFFFFFF00U) | r;
            }
            cycles_ += 2; // Motorola: Dn=6 (4+2), -(An)=18 (4+4+4+4+2)
            return;
        }
        if ((op & 0xF1F8U) == 0xC140U) { // EXG Dx,Dy
            std::swap(d_[dni], d_[eri]);
            cycles_ += 2; // Motorola EXG = 6 cycles (4 opcode + 2 internal)
            return;
        }
        if ((op & 0xF1F8U) == 0xC148U) { // EXG Ax,Ay
            std::swap(a_[dni], a_[eri]);
            cycles_ += 2;
            return;
        }
        if ((op & 0xF1F8U) == 0xC188U) { // EXG Dx,Ay
            std::swap(d_[dni], a_[eri]);
            cycles_ += 2;
            return;
        }
        if (opm == 3) { // MULU
            const auto src = static_cast<std::uint16_t>(ea_read(em, er, op_size::word));
            const std::uint32_t r =
                (d_[static_cast<std::size_t>(dn)] & 0xFFFFU) * static_cast<std::uint32_t>(src);
            d_[static_cast<std::size_t>(dn)] = r;
            set_logic_flags(op_size::longword, r);
            cycles_ += 34 + 2 * popcount16(src);
            return;
        }
        if (opm == 7) { // MULS
            const auto src = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(ea_read(em, er, op_size::word)));
            const std::int32_t r = static_cast<std::int32_t>(static_cast<std::int16_t>(
                                       d_[static_cast<std::size_t>(dn)] & 0xFFFFU)) *
                                   src;
            d_[static_cast<std::size_t>(dn)] = static_cast<std::uint32_t>(r);
            set_logic_flags(op_size::longword, static_cast<std::uint32_t>(r));
            const auto sv = static_cast<std::uint16_t>(src);
            cycles_ += 34 + 2 * popcount16(static_cast<std::uint16_t>(sv ^ (sv << 1U)));
            return;
        }
        // AND <ea>,Dn (opm 0-2) or AND Dn,<ea> (opm 4-6, memory only). ABCD/EXG are
        // register-only (em < 2) and arrive in a later phase.
        const op_size sz = static_cast<op_size>(opm & 3);
        const std::uint32_t m = size_mask(sz);
        if (opm < 3) {
            const std::uint32_t src = ea_read(em, er, sz);
            const std::uint32_t res = (d_[static_cast<std::size_t>(dn)] & src) & m;
            d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | res;
            set_logic_flags(sz, res);
            if (sz == op_size::longword) {
                cycles_ += (em <= 1 || (em == 7 && er == 4)) ? 4 : 2;
            }
        } else if (em >= 2) {
            std::uint32_t addr = 0;
            const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
            const std::uint32_t res = (dst & d_[static_cast<std::size_t>(dn)]) & m;
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
        }
    }

    void m68000::op_or(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7, dn = (op >> 9U) & 7, opm = (op >> 6U) & 7;
        const auto dni = static_cast<std::size_t>(dn);
        if (opm == 3) { // DIVU.W <ea>,Dn
            const auto divisor = static_cast<std::uint16_t>(ea_read(em, er, op_size::word));
            if (divisor == 0U) {
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c));
                raise_exception(vec_divzero, inst_addr_);
                return;
            }
            const std::uint32_t dividend = d_[dni];
            const std::uint32_t q = dividend / divisor;
            const std::uint32_t rem = dividend % divisor;
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_c);
            // Both overflow and successful-division paths cost cycles; the
            // overflow shortcut still runs the early-exit micro-sequence.
            cycles_ += divu_cycles(dividend, divisor);
            if (q > 0xFFFFU) {
                sr_ |= sr_v;
            } else {
                d_[dni] = (rem << 16U) | (q & 0xFFFFU);
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v));
                if ((q & 0x8000U) != 0U) {
                    sr_ |= sr_n;
                }
                if (q == 0U) {
                    sr_ |= sr_z;
                }
            }
            return;
        }
        if (opm == 7) { // DIVS.W <ea>,Dn
            const auto divisor = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(ea_read(em, er, op_size::word)));
            if (divisor == 0) {
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c));
                raise_exception(vec_divzero, inst_addr_);
                return;
            }
            const auto dividend = static_cast<std::int32_t>(d_[dni]);
            const std::int64_t q64 = static_cast<std::int64_t>(dividend) / divisor;
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_c);
            const bool overflow = q64 < -32768 || q64 > 32767;
            if (overflow) {
                // The cycle accounting cares about the actual signed-overflow
                // condition (which divs_cycles' simpler `>>16` test undercatches).
                int mcyc = 6 + (dividend < 0 ? 1 : 0);
                cycles_ += (mcyc + 2) * 2 - 4;
                sr_ |= sr_v;
            } else {
                cycles_ += divs_cycles(dividend, divisor);
                const auto q = static_cast<std::int32_t>(q64);
                const std::int32_t rem = dividend % divisor;
                d_[dni] = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(rem)) << 16U) |
                          static_cast<std::uint16_t>(q);
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v));
                if ((static_cast<std::uint16_t>(q) & 0x8000U) != 0U) {
                    sr_ |= sr_n;
                }
                if (q == 0) {
                    sr_ |= sr_z;
                }
            }
            return;
        }
        if ((op & 0xF1F0U) == 0x8100U) { // SBCD
            const bool mem = (op & 8U) != 0U;
            const auto eri = static_cast<std::size_t>(er);
            std::uint8_t src{};
            std::uint8_t dst{};
            if (mem) {
                a_[eri] -= (er == 7) ? 2U : 1U;
                src = read8(a_[eri]);
                a_[dni] -= (dn == 7) ? 2U : 1U;
                dst = read8(a_[dni]);
            } else {
                src = static_cast<std::uint8_t>(d_[eri]);
                dst = static_cast<std::uint8_t>(d_[dni]);
            }
            const std::uint8_t r = bcd_sub(dst, src);
            if (mem) {
                write8(a_[dni], r);
            } else {
                d_[dni] = (d_[dni] & 0xFFFFFF00U) | r;
            }
            cycles_ += 2; // Motorola: SBCD Dn=6, -(An)=18 (same pattern as ABCD)
            return;
        }
        const op_size sz = static_cast<op_size>(opm & 3);
        const std::uint32_t m = size_mask(sz);
        if (opm < 3) { // OR <ea>,Dn
            const std::uint32_t src = ea_read(em, er, sz);
            const std::uint32_t res = (d_[static_cast<std::size_t>(dn)] | src) & m;
            d_[static_cast<std::size_t>(dn)] = (d_[static_cast<std::size_t>(dn)] & ~m) | res;
            set_logic_flags(sz, res);
            if (sz == op_size::longword) {
                cycles_ += (em <= 1 || (em == 7 && er == 4)) ? 4 : 2;
            }
        } else if (em >= 2) { // OR Dn,<ea>
            std::uint32_t addr = 0;
            const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
            const std::uint32_t res = (dst | d_[static_cast<std::size_t>(dn)]) & m;
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
        }
    }

    void m68000::op_bit(std::uint16_t op, bool dynamic) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7;
        const int ty = (op >> 6U) & 3; // 0=BTST 1=BCHG 2=BCLR 3=BSET
        const bool mem = em != 0;
        unsigned bn = dynamic ? d_[static_cast<std::size_t>((op >> 9U) & 7)]
                              : static_cast<unsigned>(fetch16() & 0xFFU);
        bn &= mem ? 7U : 31U;
        const std::uint32_t bit = 1U << bn;
        const op_size bs = mem ? op_size::byte : op_size::longword;
        std::uint32_t addr = 0;
        std::uint32_t v = mem ? ea_rmw_read(em, er, bs, addr)
                              : (d_[static_cast<std::size_t>(er)] & size_mask(bs));
        if ((v & bit) != 0U) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
        } else {
            sr_ |= sr_z;
        }
        // Dn destination cycle table (on top of the 4-cycle opcode fetch and,
        // for static ops, the immediate-word fetch):
        //   BTST  Dn,Dn  : +2
        //   BCHG  Dn,Dn  : +2  (+2 if bit >= 16)
        //   BSET  Dn,Dn  : +2  (+2 if bit >= 16)
        //   BCLR  Dn,Dn  : +4  (+2 if bit >= 16)
        if (!mem) {
            int extra = 2;
            if (ty == 2) {
                extra += 2; // BCLR base
            }
            if (ty != 0 && (bn & 31U) >= 16U) {
                extra += 2; // high-bit penalty (BCHG/BCLR/BSET only)
            }
            cycles_ += extra;
        }
        if (ty == 0) {
            return; // BTST: test only, no write-back
        }
        if (ty == 1) {
            v ^= bit; // BCHG
        } else if (ty == 2) {
            v &= ~bit; // BCLR
        } else {
            v |= bit; // BSET
        }
        if (mem) {
            ea_rmw_write(em, er, bs, v, addr);
        } else {
            d_[static_cast<std::size_t>(er)] =
                (d_[static_cast<std::size_t>(er)] & ~size_mask(bs)) | (v & size_mask(bs));
        }
    }

    void m68000::op_movep(std::uint16_t op) noexcept {
        const auto dn = static_cast<std::size_t>((op >> 9U) & 7);
        const auto an = static_cast<std::size_t>(op & 7);
        const auto disp = static_cast<std::int16_t>(fetch16());
        const std::uint32_t a =
            a_[an] + static_cast<std::uint32_t>(static_cast<std::int32_t>(disp));
        const bool to_memory = (op & 0x80U) != 0U;
        const bool is_long = (op & 0x40U) != 0U;
        if (to_memory) {
            if (is_long) {
                write8(a, static_cast<std::uint8_t>(d_[dn] >> 24U));
                write8(a + 2U, static_cast<std::uint8_t>(d_[dn] >> 16U));
                write8(a + 4U, static_cast<std::uint8_t>(d_[dn] >> 8U));
                write8(a + 6U, static_cast<std::uint8_t>(d_[dn]));
            } else {
                write8(a, static_cast<std::uint8_t>(d_[dn] >> 8U));
                write8(a + 2U, static_cast<std::uint8_t>(d_[dn]));
            }
        } else if (is_long) {
            d_[dn] = (static_cast<std::uint32_t>(read8(a)) << 24U) |
                     (static_cast<std::uint32_t>(read8(a + 2U)) << 16U) |
                     (static_cast<std::uint32_t>(read8(a + 4U)) << 8U) | read8(a + 6U);
        } else {
            d_[dn] = (d_[dn] & 0xFFFF0000U) | (static_cast<std::uint32_t>(read8(a)) << 8U) |
                     read8(a + 2U);
        }
    }

    void m68000::op_quick(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7;
        int data = (op >> 9U) & 7;
        if (data == 0) {
            data = 8;
        }
        const op_size sz = static_cast<op_size>((op >> 6U) & 3);
        if (static_cast<int>(sz) == 3) {
            op_dbcc_scc(op); // size field 3 in group 5 is DBcc / Scc
            return;
        }
        if (em == 1) { // ADDQ/SUBQ An: full 32-bit, no flags
            if ((op & 0x0100U) != 0U) {
                a_[static_cast<std::size_t>(er)] -= static_cast<std::uint32_t>(data);
            } else {
                a_[static_cast<std::size_t>(er)] += static_cast<std::uint32_t>(data);
            }
            cycles_ += sz == op_size::longword ? 2 : 4; // .L=6 total, .W=8 total.
            return;
        }
        std::uint32_t addr = 0;
        const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
        std::uint32_t res{};
        if ((op & 0x0100U) != 0U) {
            res = dst - static_cast<std::uint32_t>(data);
            flags_sub(sz, static_cast<std::uint32_t>(data), dst, res);
        } else {
            res = dst + static_cast<std::uint32_t>(data);
            flags_add(sz, static_cast<std::uint32_t>(data), dst, res);
        }
        ea_rmw_write(em, er, sz, res, addr);
        if (em == 0 && sz == op_size::longword) {
            cycles_ += 4;
        }
    }

    void m68000::op_immediate(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7;
        // Bit 8 set: dynamic bit op (BTST/BCHG/BCLR/BSET Dn,<ea>), MOVEP (mode 1),
        // or BTST Dn,#imm (the immediate operand form).
        if ((op & 0x0100U) != 0U) {
            const int ty = (op >> 6U) & 3;
            if (em == 7 && er == 4 && ty == 0) { // BTST Dn,#imm
                const unsigned bn = d_[static_cast<std::size_t>((op >> 9U) & 7)] & 7U;
                const auto v = static_cast<std::uint8_t>(fetch16() & 0xFFU);
                if (((v >> bn) & 1U) != 0U) {
                    sr_ = static_cast<std::uint16_t>(sr_ & ~sr_z);
                } else {
                    sr_ |= sr_z;
                }
                cycles_ += 2; // BTST bit-test microcode
                return;
            }
            if (em == 1) {
                op_movep(op);
                return;
            }
            op_bit(op, true);
            return;
        }
        const int sub = (op >> 9U) & 7;
        if (sub == 4) { // static bit op (#imm bit number)
            op_bit(op, false);
            return;
        }
        if (sub == 7) {
            return; // MOVES (68010+) -- unsupported on the 68000
        }
        // Immediate ALU: ORI(0)/ANDI(1)/SUBI(2)/ADDI(3)/EORI(5)/CMPI(6).
        const op_size sz = static_cast<op_size>((op >> 6U) & 3);
        if (static_cast<int>(sz) == 3) {
            return;
        }
        std::uint32_t imm =
            sz == op_size::longword ? fetch32() : static_cast<std::uint32_t>(fetch16());
        if (sz == op_size::byte) {
            imm &= 0xFFU;
        }
        // ORI/ANDI/EORI #imm,CCR (byte). The SR (word) forms need the supervisor
        // machinery + the privilege trap, so they land with the exception phase.
        if ((op & 0x3FU) == 0x3CU) {
            if (sz == op_size::byte) { // ORI/ANDI/EORI to CCR
                const auto ccr = static_cast<std::uint16_t>(imm & sr_ccr);
                if (sub == 0) {
                    sr_ |= ccr; // ORI CCR
                } else if (sub == 1) {
                    sr_ = static_cast<std::uint16_t>((sr_ & ~sr_ccr) | (sr_ & ccr)); // ANDI CCR
                } else if (sub == 5) {
                    sr_ ^= ccr; // EORI CCR
                }
            } else { // ORI/ANDI/EORI to SR (word, privileged)
                if ((sr_ & sr_s) == 0U) {
                    raise_exception(vec_privilege, inst_addr_);
                    return;
                }
                const auto v = static_cast<std::uint16_t>(imm);
                if (sub == 0) {
                    write_sr(static_cast<std::uint16_t>(sr_ | v)); // ORI SR
                } else if (sub == 1) {
                    write_sr(static_cast<std::uint16_t>(sr_ & v)); // ANDI SR
                } else if (sub == 5) {
                    write_sr(static_cast<std::uint16_t>(sr_ ^ v)); // EORI SR
                }
            }
            // Motorola: ORI/ANDI/EORI to CCR or SR all = 20 cycles base.
            // Bus model already counts 4 (opcode) + 4 (imm fetch) = 8. Add 12.
            cycles_ += 12;
            return;
        }
        if (sub == 6) { // CMPI
            const std::uint32_t dst = ea_read(em, er, sz);
            flags_cmp(sz, imm, dst, dst - imm);
            // CMPI.L #imm, Dn: 2 extra cycles for the 32-bit compare on a
            // register (no write-back, so 2 less than the ORI/ANDI/etc family).
            if (em == 0 && sz == op_size::longword) {
                cycles_ += 2;
            }
            return;
        }
        std::uint32_t addr = 0;
        const std::uint32_t dst = ea_rmw_read(em, er, sz, addr);
        std::uint32_t res{};
        switch (sub) {
        case 0: // ORI
            res = dst | imm;
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
            break;
        case 1: // ANDI
            res = dst & imm;
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
            break;
        case 2: // SUBI
            res = dst - imm;
            ea_rmw_write(em, er, sz, res, addr);
            flags_sub(sz, imm, dst, res);
            break;
        case 3: // ADDI
            res = dst + imm;
            ea_rmw_write(em, er, sz, res, addr);
            flags_add(sz, imm, dst, res);
            break;
        case 5: // EORI
            res = dst ^ imm;
            ea_rmw_write(em, er, sz, res, addr);
            set_logic_flags(sz, res);
            break;
        default:
            break;
        }
        // ORI/ANDI/SUBI/ADDI/EORI.L #imm, Dn: 4 extra cycles for the 32-bit
        // RMW on a register (same .l-on-Dn quirk as NEG/CLR/NOT/NEGX).
        if (em == 0 && sz == op_size::longword) {
            cycles_ += 4;
        }
    }

    void m68000::op_group4(std::uint16_t op) noexcept {
        if (op == 0x4E71U) {
            return; // NOP
        }
        if (op == 0x4AFCU) {
            // The dedicated ILLEGAL opcode. Take the illegal-instruction exception
            // (vector 4); the frame stacks the faulting PC. Caught here because the
            // TAS decode below ($4AC0 mask) would otherwise mis-handle it as a TAS
            // with an immediate effective address.
            raise_exception(vec_illegal, inst_addr_);
            return;
        }
        if ((op & 0xFFB8U) == 0x4880U) { // EXT
            const auto dn = static_cast<std::size_t>(op & 7);
            if ((op & 0x0040U) != 0U) { // EXT.L: word -> long
                d_[dn] = static_cast<std::uint32_t>(sign_extend(d_[dn] & 0xFFFFU, op_size::word));
                set_logic_flags(op_size::longword, d_[dn]);
            } else { // EXT.W: byte -> word
                const std::uint32_t w =
                    static_cast<std::uint32_t>(sign_extend(d_[dn] & 0xFFU, op_size::byte)) &
                    0xFFFFU;
                d_[dn] = (d_[dn] & 0xFFFF0000U) | w;
                set_logic_flags(op_size::word, w);
            }
            return;
        }
        const op_size sz = static_cast<op_size>((op >> 6U) & 3);
        const int em = (op >> 3U) & 7, er = op & 7;
        switch ((op >> 8U) & 0xF) {
        case 0x0: // NEGX
            if (static_cast<int>(sz) != 3) {
                std::uint32_t addr = 0;
                const std::uint32_t d = ea_rmw_read(em, er, sz, addr);
                const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
                const std::uint32_t r = (0U - d - x) & size_mask(sz);
                ea_rmw_write(em, er, sz, r, addr);
                if (em == 0 && sz == op_size::longword) {
                    cycles_ += 2; // .l on Dn: 2 extra internal cycles
                }
                flags_subx(sz, d, 0U, x);
                return;
            }
            break;
        case 0x2: // CLR (reads before writing on real hardware)
            if (static_cast<int>(sz) != 3) {
                std::uint32_t addr = 0;
                if (em >= 2) {
                    (void)ea_rmw_read(em, er, sz, addr);
                    ea_rmw_write(em, er, sz, 0U, addr);
                } else {
                    d_[static_cast<std::size_t>(er)] &= ~size_mask(sz);
                    if (sz == op_size::longword) {
                        cycles_ += 2;
                    }
                }
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_v | sr_c));
                sr_ |= sr_z;
                return;
            }
            break;
        case 0x4: // NEG
            if (static_cast<int>(sz) != 3) {
                std::uint32_t addr = 0;
                const std::uint32_t d = ea_rmw_read(em, er, sz, addr);
                const std::uint32_t r = (0U - d) & size_mask(sz);
                ea_rmw_write(em, er, sz, r, addr);
                if (em == 0 && sz == op_size::longword) {
                    cycles_ += 2;
                }
                flags_sub(sz, d, 0U, r);
                return;
            }
            break;
        case 0x6: // NOT
            if (static_cast<int>(sz) != 3) {
                std::uint32_t addr = 0;
                const std::uint32_t v = ea_rmw_read(em, er, sz, addr);
                const std::uint32_t r = ~v & size_mask(sz);
                ea_rmw_write(em, er, sz, r, addr);
                if (em == 0 && sz == op_size::longword) {
                    cycles_ += 2;
                }
                set_logic_flags(sz, r);
                return;
            }
            break;
        case 0xA: // TST, or TAS when the size field is 3 ($4AC0)
            if (static_cast<int>(sz) != 3) {
                const std::uint32_t v = ea_read(em, er, sz);
                set_logic_flags(sz, v);
                return;
            }
            if ((op & 0xFFC0U) == 0x4AC0U) { // TAS <ea>.B: test, then set bit 7
                std::uint32_t addr = 0;
                const auto v = static_cast<std::uint8_t>(ea_rmw_read(em, er, op_size::byte, addr));
                set_logic_flags(op_size::byte, v);
                if (em != 0 && tas_callback_) {
                    // Genesis bus controller drops the write phase; the lock
                    // still costs the 2 extra cycles a real RMW would.
                    tas_callback_(addr);
                } else {
                    ea_rmw_write(em, er, op_size::byte, static_cast<std::uint8_t>(v | 0x80U), addr);
                }
                // Memory operand: the locked RMW holds AS across read+write,
                // costing 2 cycles on top of separate-read-and-write bus time.
                if (em != 0) {
                    cycles_ += 2;
                }
                return;
            }
            break;
        default:
            break;
        }

        // LEA <ea>,An -- load the effective address (control modes only).
        // The 68000 charges 2 extra cycles for LEA's brief-extension index
        // decode that the generic ea_address path doesn't account for.
        if ((op & 0xF1C0U) == 0x41C0U) {
            a_[static_cast<std::size_t>((op >> 9U) & 7)] =
                ea_address(em, er, op_size::longword, false);
            if (em == 6 || (em == 7 && er == 3)) {
                cycles_ += 2;
            }
            return;
        }
        // NBCD <ea>.B -- BCD negate (0 - operand - X).
        if ((op & 0xFFC0U) == 0x4800U) {
            std::uint32_t addr = 0;
            const auto d = static_cast<std::uint8_t>(ea_rmw_read(em, er, op_size::byte, addr));
            const std::uint8_t r = bcd_sub(0U, d);
            ea_rmw_write(em, er, op_size::byte, r, addr);
            if (em == 0) {
                cycles_ += 2; // NBCD Dn: 2 extra internal cycles
            }
            return;
        }
        // SWAP Dn -- exchange the register halves.
        if ((op & 0xFFF8U) == 0x4840U) {
            const auto dn = static_cast<std::size_t>(op & 7);
            d_[dn] = (d_[dn] >> 16U) | (d_[dn] << 16U);
            set_logic_flags(op_size::longword, d_[dn]);
            return;
        }
        // PEA <ea> -- push the effective address. Same indexed-mode +2 quirk
        // as LEA.
        if ((op & 0xFFC0U) == 0x4840U && em >= 2) {
            push32(ea_address(em, er, op_size::longword, false));
            if (em == 6 || (em == 7 && er == 3)) {
                cycles_ += 2;
            }
            return;
        }
        // MOVEM <list>,<ea> / <ea>,<list> -- register-list transfer.
        if ((op & 0xFB80U) == 0x4880U && em >= 2) {
            const bool to_memory = (op & 0x0400U) == 0U;
            const op_size movem_sz = (op & 0x0040U) != 0U ? op_size::longword : op_size::word;
            const std::uint16_t mask = fetch16();
            if (to_memory && em == 4) { // register -> memory, predecrement -(An)
                const std::uint32_t initial = a_[static_cast<std::size_t>(er)];
                std::uint32_t a = initial;
                for (int i = 15; i >= 0; --i) {
                    if (!movem_has(mask, i, true)) {
                        continue;
                    }
                    std::uint32_t val = reg_is_addr(i) ? (reg_num(i) == static_cast<std::size_t>(er)
                                                              ? initial
                                                              : a_[reg_num(i)])
                                                       : d_[reg_num(i)];
                    if (movem_sz == op_size::longword) {
                        a -= 2U;
                        write16(a, static_cast<std::uint16_t>(val));
                        a -= 2U;
                        write16(a, static_cast<std::uint16_t>(val >> 16U));
                    } else {
                        a -= 2U;
                        write16(a, static_cast<std::uint16_t>(val));
                    }
                }
                a_[static_cast<std::size_t>(er)] = a;
            } else if (to_memory) { // register -> memory, other control modes
                std::uint32_t a = ea_address(em, er, op_size::longword, false);
                for (int i = 0; i < 16; ++i) {
                    if (!movem_has(mask, i, false)) {
                        continue;
                    }
                    const std::uint32_t val = reg_is_addr(i) ? a_[reg_num(i)] : d_[reg_num(i)];
                    if (movem_sz == op_size::longword) {
                        write16(a, static_cast<std::uint16_t>(val >> 16U));
                        a += 2U;
                        write16(a, static_cast<std::uint16_t>(val));
                        a += 2U;
                    } else {
                        write16(a, static_cast<std::uint16_t>(val));
                        a += 2U;
                    }
                }
            } else { // memory -> register
                const bool program_source = is_pc_relative(em, er);
                std::uint32_t a = em == 3 ? a_[static_cast<std::size_t>(er)]
                                          : ea_address(em, er, op_size::longword, false);
                for (int i = 0; i < 16; ++i) {
                    if (!movem_has(mask, i, false)) {
                        continue;
                    }
                    std::uint32_t v{};
                    if (movem_sz == op_size::longword) {
                        const std::uint16_t hi = read16(a, program_source);
                        a += 2U;
                        const std::uint16_t lo = read16(a, program_source);
                        a += 2U;
                        v = (static_cast<std::uint32_t>(hi) << 16U) | lo;
                    } else {
                        v = static_cast<std::uint32_t>(
                            sign_extend(read16(a, program_source),
                                        op_size::word)); // word loads sign-extend
                        a += 2U;
                    }
                    if (reg_is_addr(i)) {
                        a_[reg_num(i)] = v;
                    } else {
                        d_[reg_num(i)] = v;
                    }
                }
                if (em == 3) {
                    a_[static_cast<std::size_t>(er)] = a; // postincrement leaves An past the block
                }
                // The 68000's MOVEM-from-memory does one extra word read past
                // the last register (a documented bus quirk); model the 4
                // cycles without touching memory.
                cycles_ += 4;
            }
            return;
        }

        // CHK <ea>,Dn -- word bound check; traps if Dn < 0 or Dn > bound.
        if ((op & 0xF1C0U) == 0x4180U) {
            const int dn = (op >> 9U) & 7;
            const auto bound = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(ea_read(em, er, op_size::word)));
            const auto value = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(d_[static_cast<std::size_t>(dn)]));
            // The two trap conditions diverge in cycle cost: value > bound
            // detects after 4 internal cycles, value < 0 takes 6 (it has to
            // observe the sign before testing the bound). No-trap = 6.
            if (value > bound) {
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c));
                if (value < 0) {
                    sr_ |= sr_n;
                }
                cycles_ += 4;
                raise_exception(vec_chk, pc_);
            } else if (value < 0) {
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z | sr_v | sr_c));
                sr_ |= sr_n;
                cycles_ += 6;
                raise_exception(vec_chk, pc_);
            } else {
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_z | sr_v | sr_c));
                cycles_ += 6;
            }
            return;
        }
        // MOVE from SR -> <ea> (word). Not privileged on the 68000. Motorola:
        // Dn = 6, mem = 8 + EAwrite -- the mem path performs an internal read
        // before the write, so it's 4 cycles more than a plain write.
        if ((op & 0xFFC0U) == 0x40C0U) {
            if (em != 0 && em != 1) {
                cycles_ += 4; // internal-read overhead before the destination write
            } else {
                cycles_ += 2; // Dn / An: 6 - 4 opcode bus = 2
            }
            ea_write(em, er, op_size::word, sr_);
            return;
        }
        // MOVE to CCR <- <ea> (word, low byte). 12 + EAread; +8 over bus model.
        if ((op & 0xFFC0U) == 0x44C0U) {
            const auto v = static_cast<std::uint16_t>(ea_read(em, er, op_size::word));
            sr_ = static_cast<std::uint16_t>((sr_ & ~sr_ccr) | (v & sr_ccr));
            cycles_ += 8;
            return;
        }
        // MOVE to SR <- <ea> (word, privileged). 12 + EAread; +8 over bus.
        if ((op & 0xFFC0U) == 0x46C0U) {
            if ((sr_ & sr_s) == 0U) {
                raise_exception(vec_privilege, inst_addr_);
                return;
            }
            write_sr(static_cast<std::uint16_t>(ea_read(em, er, op_size::word)));
            cycles_ += 8;
            return;
        }

        // ---- control flow + system ops ($4Exx) ----
        if ((op & 0xFFF0U) == 0x4E40U) { // TRAP #n
            raise_exception(vec_trap0 + (op & 0xFU), pc_);
            return;
        }
        if ((op & 0xFFF8U) == 0x4E50U) { // LINK An,#disp
            const auto an = static_cast<std::size_t>(op & 7);
            const auto disp = static_cast<std::int16_t>(fetch16());
            std::uint32_t saved = a_[an];
            if ((op & 7) == 7) {
                saved -= 4U; // LINK A7 pushes the post-push SP
            }
            push32(saved);
            a_[an] = a_[7];
            a_[7] += static_cast<std::uint32_t>(static_cast<std::int32_t>(disp));
            return;
        }
        if ((op & 0xFFF8U) == 0x4E58U) { // UNLK An
            const auto an = static_cast<std::size_t>(op & 7);
            a_[7] = a_[an];
            a_[an] = pop32();
            return;
        }
        if ((op & 0xFFF0U) == 0x4E60U) { // MOVE USP (privileged)
            if ((sr_ & sr_s) == 0U) {
                raise_exception(vec_privilege, inst_addr_);
                return;
            }
            const auto an = static_cast<std::size_t>(op & 7);
            if ((op & 8U) != 0U) {
                a_[an] = usp_;
            } else {
                usp_ = a_[an];
            }
            return;
        }
        // Additive cycle accounting throughout: fetch16 has already added 4
        // for the opcode (and pop32/pop16/push32/fetch16 self-account inside
        // the bus helpers), so each case adds only the remaining INTERNAL
        // IDLE to reach Motorola's published total. Earlier code used
        // `cycles_ = N` which silently overwrote any bus-refresh +2 added by
        // step_instruction(), causing Bcc/RTS/JMP/etc to never reflect
        // refresh -- the source of the -747K master BoV drift.
        switch (op) {
        case 0x4E70U: // RESET (privileged) -- Motorola total 132; no bus accesses, all idle - 4
                      // prefetch
            if ((sr_ & sr_s) == 0U) {
                raise_exception(vec_privilege, inst_addr_);
                return;
            }
            if (reset_callback_) {
                reset_callback_();
            }
            cycles_ += 128; // 132 total - 4 fetch16 (opcode)
            return;
        case 0x4E71U: // NOP -- total 4 = fetch16 only
            return;
        case 0x4E72U: // STOP #imm (privileged) -- total 4 (rest of stop happens via stopped_ flag)
            if ((sr_ & sr_s) == 0U) {
                raise_exception(vec_privilege, inst_addr_);
                return;
            }
            write_sr(fetch16()); // adds 4 for the imm fetch
            stopped_ = true;
            // 4 (opcode fetch) + 4 (imm fetch) = 8 already; STOP itself = 4 idle
            // but the imm-fetch is part of STOP's encoded cost in Motorola's table -- keep total at
            // 4
            cycles_ -= 4; // counter compensates the extra fetch16 above; matches old behaviour
            if (cycles_ < 0)
                cycles_ = 0;
            return;
        case 0x4E73U: { // RTE (privileged) -- total 20 = pop16(4) + pop32(8) + fetch16(4) + 4 idle
            if ((sr_ & sr_s) == 0U) {
                raise_exception(vec_privilege, inst_addr_);
                return;
            }
            const std::uint16_t s = pop16();
            pc_ = pop32();
            write_sr(s);
            cycles_ += 4;
            return;
        }
        case 0x4E75U: // RTS -- total 16 = fetch16(4) + pop32(8) + 4 idle
            pc_ = pop32();
            cycles_ += 4;
            return;
        case 0x4E76U: // TRAPV -- 4 cycles when no trap; just the fetch16
            if ((sr_ & sr_v) != 0U) {
                raise_exception(vec_trapv, pc_);
            }
            return;
        case 0x4E77U: { // RTR -- total 20 = pop16(4) + pop32(8) + fetch16(4) + 4 idle
            const std::uint16_t ccr = pop16();
            sr_ = static_cast<std::uint16_t>((sr_ & ~sr_ccr) | (ccr & sr_ccr));
            pc_ = pop32();
            cycles_ += 4;
            return;
        }
        default:
            break;
        }
        if ((op & 0xFFC0U) == 0x4E80U) { // JSR <ea>
            const std::uint32_t target = ea_address(em, er, op_size::longword, false);
            push32(pc_);
            pc_ = target;
            // JSR Motorola totals (incl. opcode fetch + EA reads + push):
            //   (An) 16, (d16,An) 18, (d8,An,Xn) 22, (xxx).W 18, (xxx).L 20,
            //   (d16,PC) 18, (d8,PC,Xn) 22.
            // Bus already counted: fetch16 opcode (4) + EA address resolve
            // (variable: 0 for An, fetch16 for (d16,An)/(xxx).W/(d16,PC),
            // fetch32 for (xxx).L, indexed extension fetch + idle for
            // (d8,An,Xn)/(d8,PC,Xn), etc.) + push32 (8).
            int internal_idle = 4; // (An): 4 + 0 + 8 + 4 = 16
            if (em == 5) {
                internal_idle = 2;
            } // (d16,An): 4 + 4 + 8 + 2 = 18
            else if (em == 6) {
                internal_idle = 4;
            } // (d8,An,Xn): 4 + 6 + 8 + 4 = 22
            else if (em == 7) {
                if (er == 0) {
                    internal_idle = 2;
                } // (xxx).W: 4 + 4 + 8 + 2 = 18
                else if (er == 1) {
                    internal_idle = 0;
                } // (xxx).L: 4 + 8 + 8 + 0 = 20
                else if (er == 2) {
                    internal_idle = 2;
                } // (d16,PC): 4 + 4 + 8 + 2 = 18
                else if (er == 3) {
                    internal_idle = 4;
                } // (d8,PC,Xn): 4 + 6 + 8 + 4 = 22
            }
            cycles_ += internal_idle;
            return;
        }
        if ((op & 0xFFC0U) == 0x4EC0U) { // JMP <ea>
            pc_ = ea_address(em, er, op_size::longword, false);
            // JMP Motorola totals: (An) 8, (d16,An) 10, (d8,An,Xn) 14,
            //   (xxx).W 10, (xxx).L 12, (d16,PC) 10, (d8,PC,Xn) 14.
            // Bus already counted: fetch16 (4) + EA address (0/4/6/8 depending on mode).
            int internal_idle = 4; // (An): 4 + 0 + 4 = 8
            if (em == 5) {
                internal_idle = 2;
            } // (d16,An): 4 + 4 + 2 = 10
            else if (em == 6) {
                internal_idle = 4;
            } // (d8,An,Xn): 4 + 6 + 4 = 14
            else if (em == 7) {
                if (er == 0) {
                    internal_idle = 2;
                } // (xxx).W: 4 + 4 + 2 = 10
                else if (er == 1) {
                    internal_idle = 0;
                } // (xxx).L: 4 + 8 + 0 = 12
                else if (er == 2) {
                    internal_idle = 2;
                } // (d16,PC): 4 + 4 + 2 = 10
                else if (er == 3) {
                    internal_idle = 4;
                } // (d8,PC,Xn): 4 + 6 + 4 = 14
            }
            cycles_ += internal_idle;
            return;
        }
        // NBCD/SWAP/PEA/LEA/MOVEM/MOVE-to-from-SR/CCR/CHK land in later phases.
    }

    // ---- supervisor state, stack, exceptions ----

    void m68000::set_supervisor(bool supervisor) noexcept {
        const bool was = (sr_ & sr_s) != 0U;
        if (was == supervisor) {
            return;
        }
        if (was) { // leaving supervisor: bank out SSP, bank in USP
            ssp_ = a_[7];
            a_[7] = usp_;
        } else { // entering supervisor
            usp_ = a_[7];
            a_[7] = ssp_;
        }
        sr_ = static_cast<std::uint16_t>(supervisor ? (sr_ | sr_s) : (sr_ & ~sr_s));
    }

    void m68000::write_sr(std::uint16_t value) noexcept {
        set_supervisor((value & sr_s) != 0U);
        sr_ = static_cast<std::uint16_t>(value & 0xA71FU); // T, S, IPM, CCR are the live bits
    }

    void m68000::push16(std::uint16_t value) noexcept {
        a_[7] -= 2U;
        write16(a_[7], value);
    }
    void m68000::push32(std::uint32_t value) noexcept {
        a_[7] -= 4U;
        write32(a_[7], value);
    }
    std::uint16_t m68000::pop16() noexcept {
        const std::uint16_t v = read16(a_[7]);
        a_[7] += 2U;
        return v;
    }
    std::uint32_t m68000::pop32() noexcept {
        const std::uint32_t v = read32(a_[7]);
        a_[7] += 4U;
        return v;
    }

    void m68000::raise_exception(int vector, std::uint32_t exc_pc) noexcept {
        exception_raised_ = true;
        const std::uint16_t old_sr = sr_;
        set_supervisor(true);
        sr_ = static_cast<std::uint16_t>(sr_ & ~sr_t);
        exception_entry_ = true;
        push32(exc_pc);
        push16(old_sr);
        pc_ = read32(static_cast<std::uint32_t>(vector) * 4U);
        (void)consume_bus_fault();
        exception_entry_ = false;
        // Motorola 68000 internal-exception entry = 34 cycles (TRAP, TRAPV,
        // CHK trap, illegal, divzero, privilege). The bus model above counts
        // push32 + push16 + read32 = 20; add the remaining 10 of internal
        // microcode + idle cycles. The opcode-fetch cycles for the triggering
        // instruction are counted by the caller's own fetch16, so this is the
        // exception-entry-only cost, applied uniformly.
        cycles_ += 10;
    }

    std::uint16_t m68000::group0_status_word(bool instruction_fetch,
                                             bool read_access) const noexcept {
        const bool supervisor = (sr_ & sr_s) != 0U;
        const std::uint16_t function_code =
            instruction_fetch ? (supervisor ? 0x6U : 0x2U) : (supervisor ? 0x5U : 0x1U);
        const std::uint16_t read_bit = read_access ? 0x0010U : 0U;
        const std::uint16_t not_instruction_bit = instruction_fetch ? 0U : 0x0008U;
        return static_cast<std::uint16_t>(read_bit | not_instruction_bit | function_code);
    }

    void m68000::queue_address_error(std::uint32_t access_address, std::uint32_t stacked_pc,
                                     bool instruction_fetch, bool read_access) noexcept {
        if (group0_fault_.pending) {
            return;
        }
        group0_fault_ = {
            .pending = true,
            .vector = vec_address_error,
            .access_address = access_address & address_mask,
            .stacked_pc = stacked_pc & address_mask,
            .instruction_register = current_opcode_,
            .status_word = group0_status_word(instruction_fetch, read_access),
        };
    }

    void m68000::queue_bus_error(std::uint32_t access_address, std::uint32_t stacked_pc,
                                 bool instruction_fetch, bool read_access) noexcept {
        if (group0_fault_.pending) {
            return;
        }
        group0_fault_ = {
            .pending = true,
            .vector = vec_bus_error,
            .access_address = access_address & address_mask,
            .stacked_pc = stacked_pc & address_mask,
            .instruction_register = current_opcode_,
            .status_word = group0_status_word(instruction_fetch, read_access),
        };
    }

    ibus::bus_fault m68000::consume_bus_fault() noexcept {
        return bus_ != nullptr ? bus_->consume_bus_fault() : ibus::bus_fault{};
    }

    void m68000::raise_group0_exception(int vector, const group0_fault& fault) noexcept {
        exception_raised_ = true;
        const std::uint16_t old_sr = sr_;
        set_supervisor(true);
        sr_ = static_cast<std::uint16_t>(sr_ & ~sr_t);
        exception_entry_ = true;

        // MC68000 Figure 6-7 stack order, lowest address first after stacking:
        // status word, access address, instruction register, old SR, saved PC.
        push32(fault.stacked_pc);
        push16(old_sr);
        push16(fault.instruction_register);
        push32(fault.access_address);
        push16(fault.status_word);
        pc_ = read32(static_cast<std::uint32_t>(vector) * 4U);
        (void)consume_bus_fault();
        exception_entry_ = false;

        // Address-error entry is 50 cycles on the MC68000. The bus helpers above
        // have charged the five frame writes plus the vector read (36 cycles).
        cycles_ += 14;
    }

    void m68000::process_interrupt() noexcept {
        const int level = irq_level_;
        const std::uint16_t old_sr = sr_;
        set_supervisor(true);
        sr_ = static_cast<std::uint16_t>(sr_ & ~sr_t);
        sr_ =
            static_cast<std::uint16_t>((sr_ & ~sr_ipm) | (static_cast<std::uint16_t>(level) << 8U));
        exception_entry_ = true;
        push32(pc_);
        push16(old_sr);
        pc_ = read32(static_cast<std::uint32_t>(vec_autovector_base + level) * 4U);
        (void)consume_bus_fault();
        exception_entry_ = false;
        stopped_ = false;
        if (irq_ack_) {
            irq_ack_(level); // IACK cycle: let the device clear its interrupt request
        }
        // Standard MC68000 autovector entry is 42 cycles. The bus helpers above
        // billed 20 cycles (push32 + push16 + vector read); add the remaining
        // internal/IACK/prefetch slots. Genesis opts into its VDP phase table.
        if (!genesis_interrupt_phase_timing_enabled_) {
            cycles_ += 22;
            return;
        }

        // Autovectored IRQ entry on the Genesis 68K is cycle-dependent:
        // total entry cost is {50,59,58,57,56,55,54,53,52,51} CPU cycles
        // indexed by (cycles / MUL) % 10. The index uses elapsed_ (pre-
        // instruction) -- the table is evaluated at the *start* of entry.
        constexpr int irq_idle[10] = {30, 39, 38, 37, 36, 35, 34, 33, 32, 31};
        cycles_ += irq_idle[static_cast<std::size_t>(elapsed_ % 10U)];
    }

    bool m68000::test_cc(int cc) const noexcept {
        const bool c = (sr_ & sr_c) != 0U;
        const bool v = (sr_ & sr_v) != 0U;
        const bool z = (sr_ & sr_z) != 0U;
        const bool n = (sr_ & sr_n) != 0U;
        switch (cc & 0xF) {
        case 0x0:
            return true; // T
        case 0x1:
            return false; // F
        case 0x2:
            return !c && !z; // HI
        case 0x3:
            return c || z; // LS
        case 0x4:
            return !c; // CC/HS
        case 0x5:
            return c; // CS/LO
        case 0x6:
            return !z; // NE
        case 0x7:
            return z; // EQ
        case 0x8:
            return !v; // VC
        case 0x9:
            return v; // VS
        case 0xA:
            return !n; // PL
        case 0xB:
            return n; // MI
        case 0xC:
            return n == v; // GE
        case 0xD:
            return n != v; // LT
        case 0xE:
            return !z && (n == v); // GT
        default:
            return z || (n != v); // LE
        }
    }

    void m68000::op_branch(std::uint16_t op) noexcept {
        const int cc = (op >> 8U) & 0xF;
        const auto d8 = static_cast<std::int8_t>(static_cast<std::uint8_t>(op & 0xFFU));
        const std::uint32_t base = pc_;
        const std::int32_t disp = d8 == 0 ? static_cast<std::int16_t>(fetch16()) : d8;
        const std::uint32_t target =
            static_cast<std::uint32_t>(static_cast<std::int32_t>(base) + disp);
        // Additive cycle accounting: the calling step_instruction has
        // already added fetch16(4) for the opcode, plus possibly +2 if bus
        // refresh fired this instruction; the word-displacement branch above
        // adds another fetch16(4) for the displacement. Add ONLY the
        // handler's internal idle (and push32 for BSR) here so refresh and
        // prefetch costs aren't overwritten -- refresh stalls bill +14
        // master independently of the instruction's table cost.
        if (cc == 1) {   // BSR (Motorola total = 18 cycles)
            push32(pc_); // adds 8 cycles
            pc_ = target;
            cycles_ += (d8 == 0) ? 2 : 6;    // .W: 4+4+8+2=18 ; .S: 4+8+6=18
        } else if (cc == 0 || test_cc(cc)) { // BRA / Bcc taken (= 10)
            pc_ = target;
            cycles_ += (d8 == 0) ? 2 : 6; // .W: 4+4+2=10 ; .S: 4+6=10
        } else {                          // Bcc not taken
            cycles_ += 4;                 // .W: 4+4+4=12 ; .S: 4+4=8
        }
    }

    void m68000::op_dbcc_scc(std::uint16_t op) noexcept {
        const int cc = (op >> 8U) & 0xF;
        const int em = (op >> 3U) & 7, er = op & 7;
        if (em == 1) { // DBcc Dn,#disp (2-word)
            const auto disp = static_cast<std::int16_t>(fetch16());
            const std::uint32_t target =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(pc_ - 2U) + disp);
            // Motorola DBcc:
            //   cc true                       -> 12 cycles (no branch, no count)
            //   cc false, branch taken        -> 10 cycles
            //   cc false, counter expired -1  -> 14 cycles (fall through)
            // My existing bus model already counts: opcode 4 + disp fetch 4 = 8.
            // Add the remaining internal cycles per case.
            if (test_cc(cc)) {
                cycles_ += 4; // 12 - 8 bus
            } else {
                const auto cnt =
                    static_cast<std::uint16_t>((d_[static_cast<std::size_t>(er)] & 0xFFFFU) - 1U);
                d_[static_cast<std::size_t>(er)] =
                    (d_[static_cast<std::size_t>(er)] & 0xFFFF0000U) | cnt;
                if (cnt != 0xFFFFU) {
                    pc_ = target;
                    cycles_ += 2; // 10 - 8 bus
                } else {
                    cycles_ += 6; // 14 - 8 bus (one extra prefetch when falling through)
                }
            }
            return;
        }
        // Scc <ea> (1-word). Motorola:
        //   Scc Dn: 4 (cc false) / 6 (cc true)
        //   Scc <mem>: 8 + EA (cc false) / 8 + EA (cc true), both include the
        //              internal read-modify cycle before the write.
        const bool cc_true = test_cc(cc);
        const std::uint8_t v = cc_true ? 0xFFU : 0x00U;
        if (em >= 2) {
            std::uint32_t addr = 0;
            (void)ea_rmw_read(em, er, op_size::byte, addr);
            ea_rmw_write(em, er, op_size::byte, v, addr);
            // Bus already counts opcode 4 + EA fetch + read 4 + write 4. The
            // remainder of Motorola's 8 + EA matches the EA cost we already
            // counted, no extra internal.
        } else {
            d_[static_cast<std::size_t>(er)] = (d_[static_cast<std::size_t>(er)] & ~0xFFU) | v;
            cycles_ += cc_true ? 2 : 0; // 6 - 4 opcode for true; 4 - 4 = 0 for false
        }
    }

    void m68000::op_shift(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7;
        const op_size size_field = static_cast<op_size>((op >> 6U) & 3);

        // Memory shift: size field == 3 -> a word memory operand shifted by one.
        if (static_cast<int>(size_field) == 3) {
            const int ty = (op >> 9U) & 3;
            const bool left = (op & 0x0100U) != 0U;
            constexpr std::uint32_t msb = 0x8000U;
            std::uint32_t addr = 0;
            const std::uint32_t v = ea_rmw_read(em, er, op_size::word, addr);
            std::uint32_t r = v;
            switch (ty) {
            case 0: // ASL / ASR
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_x | sr_v));
                if (left) {
                    if ((v & msb) != 0U) {
                        sr_ |= sr_c | sr_x;
                    }
                    r = (v << 1U) & 0xFFFFU;
                    if (((r ^ v) & msb) != 0U) {
                        sr_ |= sr_v;
                    }
                } else {
                    if ((v & 1U) != 0U) {
                        sr_ |= sr_c | sr_x;
                    }
                    r = (v >> 1U) | (v & msb);
                }
                break;
            case 1: // LSL / LSR
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_x | sr_v));
                if (left) {
                    if ((v & msb) != 0U) {
                        sr_ |= sr_c | sr_x;
                    }
                    r = (v << 1U) & 0xFFFFU;
                } else {
                    if ((v & 1U) != 0U) {
                        sr_ |= sr_c | sr_x;
                    }
                    r = v >> 1U;
                }
                break;
            case 2: { // ROXL / ROXR
                const std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_v));
                if (left) {
                    const bool carry = (v & msb) != 0U;
                    r = ((v << 1U) | x) & 0xFFFFU;
                    sr_ = static_cast<std::uint16_t>(carry ? (sr_ | sr_c | sr_x) : (sr_ & ~sr_x));
                } else {
                    const bool carry = (v & 1U) != 0U;
                    r = (v >> 1U) | (x != 0U ? msb : 0U);
                    sr_ = static_cast<std::uint16_t>(carry ? (sr_ | sr_c | sr_x) : (sr_ & ~sr_x));
                }
                break;
            }
            default: // 3: ROL / ROR
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_v));
                if (left) {
                    const std::uint32_t carry = (v & msb) != 0U ? 1U : 0U;
                    r = ((v << 1U) | carry) & 0xFFFFU;
                    if (carry != 0U) {
                        sr_ |= sr_c;
                    }
                } else {
                    const std::uint32_t carry = v & 1U;
                    r = (v >> 1U) | (carry << 15U);
                    if (carry != 0U) {
                        sr_ |= sr_c;
                    }
                }
                break;
            }
            ea_rmw_write(em, er, op_size::word, r, addr);
            sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z));
            if ((r & 0xFFFFU) == 0U) {
                sr_ |= sr_z;
            }
            if ((r & msb) != 0U) {
                sr_ |= sr_n;
            }
            return;
        }

        // Register shift / rotate.
        const int count_reg = (op >> 9U) & 7;
        const bool left = (op & 0x0100U) != 0U;
        const int ty = (op >> 3U) & 3;
        const auto dn = static_cast<std::size_t>(op & 7);
        const bool count_in_reg = (op & 0x0020U) != 0U;
        const op_size sz = size_field;
        const int count = count_in_reg
                              ? static_cast<int>(d_[static_cast<std::size_t>(count_reg)] & 63U)
                              : (count_reg != 0 ? count_reg : 8);
        const std::uint32_t v = d_[dn] & size_mask(sz);
        const std::uint32_t msb = size_sign_bit(sz);
        const std::uint32_t m = size_mask(sz);
        const int bits = 8 * size_bytes(sz);
        std::uint32_t r = v;

        sr_ = static_cast<std::uint16_t>(sr_ & ~sr_v);
        if (count == 0) {
            sr_ = static_cast<std::uint16_t>(sr_ & ~sr_c);
            if (ty == 2 && (sr_ & sr_x) != 0U) {
                sr_ |= sr_c; // ROXL/ROXR by 0 copies X into C
            }
        } else {
            switch (ty) {
            case 0: // ASL / ASR
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_x));
                if (left) {
                    for (int i = 0; i < count; ++i) {
                        const std::uint32_t prev = r;
                        sr_ = static_cast<std::uint16_t>((r & msb) != 0U ? (sr_ | sr_c | sr_x)
                                                                         : (sr_ & ~(sr_c | sr_x)));
                        r = (r << 1U) & m;
                        if (((r ^ prev) & msb) != 0U) {
                            sr_ |= sr_v;
                        }
                    }
                } else if (count >= bits) {
                    if ((v & msb) != 0U) {
                        r = m;
                        // Every shifted-out bit is the sign bit: C/X are set for
                        // any count >= the operand width, not only count == width.
                        sr_ |= sr_c | sr_x;
                    } else {
                        r = 0U;
                    }
                } else {
                    const std::uint32_t sign = (v & msb) != 0U ? msb : 0U;
                    for (int i = 0; i < count; ++i) {
                        sr_ = static_cast<std::uint16_t>((r & 1U) != 0U ? (sr_ | sr_c | sr_x)
                                                                        : (sr_ & ~(sr_c | sr_x)));
                        r = (r >> 1U) | sign;
                    }
                }
                break;
            case 1: // LSL / LSR
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_x));
                for (int i = 0; i < count; ++i) {
                    if (left) {
                        sr_ = static_cast<std::uint16_t>((r & msb) != 0U ? (sr_ | sr_c | sr_x)
                                                                         : (sr_ & ~(sr_c | sr_x)));
                        r = (r << 1U) & m;
                    } else {
                        sr_ = static_cast<std::uint16_t>((r & 1U) != 0U ? (sr_ | sr_c | sr_x)
                                                                        : (sr_ & ~(sr_c | sr_x)));
                        r >>= 1U;
                    }
                }
                break;
            case 2: { // ROXL / ROXR (rotate through X)
                std::uint32_t x = (sr_ & sr_x) != 0U ? 1U : 0U;
                for (int i = 0; i < count; ++i) {
                    if (left) {
                        const std::uint32_t out = (r & msb) != 0U ? 1U : 0U;
                        r = ((r << 1U) | x) & m;
                        x = out;
                    } else {
                        const std::uint32_t out = r & 1U;
                        r = (r >> 1U) | (x != 0U ? msb : 0U);
                        x = out;
                    }
                }
                sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_c | sr_x));
                if (x != 0U) {
                    sr_ |= sr_c | sr_x;
                }
                break;
            }
            default: { // 3: ROL / ROR
                const int sh = count % bits;
                sr_ = static_cast<std::uint16_t>(sr_ & ~sr_c);
                if (sh == 0) {
                    r = v;
                } else if (left) {
                    r = ((v << sh) | (v >> (bits - sh))) & m;
                } else {
                    r = ((v >> sh) | (v << (bits - sh))) & m;
                }
                if (left ? ((r & 1U) != 0U) : ((r & msb) != 0U)) {
                    sr_ |= sr_c;
                }
                break;
            }
            }
        }

        d_[dn] = (d_[dn] & ~m) | (r & m);
        sr_ = static_cast<std::uint16_t>(sr_ & ~(sr_n | sr_z));
        if ((r & m) == 0U) {
            sr_ |= sr_z;
        }
        if ((r & msb) != 0U) {
            sr_ |= sr_n;
        }
        cycles_ += (sz == op_size::longword ? 4 : 2) + 2 * count;
    }

    void m68000::exec(std::uint16_t op) {
        switch (op >> 12U) {
        case 0x0: // immediates (ORI/ANDI/SUBI/ADDI/EORI/CMPI) + bit ops
            op_immediate(op);
            break;
        case 0x1: // MOVE.B
        case 0x2: // MOVE.L
        case 0x3: // MOVE.W
            op_move(op);
            break;
        case 0x4: // NOP / EXT / NEGX / CLR / NEG / NOT / TST (rest of group 4 later)
            op_group4(op);
            break;
        case 0x5: // ADDQ / SUBQ (Scc / DBcc later)
            op_quick(op);
            break;
        case 0x7: // MOVEQ (bit 8 must be 0)
            if ((op & 0x0100U) == 0U) {
                op_moveq(op);
            }
            break;
        case 0x9: // SUB / SUBA / SUBX
            op_sub(op);
            break;
        case 0xB: // CMP / CMPA / CMPM
            op_cmp(op);
            break;
        case 0x8: // OR (DIVU/DIVS, SBCD later)
            op_or(op);
            break;
        case 0xC: // AND / MULU / MULS (ABCD / EXG later)
            op_mul(op);
            break;
        case 0xD: // ADD / ADDA / ADDX
            op_add(op);
            break;
        case 0x6: // Bcc / BRA / BSR
            op_branch(op);
            break;
        case 0xE: // ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR
            op_shift(op);
            break;
        case 0xA:
            // Line-1010 emulator exception. Unimplemented $Axxx opcodes vector
            // through $28; the frame stacks the FAULTING opcode's PC (inst_addr_),
            // not the next instruction, so a handler can read the trapping opcode
            // back -- some titles use line-A/F as syscalls and depend on it.
            raise_exception(vec_line_a, inst_addr_);
            break;
        case 0xF:
            // Line-1111 emulator exception ($Fxxx -> $2C), same faulting-PC frame.
            raise_exception(vec_line_f, inst_addr_);
            break;
        default:
            break;
        }
    }

    // ---- step / lifecycle ----

    int m68000::step_instruction() {
        cycles_ = 0;
        cycle_sources_ = {};
        exception_raised_ = false;
        exception_entry_ = false;
        group0_fault_ = {};
        current_opcode_ = 0U;
        (void)consume_bus_fault();

        // Interrupt dispatch. Level 7 is edge-triggered (NMI); the others are
        // accepted when the request level exceeds the SR interrupt-priority mask.
        if (irq_level_ > 0) {
            const int ipm = (sr_ >> 8U) & 7;
            const bool fire = irq_level_ == 7 ? prev_irq_level_ < 7 : irq_level_ > ipm;
            if (fire) {
                prev_irq_level_ = irq_level_;
                cycle_sources_.irq_entered = 1U;
                process_interrupt();
                if (cycles_ < 4) {
                    cycles_ = 4;
                }
                elapsed_ += static_cast<std::uint64_t>(cycles_);
                last_cycle_sources_ = cycle_sources_;
                return cycles_;
            }
        }

        if (halted_ || stopped_) {
            last_cycle_sources_ = cycle_sources_;
            // STOP/HALT: return one bus cycle's worth of time without
            // executing. Account it in elapsed_ so the CPU clock keeps in
            // step with the master clock.
            cycles_ = 4;
            elapsed_ += 4U;
            return 4;
        }

        // Genesis 68K DRAM refresh: ~every 128 68K cycles the bus steals 2
        // cycles. The schedule is SLIDING -- a single `if` per instruction that
        // reloads the next-due point relative to the current elapsed count, so a
        // long instruction that overshoots a refresh window pushes the schedule
        // forward by the overshoot rather than firing a catch-up burst on the
        // following instructions. An absolute 128-cycle grid (`while` + `+= 128`,
        // commit 21d2565) fires ~2% more refreshes than the hardware in tight
        // loops, which accumulates cumulative cycle drift over a boot sequence;
        // the sliding form matches the bus controller's actual behaviour.
        if (bus_refresh_enabled_ && elapsed_ >= bus_refresh_due_) {
            bus_refresh_due_ = elapsed_ + genesis_refresh_period;
            cycles_ += 2;
            cycle_sources_.refresh_fired = 1U;
        }

        const std::uint16_t pre_sr = sr_;
        inst_addr_ = pc_;
        const auto saved_d = d_;
        const auto saved_a = a_;
        const std::uint32_t saved_pc = pc_;
        const std::uint16_t saved_sr = sr_;
        const std::uint32_t saved_usp = usp_;
        const std::uint32_t saved_ssp = ssp_;
        const bool saved_stopped = stopped_;
        const bool saved_halted = halted_;
        if (trace_callback_) {
            trace_callback_(pc_);
        }
        const std::uint16_t op = fetch16();
        current_opcode_ = op;
        if (!group0_fault_.pending) {
            exec(op);
        }

        if (group0_fault_.pending) {
            const group0_fault fault = group0_fault_;
            group0_fault_ = {};
            d_ = saved_d;
            a_ = saved_a;
            pc_ = saved_pc;
            sr_ = saved_sr;
            usp_ = saved_usp;
            ssp_ = saved_ssp;
            stopped_ = saved_stopped;
            halted_ = saved_halted;
            raise_group0_exception(fault.vector, fault);
        }

        // Trace: if T was set entering the instruction, take the trace exception.
        if ((pre_sr & sr_t) != 0U && !halted_ && !exception_raised_) {
            raise_exception(vec_trace, pc_);
        }
        if (cycles_ < 4) {
            cycles_ = 4;
        }
        elapsed_ += static_cast<std::uint64_t>(cycles_);
        last_cycle_sources_ = cycle_sources_;

        // V-int-enable-via-MOVE.W schedules the VINT IRQ for one instruction
        // later. The counter is set to 2 inside the triggering instruction
        // and decrements at the end of each step_instruction; when it hits
        // 0, irq_level_ is raised. Net effect: the scheduling instruction
        // plus ONE more run without the IRQ; the next inst takes it.
        if (delayed_irq_counter_ > 0) {
            --delayed_irq_counter_;
            if (delayed_irq_counter_ == 0) {
                set_irq_level(delayed_irq_level_);
                delayed_irq_level_ = 0;
            }
        }
        return cycles_;
    }

    void m68000::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void m68000::reset(reset_kind /*kind*/) {
        d_.fill(0U);
        a_.fill(0U);
        pc_ = 0U;
        usp_ = 0U;
        ssp_ = 0U;
        irq_level_ = 0;
        prev_irq_level_ = 0;
        delayed_irq_level_ = 0;
        delayed_irq_counter_ = 0;
        inst_addr_ = 0U;
        current_opcode_ = 0U;
        group0_fault_ = {};
        exception_raised_ = false;
        exception_entry_ = false;
        (void)consume_bus_fault();
        stopped_ = false;
        halted_ = false;
        cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
        // Genesis-only initial DRAM refresh phase. The semantically canonical
        // first-refresh point after the reset exception is 88, but 62 empirically
        // aligns the refresh boundaries better with the rest of the boot-time
        // cycle accounting, so it is the value used when a Genesis board opts in.
        bus_refresh_due_ = bus_refresh_enabled_ ? genesis_refresh_initial_due : 0U;

        // Supervisor mode, interrupts fully masked; the reset vector lives at $0
        // (SSP) and $4 (PC), read big-endian off the bus. The vector sources the
        // initial PC, so it is an instruction fetch: on an opcode/data-split board
        // (CPS-2) it must come from the decrypted opcode image. fetch16_be_opcode
        // defaults to read16_be, so every other system is byte-for-byte unchanged.
        sr_ = static_cast<std::uint16_t>(sr_s | (7U << 8U));
        if (bus_ != nullptr) {
            const std::uint32_t ssp =
                (static_cast<std::uint32_t>(bus_->fetch16_be_opcode(0U)) << 16U) |
                bus_->fetch16_be_opcode(2U);
            const std::uint32_t pc =
                (static_cast<std::uint32_t>(bus_->fetch16_be_opcode(4U)) << 16U) |
                bus_->fetch16_be_opcode(6U);
            a_[7] = ssp;
            ssp_ = ssp;
            pc_ = pc;
        }
    }

    void m68000::set_irq_level(int level) noexcept {
        prev_irq_level_ = irq_level_;
        irq_level_ = level < 0 ? 0 : (level > 7 ? 7 : level);
        // STOP ends only when an interrupt is actually ACCEPTED (level above
        // the SR mask, or level 7) -- process_interrupt() clears stopped_ then.
        // Waking on any nonzero request resumed execution on requests the mask
        // holds off, which STOP #$2700 code relies on staying stopped through.
    }

    m68000::registers m68000::cpu_registers() const noexcept {
        registers r;
        r.d = d_;
        r.a = a_;
        r.pc = pc_;
        r.sr = sr_;
        r.usp = usp_;
        r.ssp = ssp_;
        return r;
    }

    void m68000::set_registers(const registers& values) noexcept {
        d_ = values.d;
        a_ = values.a;
        pc_ = values.pc;
        sr_ = values.sr;
        usp_ = values.usp;
        ssp_ = values.ssp;
        current_opcode_ = 0U;
        group0_fault_ = {};
        exception_raised_ = false;
        exception_entry_ = false;
        (void)consume_bus_fault();
    }

    void m68000::save_state(state_writer& writer) const {
        for (const std::uint32_t v : d_) {
            writer.u32(v);
        }
        for (const std::uint32_t v : a_) {
            writer.u32(v);
        }
        writer.u32(pc_);
        writer.u16(sr_);
        writer.u32(usp_);
        writer.u32(ssp_);
        writer.u8(static_cast<std::uint8_t>(irq_level_));
        writer.u8(static_cast<std::uint8_t>(prev_irq_level_));
        writer.boolean(stopped_);
        writer.boolean(halted_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
        // Timing/IRQ state appended after the v1 layout: the refresh schedule
        // phase (a resumed run must fire refreshes at the saved cadence, not
        // re-seed at a different phase) and a delayed IRQ parked in its
        // instruction-count window (dropping it loses the interrupt entirely).
        writer.u64(bus_refresh_due_);
        writer.u8(static_cast<std::uint8_t>(delayed_irq_level_));
        writer.u8(static_cast<std::uint8_t>(delayed_irq_counter_));
    }

    void m68000::load_state(state_reader& reader) {
        for (std::uint32_t& v : d_) {
            v = reader.u32();
        }
        for (std::uint32_t& v : a_) {
            v = reader.u32();
        }
        pc_ = reader.u32();
        sr_ = reader.u16();
        usp_ = reader.u32();
        ssp_ = reader.u32();
        irq_level_ = reader.u8();
        prev_irq_level_ = reader.u8();
        stopped_ = reader.boolean();
        halted_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
        bus_refresh_due_ = reader.u64();
        delayed_irq_level_ = reader.u8() & 0x7;
        delayed_irq_counter_ = reader.u8();
        current_opcode_ = 0U;
        group0_fault_ = {};
        exception_raised_ = false;
        exception_entry_ = false;
        (void)consume_bus_fault();
    }

    instrumentation::ichip_introspection& m68000::introspection() noexcept {
        return introspection_;
    }

    void m68000::configure(const config_table& cfg, const callback_table& callbacks) {
        // Genesis-style Z80-bus access latency: every $A00000-$A0FFFF bus
        // access on the cycle-accounted path costs one extra CPU cycle. The
        // Sega Genesis manifest sets `z80_bus_latency = true`; other m68000
        // systems leave it off (default).
        if (const auto v = chips::cfg_bool(cfg, "z80_bus_latency")) {
            z80_bus_latency_enabled_ = *v;
        }
        if (const auto v = chips::cfg_bool(cfg, "genesis_dram_refresh")) {
            set_bus_refresh_enabled(*v);
        }
        if (const auto v = chips::cfg_bool(cfg, "genesis_interrupt_phase_timing")) {
            set_genesis_interrupt_phase_timing_enabled(*v);
        }

        // Genesis IRQ-ack callback (clears VDP V-int latch on IACK cycle).
        // Manifest names a void(int) callback; host registers the actual
        // function. Phase A.2 plumbing: chip-side consumption ready, but no
        // production system installs this through the manifest yet --
        // assemble_genesis still wires set_irq_ack_callback inline. Phase B
        // will switch over.
        if (const auto id = chips::cfg_string(cfg, "irq_ack_callback")) {
            if (const auto* fn = chips::find_callback<void(int)>(callbacks, *id)) {
                irq_ack_ = *fn;
            }
        }

        // TAS write-suppression callback (Genesis-only; Sega's bus controller
        // ignores the TAS write phase). Manifest names a void(uint32_t) hook;
        // host supplies it.
        if (const auto id = chips::cfg_string(cfg, "tas_callback")) {
            if (const auto* fn = chips::find_callback<void(std::uint32_t)>(callbacks, *id)) {
                tas_callback_ = *fn;
            }
        }
    }

    std::span<const register_descriptor> m68000::register_snapshot() noexcept {
        using fmt = register_value_format;
        static constexpr std::array<std::string_view, 8> dn = {"D0", "D1", "D2", "D3",
                                                               "D4", "D5", "D6", "D7"};
        static constexpr std::array<std::string_view, 8> an = {"A0", "A1", "A2", "A3",
                                                               "A4", "A5", "A6", "A7"};
        for (std::size_t i = 0; i < 8; ++i) {
            register_view_[i] = {dn[i], d_[i], 32U, fmt::unsigned_integer};
            register_view_[8 + i] = {an[i], a_[i], 32U, fmt::unsigned_integer};
        }
        register_view_[16] = {"PC", pc_, 32U, fmt::unsigned_integer};
        register_view_[17] = {"SR", sr_, 16U, fmt::flags};
        register_view_[18] = {"USP", usp_, 32U, fmt::unsigned_integer};
        register_view_[19] = {"SSP", ssp_, 32U, fmt::unsigned_integer};
        return register_view_;
    }

    void m68000_diagnostics::set_trace_callback(
        std::function<void(std::uint32_t pc)> callback) noexcept {
        owner_->trace_callback_ = std::move(callback);
    }

    const m68000_diagnostics::cycle_sources&
    m68000_diagnostics::last_cycle_sources() const noexcept {
        return owner_->last_cycle_sources_;
    }

} // namespace mnemos::chips::cpu
