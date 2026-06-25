#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Sony R3000A-class MIPS I CPU, introduced for the PlayStation-derived
    // arcade boards in the Taito G-NET / Sony ZN family.
    //
    // This is the first board-enabling slice: a deterministic, instruction-
    // stepped integer core with little-endian instruction/data access, branch
    // delay slots, the MIPS I load-delay rule, HI/LO multiply/divide paths,
    // core CP0 status/cause/EPC registers, COP2/GTE register transfer shell,
    // and exception vectoring. Deferred until later G-NET board slices: real GTE
    // command math, caches/scratchpad timing, GPU/SPU, full DMA timing, and
    // cycle-accurate bus wait-state modelling.
    class r3000a final : public icpu, public cpu_catch_up<r3000a> {
      public:
        static constexpr std::uint32_t reset_vector = 0xBFC00000U;
        static constexpr std::uint32_t exception_vector = 0x80000080U;
        static constexpr std::uint32_t boot_exception_vector = 0xBFC00180U;

        static constexpr std::uint32_t status_bev = 1U << 22U;
        static constexpr std::uint32_t status_interrupt_enable = 1U << 0U;
        static constexpr std::uint32_t status_external_irq2_mask = 1U << 10U;
        static constexpr std::uint32_t cause_bd = 1U << 31U;
        static constexpr std::uint32_t cause_exception_code_mask = 0x7CU;
        static constexpr std::uint32_t cause_interrupt_pending_mask = 0x0000FF00U;
        static constexpr std::uint32_t cause_external_irq2_pending = 1U << 10U;

        enum class exception_code : std::uint32_t {
            interrupt = 0U,
            address_load = 4U,
            address_store = 5U,
            syscall = 8U,
            breakpoint = 9U,
            reserved_instruction = 10U,
            coprocessor_unusable = 11U,
            arithmetic_overflow = 12U,
        };

        struct registers final {
            std::array<std::uint32_t, 32> r{};
            std::uint32_t pc{};
            std::uint32_t hi{};
            std::uint32_t lo{};
            std::array<std::uint32_t, 32> cop0{};
            std::array<std::uint32_t, 32> cop2_data{};
            std::array<std::uint32_t, 32> cop2_control{};
            std::uint32_t cop2_command{};
            bool branch_pending{};
            std::uint32_t branch_target{};
            std::int32_t delayed_load_reg{-1};
            std::uint32_t delayed_load_value{};
            exception_code last_exception{exception_code::interrupt};
        };

        enum cop0_register : std::uint8_t {
            cop0_badvaddr = 8U,
            cop0_status = 12U,
            cop0_cause = 13U,
            cop0_epc = 14U,
            cop0_prid = 15U,
        };

        r3000a() {
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

        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }
        [[nodiscard]] exception_code last_exception() const noexcept { return last_exception_; }
        void set_external_interrupt_line(bool asserted) noexcept;
        [[nodiscard]] bool external_interrupt_line() const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        [[nodiscard]] static constexpr std::uint32_t physical_address(
            std::uint32_t address) noexcept {
            return (address >= 0x80000000U && address < 0xC0000000U)
                       ? (address & 0x1FFFFFFFU)
                       : address;
        }

        [[nodiscard]] std::uint8_t rb(std::uint32_t address) noexcept;
        void wb(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t rh(std::uint32_t address) noexcept;
        void wh(std::uint32_t address, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint32_t rw(std::uint32_t address) noexcept;
        void ww(std::uint32_t address, std::uint32_t value) noexcept;
        [[nodiscard]] std::uint32_t fetch32(std::uint32_t address) noexcept;

        void write_reg(std::uint8_t reg, std::uint32_t value) noexcept;
        void schedule_load(std::uint8_t reg, std::uint32_t value) noexcept;
        void schedule_branch(std::uint32_t target) noexcept;
        [[nodiscard]] bool interrupt_pending() const noexcept;
        void raise_exception(exception_code code, std::uint32_t fault_pc, bool delay_slot,
                             std::uint32_t badvaddr = 0U) noexcept;

        void exec_special(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept;
        void exec_regimm(std::uint32_t op, std::uint32_t inst_pc) noexcept;
        void exec_cop0(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept;
        void exec_cop2(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept;
        void exec_one(std::uint32_t op, std::uint32_t inst_pc, bool delay_slot) noexcept;

        std::array<std::uint32_t, 32> r_{};
        std::uint32_t pc_{reset_vector};
        std::uint32_t hi_{};
        std::uint32_t lo_{};
        std::array<std::uint32_t, 32> cop0_{};
        std::array<std::uint32_t, 32> cop2_data_{};
        std::array<std::uint32_t, 32> cop2_control_{};
        std::uint32_t cop2_command_{};
        bool branch_pending_{};
        std::uint32_t branch_target_{};
        std::int32_t delayed_load_reg_{-1};
        std::uint32_t delayed_load_value_{};
        std::int32_t next_load_reg_{-1};
        std::uint32_t next_load_value_{};
        bool exception_taken_{};
        exception_code last_exception_{exception_code::interrupt};

        int step_cycles_{};
        friend class cpu_catch_up<r3000a>;
        std::uint64_t elapsed_{};

        ibus* bus_{};
        std::function<void(std::uint32_t pc)> trace_callback_{};
        std::array<register_descriptor, 42> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
