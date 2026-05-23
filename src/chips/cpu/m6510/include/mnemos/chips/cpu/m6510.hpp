#pragma once

#include <mnemos/chips/common/chip.hpp>

#include <cstdint>

namespace mnemos::chips::cpu {

    struct decoded;

    // MOS Technology 6510 — the 6502 core used in the Commodore 64, with an added
    // on-chip 8-bit I/O port at $00/$01.
    //
    // This is the M1 skeleton: register file, status-flag and addressing-mode
    // taxonomy, functional reset (register state + reset-vector load), and bus
    // attachment. Opcode execution, cycle-accurate interrupts, the $00/$01 I/O
    // port, save/load state, and the introspection surface land in the following
    // M1 tasks; the stubs below mark exactly where.
    class m6510 final : public i_cpu {
      public:
        // Bit positions within the P (processor status) register.
        enum class status_flag : std::uint8_t {
            carry = 0,
            zero = 1,
            irq_disable = 2,
            decimal = 3,
            break_command = 4,
            unused = 5, // physically absent; reads as 1
            overflow = 6,
            negative = 7,
        };

        // The thirteen addressing modes of the 6502 instruction set.
        enum class addressing_mode : std::uint8_t {
            implied,
            accumulator,
            immediate,
            zero_page,
            zero_page_x,
            zero_page_y,
            relative,
            absolute,
            absolute_x,
            absolute_y,
            indirect,
            indexed_indirect, // (zp,X)
            indirect_indexed, // (zp),Y
        };

        // Decoded operation (the "what"), independent of addressing mode.
        enum class operation : std::uint8_t {
            kil, // illegal/jam; also the default for undecoded opcodes
            nop,
            lda,
        };

        // How an instruction touches memory; selects the cycle micro-sequence.
        enum class access_kind : std::uint8_t {
            other,
            read,
            write,
            read_modify_write,
            implied,
            relative,
            stack,
            jump,
        };

        struct registers final {
            std::uint8_t a{};   // accumulator
            std::uint8_t x{};   // index X
            std::uint8_t y{};   // index Y
            std::uint8_t sp{};  // stack pointer (offset into the $0100 page)
            std::uint8_t p{};   // processor status
            std::uint16_t pc{}; // program counter
        };

        // CPU-visible address of the RES vector low byte ($FFFC/$FFFD).
        static constexpr std::uint16_t reset_vector = 0xFFFCU;

        m6510() = default;

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        void attach_bus(i_bus& bus) noexcept override;

        [[nodiscard]] const registers& cpu_registers() const noexcept { return registers_; }
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return cycles_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return tcu_ == 0U; }

        [[nodiscard]] bool flag(status_flag bit) const noexcept;
        void set_flag(status_flag bit, bool value) noexcept;

        // CPU-side memory access. Intercepts the on-chip $00/$01 I/O port before
        // delegating to the attached bus.
        [[nodiscard]] std::uint8_t read(std::uint16_t address) noexcept;
        void write(std::uint16_t address, std::uint8_t value) noexcept;

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        // Pins configured as inputs read high through the default pull.
        static constexpr std::uint8_t port_input_pull = 0xFFU;

        void step_one_cycle();
        void step_read(const decoded& entry);
        void step_implied(const decoded& entry);
        void execute_read(operation op) noexcept;
        void execute_implied(operation op) noexcept;
        void op_lda(std::uint8_t value) noexcept;
        void set_nz(std::uint8_t value) noexcept;

        registers registers_{};
        std::uint64_t cycles_{};
        i_bus* bus_{};
        std::uint8_t port_ddr_{};
        std::uint8_t port_data_{};

        // Cycle-stepped execution state. tcu_ == 0 marks an instruction boundary;
        // ir_ holds the opcode being executed; ea_/operand_ are per-instruction
        // scratch used by the addressing-mode micro-engine.
        std::uint8_t ir_{};
        std::uint8_t tcu_{};
        std::uint16_t ea_{};
        std::uint8_t operand_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::cpu
