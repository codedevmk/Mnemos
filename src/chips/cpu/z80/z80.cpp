#include "z80.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <array>
#include <memory>

namespace mnemos::chips::cpu {
    namespace {

        // Precomputed flag tables (computed once on first use).
        struct flag_tables final {
            std::array<std::uint8_t, 256> parity{};
            std::array<std::uint8_t, 256> sz53{};
            std::array<std::uint8_t, 256> sz53p{};

            flag_tables() {
                for (int i = 0; i < 256; ++i) {
                    int bits = 0;
                    for (int b = 0; b < 8; ++b) {
                        if ((i & (1 << b)) != 0) {
                            ++bits;
                        }
                    }
                    parity[static_cast<std::size_t>(i)] = (bits & 1) != 0 ? 0U : z80::flag_p;
                    auto s =
                        static_cast<std::uint8_t>(i & (z80::flag_s | z80::flag_x | z80::flag_y));
                    if (i == 0) {
                        s |= z80::flag_z;
                    }
                    sz53[static_cast<std::size_t>(i)] = s;
                    sz53p[static_cast<std::size_t>(i)] =
                        static_cast<std::uint8_t>(s | parity[static_cast<std::size_t>(i)]);
                }
            }
        };

        const flag_tables& tables() {
            static const flag_tables t;
            return t;
        }

        std::uint8_t add8_flags(std::uint8_t a, std::uint8_t b, std::uint8_t carry,
                                std::uint8_t& result) {
            const std::uint16_t sum = static_cast<std::uint16_t>(a + b + carry);
            result = static_cast<std::uint8_t>(sum);
            std::uint8_t fl = tables().sz53[result];
            if (sum > 0xFFU) {
                fl |= z80::flag_c;
            }
            if (((a ^ b ^ result) & 0x10U) != 0U) {
                fl |= z80::flag_h;
            }
            if (((~(a ^ b) & (a ^ result)) & 0x80U) != 0U) {
                fl |= z80::flag_p;
            }
            return fl;
        }

        std::uint8_t sub8_flags(std::uint8_t a, std::uint8_t b, std::uint8_t carry,
                                std::uint8_t& result) {
            const std::uint16_t diff = static_cast<std::uint16_t>(a - b - carry);
            result = static_cast<std::uint8_t>(diff);
            std::uint8_t fl = static_cast<std::uint8_t>(tables().sz53[result] | z80::flag_n);
            if (diff > 0xFFU) {
                fl |= z80::flag_c;
            }
            if (((a ^ b ^ result) & 0x10U) != 0U) {
                fl |= z80::flag_h;
            }
            if ((((a ^ b) & (a ^ result)) & 0x80U) != 0U) {
                fl |= z80::flag_p;
            }
            return fl;
        }
    } // namespace

    chip_metadata z80::metadata() const noexcept {
        return {
            .manufacturer = "Zilog",
            .part_number = "Z80",
            .family = "Z80",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    // ---- memory + I/O ----

    std::uint8_t z80::rb(std::uint16_t addr) noexcept {
        return bus_ != nullptr ? bus_->read8(addr) : 0xFFU;
    }
    void z80::wb(std::uint16_t addr, std::uint8_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(addr, value);
        }
    }
    std::uint16_t z80::rw(std::uint16_t addr) noexcept {
        const std::uint8_t lo = rb(addr);
        const std::uint8_t hi = rb(static_cast<std::uint16_t>(addr + 1U));
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }
    void z80::ww(std::uint16_t addr, std::uint16_t value) noexcept {
        wb(addr, static_cast<std::uint8_t>(value));
        wb(static_cast<std::uint16_t>(addr + 1U), static_cast<std::uint8_t>(value >> 8U));
    }
    // M1 opcode fetch: routes through the bus's instruction-fetch path
    // (fetch_opcode8, default = read8) so an opcode/data-split board (Kabuki on
    // CPS1 QSound) can serve the decrypted opcode stream here while operand and
    // data reads stay on read8. Identical to read8 for every normal system.
    std::uint8_t z80::op_fetch8() noexcept {
        const std::uint8_t v = bus_ != nullptr ? bus_->fetch_opcode8(pc_) : 0xFFU;
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return v;
    }
    // Operand fetch (immediates, displacements): a non-M1 read, so it uses the
    // data path (read8), distinct from op_fetch8's opcode path.
    std::uint8_t z80::imm_fetch8() noexcept {
        const std::uint8_t v = rb(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return v;
    }
    std::uint16_t z80::fetch16() noexcept {
        const std::uint8_t lo = imm_fetch8();
        const std::uint8_t hi = imm_fetch8();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }
    std::int8_t z80::fetchd() noexcept { return static_cast<std::int8_t>(imm_fetch8()); }

    std::uint8_t z80::port_in8(std::uint16_t port) { return port_in_ ? port_in_(port) : 0xFFU; }
    void z80::port_out8(std::uint16_t port, std::uint8_t value) {
        if (port_out_) {
            port_out_(port, value);
        }
    }

    void z80::push16(std::uint16_t v) noexcept {
        sp_ = static_cast<std::uint16_t>(sp_ - 2U);
        ww(sp_, v);
    }
    std::uint16_t z80::pop16() noexcept {
        const std::uint16_t v = rw(sp_);
        sp_ = static_cast<std::uint16_t>(sp_ + 2U);
        return v;
    }
    std::uint16_t z80::ex_sp16(std::uint16_t reg) noexcept {
        const std::uint16_t mem = rw(sp_);
        ww(sp_, reg);
        return mem;
    }

    // ---- ALU helpers ----

    void z80::do_add_a(std::uint8_t v) noexcept {
        std::uint8_t r = 0;
        set_f(add8_flags(a(), v, 0U, r));
        set_a(r);
    }
    void z80::do_adc_a(std::uint8_t v) noexcept {
        std::uint8_t r = 0;
        set_f(add8_flags(a(), v, (f() & flag_c) != 0U ? 1U : 0U, r));
        set_a(r);
    }
    void z80::do_sub_a(std::uint8_t v) noexcept {
        std::uint8_t r = 0;
        set_f(sub8_flags(a(), v, 0U, r));
        set_a(r);
    }
    void z80::do_sbc_a(std::uint8_t v) noexcept {
        std::uint8_t r = 0;
        set_f(sub8_flags(a(), v, (f() & flag_c) != 0U ? 1U : 0U, r));
        set_a(r);
    }
    void z80::do_cp(std::uint8_t v) noexcept {
        std::uint8_t r = 0;
        std::uint8_t fl = sub8_flags(a(), v, 0U, r);
        // CP leaves bits 3,5 from the operand, not the result.
        fl = static_cast<std::uint8_t>((fl & ~(flag_x | flag_y)) | (v & (flag_x | flag_y)));
        set_f(fl);
    }
    void z80::do_and(std::uint8_t v) noexcept {
        set_a(static_cast<std::uint8_t>(a() & v));
        set_f(static_cast<std::uint8_t>(tables().sz53p[a()] | flag_h));
    }
    void z80::do_or(std::uint8_t v) noexcept {
        set_a(static_cast<std::uint8_t>(a() | v));
        set_f(tables().sz53p[a()]);
    }
    void z80::do_xor(std::uint8_t v) noexcept {
        set_a(static_cast<std::uint8_t>(a() ^ v));
        set_f(tables().sz53p[a()]);
    }
    std::uint8_t z80::do_inc(std::uint8_t v) noexcept {
        const auto r = static_cast<std::uint8_t>(v + 1U);
        std::uint8_t fl = static_cast<std::uint8_t>((f() & flag_c) | tables().sz53[r]);
        if (r == 0x80U) {
            fl |= flag_p;
        }
        if ((r & 0x0FU) == 0U) {
            fl |= flag_h;
        }
        set_f(fl);
        return r;
    }
    std::uint8_t z80::do_dec(std::uint8_t v) noexcept {
        const auto r = static_cast<std::uint8_t>(v - 1U);
        std::uint8_t fl = static_cast<std::uint8_t>((f() & flag_c) | tables().sz53[r] | flag_n);
        if (v == 0x80U) {
            fl |= flag_p;
        }
        if ((v & 0x0FU) == 0U) {
            fl |= flag_h;
        }
        set_f(fl);
        return r;
    }
    void z80::do_add16(std::uint16_t& dst, std::uint16_t src) noexcept {
        const std::uint32_t sum = static_cast<std::uint32_t>(dst) + src;
        std::uint8_t fl = static_cast<std::uint8_t>(f() & (flag_s | flag_z | flag_p));
        if (sum > 0xFFFFU) {
            fl |= flag_c;
        }
        if (((dst ^ src ^ static_cast<std::uint16_t>(sum)) & 0x1000U) != 0U) {
            fl |= flag_h;
        }
        dst = static_cast<std::uint16_t>(sum);
        fl |= static_cast<std::uint8_t>((dst >> 8U) & (flag_x | flag_y));
        set_f(fl);
    }
    void z80::do_adc16(std::uint16_t src) noexcept {
        const std::uint32_t sum =
            static_cast<std::uint32_t>(hl_) + src + ((f() & flag_c) != 0U ? 1U : 0U);
        const auto r = static_cast<std::uint16_t>(sum);
        std::uint8_t fl = static_cast<std::uint8_t>((r >> 8U) & (flag_s | flag_x | flag_y));
        if (r == 0U) {
            fl |= flag_z;
        }
        if (sum > 0xFFFFU) {
            fl |= flag_c;
        }
        if (((hl_ ^ src ^ r) & 0x1000U) != 0U) {
            fl |= flag_h;
        }
        if (((~(hl_ ^ src) & (hl_ ^ r)) & 0x8000U) != 0U) {
            fl |= flag_p;
        }
        hl_ = r;
        set_f(fl);
    }
    void z80::do_sbc16(std::uint16_t src) noexcept {
        const std::uint32_t diff =
            static_cast<std::uint32_t>(hl_) - src - ((f() & flag_c) != 0U ? 1U : 0U);
        const auto r = static_cast<std::uint16_t>(diff);
        std::uint8_t fl =
            static_cast<std::uint8_t>(((r >> 8U) & (flag_s | flag_x | flag_y)) | flag_n);
        if (r == 0U) {
            fl |= flag_z;
        }
        if (diff > 0xFFFFU) {
            fl |= flag_c;
        }
        if (((hl_ ^ src ^ r) & 0x1000U) != 0U) {
            fl |= flag_h;
        }
        if ((((hl_ ^ src) & (hl_ ^ r)) & 0x8000U) != 0U) {
            fl |= flag_p;
        }
        hl_ = r;
        set_f(fl);
    }
    void z80::do_alu(int op, std::uint8_t v) noexcept {
        switch (op) {
        case 0:
            do_add_a(v);
            break;
        case 1:
            do_adc_a(v);
            break;
        case 2:
            do_sub_a(v);
            break;
        case 3:
            do_sbc_a(v);
            break;
        case 4:
            do_and(v);
            break;
        case 5:
            do_xor(v);
            break;
        case 6:
            do_or(v);
            break;
        default:
            do_cp(v);
            break;
        }
    }

    // ---- register access by 3-bit encoding ----

    std::uint8_t z80::get_reg8(int reg) noexcept {
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
            return a();
        }
    }
    void z80::set_reg8(int reg, std::uint8_t v) noexcept {
        switch (reg) {
        case 0:
            set_b(v);
            break;
        case 1:
            set_c(v);
            break;
        case 2:
            set_d(v);
            break;
        case 3:
            set_e(v);
            break;
        case 4:
            set_h(v);
            break;
        case 5:
            set_l(v);
            break;
        case 6:
            wb(hl_, v);
            break;
        default:
            set_a(v);
            break;
        }
    }
    std::uint16_t* z80::get_rp(int p) noexcept {
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
    std::uint16_t* z80::get_rp2(int p) noexcept {
        switch (p) {
        case 0:
            return &bc_;
        case 1:
            return &de_;
        case 2:
            return &hl_;
        default:
            return &af_;
        }
    }
    bool z80::test_cc(int cc) const noexcept {
        switch (cc) {
        case 0:
            return (f() & flag_z) == 0U;
        case 1:
            return (f() & flag_z) != 0U;
        case 2:
            return (f() & flag_c) == 0U;
        case 3:
            return (f() & flag_c) != 0U;
        case 4:
            return (f() & flag_p) == 0U;
        case 5:
            return (f() & flag_p) != 0U;
        case 6:
            return (f() & flag_s) == 0U;
        default:
            return (f() & flag_s) != 0U;
        }
    }

    namespace {
        // Rotate/shift returning the result and setting the flags via `fl`.
        std::uint8_t do_rot(int op, std::uint8_t v, std::uint8_t& fl) {
            const auto& t = tables();
            std::uint8_t r = 0;
            switch (op) {
            case 0: // RLC
                r = static_cast<std::uint8_t>((v << 1U) | (v >> 7U));
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (r & z80::flag_c));
                break;
            case 1: // RRC
                r = static_cast<std::uint8_t>((v >> 1U) | (v << 7U));
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v & z80::flag_c));
                break;
            case 2: // RL
                r = static_cast<std::uint8_t>((v << 1U) | ((fl & z80::flag_c) != 0U ? 1U : 0U));
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v >> 7U));
                break;
            case 3: // RR
                r = static_cast<std::uint8_t>((v >> 1U) | ((fl & z80::flag_c) != 0U ? 0x80U : 0U));
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v & 1U));
                break;
            case 4: // SLA
                r = static_cast<std::uint8_t>(v << 1U);
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v >> 7U));
                break;
            case 5: // SRA
                r = static_cast<std::uint8_t>((v >> 1U) | (v & 0x80U));
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v & 1U));
                break;
            case 6: // SLL (undocumented)
                r = static_cast<std::uint8_t>((v << 1U) | 1U);
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v >> 7U));
                break;
            default: // SRL
                r = static_cast<std::uint8_t>(v >> 1U);
                fl = static_cast<std::uint8_t>(t.sz53p[r] | (v & 1U));
                break;
            }
            return r;
        }
    } // namespace

    // ---- CB prefix ----

    void z80::exec_cb() {
        const std::uint8_t op = op_fetch8();
        inc_r();
        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;

        if (x == 0) {
            std::uint8_t fl = f();
            const std::uint8_t r = do_rot(y, get_reg8(z), fl);
            set_f(fl);
            set_reg8(z, r);
            step_cycles_ += (z == 6) ? 15 : 8;
        } else if (x == 1) { // BIT y, r
            const std::uint8_t v = get_reg8(z);
            const auto bit = static_cast<std::uint8_t>(v & (1U << y));
            std::uint8_t fl =
                static_cast<std::uint8_t>((f() & flag_c) | flag_h | (v & (flag_x | flag_y)));
            if (bit == 0U) {
                fl |= flag_z | flag_p;
            }
            if (y == 7 && bit != 0U) {
                fl |= flag_s;
            }
            set_f(fl);
            step_cycles_ += (z == 6) ? 12 : 8;
        } else if (x == 2) { // RES y, r
            set_reg8(z, static_cast<std::uint8_t>(get_reg8(z) & ~(1U << y)));
            step_cycles_ += (z == 6) ? 15 : 8;
        } else { // SET y, r
            set_reg8(z, static_cast<std::uint8_t>(get_reg8(z) | (1U << y)));
            step_cycles_ += (z == 6) ? 15 : 8;
        }
    }

    // ---- DDCB / FDCB prefix (indexed bit/rotate) ----

    void z80::exec_xcb(std::uint16_t idx) {
        const std::int8_t disp = fetchd();
        const std::uint8_t op = imm_fetch8();
        const auto addr = static_cast<std::uint16_t>(idx + disp);
        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;
        const std::uint8_t v = rb(addr);

        if (x == 0) {
            std::uint8_t fl = f();
            const std::uint8_t r = do_rot(y, v, fl);
            set_f(fl);
            wb(addr, r);
            if (z != 6) {
                set_reg8(z, r); // undocumented: also copy to the register
            }
            step_cycles_ += 23;
        } else if (x == 1) { // BIT y, (IX+d)
            const auto bit = static_cast<std::uint8_t>(v & (1U << y));
            std::uint8_t fl = static_cast<std::uint8_t>((f() & flag_c) | flag_h);
            fl |= static_cast<std::uint8_t>((addr >> 8U) & (flag_x | flag_y));
            if (bit == 0U) {
                fl |= flag_z | flag_p;
            }
            if (y == 7 && bit != 0U) {
                fl |= flag_s;
            }
            set_f(fl);
            step_cycles_ += 20;
        } else if (x == 2) {
            const auto r = static_cast<std::uint8_t>(v & ~(1U << y));
            wb(addr, r);
            if (z != 6) {
                set_reg8(z, r);
            }
            step_cycles_ += 23;
        } else {
            const auto r = static_cast<std::uint8_t>(v | (1U << y));
            wb(addr, r);
            if (z != 6) {
                set_reg8(z, r);
            }
            step_cycles_ += 23;
        }
    }

    // ---- ED prefix ----

    void z80::exec_ed() {
        const std::uint8_t op = op_fetch8();
        inc_r();
        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;

        if (x == 1) {
            switch (z) {
            case 0: { // IN r,(C)
                const std::uint8_t v = port_in8(bc_);
                if (y != 6) {
                    set_reg8(y, v);
                }
                set_f(static_cast<std::uint8_t>((f() & flag_c) | tables().sz53p[v]));
                step_cycles_ += 12;
                break;
            }
            case 1: // OUT (C),r
                port_out8(bc_, (y != 6) ? get_reg8(y) : 0U);
                step_cycles_ += 12;
                break;
            case 2: // SBC/ADC HL,rp
                if ((y & 1) != 0) {
                    do_adc16(*get_rp(y >> 1));
                } else {
                    do_sbc16(*get_rp(y >> 1));
                }
                step_cycles_ += 15;
                break;
            case 3: { // LD (nn),rp / LD rp,(nn)
                const std::uint16_t addr = fetch16();
                if ((y & 1) != 0) {
                    *get_rp(y >> 1) = rw(addr);
                } else {
                    ww(addr, *get_rp(y >> 1));
                }
                step_cycles_ += 20;
                break;
            }
            case 4: { // NEG
                const std::uint8_t old = a();
                std::uint8_t r = 0;
                set_f(sub8_flags(0U, old, 0U, r));
                set_a(r);
                step_cycles_ += 8;
                break;
            }
            case 5: // RETN / RETI
                iff1_ = iff2_;
                pc_ = pop16();
                step_cycles_ += 14;
                break;
            case 6: // IM n
                switch (y) {
                case 2:
                case 6:
                    im_ = 1;
                    break;
                case 3:
                case 7:
                    im_ = 2;
                    break;
                default:
                    im_ = 0;
                    break;
                }
                step_cycles_ += 8;
                break;
            default: // z == 7: LD I/R/A,..., RRD, RLD
                switch (y) {
                case 0:
                    i_ = a();
                    step_cycles_ += 9;
                    break;
                case 1:
                    r_ = a();
                    step_cycles_ += 9;
                    break;
                case 2: // LD A,I
                    set_a(i_);
                    set_f(static_cast<std::uint8_t>((f() & flag_c) | tables().sz53[a()] |
                                                    (iff2_ ? flag_p : 0U)));
                    step_cycles_ += 9;
                    break;
                case 3: // LD A,R
                    set_a(r_);
                    set_f(static_cast<std::uint8_t>((f() & flag_c) | tables().sz53[a()] |
                                                    (iff2_ ? flag_p : 0U)));
                    step_cycles_ += 9;
                    break;
                case 4: { // RRD
                    std::uint8_t m = rb(hl_);
                    const std::uint8_t lo_a = a() & 0x0FU;
                    set_a(static_cast<std::uint8_t>((a() & 0xF0U) | (m & 0x0FU)));
                    m = static_cast<std::uint8_t>((m >> 4U) | (lo_a << 4U));
                    wb(hl_, m);
                    set_f(static_cast<std::uint8_t>((f() & flag_c) | tables().sz53p[a()]));
                    step_cycles_ += 18;
                    break;
                }
                case 5: { // RLD
                    std::uint8_t m = rb(hl_);
                    const std::uint8_t lo_a = a() & 0x0FU;
                    set_a(static_cast<std::uint8_t>((a() & 0xF0U) | (m >> 4U)));
                    m = static_cast<std::uint8_t>((m << 4U) | lo_a);
                    wb(hl_, m);
                    set_f(static_cast<std::uint8_t>((f() & flag_c) | tables().sz53p[a()]));
                    step_cycles_ += 18;
                    break;
                }
                default:
                    step_cycles_ += 8;
                    break;
                }
                break;
            }
        } else if (x == 2 && y >= 4 && z <= 3) {
            switch (z) {
            case 0: { // LDI / LDD / LDIR / LDDR
                const std::uint8_t v = rb(hl_);
                wb(de_, v);
                const auto n = static_cast<std::uint8_t>(v + a());
                std::uint8_t fl = static_cast<std::uint8_t>(f() & (flag_s | flag_z | flag_c));
                fl |= static_cast<std::uint8_t>(n & flag_x);
                if ((n & 0x02U) != 0U) {
                    fl |= flag_y;
                }
                if ((y & 1) != 0) {
                    hl_ = static_cast<std::uint16_t>(hl_ - 1U);
                    de_ = static_cast<std::uint16_t>(de_ - 1U);
                } else {
                    hl_ = static_cast<std::uint16_t>(hl_ + 1U);
                    de_ = static_cast<std::uint16_t>(de_ + 1U);
                }
                bc_ = static_cast<std::uint16_t>(bc_ - 1U);
                if (bc_ != 0U) {
                    fl |= flag_p;
                }
                set_f(fl);
                step_cycles_ += 16;
                if (y >= 6 && bc_ != 0U) {
                    pc_ = static_cast<std::uint16_t>(pc_ - 2U);
                    step_cycles_ += 5;
                }
                break;
            }
            case 1: { // CPI / CPD / CPIR / CPDR
                const std::uint8_t v = rb(hl_);
                const auto r = static_cast<std::uint8_t>(a() - v);
                const auto hc = static_cast<std::uint8_t>((a() ^ v ^ r) & 0x10U);
                std::uint8_t fl =
                    static_cast<std::uint8_t>((f() & flag_c) | flag_n | tables().sz53[r]);
                if (hc != 0U) {
                    fl |= flag_h;
                }
                const auto n = static_cast<std::uint8_t>(r - (hc != 0U ? 1U : 0U));
                fl = static_cast<std::uint8_t>((fl & ~(flag_x | flag_y)) | (n & flag_x));
                if ((n & 0x02U) != 0U) {
                    fl |= flag_y;
                }
                if ((y & 1) != 0) {
                    hl_ = static_cast<std::uint16_t>(hl_ - 1U);
                } else {
                    hl_ = static_cast<std::uint16_t>(hl_ + 1U);
                }
                bc_ = static_cast<std::uint16_t>(bc_ - 1U);
                if (bc_ != 0U) {
                    fl |= flag_p;
                }
                set_f(fl);
                step_cycles_ += 16;
                if (y >= 6 && bc_ != 0U && r != 0U) {
                    pc_ = static_cast<std::uint16_t>(pc_ - 2U);
                    step_cycles_ += 5;
                }
                break;
            }
            case 2: { // INI / IND / INIR / INDR
                const std::uint8_t v = port_in8(bc_);
                wb(hl_, v);
                set_b(static_cast<std::uint8_t>(b() - 1U));
                if ((y & 1) != 0) {
                    hl_ = static_cast<std::uint16_t>(hl_ - 1U);
                } else {
                    hl_ = static_cast<std::uint16_t>(hl_ + 1U);
                }
                const auto port_lo =
                    static_cast<std::uint8_t>((y & 1) != 0 ? (c() - 1U) : (c() + 1U));
                const std::uint16_t k = static_cast<std::uint16_t>(v + port_lo);
                // Undocumented Z80 Documented: N = bit 7 of the transferred
                // value; H = C = (k > 255); P = parity((k & 7) ^ B).
                std::uint8_t fl = tables().sz53[b()];
                if ((v & 0x80U) != 0U) {
                    fl |= flag_n;
                }
                if (k > 255U) {
                    fl |= flag_h | flag_c;
                }
                fl |= tables().parity[static_cast<std::uint8_t>((k & 7U) ^ b())];
                set_f(fl);
                step_cycles_ += 16;
                if (y >= 6 && b() != 0U) {
                    pc_ = static_cast<std::uint16_t>(pc_ - 2U);
                    step_cycles_ += 5;
                }
                break;
            }
            default: { // z == 3: OUTI / OUTD / OTIR / OTDR
                const std::uint8_t v = rb(hl_);
                set_b(static_cast<std::uint8_t>(b() - 1U));
                port_out8(bc_, v);
                if ((y & 1) != 0) {
                    hl_ = static_cast<std::uint16_t>(hl_ - 1U);
                } else {
                    hl_ = static_cast<std::uint16_t>(hl_ + 1U);
                }
                // Undocumented Z80 Documented: k = v + L (post-update); H = C =
                // (k > 255); P = parity((k & 7) ^ B); N = bit 7 of the value.
                const std::uint16_t k =
                    static_cast<std::uint16_t>(v + static_cast<std::uint8_t>(hl_ & 0xFFU));
                std::uint8_t fl = tables().sz53[b()];
                if ((v & 0x80U) != 0U) {
                    fl |= flag_n;
                }
                if (k > 255U) {
                    fl |= flag_h | flag_c;
                }
                fl |= tables().parity[static_cast<std::uint8_t>((k & 7U) ^ b())];
                set_f(fl);
                step_cycles_ += 16;
                if (y >= 6 && b() != 0U) {
                    pc_ = static_cast<std::uint16_t>(pc_ - 2U);
                    step_cycles_ += 5;
                }
                break;
            }
            }
        } else {
            step_cycles_ += 8; // invalid ED -> NOP
        }
    }

    // ---- DD/FD prefix (IX/IY) ----

    void z80::exec_dd_fd(bool use_iy) {
        std::uint16_t& idx = use_iy ? iy_ : ix_;

        const std::uint8_t op = op_fetch8();
        inc_r();

        if (op == 0xCB) {
            exec_xcb(idx);
            return;
        }
        if (op == 0xDD || op == 0xFD) {
            pc_ = static_cast<std::uint16_t>(pc_ - 1U);
            step_cycles_ += 4;
            return;
        }
        if (op == 0xED) {
            exec_ed();
            return;
        }

        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;
        const int p = y >> 1;
        const int q = y & 1;

        const auto idx_addr = [&]() { return static_cast<std::uint16_t>(idx + fetchd()); };
        const auto idxh = [&]() { return static_cast<std::uint8_t>(idx >> 8U); };
        const auto idxl = [&]() { return static_cast<std::uint8_t>(idx); };
        const auto set_idxh = [&](std::uint8_t v) {
            idx = static_cast<std::uint16_t>((idx & 0x00FFU) | (v << 8U));
        };
        const auto set_idxl = [&](std::uint8_t v) {
            idx = static_cast<std::uint16_t>((idx & 0xFF00U) | v);
        };

        bool handled = true;
        switch (x) {
        case 0:
            switch (z) {
            case 1:
                if (q == 0) {
                    if (p == 2) {
                        idx = fetch16();
                        step_cycles_ += 14;
                    } else {
                        *get_rp(p) = fetch16();
                        step_cycles_ += 10;
                    }
                } else {
                    std::uint16_t src = 0;
                    switch (p) {
                    case 0:
                        src = bc_;
                        break;
                    case 1:
                        src = de_;
                        break;
                    case 2:
                        src = idx;
                        break;
                    default:
                        src = sp_;
                        break;
                    }
                    do_add16(idx, src);
                    step_cycles_ += 15;
                }
                break;
            case 2:
                if (p == 2) {
                    const std::uint16_t addr = fetch16();
                    if (q == 0) {
                        ww(addr, idx);
                    } else {
                        idx = rw(addr);
                    }
                    step_cycles_ += 20;
                } else {
                    handled = false;
                }
                break;
            case 3:
                if (p == 2) {
                    if (q == 0) {
                        idx = static_cast<std::uint16_t>(idx + 1U);
                    } else {
                        idx = static_cast<std::uint16_t>(idx - 1U);
                    }
                    step_cycles_ += 10;
                } else {
                    handled = false;
                }
                break;
            case 4: // INC
                if (y == 4) {
                    set_idxh(do_inc(idxh()));
                    step_cycles_ += 8;
                } else if (y == 5) {
                    set_idxl(do_inc(idxl()));
                    step_cycles_ += 8;
                } else if (y == 6) {
                    const std::uint16_t addr = idx_addr();
                    wb(addr, do_inc(rb(addr)));
                    step_cycles_ += 23;
                } else {
                    handled = false;
                }
                break;
            case 5: // DEC
                if (y == 4) {
                    set_idxh(do_dec(idxh()));
                    step_cycles_ += 8;
                } else if (y == 5) {
                    set_idxl(do_dec(idxl()));
                    step_cycles_ += 8;
                } else if (y == 6) {
                    const std::uint16_t addr = idx_addr();
                    wb(addr, do_dec(rb(addr)));
                    step_cycles_ += 23;
                } else {
                    handled = false;
                }
                break;
            case 6: // LD r,n
                if (y == 4) {
                    set_idxh(imm_fetch8());
                    step_cycles_ += 11;
                } else if (y == 5) {
                    set_idxl(imm_fetch8());
                    step_cycles_ += 11;
                } else if (y == 6) {
                    const std::uint16_t addr = idx_addr();
                    wb(addr, imm_fetch8());
                    step_cycles_ += 19;
                } else {
                    handled = false;
                }
                break;
            default:
                handled = false;
                break;
            }
            break;

        case 1: { // LD r,r' with IXH/IXL/(IX+d) substitution
            if (y == 6 && z == 6) {
                halted_ = true;
                step_cycles_ += 4;
                break;
            }
            std::uint8_t src_val = 0;
            if (z == 6) {
                src_val = rb(idx_addr());
                step_cycles_ += 19;
            } else if (z == 4 && y != 6) {
                src_val = idxh();
                step_cycles_ += 8;
            } else if (z == 5 && y != 6) {
                src_val = idxl();
                step_cycles_ += 8;
            } else {
                src_val = get_reg8(z);
                step_cycles_ += 8;
            }
            if (y == 6) {
                wb(idx_addr(), src_val);
                step_cycles_ = 19;
            } else if (y == 4 && z != 6) {
                set_idxh(src_val);
            } else if (y == 5 && z != 6) {
                set_idxl(src_val);
            } else {
                set_reg8(y, src_val);
            }
            break;
        }

        case 2: { // ALU A,r with substitution
            std::uint8_t v = 0;
            if (z == 6) {
                v = rb(idx_addr());
                step_cycles_ += 19;
            } else if (z == 4) {
                v = idxh();
                step_cycles_ += 8;
            } else if (z == 5) {
                v = idxl();
                step_cycles_ += 8;
            } else {
                v = get_reg8(z);
                step_cycles_ += 8;
            }
            do_alu(y, v);
            break;
        }

        default: // x == 3
            if (op == 0xE1) {
                idx = pop16();
                step_cycles_ += 14;
            } else if (op == 0xE3) {
                idx = ex_sp16(idx);
                step_cycles_ += 23;
            } else if (op == 0xE5) {
                push16(idx);
                step_cycles_ += 15;
            } else if (op == 0xE9) {
                pc_ = idx; // JP (IX)
                step_cycles_ += 8;
            } else if (op == 0xF9) {
                sp_ = idx; // LD SP,IX
                step_cycles_ += 10;
            } else {
                handled = false;
            }
            break;
        }

        if (!handled) {
            // The prefix has no effect on this opcode: rewind and let the next
            // step re-decode it unprefixed (DD/FD each still cost 4T + an R inc).
            r_ = static_cast<std::uint8_t>((r_ & 0x80U) | ((r_ - 1U) & 0x7FU));
            pc_ = static_cast<std::uint16_t>(pc_ - 1U);
            step_cycles_ += 4;
        }
    }

    // ---- main (unprefixed) decode ----

    void z80::exec_main(std::uint8_t op) {
        const int x = op >> 6U;
        const int y = (op >> 3U) & 7;
        const int z = op & 7;
        const int p = y >> 1;
        const int q = y & 1;

        switch (x) {
        case 0:
            switch (z) {
            case 0:
                switch (y) {
                case 0:
                    step_cycles_ += 4;
                    break; // NOP
                case 1: {  // EX AF,AF'
                    const std::uint16_t t = af_;
                    af_ = af2_;
                    af2_ = t;
                    step_cycles_ += 4;
                    break;
                }
                case 2: { // DJNZ d
                    const std::int8_t d = fetchd();
                    set_b(static_cast<std::uint8_t>(b() - 1U));
                    if (b() != 0U) {
                        pc_ = static_cast<std::uint16_t>(pc_ + d);
                        step_cycles_ += 13;
                    } else {
                        step_cycles_ += 8;
                    }
                    break;
                }
                case 3: { // JR d
                    const std::int8_t d = fetchd();
                    pc_ = static_cast<std::uint16_t>(pc_ + d);
                    step_cycles_ += 12;
                    break;
                }
                default: { // JR cc,d (y=4..7)
                    const std::int8_t d = fetchd();
                    if (test_cc(y - 4)) {
                        pc_ = static_cast<std::uint16_t>(pc_ + d);
                        step_cycles_ += 12;
                    } else {
                        step_cycles_ += 7;
                    }
                    break;
                }
                }
                break;
            case 1:
                if (q == 0) {
                    *get_rp(p) = fetch16();
                    step_cycles_ += 10;
                } else {
                    do_add16(hl_, *get_rp(p));
                    step_cycles_ += 11;
                }
                break;
            case 2:
                switch (y) {
                case 0:
                    wb(bc_, a());
                    step_cycles_ += 7;
                    break;
                case 1:
                    set_a(rb(bc_));
                    step_cycles_ += 7;
                    break;
                case 2:
                    wb(de_, a());
                    step_cycles_ += 7;
                    break;
                case 3:
                    set_a(rb(de_));
                    step_cycles_ += 7;
                    break;
                case 4: {
                    const std::uint16_t addr = fetch16();
                    ww(addr, hl_);
                    step_cycles_ += 16;
                    break;
                }
                case 5: {
                    const std::uint16_t addr = fetch16();
                    hl_ = rw(addr);
                    step_cycles_ += 16;
                    break;
                }
                case 6: {
                    const std::uint16_t addr = fetch16();
                    wb(addr, a());
                    step_cycles_ += 13;
                    break;
                }
                default: {
                    const std::uint16_t addr = fetch16();
                    set_a(rb(addr));
                    step_cycles_ += 13;
                    break;
                }
                }
                break;
            case 3:
                if (q == 0) {
                    *get_rp(p) = static_cast<std::uint16_t>(*get_rp(p) + 1U);
                } else {
                    *get_rp(p) = static_cast<std::uint16_t>(*get_rp(p) - 1U);
                }
                step_cycles_ += 6;
                break;
            case 4: // INC r
                set_reg8(y, do_inc(get_reg8(y)));
                step_cycles_ += (y == 6) ? 11 : 4;
                break;
            case 5: // DEC r
                set_reg8(y, do_dec(get_reg8(y)));
                step_cycles_ += (y == 6) ? 11 : 4;
                break;
            case 6: // LD r,n
                set_reg8(y, imm_fetch8());
                step_cycles_ += (y == 6) ? 10 : 7;
                break;
            default: // z == 7: accumulator/flag ops
                switch (y) {
                case 0: { // RLCA
                    const auto bit7 = static_cast<std::uint8_t>(a() >> 7U);
                    set_a(static_cast<std::uint8_t>((a() << 1U) | bit7));
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) |
                                                    (a() & (flag_x | flag_y)) | bit7));
                    step_cycles_ += 4;
                    break;
                }
                case 1: { // RRCA
                    const auto bit0 = static_cast<std::uint8_t>(a() & 1U);
                    set_a(static_cast<std::uint8_t>((a() >> 1U) | (bit0 << 7U)));
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) |
                                                    (a() & (flag_x | flag_y)) | bit0));
                    step_cycles_ += 4;
                    break;
                }
                case 2: { // RLA
                    const auto bit7 = static_cast<std::uint8_t>(a() >> 7U);
                    set_a(static_cast<std::uint8_t>((a() << 1U) | (f() & flag_c)));
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) |
                                                    (a() & (flag_x | flag_y)) | bit7));
                    step_cycles_ += 4;
                    break;
                }
                case 3: { // RRA
                    const auto bit0 = static_cast<std::uint8_t>(a() & 1U);
                    set_a(static_cast<std::uint8_t>((a() >> 1U) | ((f() & flag_c) << 7U)));
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) |
                                                    (a() & (flag_x | flag_y)) | bit0));
                    step_cycles_ += 4;
                    break;
                }
                case 4: { // DAA
                    const std::uint8_t old_a = a();
                    std::uint8_t corr = 0;
                    std::uint8_t carry = f() & flag_c;
                    if ((f() & flag_h) != 0U || (a() & 0x0FU) > 9U) {
                        corr |= 0x06U;
                    }
                    if (carry != 0U || a() > 0x99U) {
                        corr |= 0x60U;
                        carry = flag_c;
                    }
                    if ((f() & flag_n) != 0U) {
                        set_a(static_cast<std::uint8_t>(a() - corr));
                    } else {
                        set_a(static_cast<std::uint8_t>(a() + corr));
                    }
                    set_f(static_cast<std::uint8_t>(tables().sz53p[a()] | carry | (f() & flag_n) |
                                                    ((old_a ^ a()) & flag_h)));
                    step_cycles_ += 4;
                    break;
                }
                case 5: // CPL
                    set_a(static_cast<std::uint8_t>(~a()));
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p | flag_c)) |
                                                    flag_h | flag_n | (a() & (flag_x | flag_y))));
                    step_cycles_ += 4;
                    break;
                case 6: // SCF
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) | flag_c |
                                                    (a() & (flag_x | flag_y))));
                    step_cycles_ += 4;
                    break;
                default: { // CCF
                    // H = previous carry, C = NOT previous carry, N = 0,
                    // S/Z/P preserved, X/Y from A. Compute the new carry from the
                    // old one directly -- XOR-ing the whole F register would drag
                    // the old N/H bits back in via the OR (a documented-flag bug).
                    const auto old_c = static_cast<std::uint8_t>(f() & flag_c);
                    const auto hf = static_cast<std::uint8_t>(old_c != 0U ? flag_h : 0U);
                    const auto cf = static_cast<std::uint8_t>(old_c != 0U ? 0U : flag_c);
                    set_f(static_cast<std::uint8_t>((f() & (flag_s | flag_z | flag_p)) | cf | hf |
                                                    (a() & (flag_x | flag_y))));
                    step_cycles_ += 4;
                    break;
                }
                }
                break;
            }
            break;

        case 1:
            if (y == 6 && z == 6) {
                halted_ = true;
                step_cycles_ += 4;
            } else {
                set_reg8(y, get_reg8(z));
                step_cycles_ += (y == 6 || z == 6) ? 7 : 4;
            }
            break;

        case 2: // ALU A,r
            do_alu(y, get_reg8(z));
            step_cycles_ += (z == 6) ? 7 : 4;
            break;

        default: // x == 3
            switch (z) {
            case 0: // RET cc
                if (test_cc(y)) {
                    pc_ = pop16();
                    step_cycles_ += 11;
                } else {
                    step_cycles_ += 5;
                }
                break;
            case 1:
                if (q == 0) {
                    *get_rp2(p) = pop16();
                    step_cycles_ += 10;
                } else {
                    switch (p) {
                    case 0:
                        pc_ = pop16();
                        step_cycles_ += 10;
                        break; // RET
                    case 1: {  // EXX
                        std::uint16_t t = bc_;
                        bc_ = bc2_;
                        bc2_ = t;
                        t = de_;
                        de_ = de2_;
                        de2_ = t;
                        t = hl_;
                        hl_ = hl2_;
                        hl2_ = t;
                        step_cycles_ += 4;
                        break;
                    }
                    case 2:
                        pc_ = hl_;
                        step_cycles_ += 4;
                        break; // JP (HL)
                    default:
                        sp_ = hl_;
                        step_cycles_ += 6;
                        break; // LD SP,HL
                    }
                }
                break;
            case 2: { // JP cc,nn
                const std::uint16_t addr = fetch16();
                if (test_cc(y)) {
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
                    break; // JP nn
                case 1:
                    exec_cb();
                    break;
                case 2: // OUT (n),A
                    port_out8(static_cast<std::uint16_t>((a() << 8U) | imm_fetch8()), a());
                    step_cycles_ += 11;
                    break;
                case 3: // IN A,(n)
                    set_a(port_in8(static_cast<std::uint16_t>((a() << 8U) | imm_fetch8())));
                    step_cycles_ += 11;
                    break;
                case 4:
                    hl_ = ex_sp16(hl_);
                    step_cycles_ += 19;
                    break; // EX (SP),HL
                case 5: {  // EX DE,HL
                    const std::uint16_t t = de_;
                    de_ = hl_;
                    hl_ = t;
                    step_cycles_ += 4;
                    break;
                }
                case 6:
                    iff1_ = iff2_ = false;
                    step_cycles_ += 4;
                    break; // DI
                default:
                    ei_pending_ = true;
                    step_cycles_ += 4;
                    break; // EI
                }
                break;
            case 4: { // CALL cc,nn
                const std::uint16_t addr = fetch16();
                if (test_cc(y)) {
                    push16(pc_);
                    pc_ = addr;
                    step_cycles_ += 17;
                } else {
                    step_cycles_ += 10;
                }
                break;
            }
            case 5:
                if (q == 0) {
                    push16(*get_rp2(p));
                    step_cycles_ += 11;
                } else {
                    switch (p) {
                    case 0: { // CALL nn
                        const std::uint16_t addr = fetch16();
                        push16(pc_);
                        pc_ = addr;
                        step_cycles_ += 17;
                        break;
                    }
                    case 1:
                        exec_dd_fd(false);
                        break; // DD (IX)
                    case 2:
                        exec_ed();
                        break;
                    default:
                        exec_dd_fd(true);
                        break; // FD (IY)
                    }
                }
                break;
            case 6: // ALU A,n
                do_alu(y, imm_fetch8());
                step_cycles_ += 7;
                break;
            default: // RST n
                push16(pc_);
                pc_ = static_cast<std::uint16_t>(y * 8);
                step_cycles_ += 11;
                break;
            }
            break;
        }
    }

    // ---- step / tick ----

    int z80::step_instruction() {
        step_cycles_ = 0;

        // /RESET held: the CPU performs no work; cycles still elapse so the
        // system schedule keeps its pacing.
        if (reset_line_) {
            elapsed_ += 4U;
            return 4;
        }

        if (nmi_pending_) {
            nmi_pending_ = false;
            halted_ = false;
            iff2_ = iff1_;
            iff1_ = false;
            inc_r(); // the IACK/opcode-fetch cycle refreshes R (visible via LD A,R)
            push16(pc_);
            pc_ = 0x0066U;
            step_cycles_ += 11;
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        if (irq_line_ && iff1_ && !ei_pending_) {
            halted_ = false;
            iff1_ = iff2_ = false;
            inc_r(); // the IACK cycle refreshes R
            switch (im_) {
            case 2: {
                // The device supplies the vector byte during IACK; with no
                // vector source the floating bus reads 0xFF (the MSX/Spectrum
                // convention IM2 software relies on: I*256 + 0xFF).
                push16(pc_);
                const std::uint8_t vec = irq_vector_ ? irq_vector_() : 0xFFU;
                const auto vec_addr = static_cast<std::uint16_t>((i_ << 8U) | vec);
                pc_ = rw(vec_addr);
                step_cycles_ += 19;
                break;
            }
            case 0: {
                // The device jams an opcode on the bus during IACK; only
                // single-byte instructions are supported (the RST family in
                // practice -- interrupt controllers drive RST n per source).
                // The floating-bus default 0xFF is RST 38h, matching IM 1.
                // The jammed instruction does its own stacking; the IACK adds
                // two wait states over a normal fetch.
                const std::uint8_t op = irq_vector_ ? irq_vector_() : 0xFFU;
                exec_main(op);
                step_cycles_ += 2;
                break;
            }
            default: // IM 1: fixed RST to $0038
                push16(pc_);
                pc_ = 0x0038U;
                step_cycles_ += 13;
                break;
            }
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        if (ei_pending_) {
            ei_pending_ = false;
            iff1_ = iff2_ = true;
        }

        if (halted_) {
            inc_r();
            elapsed_ += 4U;
            return 4;
        }

        if (trace_callback_) {
            trace_callback_(pc_);
        }
        const std::uint8_t op = op_fetch8();
        inc_r();
        exec_main(op);
        if (step_cycles_ < 4) {
            step_cycles_ = 4;
        }
        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void z80::tick(std::uint64_t cycles) { run_catch_up(cycles); }

    void z80::set_nmi_line(bool asserted) noexcept {
        // Edge-triggered: latch one NMI per inactive->active transition, like
        // the hardware /NMI pin (and the m6510's identically named API). A
        // system holding the line and re-asserting each frame must not get one
        // NMI per call.
        if (asserted && !nmi_line_) {
            nmi_pending_ = true;
            halted_ = false;
        }
        nmi_line_ = asserted;
    }

    void z80::set_reset_line(bool asserted) noexcept {
        // The /RESET pin. Asserting resets the architectural state and parks
        // the CPU; releasing starts execution from $0000. Boards whose CPU
        // program lives in host-uploaded RAM (e.g. a main CPU loading a sound
        // program) hold this until the upload completes. The cycle pacing
        // counters survive so the system schedule stays anchored.
        if (asserted && !reset_line_) {
            const std::int64_t debt = cycle_debt_;
            const std::uint64_t elapsed = elapsed_;
            reset(reset_kind::soft);
            cycle_debt_ = debt;
            elapsed_ = elapsed;
        }
        reset_line_ = asserted;
    }

    void z80::reset(reset_kind /*kind*/) {
        af_ = 0xFFFFU;
        bc_ = de_ = hl_ = 0U;
        af2_ = bc2_ = de2_ = hl2_ = 0U;
        ix_ = iy_ = 0U;
        sp_ = 0xFFFFU;
        pc_ = 0U;
        i_ = r_ = im_ = 0U;
        iff1_ = iff2_ = false;
        halted_ = ei_pending_ = irq_line_ = nmi_pending_ = nmi_line_ = false;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    z80::registers z80::cpu_registers() const noexcept {
        return {.af = af_,
                .bc = bc_,
                .de = de_,
                .hl = hl_,
                .af2 = af2_,
                .bc2 = bc2_,
                .de2 = de2_,
                .hl2 = hl2_,
                .ix = ix_,
                .iy = iy_,
                .sp = sp_,
                .pc = pc_,
                .i = i_,
                .r = r_,
                .im = im_,
                .iff1 = iff1_,
                .iff2 = iff2_,
                .halted = halted_};
    }

    void z80::set_registers(const registers& v) noexcept {
        af_ = v.af;
        bc_ = v.bc;
        de_ = v.de;
        hl_ = v.hl;
        af2_ = v.af2;
        bc2_ = v.bc2;
        de2_ = v.de2;
        hl2_ = v.hl2;
        ix_ = v.ix;
        iy_ = v.iy;
        sp_ = v.sp;
        pc_ = v.pc;
        i_ = v.i;
        r_ = v.r;
        im_ = v.im;
        iff1_ = v.iff1;
        iff2_ = v.iff2;
        halted_ = v.halted;
    }

    void z80::save_state(state_writer& writer) const {
        writer.u16(af_);
        writer.u16(bc_);
        writer.u16(de_);
        writer.u16(hl_);
        writer.u16(af2_);
        writer.u16(bc2_);
        writer.u16(de2_);
        writer.u16(hl2_);
        writer.u16(ix_);
        writer.u16(iy_);
        writer.u16(sp_);
        writer.u16(pc_);
        writer.u8(i_);
        writer.u8(r_);
        writer.u8(im_);
        writer.boolean(iff1_);
        writer.boolean(iff2_);
        writer.boolean(halted_);
        writer.boolean(ei_pending_);
        writer.boolean(irq_line_);
        writer.boolean(nmi_pending_);
        writer.boolean(nmi_line_);
        writer.boolean(reset_line_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void z80::load_state(state_reader& reader) {
        af_ = reader.u16();
        bc_ = reader.u16();
        de_ = reader.u16();
        hl_ = reader.u16();
        af2_ = reader.u16();
        bc2_ = reader.u16();
        de2_ = reader.u16();
        hl2_ = reader.u16();
        ix_ = reader.u16();
        iy_ = reader.u16();
        sp_ = reader.u16();
        pc_ = reader.u16();
        i_ = reader.u8();
        r_ = reader.u8();
        im_ = reader.u8();
        iff1_ = reader.boolean();
        iff2_ = reader.boolean();
        halted_ = reader.boolean();
        ei_pending_ = reader.boolean();
        irq_line_ = reader.boolean();
        nmi_pending_ = reader.boolean();
        nmi_line_ = reader.boolean();
        reset_line_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& z80::introspection() noexcept { return introspection_; }

    void z80::configure(const config_table& cfg, const callback_table& callbacks) {
        // Z80 IN/OUT port handlers. The Z80's 64K I/O space is separate from
        // its memory bus; each system dispatches IN/OUT to its own peripherals
        // (e.g. on SMS: PSG, VDP, joypads, I/O control). Manifests name these
        // callbacks; the host registers the actual handlers. Falls back to
        // the chip's defaults (unset = open bus / drop) when missing.
        if (const auto id = chips::cfg_string(cfg, "port_in_callback")) {
            if (const auto* fn =
                    chips::find_callback<std::uint8_t(std::uint16_t)>(callbacks, *id)) {
                set_port_in(*fn);
            }
        }
        if (const auto id = chips::cfg_string(cfg, "port_out_callback")) {
            if (const auto* fn =
                    chips::find_callback<void(std::uint16_t, std::uint8_t)>(callbacks, *id)) {
                set_port_out(*fn);
            }
        }
    }

    std::span<const register_descriptor> z80::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"AF", af_, 16U, fmt::unsigned_integer};
        register_view_[1] = {"BC", bc_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"DE", de_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"HL", hl_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"AF'", af2_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"BC'", bc2_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"DE'", de2_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"HL'", hl2_, 16U, fmt::unsigned_integer};
        register_view_[8] = {"IX", ix_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"IY", iy_, 16U, fmt::unsigned_integer};
        register_view_[10] = {"SP", sp_, 16U, fmt::unsigned_integer};
        register_view_[11] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[12] = {"I", i_, 8U, fmt::unsigned_integer};
        register_view_[13] = {"R", r_, 8U, fmt::unsigned_integer};
        register_view_[14] = {"IM", im_, 8U, fmt::unsigned_integer};
        register_view_[15] = {"F", f(), 8U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto z80_registration =
            register_factory("zilog.z80", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<z80>(); });
    } // namespace

} // namespace mnemos::chips::cpu
