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

    // ---- flags ----

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

    void m68000::exec(std::uint16_t op) {
        switch (op >> 12U) {
        case 0x1: // MOVE.B
        case 0x2: // MOVE.L
        case 0x3: // MOVE.W
            op_move(op);
            break;
        case 0x7: // MOVEQ (bit 8 must be 0)
            if ((op & 0x0100U) == 0U) {
                op_moveq(op);
            }
            break;
        case 0x4:
            // NOP ($4E71). The rest of group 4 arrives in later phases.
            break;
        default:
            // Not yet decoded -- a 4-cycle no-op until the relevant phase lands.
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

    instrumentation::i_chip_introspection& m68000::introspection() noexcept {
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
            register_factory("motorola.68000", chip_class::cpu, []() -> std::unique_ptr<i_chip> {
                return std::make_unique<m68000>();
            });
    } // namespace

} // namespace mnemos::chips::cpu
