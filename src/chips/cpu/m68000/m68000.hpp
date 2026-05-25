#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::cpu {

    // Motorola 68000 (MC68000) CPU.
    //
    // Ported from the Emu reference core (ADR 0006). 32-bit programming model on a
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

        // icpu: the memory address space the CPU executes against.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Interrupt request level (0-7, IPL pins). Stored now; the dispatch arrives
        // with the exception-framework phase.
        void set_irq_level(int level) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

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
        bool stopped_{};
        bool halted_{};

        int cycles_{};              // cycles of the instruction in flight
        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()
        std::uint64_t elapsed_{};   // total cycles executed

        ibus* bus_{};

        std::array<register_descriptor, 20> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::cpu
