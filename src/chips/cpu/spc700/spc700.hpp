#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Sony SPC700 8-bit sound CPU.
    //
    // The CPU core of the S-SMP audio package: a 6502-flavoured register file
    // (A/X/Y/SP/PSW/PC, plus the virtual 16-bit YA = Y:A) over a distinct
    // instruction set. Direct-page addressing is page-selectable via the PSW.P
    // bit (page $00 vs $01); the stack lives in page $01 ($0100..$01FF) with an
    // 8-bit SP.
    //
    // This is a correct, compiling SUBSET (~69 opcodes): the register/PSW
    // plumbing, MOV in immediate/direct-page/absolute/(X)/dp+X forms, ADC/SBC/
    // CMP/AND/OR/EOR, INC/DEC, the shift/rotate-on-A group, the conditional
    // branch family, CALL/RET, PUSH/POP, the virtual-YA word ops (MOVW/INCW/
    // DECW), and MUL/DIV. Opcodes outside the set halt the core (a defined,
    // observable stop) rather than misexecuting. The remaining opcode space
    // (the full ALU addressing matrix, XCN, the BBC/BBS bit-branch family,
    // decimal DAA/DAS) is stubbed -- it lands as the SNES audio work requires.
    //
    // Instruction-stepped like the rest of the CPU fleet: step_instruction()
    // executes exactly one instruction and returns its cycle cost;
    // tick(cycles) catches up by running whole instructions until the budget is
    // spent. Memory is the attached ibus (the 64K S-SMP space: work RAM, the
    // S-DSP register port, the IPL ROM at the top of the map).
    class spc700 final : public icpu, public cpu_catch_up<spc700> {
      public:
        // PSW (status) flag bits.
        static constexpr std::uint8_t flag_c = 0x01U; // carry
        static constexpr std::uint8_t flag_z = 0x02U; // zero
        static constexpr std::uint8_t flag_i = 0x04U; // IRQ disable
        static constexpr std::uint8_t flag_h = 0x08U; // half-carry (BCD)
        static constexpr std::uint8_t flag_b = 0x10U; // break
        static constexpr std::uint8_t flag_p = 0x20U; // direct-page select ($00 / $01)
        static constexpr std::uint8_t flag_v = 0x40U; // overflow
        static constexpr std::uint8_t flag_n = 0x80U; // negative

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint8_t a{};
            std::uint8_t x{};
            std::uint8_t y{};
            std::uint8_t sp{};
            std::uint8_t psw{};
            std::uint16_t pc{};
            bool halted{};
        };

        spc700() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_trace(instrumentation::pc_trace_installer(
                    trace_callback_, [this] { return elapsed_cycles(); }));
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // icpu: the memory address space the CPU reads/writes.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // Instruction-stepped: the CPU is always between instructions on return.
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // The /RESET pin: assert to reset-and-park, release to run from the
        // reset vector (boards holding the audio CPU while its program RAM is
        // uploaded by the main CPU).
        void set_reset_line(bool asserted) noexcept;
        [[nodiscard]] bool reset_line_held() const noexcept { return reset_line_; }

        // The virtual 16-bit YA register (Y high, A low).
        [[nodiscard]] std::uint16_t ya() const noexcept {
            return static_cast<std::uint16_t>((y_ << 8U) | a_);
        }
        void set_ya(std::uint16_t v) noexcept {
            y_ = static_cast<std::uint8_t>(v >> 8U);
            a_ = static_cast<std::uint8_t>(v);
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // ---- memory access ----
        [[nodiscard]] std::uint8_t rb(std::uint16_t addr) noexcept;
        void wb(std::uint16_t addr, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t fetch8() noexcept;
        [[nodiscard]] std::uint16_t fetch16() noexcept;

        // The direct-page base: page $00 or $01 selected by PSW.P.
        [[nodiscard]] std::uint16_t dp_addr(std::uint8_t dp) const noexcept {
            const std::uint16_t page = (psw_ & flag_p) != 0U ? 0x0100U : 0x0000U;
            return static_cast<std::uint16_t>(page | dp);
        }

        void push8(std::uint8_t v) noexcept;
        [[nodiscard]] std::uint8_t pop8() noexcept;

        // ---- flag / ALU helpers ----
        void set_nz(std::uint8_t v) noexcept;
        void do_adc(std::uint8_t operand) noexcept;
        void do_sbc(std::uint8_t operand) noexcept;
        void do_cmp(std::uint8_t lhs, std::uint8_t operand) noexcept;
        void branch_if(bool taken) noexcept;

        // ---- decode ----
        void exec(std::uint8_t op);

        // Architectural state.
        std::uint8_t a_{};
        std::uint8_t x_{};
        std::uint8_t y_{};
        std::uint8_t sp_{};
        std::uint8_t psw_{};
        std::uint16_t pc_{};
        bool halted_{};
        bool reset_line_{}; // /RESET held: parked, no execution

        int step_cycles_{}; // cycles of the instruction in flight
        // tick()'s catch-up loop and cycle_debt_ live in cpu_catch_up.
        friend class cpu_catch_up<spc700>;
        std::uint64_t elapsed_{}; // total cycles executed

        ibus* bus_{};

        // Per-instruction trace hook installed via the introspection surface.
        std::function<void(std::uint32_t pc)> trace_callback_{};

        std::array<register_descriptor, 7> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
