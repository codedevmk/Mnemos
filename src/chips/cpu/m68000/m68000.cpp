#include "m68000.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <array>
#include <memory>
#include <string_view>

namespace mnemos::chips::cpu {

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
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(rd8(a)) << 8U) | rd8(a + 1U));
    }
    void m68000::wr16(std::uint32_t a, std::uint16_t v) noexcept {
        wr8(a, static_cast<std::uint8_t>(v >> 8U));
        wr8(a + 1U, static_cast<std::uint8_t>(v));
    }

    // ---- cycle-accounted accesses (one bus cycle = 4 clocks) ----

    std::uint8_t m68000::read8(std::uint32_t a) noexcept {
        cycles_ += 4;
        return rd8(a);
    }
    std::uint16_t m68000::read16(std::uint32_t a) noexcept {
        cycles_ += 4;
        return rd16(a);
    }
    std::uint32_t m68000::read32(std::uint32_t a) noexcept {
        cycles_ += 8;
        return (static_cast<std::uint32_t>(rd16(a)) << 16U) | rd16(a + 2U);
    }
    void m68000::write8(std::uint32_t a, std::uint8_t v) noexcept {
        cycles_ += 4;
        wr8(a, v);
    }
    void m68000::write16(std::uint32_t a, std::uint16_t v) noexcept {
        cycles_ += 4;
        wr16(a, v);
    }
    void m68000::write32(std::uint32_t a, std::uint32_t v) noexcept {
        cycles_ += 8;
        wr16(a, static_cast<std::uint16_t>(v >> 16U));
        wr16(a + 2U, static_cast<std::uint16_t>(v));
    }

    std::uint32_t m68000::read_sized(std::uint32_t a, op_size s) noexcept {
        switch (s) {
        case op_size::byte:
            return read8(a);
        case op_size::word:
            return read16(a);
        case op_size::longword:
            break;
        }
        return read32(a);
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

    std::uint16_t m68000::fetch16() noexcept {
        cycles_ += 4;
        const std::uint16_t v = rd16(pc_);
        pc_ += 2U;
        return v;
    }
    std::uint32_t m68000::fetch32() noexcept {
        cycles_ += 8;
        const std::uint32_t v = (static_cast<std::uint32_t>(rd16(pc_)) << 16U) | rd16(pc_ + 2U);
        pc_ += 4U;
        return v;
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
        return read_sized(ea_address(mode, reg, s, true), s);
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
        return read_sized(addr, s);
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
            const std::uint32_t r = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(a_[static_cast<std::size_t>(dn)]) - src);
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
        // DIVU/DIVS (opm 3/7) and SBCD (opm 4-6, em < 2) land in later phases.
        if (opm == 3 || opm == 7) {
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

    void m68000::op_quick(std::uint16_t op) noexcept {
        const int em = (op >> 3U) & 7, er = op & 7;
        int data = (op >> 9U) & 7;
        if (data == 0) {
            data = 8;
        }
        const op_size sz = static_cast<op_size>((op >> 6U) & 3);
        if (static_cast<int>(sz) == 3) {
            return; // Scc / DBcc -- control flow, phase 4
        }
        if (em == 1) { // ADDQ/SUBQ An: full 32-bit, no flags
            if ((op & 0x0100U) != 0U) {
                a_[static_cast<std::size_t>(er)] -= static_cast<std::uint32_t>(data);
            } else {
                a_[static_cast<std::size_t>(er)] += static_cast<std::uint32_t>(data);
            }
            cycles_ += sz == op_size::longword ? 2 : 4;
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
        const int em = (op >> 3U) & 7;
        // Bit 8 set: dynamic bit op (BTST/BCHG/BCLR/BSET Dn,<ea>); MOVEP (mode 1)
        // is deferred to a later phase.
        if ((op & 0x0100U) != 0U) {
            if (em == 1) {
                return; // MOVEP
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
        const int er = op & 7;
        std::uint32_t imm =
            sz == op_size::longword ? fetch32() : static_cast<std::uint32_t>(fetch16());
        if (sz == op_size::byte) {
            imm &= 0xFFU;
        }
        // ORI/ANDI/EORI #imm,CCR (byte). The SR (word) forms need the supervisor
        // machinery + the privilege trap, so they land with the exception phase.
        if ((op & 0x3FU) == 0x3CU) {
            if (sz == op_size::byte) {
                const auto ccr = static_cast<std::uint16_t>(imm & sr_ccr);
                if (sub == 0) {
                    sr_ |= ccr; // ORI CCR
                } else if (sub == 1) {
                    sr_ = static_cast<std::uint16_t>((sr_ & ~sr_ccr) | (sr_ & ccr)); // ANDI CCR
                } else if (sub == 5) {
                    sr_ ^= ccr; // EORI CCR
                }
            }
            return;
        }
        if (sub == 6) { // CMPI
            const std::uint32_t dst = ea_read(em, er, sz);
            flags_cmp(sz, imm, dst, dst - imm);
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
    }

    void m68000::op_group4(std::uint16_t op) noexcept {
        if (op == 0x4E71U) {
            return; // NOP
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
                set_logic_flags(sz, r);
                return;
            }
            break;
        case 0xA: // TST (TAS, size field 3, is deferred)
            if (static_cast<int>(sz) != 3) {
                const std::uint32_t v = ea_read(em, er, sz);
                set_logic_flags(sz, v);
                return;
            }
            break;
        default:
            break;
        }
        // NOT, SWAP, PEA, LEA, JMP, JSR, MOVEM, MOVE-to/from-SR/CCR, TRAP, etc. land
        // in later phases.
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
        default:
            // Groups 6 (Bcc), A/F (line traps), E (shifts) arrive in later phases --
            // a 4-cycle no-op until then.
            break;
        }
    }

    // ---- step / lifecycle ----

    int m68000::step_instruction() {
        cycles_ = 0;
        if (halted_ || stopped_) {
            return 4;
        }
        const std::uint16_t op = fetch16();
        exec(op);
        if (cycles_ < 4) {
            cycles_ = 4;
        }
        elapsed_ += static_cast<std::uint64_t>(cycles_);
        return cycles_;
    }

    void m68000::tick(std::uint64_t cycles) {
        cycle_debt_ += static_cast<std::int64_t>(cycles);
        while (cycle_debt_ > 0) {
            cycle_debt_ -= step_instruction();
        }
    }

    void m68000::reset(reset_kind /*kind*/) {
        d_.fill(0U);
        a_.fill(0U);
        pc_ = 0U;
        usp_ = 0U;
        ssp_ = 0U;
        irq_level_ = 0;
        stopped_ = false;
        halted_ = false;
        cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;

        // Supervisor mode, interrupts fully masked; the reset vector lives at $0
        // (SSP) and $4 (PC), read big-endian off the bus.
        sr_ = static_cast<std::uint16_t>(sr_s | (7U << 8U));
        if (bus_ != nullptr) {
            const std::uint32_t ssp = (static_cast<std::uint32_t>(rd16(0U)) << 16U) | rd16(2U);
            const std::uint32_t pc = (static_cast<std::uint32_t>(rd16(4U)) << 16U) | rd16(6U);
            a_[7] = ssp;
            ssp_ = ssp;
            pc_ = pc;
        }
    }

    void m68000::set_irq_level(int level) noexcept {
        irq_level_ = level < 0 ? 0 : (level > 7 ? 7 : level);
        if (irq_level_ > 0) {
            stopped_ = false;
        }
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
        writer.boolean(stopped_);
        writer.boolean(halted_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
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
        stopped_ = reader.boolean();
        halted_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& m68000::introspection() noexcept {
        return introspection_;
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

    namespace {
        [[maybe_unused]] const auto m68000_registration =
            register_factory("motorola.68000", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<m68000>(); });
    } // namespace

} // namespace mnemos::chips::cpu
