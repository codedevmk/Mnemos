#pragma once

#include "chip.hpp"
#include "sh2_peripherals.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Hitachi SH7604 (SH-2) CPU.
    //
    // 32-bit big-endian RISC with a fixed 16-bit instruction width and sixteen
    // general registers (R0-R15; R15 is the hardware stack pointer). The control
    // set is PC, PR (procedure/return register), the status register SR (T/S/
    // I0-I3/Q/M), the global and vector base registers GBR/VBR, and the multiply
    // accumulators MACH/MACL. Two of these drive the Sega 32X (a master and a
    // slave); the chip itself carries no 32X knowledge -- the 32X manifest
    // instantiates two and wires them.
    //
    // Built in phases (see docs/plans/2026-06-09-sega-32x-port.md), cannibalized
    // from the Emu reference (chips/sh2). Implemented: the programming model,
    // reset (PC + SP loaded big-endian from the vector table at VBR=0), the full
    // instruction set (data transfer, ALU, logical, shift/rotate, multiply,
    // divide-step, control flow with delay slots, system-register ops, all
    // addressing modes), and the TRAPA/RTE + external-interrupt model (set_irq /
    // an accept callback, with a one-instruction inhibit after SR loads). Still to
    // come: the illegal-instruction / address-error exceptions, SLEEP, and the
    // on-chip peripherals the 32X drives (FRT/WDT timer, DMAC, INTC, serial).
    // Undecoded opcodes execute as 1-cycle no-ops, the m68000 bring-up convention.
    //
    // Instruction-stepped like the m68000: step_instruction() runs one
    // instruction and returns its cycle cost; tick(cycles) catches up by running
    // whole instructions. Memory is the attached ibus (byte-addressed; 16/32-bit
    // accesses are assembled big-endian).
    class sh2 final : public icpu {
      public:
        // Status-register bits.
        static constexpr std::uint32_t sr_t = 1U << 0U;       // true/carry/borrow bit
        static constexpr std::uint32_t sr_s = 1U << 1U;       // saturation (MAC)
        static constexpr std::uint32_t sr_imask = 0xFU << 4U; // I0-I3 interrupt mask
        static constexpr std::uint32_t sr_q = 1U << 8U;       // DIV0/DIV1 quotient
        static constexpr std::uint32_t sr_m = 1U << 9U;       // DIV0/DIV1 divisor
        static constexpr std::uint32_t sr_mask = 0x000003F3U;

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::array<std::uint32_t, 16> r{};
            std::uint32_t pc{};
            std::uint32_t pr{};
            std::uint32_t sr{};
            std::uint32_t gbr{};
            std::uint32_t vbr{};
            std::uint32_t mach{};
            std::uint32_t macl{};
        };

        sh2() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // icpu: the memory address space the CPU executes against.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // ---- interrupt delivery (driven by the system / 32X INTC) ----
        // Present an external interrupt request at priority `level` (1-15) and
        // the given vector number. The CPU accepts it at the next instruction
        // boundary once level > SR.IMASK, pushing SR+PC and vectoring through
        // VBR + vector*4 (raising IMASK to the accepted level). level 0 clears.
        void set_irq(int level, std::uint8_t vector) noexcept {
            pending_irq_level_ = level;
            pending_irq_vector_ = vector;
        }
        void clear_irq() noexcept { pending_irq_level_ = 0; }
        // Invoked when an external IRQ is accepted so the system can clear its
        // own source latch and present the next pending request. Not serialized.
        void set_irq_accept_callback(std::function<void(int level, std::uint8_t vector)> cb) {
            irq_accept_ = std::move(cb);
        }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // PC of the instruction currently executing (captured at its fetch).
        [[nodiscard]] std::uint32_t current_instruction_addr() const noexcept { return inst_addr_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // Bridges the chip's diagnostic surface into the generic
        // `instrumentation::ichip_introspection` capability container: a
        // per-instruction PC+cycles trace target and a register view.
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(sh2& owner) noexcept;

            [[nodiscard]] instrumentation::trace_target* trace() override { return &trace_impl_; }
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }

          private:
            class trace_impl final : public instrumentation::trace_target {
              public:
                explicit trace_impl(sh2& owner) noexcept : owner_(&owner) {}
                void install(callback cb) override;

              private:
                sh2* owner_;
            };

            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(sh2& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                sh2* owner_;
            };

            trace_impl trace_impl_;
            registers_impl registers_impl_;
        };

        // ---- raw memory (24/32-bit address, big-endian) ----
        [[nodiscard]] std::uint8_t rd8(std::uint32_t a) const noexcept;
        void wr8(std::uint32_t a, std::uint8_t v) noexcept;
        [[nodiscard]] std::uint16_t rd16(std::uint32_t a) const noexcept;
        void wr16(std::uint32_t a, std::uint16_t v) noexcept;
        [[nodiscard]] std::uint32_t rd32(std::uint32_t a) const noexcept;
        void wr32(std::uint32_t a, std::uint32_t v) noexcept;

        // ---- status-register T bit ----
        void set_t(bool value) noexcept { sr_ = value ? (sr_ | sr_t) : (sr_ & ~sr_t); }
        [[nodiscard]] std::uint32_t t_in() const noexcept { return (sr_ & sr_t) != 0U ? 1U : 0U; }

        // ---- multiply-accumulate (memory operands; SR.S saturates) ----
        void mac_long(std::size_t rn, std::size_t rm) noexcept;
        void mac_word(std::size_t rn, std::size_t rm) noexcept;

        // ---- control transfer: run the delay-slot instruction, then redirect ----
        void branch_delayed(std::uint32_t target);

        // ---- exceptions + interrupts ----
        // Push SR then PC to @-R15 and vector through VBR + vector*4.
        void raise_exception(std::uint8_t vector, std::uint32_t saved_pc);
        // Accept the presented external IRQ if level > SR.IMASK (called at the
        // instruction boundary). Returns true if an interrupt was taken.
        bool try_service_irq();
        // Raise the illegal-instruction exception for an undecoded opcode
        // (slot-illegal, vector 6, inside a delay slot; general-illegal, vector
        // 4, otherwise).
        void illegal(std::uint16_t op);

        // ---- decode + execute one fetched opcode ----
        void exec(std::uint16_t op);

        std::array<std::uint32_t, 16> r_{};
        std::uint32_t pc_{};
        std::uint32_t pr_{};
        std::uint32_t sr_{};
        std::uint32_t gbr_{};
        std::uint32_t vbr_{};
        std::uint32_t mach_{};
        std::uint32_t macl_{};
        std::uint32_t inst_addr_{};
        bool in_delay_slot_{};                // transient: running a branch's delay slot
        bool sleeping_{};                     // halted by SLEEP until an interrupt arrives
        bool exception_taken_{};              // transient: an exception vectored this step
        std::uint32_t delay_resume_target_{}; // transient: a delayed branch's target
        int pending_irq_level_{};             // 0 = none; else presented IRQ level (1-15)
        std::uint8_t pending_irq_vector_{};
        int interrupt_inhibit_{}; // >0: skip IRQ acceptance for that many boundaries

        int cycles_{};              // cycles of the instruction in flight
        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()
        std::uint64_t elapsed_{};   // total cycles executed

        std::function<void(std::uint32_t)> trace_callback_{};
        std::function<void(int, std::uint8_t)> irq_accept_{};

        ibus* bus_{};
        sh2_peripherals peripherals_{}; // on-chip SH7604 peripherals ($FFFFFE00 window)

        std::array<register_descriptor, 23> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::cpu
