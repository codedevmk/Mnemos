#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace mnemos::runtime {
    class scheduler;
}

namespace mnemos::instrumentation {

    using breakpoint_id = std::uint32_t;

    // Why a run/step stopped.
    enum class halt_reason : std::uint8_t {
        budget_exhausted, // ran the requested number of instructions without a hit
        breakpoint,       // a PC breakpoint matched
        step_complete,    // an explicit single step finished
    };

    // A program-counter breakpoint: fire when the CPU is about to execute
    // `address`. The optional condition (evaluated against the live machine, which
    // the caller captures) gates it; an empty condition is unconditional.
    struct breakpoint_spec final {
        std::uint16_t address{};
        std::function<bool()> condition{};
        bool enabled{true};
    };

    // The outcome of run()/step_instruction().
    struct stop_event final {
        halt_reason reason{halt_reason::budget_exhausted};
        breakpoint_id breakpoint{}; // meaningful only when reason == breakpoint
        std::uint16_t pc{};         // the next instruction's address
        std::uint64_t master_cycle{};
    };

    // How the debugger observes the CPU under test. Injected so instrumentation
    // (tier 6) never forces a contract change on the chip / runtime tiers: the
    // caller wires these to the concrete CPU (e.g. m6510::cpu_registers().pc and
    // m6510::at_instruction_boundary()).
    struct cpu_probe final {
        std::function<std::uint16_t()> program_counter; // current program counter
        std::function<bool()> at_instruction_boundary;  // true at an opcode fetch
    };

    // Pull-based queries plus execution control over a runtime (TDS §12). This v0.1
    // surface covers master-cycle / frame queries, instruction + frame stepping,
    // and PC breakpoints; memory watchpoints and event subscription layer on in
    // later phases.
    class i_runtime_introspection {
      public:
        i_runtime_introspection() = default;
        i_runtime_introspection(const i_runtime_introspection&) = delete;
        i_runtime_introspection& operator=(const i_runtime_introspection&) = delete;
        virtual ~i_runtime_introspection() = default;

        [[nodiscard]] virtual std::uint64_t master_cycle() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t frame_index() const noexcept = 0;

        virtual breakpoint_id add_breakpoint(breakpoint_spec spec) = 0;
        virtual bool remove_breakpoint(breakpoint_id id) = 0;
        virtual bool set_breakpoint_enabled(breakpoint_id id, bool enabled) = 0;
        virtual void clear_breakpoints() noexcept = 0;
        [[nodiscard]] virtual std::size_t breakpoint_count() const noexcept = 0;

        // Advance exactly one CPU instruction (returns reason step_complete).
        virtual stop_event step_instruction() = 0;
        // Advance one video frame; returns the new frame index (no breakpoint
        // checking within the frame).
        virtual std::uint64_t step_frame() = 0;
        // Run up to `max_instructions`, stopping early when a PC breakpoint hits.
        [[nodiscard]] virtual stop_event run(std::uint64_t max_instructions) = 0;
    };

    // Concrete debugger: drives a scheduler one master cycle at a time and checks
    // PC breakpoints at instruction boundaries. The scheduler and the probe targets
    // must outlive the debugger.
    class debugger final : public i_runtime_introspection {
      public:
        debugger(runtime::scheduler& scheduler, cpu_probe probe);

        [[nodiscard]] std::uint64_t master_cycle() const noexcept override;
        [[nodiscard]] std::uint64_t frame_index() const noexcept override;

        breakpoint_id add_breakpoint(breakpoint_spec spec) override;
        bool remove_breakpoint(breakpoint_id id) override;
        bool set_breakpoint_enabled(breakpoint_id id, bool enabled) override;
        void clear_breakpoints() noexcept override;
        [[nodiscard]] std::size_t breakpoint_count() const noexcept override;

        stop_event step_instruction() override;
        std::uint64_t step_frame() override;
        [[nodiscard]] stop_event run(std::uint64_t max_instructions) override;

      private:
        struct entry final {
            breakpoint_id id{};
            breakpoint_spec spec{};
        };

        // Advance the scheduler until the CPU reaches its next instruction boundary.
        void advance_one_instruction();
        [[nodiscard]] std::uint16_t current_pc() const;
        // The id of the first enabled, matching breakpoint at `pc`, or 0 for none.
        [[nodiscard]] breakpoint_id matching_breakpoint(std::uint16_t pc) const;

        runtime::scheduler& scheduler_;
        cpu_probe probe_;
        std::vector<entry> breakpoints_;
        breakpoint_id next_id_{1U};
    };

} // namespace mnemos::instrumentation
