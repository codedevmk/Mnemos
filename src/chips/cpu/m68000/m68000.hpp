#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "cpu_fetch_span.hpp"
#include "introspection_adapters.hpp"
#include "m68000_diagnostics.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

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
    // and CCR forms) and bit ops; BCD helpers; the shift/rotate family; misc/control
    // ops such as MOVEM/SWAP/PEA; and control flow plus the exception core
    // (Bcc/DBcc/Scc/BSR/BRA/JMP/JSR/RTS/RTR/RTE/TRAP/TRAPV/LINK/UNLK/STOP/RESET,
    // privilege checks, the 7-level autovectored interrupt dispatch, and trace).
    // Cycle counts are 4 clocks per bus access plus the documented internal idles.
    // Still to come: the cycle-accurate two-word prefetch pipeline, concrete system
    // bus-error map policy, and prefetch-exact group-0 corpus parity. Opcodes not yet
    // decoded execute as 4-cycle no-ops.
    //
    // Instruction-stepped like the Z80: step_instruction() runs one instruction and
    // returns its cycle cost; tick(cycles) catches up by running whole instructions.
    // Memory is the attached ibus (byte-addressed; 16/32-bit accesses are assembled
    // big-endian).
    class m68000 final : public icpu, public cpu_fetch_span<m68000>, public cpu_catch_up<m68000> {
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

        m68000() {
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
        void configure(const config_table& cfg, const callback_table& callbacks) override;

        // icpu: the memory address space the CPU executes against. The fetch
        // fast path caches a direct span; the bus's invalidation listener
        // drops it on any remap / bank retarget / observer install.
        void attach_bus(ibus& bus) noexcept override;

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
        // Cycles accrued so far by the in-flight instruction (incl. the current bus
        // access's own cost). Lets a VDP-port write be positioned at its true
        // sub-instruction beam cycle instead of the instruction-dispatch tick.
        [[nodiscard]] int current_instruction_cycles() const noexcept { return cycles_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Interrupt request level (0-7, IPL pins). Stored now; the dispatch arrives
        // with the exception-framework phase.
        void set_irq_level(int level) noexcept;

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

        // RESET asserts the external reset bus line; it does not reload the
        // 68000's own SSP/PC vectors. Boards use this hook to reset attached
        // devices while the CPU continues with the next instruction.
        void set_reset_callback(std::function<void()> callback) noexcept {
            reset_callback_ = std::move(callback);
        }

        // Board-specific bus wait states for cycle-accounted memory accesses.
        // The callback returns additional 68000 cycles for one byte/word bus
        // transfer at `addr`; longword accesses call it once per halfword.
        // `instruction_cycles_before_access` is the cycle position of this
        // bus transfer within the current instruction before the transfer's
        // base four cycles are charged.
        // `instruction_wait_cycles` is the external wait already charged in
        // this instruction, so boards can model a lockout window as residual
        // wait instead of charging the same bus hold for every transfer.
        // Unset = no added latency. Used by Amiga chip-RAM DMA contention.
        void set_bus_wait_callback(
            std::function<std::uint32_t(std::uint32_t addr, bool program, bool write,
                                        std::uint32_t instruction_cycles_before_access,
                                        std::uint32_t instruction_wait_cycles)>
                callback) noexcept {
            bus_wait_callback_ = std::move(callback);
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

        // Genesis / Mega Drive DRAM refresh steals 2 68K cycles on a sliding
        // ~128-cycle cadence. This is a board bus-controller quirk, not part of
        // the MC68000 core, so non-Genesis systems leave it disabled.
        void set_bus_refresh_enabled(bool enabled) noexcept {
            bus_refresh_enabled_ = enabled;
            bus_refresh_due_ =
                enabled ? (bus_refresh_due_ == 0U ? elapsed_ + genesis_refresh_initial_due
                                                   : bus_refresh_due_)
                        : 0U;
        }

        // Standard MC68000 autovector entry is 42 cycles. The Sega Genesis bus
        // phase stretches VDP IRQ entry with a cycle-dependent table; Genesis
        // opts into that board quirk explicitly.
        void set_genesis_interrupt_phase_timing_enabled(bool enabled) noexcept {
            genesis_interrupt_phase_timing_enabled_ = enabled;
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

        enum class op_size : std::uint8_t { byte, word, longword };

        struct group0_fault {
            bool pending = false;
            int vector = 0;
            std::uint32_t access_address = 0U;
            std::uint32_t stacked_pc = 0U;
            std::uint16_t instruction_register = 0U;
            std::uint16_t status_word = 0U;
        };

        [[nodiscard]] static std::uint32_t size_mask(op_size s) noexcept;
        [[nodiscard]] static std::uint32_t size_sign_bit(op_size s) noexcept;
        [[nodiscard]] static int size_bytes(op_size s) noexcept;
        [[nodiscard]] static std::int32_t sign_extend(std::uint32_t v, op_size s) noexcept;

        static constexpr std::uint64_t genesis_refresh_initial_due = 62U;
        static constexpr std::uint64_t genesis_refresh_period = 128U;

        // ---- raw memory (no cycle accounting), 24-bit masked, big-endian ----
        [[nodiscard]] std::uint8_t rd8(std::uint32_t a) const noexcept;
        void wr8(std::uint32_t a, std::uint8_t v) noexcept;
        [[nodiscard]] std::uint16_t rd16(std::uint32_t a) const noexcept;
        void wr16(std::uint32_t a, std::uint16_t v) noexcept;
        [[nodiscard]] std::uint32_t rd32(std::uint32_t a) const noexcept;

        // ---- cycle-accounted accesses (4 clocks per word/byte bus cycle) ----
        // `program` selects the program-space (opcode) read path: PC-relative
        // addressing modes are program references on the 68000, so on an
        // opcode/data-split board (encrypted 68000, e.g. CPS-2) they must read the
        // decrypted instruction stream, not the encrypted data bus. No-op for every
        // system whose opcode bytes equal its data bytes.
        [[nodiscard]] std::uint8_t read8(std::uint32_t a, bool program = false) noexcept;
        [[nodiscard]] std::uint16_t read16(std::uint32_t a, bool program = false) noexcept;
        [[nodiscard]] std::uint32_t read32(std::uint32_t a, bool program = false) noexcept;
        void write8(std::uint32_t a, std::uint8_t v) noexcept;
        void write16(std::uint32_t a, std::uint16_t v) noexcept;
        void write32(std::uint32_t a, std::uint32_t v) noexcept;
        [[nodiscard]] std::uint32_t read_sized(std::uint32_t a, op_size s,
                                               bool program = false) noexcept;
        void write_sized(std::uint32_t a, op_size s, std::uint32_t v) noexcept;
        void charge_bus_cycle(std::uint32_t a, bool program, bool write) noexcept;
        // PC-relative modes (mode 7 reg 2 = d16(PC), reg 3 = d8(PC,Xn)) are the only
        // program-space EA reads; everything else uses the data bus.
        [[nodiscard]] static constexpr bool is_pc_relative(int mode, int reg) noexcept {
            return mode == 7 && (reg == 2 || reg == 3);
        }

        // ---- instruction stream ----
        void invalidate_prefetch() noexcept;
        [[nodiscard]] std::uint16_t prefetch_word(std::uint32_t a) noexcept;
        void refill_prefetch() noexcept;
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
        void notify_unhandled_opcode(std::uint16_t op) noexcept;

        // ---- supervisor state, stack, exceptions ----
        void set_supervisor(bool supervisor) noexcept; // swap USP/SSP on a mode change
        void write_sr(std::uint16_t value) noexcept;
        void push16(std::uint16_t value) noexcept;
        void push32(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint16_t pop16() noexcept;
        [[nodiscard]] std::uint32_t pop32() noexcept;
        void raise_exception(int vector, std::uint32_t exc_pc) noexcept;
        [[nodiscard]] std::uint16_t group0_status_word(bool instruction_fetch,
                                                       bool read_access) const noexcept;
        void queue_address_error(std::uint32_t access_address, std::uint32_t stacked_pc,
                                 bool instruction_fetch, bool read_access) noexcept;
        void queue_bus_error(std::uint32_t access_address, std::uint32_t stacked_pc,
                             bool instruction_fetch, bool read_access) noexcept;
        [[nodiscard]] ibus::bus_fault consume_bus_fault() noexcept;
        void raise_group0_exception(int vector, const group0_fault& fault) noexcept;
        void process_interrupt() noexcept;
        [[nodiscard]] bool test_cc(int cc) const noexcept;

        std::array<std::uint32_t, 8> d_{};
        std::array<std::uint32_t, 8> a_{};
        std::uint32_t pc_{};
        std::uint16_t sr_{};
        std::uint32_t usp_{};
        std::uint32_t ssp_{};
        std::uint32_t inst_addr_{}; // address of the instruction in flight (for exception frames)
        std::uint16_t current_opcode_{};
        group0_fault group0_fault_{};
        int irq_level_{};
        int prev_irq_level_{}; // for the level-7 (NMI) edge
        std::function<void(int)> irq_ack_{};
        std::function<void(std::uint32_t)> tas_callback_{};
        std::function<void()> reset_callback_{};
        std::function<std::uint32_t(std::uint32_t addr, bool program, bool write,
                                    std::uint32_t instruction_cycles_before_access,
                                    std::uint32_t instruction_wait_cycles)>
            bus_wait_callback_{};
        std::function<void(std::uint32_t)> trace_callback_{};
        std::function<void(std::uint32_t, std::uint16_t)> no_effect_opcode_callback_{};
        std::function<void(std::uint32_t, std::uint16_t)> unhandled_opcode_callback_{};
        std::function<void(int, std::uint32_t, std::uint32_t, std::uint16_t, std::uint16_t)>
            exception_callback_{};
        bool exception_raised_{};
        bool exception_entry_{};
        bool stopped_{};
        bool halted_{};

        int cycles_{}; // cycles of the instruction in flight
        // tick()'s catch-up loop and cycle_debt_ live in cpu_catch_up.
        friend class cpu_catch_up<m68000>;
        std::uint64_t elapsed_{}; // total cycles executed

        // Genesis / Mega Drive 68K bus DRAM refresh tracking. Disabled by
        // default because arcade and computer boards using the same MC68000 do
        // not inherit Sega's bus-controller refresh stalls.
        std::uint64_t bus_refresh_due_{};
        bool bus_refresh_enabled_{false};

        bool genesis_interrupt_phase_timing_enabled_{false};

        // Genesis $A00000-$A0FFFF access latency (see set_z80_bus_latency_enabled).
        bool z80_bus_latency_enabled_{false};

        // Delayed-IRQ state (see schedule_delayed_irq above).
        // delayed_irq_counter_ counts step_instruction() invocations until
        // the IRQ should be raised: while > 0 the CPU runs normally without
        // taking the delayed IRQ; on reaching 0, irq_level_ is set to
        // delayed_irq_level_.
        int delayed_irq_level_{};
        int delayed_irq_counter_{};

        // Functional MC68000 two-word prefetch queue. Cycle charging remains in
        // fetch16/fetch32 for now; this queue models the visible stale
        // instruction-stream semantics that self-modifying Amiga loaders rely on.
        std::array<std::uint16_t, 2> prefetch_words_{};
        std::uint32_t prefetch_pc_{};
        bool prefetch_valid_{};

        // Cycle-source accumulator for the instruction in flight; snapshotted
        // to last_cycle_sources_ at the end of each step_instruction(). The
        // type lives on m68000_diagnostics so the diagnostic facade can
        // expose it without re-declaring.
        m68000_diagnostics::cycle_sources cycle_sources_{};
        m68000_diagnostics::cycle_sources last_cycle_sources_{};

        ibus* bus_{};

        // Instruction-fetch fast path (span state + refill) lives in
        // cpu_fetch_span; the 68000 serves every address from a span.
        friend class cpu_fetch_span<m68000>;
        [[nodiscard]] ibus* fetch_bus() const noexcept { return bus_; }

        std::array<register_descriptor, 20> register_view_{};
        instrumentation::introspection_builder introspection_;
        m68000_diagnostics diagnostics_{*this};
    };

} // namespace mnemos::chips::cpu
