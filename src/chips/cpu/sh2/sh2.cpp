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

    void sh2::exec(std::uint16_t op) {
        // Starter decode (phase A1). Everything not handled here is a 1-cycle
        // no-op, the m68000's bring-up convention; the full instruction set
        // arrives in phase A2/A3.
        const auto nn = static_cast<std::size_t>((op >> 8U) & 0xFU);
        const auto mm = static_cast<std::size_t>((op >> 4U) & 0xFU);
        const auto imm8 = static_cast<std::uint8_t>(op & 0xFFU);
        // Sign-extend an 8-bit immediate to 32 bits.
        const auto sx8 =
            static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(imm8)));

        switch (op >> 12U) {
        case 0x6:
            // 0110nnnnmmmm0011: MOV Rm,Rn (register-to-register move).
            if ((op & 0xFU) == 0x3U) {
                r_[nn] = r_[mm];
            }
            break;
        case 0x7:
            // 0111nnnniiiiiiii: ADD #imm,Rn.
            r_[nn] += sx8;
            break;
        case 0xE:
            // 1110nnnniiiiiiii: MOV #imm,Rn (sign-extended).
            r_[nn] = sx8;
            break;
        default:
            // 0x0009 (NOP) and every not-yet-decoded opcode: no architectural
            // effect.
            break;
        }
    }

    int sh2::step_instruction() {
        cycles_ = 1; // SH-2 base: one instruction per cycle on a hit

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
