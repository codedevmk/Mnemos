#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Motorola MC6803 / MC6801-family CPU.
    //
    // This is the first reusable slice needed by Irem's early "Irem Audio"
    // boards. It models architectural reset through the big-endian vector at
    // $FFFE/$FFFF, the core register file, stack discipline, direct/indexed/
    // extended memory accesses, branches, subroutines, and common accumulator
    // ALU/load/store operations. It is intentionally not yet a complete opcode
    // or peripheral model; board-specific timer/port wiring lands with the Irem
    // audio-board integration tests.
    class m6803 final : public icpu, public cpu_catch_up<m6803> {
      public:
        static constexpr std::uint16_t reset_vector = 0xFFFEU;

        static constexpr std::uint8_t flag_c = 0x01U;
        static constexpr std::uint8_t flag_v = 0x02U;
        static constexpr std::uint8_t flag_z = 0x04U;
        static constexpr std::uint8_t flag_n = 0x08U;
        static constexpr std::uint8_t flag_i = 0x10U;
        static constexpr std::uint8_t flag_h = 0x20U;

        struct registers final {
            std::uint8_t a{};
            std::uint8_t b{};
            std::uint16_t x{};
            std::uint16_t sp{};
            std::uint16_t pc{};
            std::uint8_t ccr{};
        };

        m6803();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept { return regs_; }
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
        void set_nmi_line(bool asserted) noexcept { nmi_line_ = asserted; }
        [[nodiscard]] bool irq_line() const noexcept { return irq_line_; }
        [[nodiscard]] bool nmi_line() const noexcept { return nmi_line_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        friend class cpu_catch_up<m6803>;

        enum class alu_op : std::uint8_t {
            sub = 0x0U,
            cmp = 0x1U,
            sbc = 0x2U,
            and_ = 0x4U,
            bit = 0x5U,
            load = 0x6U,
            store = 0x7U,
            eor = 0x8U,
            adc = 0x9U,
            or_ = 0xAU,
            add = 0xBU,
        };

        [[nodiscard]] std::uint8_t read8(std::uint16_t address) noexcept;
        void write8(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t read16_be(std::uint16_t address) noexcept;
        void write16_be(std::uint16_t address, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t fetch8() noexcept;
        [[nodiscard]] std::uint16_t fetch16() noexcept;

        void push8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t pull8() noexcept;
        void push16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t pull16() noexcept;

        [[nodiscard]] bool flag(std::uint8_t mask) const noexcept {
            return (regs_.ccr & mask) != 0U;
        }
        void set_flag(std::uint8_t mask, bool enabled) noexcept;
        void set_nz8(std::uint8_t value) noexcept;
        void set_nz16(std::uint16_t value) noexcept;
        void set_load8_flags(std::uint8_t value) noexcept;
        void set_load16_flags(std::uint16_t value) noexcept;
        void set_store8_flags(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t add8(std::uint8_t lhs, std::uint8_t rhs,
                                        std::uint8_t carry) noexcept;
        [[nodiscard]] std::uint8_t sub8(std::uint8_t lhs, std::uint8_t rhs,
                                        std::uint8_t borrow) noexcept;
        [[nodiscard]] std::uint16_t add16(std::uint16_t lhs, std::uint16_t rhs) noexcept;
        [[nodiscard]] std::uint16_t sub16(std::uint16_t lhs, std::uint16_t rhs) noexcept;
        [[nodiscard]] std::uint8_t inc8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t dec8(std::uint8_t value) noexcept;

        [[nodiscard]] std::uint16_t direct_address() noexcept;
        [[nodiscard]] std::uint16_t indexed_address() noexcept;
        [[nodiscard]] std::uint16_t extended_address() noexcept;
        void branch_if(bool condition) noexcept;
        [[nodiscard]] bool branch_condition(std::uint8_t opcode) const noexcept;
        void exec_single(std::uint8_t opcode);
        void exec_accumulator(std::uint8_t opcode);
        void exec_memory_mutate(std::uint8_t opcode, std::uint16_t address);
        void exec_group(std::uint8_t opcode, std::uint16_t address, bool memory_operand);
        void exec_immediate(std::uint8_t opcode);

        registers regs_{};
        ibus* bus_{};
        std::uint64_t elapsed_{};
        bool irq_line_{};
        bool nmi_line_{};
        instrumentation::introspection_builder introspection_{};
        std::function<void(std::uint32_t pc)> trace_callback_{};
        std::array<register_descriptor, 6> register_view_{};
    };

} // namespace mnemos::chips::cpu
