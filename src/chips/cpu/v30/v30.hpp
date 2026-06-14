#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // NEC V30 CPU (uPD70116).
    //
    // 8086-compatible 16-bit CPU with a 20-bit segmented address space, plus
    // the 80186-class instruction additions NEC carried over (PUSHA/POPA,
    // PUSH/IMUL immediate, INS/OUTS, shift-by-immediate, ENTER/LEAVE, BOUND)
    // and the V-series REPC/REPNC prefixes. Registers are surfaced under the
    // conventional Intel names (AX..DX, SI, DI, BP, SP, CS, DS, ES, SS, IP,
    // FLAGS) that ISA references and conformance corpora use; the NEC data
    // sheet calls them AW..DW, IX, IY, BP, SP, PS, DS0, DS1, SS, PC, PSW.
    //
    // The V30 is instruction-stepped: step_instruction() executes exactly one
    // instruction (prefixes included; a REP-prefixed string instruction runs
    // to completion) and returns its cycle cost. tick(cycles) catches up by
    // running whole instructions until the requested cycles are consumed.
    // Memory is the attached ibus, accessed little-endian byte-by-byte; the
    // separate 64K I/O space (IN/OUT) routes through injected port callbacks
    // (unset -> reads 0xFF, writes dropped). INTR vector acquisition routes
    // through an injected acknowledge callback (unset -> vector 0).
    //
    // The V30 0F-prefix extension group implements the bit-manipulation set
    // (TEST1/CLR1/SET1/NOT1), the nibble rotates (ROL4/ROR4), and the
    // packed-BCD strings (ADD4S/SUB4S/CMP4S); the bitfield ops (INS/EXT) and
    // BRKEM 8080-emulation entry are consumed as no-ops. Deferred to later
    // increments (docs/plans/2026-06-10-irem-m72-port.md): those no-op forms,
    // exact V30 cycle timing (costs here are first-order documented
    // 8086-family values), and REP interruptibility.
    class v30 final : public icpu, public cpu_catch_up<v30> {
      public:
        // PSW (FLAGS) bits.
        static constexpr std::uint16_t flag_c = 0x0001U; // carry
        static constexpr std::uint16_t flag_p = 0x0004U; // parity (even)
        static constexpr std::uint16_t flag_a = 0x0010U; // auxiliary carry
        static constexpr std::uint16_t flag_z = 0x0040U; // zero
        static constexpr std::uint16_t flag_s = 0x0080U; // sign
        static constexpr std::uint16_t flag_t = 0x0100U; // trap (BRK)
        static constexpr std::uint16_t flag_i = 0x0200U; // interrupt enable
        static constexpr std::uint16_t flag_d = 0x0400U; // direction
        static constexpr std::uint16_t flag_o = 0x0800U; // overflow

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint16_t ax{};
            std::uint16_t bx{};
            std::uint16_t cx{};
            std::uint16_t dx{};
            std::uint16_t si{};
            std::uint16_t di{};
            std::uint16_t bp{};
            std::uint16_t sp{};
            std::uint16_t ip{};
            std::uint16_t cs{};
            std::uint16_t ds{};
            std::uint16_t es{};
            std::uint16_t ss{};
            std::uint16_t flags{};
        };

        using port_in_fn = std::function<std::uint8_t(std::uint16_t port)>;
        using port_out_fn = std::function<void(std::uint16_t port, std::uint8_t value)>;
        using irq_ack_fn = std::function<std::uint8_t()>;

        v30() {
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

        // The separate 64K I/O space (IN/OUT). Optional; unset ports read 0xFF.
        void set_port_in(port_in_fn handler) noexcept { port_in_ = std::move(handler); }
        void set_port_out(port_out_fn handler) noexcept { port_out_ = std::move(handler); }

        // INTR acknowledge: returns the vector the interrupt controller drives
        // during the acknowledge cycle. Optional; unset -> vector 0.
        void set_irq_ack(irq_ack_fn handler) noexcept { irq_ack_ = std::move(handler); }

        // Execute exactly one instruction (servicing a pending interrupt first);
        // returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // Instruction-stepped: the CPU is always between instructions on return.
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }
        [[nodiscard]] bool halted() const noexcept { return halted_; }

        // Interrupt inputs. INTR is level-sensed (gated by IF); NMI is edge-
        // triggered and latched.
        void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
        void set_nmi_line(bool asserted) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // Bridges the chip's diagnostic surface into the generic
        // `instrumentation::ichip_introspection`: a trace target (per-
        // instruction PC + cycles hook) and a register view.

        // ---- flags ----
        // Stored PSW with the fixed bits normalised the way the V30 reads
        // them back: bits 12-15 read 1 (bit 15 is the native-mode MD flag),
        // bit 1 reads 1, bits 3/5 read 0.
        [[nodiscard]] std::uint16_t flags_word() const noexcept {
            return static_cast<std::uint16_t>((flags_ & 0x0FD5U) | 0xF002U);
        }
        void set_flags_word(std::uint16_t value) noexcept {
            flags_ = static_cast<std::uint16_t>(value & 0x0FD5U);
        }
        [[nodiscard]] bool flag(std::uint16_t mask) const noexcept { return (flags_ & mask) != 0U; }
        void assign_flag(std::uint16_t mask, bool set) noexcept {
            flags_ = set ? static_cast<std::uint16_t>(flags_ | mask)
                         : static_cast<std::uint16_t>(flags_ & ~mask);
        }
        void set_szp8(std::uint8_t result) noexcept;
        void set_szp16(std::uint16_t result) noexcept;

        // ---- memory + I/O access (20-bit physical = (segment << 4) + offset) ----
        [[nodiscard]] static constexpr std::uint32_t phys(std::uint16_t segment,
                                                          std::uint16_t offset) noexcept {
            return ((static_cast<std::uint32_t>(segment) << 4U) + offset) & 0xFFFFFU;
        }
        [[nodiscard]] std::uint8_t rb(std::uint16_t segment, std::uint16_t offset) noexcept;
        void wb(std::uint16_t segment, std::uint16_t offset, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t rw(std::uint16_t segment, std::uint16_t offset) noexcept;
        void ww(std::uint16_t segment, std::uint16_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t fetch8() noexcept;
        [[nodiscard]] std::uint16_t fetch16() noexcept;
        void push16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t pop16() noexcept;
        [[nodiscard]] std::uint8_t port_in8(std::uint16_t port);
        void port_out8(std::uint16_t port, std::uint8_t value);
        [[nodiscard]] std::uint16_t port_in16(std::uint16_t port);
        void port_out16(std::uint16_t port, std::uint16_t value);

        // ---- modrm / effective address ----
        // Decoded per instruction by fetch_modrm(); the EA (when mod != 3) is
        // resolved once, honouring any segment-override prefix.
        void fetch_modrm() noexcept;
        [[nodiscard]] std::uint16_t data_segment(std::uint16_t default_segment) const noexcept;
        [[nodiscard]] std::uint8_t read_rm8() noexcept;
        void write_rm8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t read_rm16() noexcept;
        void write_rm16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t get_reg8(int reg) const noexcept;
        void set_reg8(int reg, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t get_reg16(int reg) const noexcept;
        void set_reg16(int reg, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t get_sreg(int reg) const noexcept;
        void set_sreg(int reg, std::uint16_t value) noexcept;

        // ---- ALU ----
        [[nodiscard]] std::uint8_t alu8(int op, std::uint8_t lhs, std::uint8_t rhs) noexcept;
        [[nodiscard]] std::uint16_t alu16(int op, std::uint16_t lhs, std::uint16_t rhs) noexcept;
        [[nodiscard]] std::uint8_t inc8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t dec8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t inc16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t dec16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t shift_rotate8(int op, std::uint8_t value,
                                                 unsigned count) noexcept;
        [[nodiscard]] std::uint16_t shift_rotate16(int op, std::uint16_t value,
                                                   unsigned count) noexcept;
        [[nodiscard]] bool test_cc(int cc) const noexcept;

        // ---- execution ----
        void interrupt(std::uint8_t vector) noexcept;
        void exec_one(std::uint8_t opcode);
        void exec_group_80_83(std::uint8_t opcode);
        void exec_group_f6_f7(std::uint8_t opcode);
        void exec_group_fe();
        void exec_group_ff();
        void exec_string(std::uint8_t opcode);
        void exec_0f();
        void exec_ins_ext(bool extract, bool immediate_length);
        // Packed-BCD string primitive shared by ADD4S/SUB4S/CMP4S: applies
        // `subtract` digit arithmetic from DS:SI into ES:DI over CL digits,
        // optionally writing the result back, and sets CF (digit borrow/carry
        // out) and ZF (whole result zero).
        void bcd_string_op(bool subtract, bool write_back) noexcept;
        void take_cycles(int cycles) noexcept { step_cycles_ += cycles; }
        // Cost of one resolved memory effective address (first-order).
        [[nodiscard]] int ea_cycles() const noexcept { return rm_is_reg_ ? 0 : 5; }

        // Register file.
        std::uint16_t ax_{};
        std::uint16_t bx_{};
        std::uint16_t cx_{};
        std::uint16_t dx_{};
        std::uint16_t si_{};
        std::uint16_t di_{};
        std::uint16_t bp_{};
        std::uint16_t sp_{};
        std::uint16_t ip_{};
        std::uint16_t cs_{};
        std::uint16_t ds_{};
        std::uint16_t es_{};
        std::uint16_t ss_{};
        std::uint16_t flags_{};

        bool halted_{};
        bool irq_line_{};
        bool nmi_pending_{};
        // One-instruction interrupt shadow after STI / MOV SS / POP SS.
        bool interrupt_inhibit_{};

        // Per-instruction prefix state (set by the prefix loop, consumed by
        // the instruction body, cleared on step exit).
        bool seg_override_{};
        std::uint16_t seg_override_value_{};
        // 0 = none, 1 = REP/REPE, 2 = REPNE, 3 = REPC (V-series), 4 = REPNC.
        int rep_prefix_{};

        // Decoded modrm of the instruction in flight.
        int modrm_reg_{};
        int modrm_rm_{};
        bool rm_is_reg_{};
        std::uint16_t rm_segment_{};
        std::uint16_t rm_offset_{};

        int step_cycles_{}; // cycles of the instruction in flight
        // tick()'s catch-up loop and cycle_debt_ live in cpu_catch_up.
        friend class cpu_catch_up<v30>;
        std::uint64_t elapsed_{}; // total cycles executed

        ibus* bus_{};
        port_in_fn port_in_{};
        port_out_fn port_out_{};
        irq_ack_fn irq_ack_{};

        // Per-instruction trace hook installed via the introspection surface;
        // fired with the physical CS:IP of each instruction start.
        std::function<void(std::uint32_t pc)> trace_callback_{};

        std::array<register_descriptor, 14> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
