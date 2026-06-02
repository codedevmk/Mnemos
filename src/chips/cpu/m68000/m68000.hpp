#pragma once

#include "chip.hpp"
#include "m68000_diagnostics.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Motorola 68000 (MC68000) CPU.
    //
    // 32-bit programming model on a
    // 16-bit, big-endian, 24-bit-address data bus: eight data + eight address
    // registers, a supervisor/user split (USP/SSP), and a status register with the
    // 68000 CCR layout (C/V/Z/N/X). The foundation of the Genesis/Mega Drive (M8).
    //
    // Built in phases. Implemented so far: all 14 addressing modes; MOVE/MOVEA/MOVEQ;
    // the integer arithmetic set (ADD/SUB/CMP families, ADDQ/SUBQ, ADDI/SUBI/CMPI,
    // MULU/MULS, NEG/NEGX/CLR/EXT/TST); the logicals (AND/OR/EOR/NOT, the immediate
    // and CCR forms) and bit ops; the shift/rotate family; and control flow plus the
    // exception core (Bcc/DBcc/Scc/BSR/BRA/JMP/JSR/RTS/RTR/RTE/TRAP/TRAPV/LINK/UNLK/
    // STOP/RESET, privilege checks, the 7-level autovectored interrupt dispatch, and
    // trace). Cycle counts are 4 clocks per bus access plus the documented internal
    // idles. Still to come: the trapping arithmetic (DIVU/DIVS/CHK), MOVE-to/from-SR,
    // BCD + the remaining misc ops (MOVEM/SWAP/PEA/...), the cycle-accurate two-word
    // prefetch pipeline, and the address/bus-error group-0 frames. Opcodes not yet
    // decoded execute as 4-cycle no-ops.
    //
    // Instruction-stepped like the Z80: step_instruction() runs one instruction and
    // returns its cycle cost; tick(cycles) catches up by running whole instructions.
    // Memory is the attached ibus (byte-addressed; 16/32-bit accesses are assembled
    // big-endian).
    class m68000 final : public icpu {
      public:
        // Status-register bits (68000 CCR layout: note N is bit 3, not bit 7).
        static constexpr std::uint16_t sr_c = 1U << 0U;   // carry
        static constexpr std::uint16_t sr_v = 1U << 1U;   // overflow
        static constexpr std::uint16_t sr_z = 1U << 2U;   // zero
        static constexpr std::uint16_t sr_n = 1U << 3U;   // negative
        static constexpr std::uint16_t sr_x = 1U << 4U;   // extend
        static constexpr std::uint16_t sr_ipm = 7U << 8U; // interrupt priority mask
        static constexpr std::uint16_t sr_s = 1U << 13U;  // supervisor
        static constexpr std::uint16_t sr_t = 1U << 15U;  // trace
        static constexpr std::uint16_t sr_ccr = 0x001FU;

        static constexpr std::uint32_t address_mask = 0x00FFFFFFU; // 24-bit bus

        // A snapshot / load image of the architectural register file. a[7] is the
        // active stack pointer; usp/ssp hold the inactive bank for the other mode.
        struct registers final {
            std::array<std::uint32_t, 8> d{};
            std::array<std::uint32_t, 8> a{};
            std::uint32_t pc{};
            std::uint16_t sr{};
            std::uint32_t usp{};
            std::uint32_t ssp{};
        };

        m68000() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;
        void configure(const config_table& cfg, const callback_table& callbacks) override;

        // icpu: the memory address space the CPU executes against.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // PC of the instruction currently executing (captured at its fetch).
        // Unlike cpu_registers().pc -- which has already advanced past the
        // instruction -- this is the address of the instruction that issued the
        // in-flight bus access, so a write-watch reports the true writer PC.
        [[nodiscard]] std::uint32_t current_instruction_addr() const noexcept { return inst_addr_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Interrupt request level (0-7, IPL pins). Stored now; the dispatch arrives
        // with the exception-framework phase.
        void set_irq_level(int level) noexcept;
        [[nodiscard]] int irq_level() const noexcept { return irq_level_; }        // diag
        [[nodiscard]] bool irq_resample() const noexcept { return irq_resample_; } // diag

        // Interrupt-acknowledge hook: invoked with the level when the CPU accepts an
        // autovectored interrupt (the IACK bus cycle). A device whose interrupt is not
        // cleared by a register read (e.g. the Genesis VDP V-blank IRQ) clears its
        // request here. Unset by default.
        void set_irq_ack_callback(std::function<void(int level)> callback) noexcept {
            irq_ack_ = std::move(callback);
        }

        // TAS write-back suppression hook. When set, TAS <ea>.B with a MEMORY operand
        // calls this with the effective address INSTEAD of writing bit 7 back. (Real
        // 68000 does the write; the Sega Genesis's bus controller ignores the TAS
        // write phase, so the manifest installs a no-op here to match that quirk and
        // unblock games that rely on it.) Unset = default 68000 write behaviour.
        void set_tas_callback(std::function<void(std::uint32_t addr)> callback) noexcept {
            tas_callback_ = std::move(callback);
        }

        // Diagnostic facade: trace callback + cycle-source decomposition for
        // the last completed instruction. Pure observation -- toggling these
        // never changes the CPU's architectural behaviour. Off-by-default,
        // zero overhead when unused.
        [[nodiscard]] m68000_diagnostics& diagnostics() noexcept { return diagnostics_; }

        // Genesis Z80-bus access latency. When enabled, every cycle-
        // accounted bus access into $A00000-$A0FFFF costs an extra 1 CPU
        // cycle (7 master cycles). The Genesis manifest enables this;
        // other m68000 systems leave it off.
        void set_z80_bus_latency_enabled(bool enabled) noexcept {
            z80_bus_latency_enabled_ = enabled;
        }

        // Schedule an IRQ to fire ONE INSTRUCTION later than the normal
        // boundary. The canonical Genesis V-int-enable-via-MOVE.W path:
        // when reg[1]'s V-int bit is flipped on while a VINT is latched in
        // the VDP, the CPU finishes the MOVE, executes ONE MORE
        // instruction, and ONLY THEN raises the IRQ -- so the saved PC on
        // the IRQ stack is the PC after that extra instruction.
        //
        // Implementation: VDP calls this from its register-1 write path.
        // The CPU then runs the in-flight + one more step_instruction
        // without taking the IRQ, then sets irq_level_ at the end so the
        // step AFTER that fires the exception. See genesis_vdp.cpp where
        // schedule_delayed_irq is wired up.
        void schedule_delayed_irq(int level) noexcept {
            delayed_irq_level_ = level < 0 ? 0 : (level > 7 ? 7 : level);
            delayed_irq_counter_ = 2; // current step + 1 more step
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        friend class m68000_diagnostics;

        // Bridges the chip's diagnostic surface into the generic
        // `instrumentation::ichip_introspection` capability container. The
        // CPU advertises a trace target (per-instruction PC+cycles hook,
        // forwarded to the existing `m68000_diagnostics::set_trace_callback`
        // path) and a register view (snapshots from `register_snapshot()`).
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(m68000& owner) noexcept;

            [[nodiscard]] instrumentation::trace_target* trace() override { return &trace_impl_; }
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }

          private:
            class trace_impl final : public instrumentation::trace_target {
              public:
                explicit trace_impl(m68000& owner) noexcept : owner_(&owner) {}
                void install(callback cb) override;

              private:
                m68000* owner_;
            };

            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(m68000& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                m68000* owner_;
            };

            trace_impl trace_impl_;
            registers_impl registers_impl_;
        };

        enum class op_size : std::uint8_t { byte, word, longword };

        [[nodiscard]] static std::uint32_t size_mask(op_size s) noexcept;
        [[nodiscard]] static std::uint32_t size_sign_bit(op_size s) noexcept;
        [[nodiscard]] static int size_bytes(op_size s) noexcept;
        [[nodiscard]] static std::int32_t sign_extend(std::uint32_t v, op_size s) noexcept;

        // ---- raw memory (no cycle accounting), 24-bit masked, big-endian ----
        [[nodiscard]] std::uint8_t rd8(std::uint32_t a) const noexcept;
        void wr8(std::uint32_t a, std::uint8_t v) noexcept;
        [[nodiscard]] std::uint16_t rd16(std::uint32_t a) const noexcept;
        void wr16(std::uint32_t a, std::uint16_t v) noexcept;

        // ---- cycle-accounted accesses (4 clocks per word/byte bus cycle) ----
        [[nodiscard]] std::uint8_t read8(std::uint32_t a) noexcept;
        [[nodiscard]] std::uint16_t read16(std::uint32_t a) noexcept;
        [[nodiscard]] std::uint32_t read32(std::uint32_t a) noexcept;
        void write8(std::uint32_t a, std::uint8_t v) noexcept;
        void write16(std::uint32_t a, std::uint16_t v) noexcept;
        void write32(std::uint32_t a, std::uint32_t v) noexcept;
        [[nodiscard]] std::uint32_t read_sized(std::uint32_t a, op_size s) noexcept;
        void write_sized(std::uint32_t a, op_size s, std::uint32_t v) noexcept;

        // ---- instruction stream ----
        [[nodiscard]] std::uint16_t fetch16() noexcept;
        [[nodiscard]] std::uint32_t fetch32() noexcept;

        // ---- effective-address resolution ----
        [[nodiscard]] std::uint32_t decode_extension(std::uint32_t base) noexcept;
        [[nodiscard]] int ea_increment(int reg, op_size s) const noexcept;
        [[nodiscard]] std::uint32_t ea_address(int mode, int reg, op_size s, bool adjust) noexcept;
        [[nodiscard]] std::uint32_t ea_read(int mode, int reg, op_size s) noexcept;
        void ea_write(int mode, int reg, op_size s, std::uint32_t value) noexcept;

        // ---- read-modify-write EA (resolve the address once) ----
        [[nodiscard]] std::uint32_t ea_rmw_read(int mode, int reg, op_size s,
                                                std::uint32_t& addr) noexcept;
        void ea_rmw_write(int mode, int reg, op_size s, std::uint32_t value,
                          std::uint32_t addr) noexcept;

        // ---- flags ----
        void set_logic_flags(op_size s, std::uint32_t value) noexcept;
        void flags_add(op_size s, std::uint32_t src, std::uint32_t dst, std::uint32_t r) noexcept;
        void flags_sub(op_size s, std::uint32_t src, std::uint32_t dst, std::uint32_t r) noexcept;
        void flags_cmp(op_size s, std::uint32_t src, std::uint32_t dst, std::uint32_t r) noexcept;
        void flags_addx(op_size s, std::uint32_t src, std::uint32_t dst, std::uint32_t x) noexcept;
        void flags_subx(op_size s, std::uint32_t src, std::uint32_t dst, std::uint32_t x) noexcept;
        [[nodiscard]] static int popcount16(std::uint16_t v) noexcept;
        // Packed-BCD add/sub with X propagation (shared by ABCD/SBCD/NBCD); sets the
        // C/X/N/V flags and only clears Z (multi-precision), returning the BCD byte.
        [[nodiscard]] std::uint8_t bcd_add(std::uint8_t dst, std::uint8_t src) noexcept;
        [[nodiscard]] std::uint8_t bcd_sub(std::uint8_t dst, std::uint8_t src) noexcept;

        // ---- decode + instruction handlers ----
        void exec(std::uint16_t op);
        void op_move(std::uint16_t op);
        void op_moveq(std::uint16_t op) noexcept;
        void op_add(std::uint16_t op) noexcept;               // group D: ADD/ADDA/ADDX
        void op_sub(std::uint16_t op) noexcept;               // group 9: SUB/SUBA/SUBX
        void op_cmp(std::uint16_t op) noexcept;               // group B: CMP/CMPA/CMPM
        void op_mul(std::uint16_t op) noexcept;               // group C: MULU/MULS
        void op_quick(std::uint16_t op) noexcept;             // group 5: ADDQ/SUBQ
        void op_immediate(std::uint16_t op) noexcept;         // group 0: immediates + bit ops
        void op_bit(std::uint16_t op, bool dynamic) noexcept; // BTST/BCHG/BCLR/BSET
        void op_movep(std::uint16_t op) noexcept;             // MOVEP Dn<->(d16,An)
        void op_or(std::uint16_t op) noexcept;                // group 8: OR (DIV/SBCD later)
        void op_group4(std::uint16_t op) noexcept;   // group 4: NOP/EXT/NEGX/CLR/NEG/NOT/TST + ctrl
        void op_shift(std::uint16_t op) noexcept;    // group E: ASL/ASR/LSL/LSR/RO[X]L/RO[X]R
        void op_branch(std::uint16_t op) noexcept;   // group 6: Bcc/BRA/BSR
        void op_dbcc_scc(std::uint16_t op) noexcept; // group 5 (size 3): DBcc/Scc

        // ---- supervisor state, stack, exceptions ----
        void set_supervisor(bool supervisor) noexcept; // swap USP/SSP on a mode change
        void write_sr(std::uint16_t value) noexcept;
        void push16(std::uint16_t value) noexcept;
        void push32(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint16_t pop16() noexcept;
        [[nodiscard]] std::uint32_t pop32() noexcept;
        void raise_exception(int vector, std::uint32_t exc_pc) noexcept;
        void process_interrupt() noexcept;
        [[nodiscard]] bool test_cc(int cc) const noexcept;

        std::array<std::uint32_t, 8> d_{};
        std::array<std::uint32_t, 8> a_{};
        std::uint32_t pc_{};
        std::uint16_t sr_{};
        std::uint32_t usp_{};
        std::uint32_t ssp_{};
        std::uint32_t inst_addr_{}; // address of the instruction in flight (for exception frames)
        int irq_level_{};
        int prev_irq_level_{}; // for the level-7 (NMI) edge
        // Event-driven interrupt sampling. A real 68000 (and the reference) only
        // re-evaluates a pending IRQ at interrupt-significant events -- the level
        // changing, or the SR mask being written -- NOT at every instruction
        // boundary. Without this, a held-pending IRQ is grabbed the instant the
        // mask happens to allow it mid-routine, even when the program is about to
        // mask interrupts; that accepts the IRQ instructions too early vs hardware.
        // Set at reset / set_irq_level / write_sr; consumed once per step.
        bool irq_resample_{true};
        std::function<void(int)> irq_ack_{};
        std::function<void(std::uint32_t)> tas_callback_{};
        std::function<void(std::uint32_t)> trace_callback_{};
        bool stopped_{};
        bool halted_{};

        int cycles_{};              // cycles of the instruction in flight
        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()
        std::uint64_t elapsed_{};   // total cycles executed

        // Genesis / Mega Drive 68K bus DRAM refresh tracking. Every 128 68K
        // cycles (= 896 master cycles) the bus takes 2 extra 68K cycles
        // (= 14 master cycles) for DRAM refresh. Checked at the start of
        // each instruction.
        std::uint64_t bus_refresh_due_{128U};

        // Genesis $A00000-$A0FFFF access latency (see set_z80_bus_latency_enabled).
        bool z80_bus_latency_enabled_{false};

        // Delayed-IRQ state (see schedule_delayed_irq above).
        // delayed_irq_counter_ counts step_instruction() invocations until
        // the IRQ should be raised: while > 0 the CPU runs normally without
        // taking the delayed IRQ; on reaching 0, irq_level_ is set to
        // delayed_irq_level_.
        int delayed_irq_level_{};
        int delayed_irq_counter_{};

        // Cycle-source accumulator for the instruction in flight; snapshotted
        // to last_cycle_sources_ at the end of each step_instruction(). The
        // type lives on m68000_diagnostics so the diagnostic facade can
        // expose it without re-declaring.
        m68000_diagnostics::cycle_sources cycle_sources_{};
        m68000_diagnostics::cycle_sources last_cycle_sources_{};

        ibus* bus_{};

        std::array<register_descriptor, 20> register_view_{};
        introspection_surface introspection_{*this};
        m68000_diagnostics diagnostics_{*this};
    };

} // namespace mnemos::chips::cpu
