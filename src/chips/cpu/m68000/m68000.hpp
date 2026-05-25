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
    // Built in phases. THIS phase is a functional core: the 14 addressing modes, the
    // MOVE / MOVEA / MOVEQ family with the correct flag model, and per-bus-cycle
    // timing (4 clocks per word access) -- enough to establish the chip contract and
    // the addressing-mode machinery. The remaining instruction groups, the
    // cycle-accurate two-word prefetch pipeline, and the full exception framework
    // (address/bus errors, traps, interrupts) arrive in later phases; opcodes not yet
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

        // ---- flags / decode ----
        void set_logic_flags(op_size s, std::uint32_t value) noexcept;
        void exec(std::uint16_t op);
        void op_move(std::uint16_t op);
        void op_moveq(std::uint16_t op) noexcept;

        std::array<std::uint32_t, 8> d_{};
        std::array<std::uint32_t, 8> a_{};
        std::uint32_t pc_{};
        std::uint16_t sr_{};
        std::uint32_t usp_{};
        std::uint32_t ssp_{};
        int irq_level_{};
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
