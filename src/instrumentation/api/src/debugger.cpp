#include <mnemos/instrumentation/debugger.hpp>

#include <mnemos/runtime/scheduler.hpp>

#include <utility>

namespace mnemos::instrumentation {

    debugger::debugger(runtime::scheduler& scheduler, cpu_probe probe)
        : scheduler_(scheduler), probe_(std::move(probe)) {}

    std::uint64_t debugger::master_cycle() const noexcept { return scheduler_.master_cycle(); }

    std::uint64_t debugger::frame_index() const noexcept { return scheduler_.frame_index(); }

    breakpoint_id debugger::add_breakpoint(breakpoint_spec spec) {
        const breakpoint_id id = next_id_++;
        breakpoints_.push_back({.id = id, .spec = std::move(spec)});
        return id;
    }

    bool debugger::remove_breakpoint(breakpoint_id id) {
        for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
            if (it->id == id) {
                breakpoints_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool debugger::set_breakpoint_enabled(breakpoint_id id, bool enabled) {
        for (auto& e : breakpoints_) {
            if (e.id == id) {
                e.spec.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    void debugger::clear_breakpoints() noexcept { breakpoints_.clear(); }

    std::size_t debugger::breakpoint_count() const noexcept { return breakpoints_.size(); }

    std::uint16_t debugger::current_pc() const {
        return probe_.program_counter ? probe_.program_counter() : 0U;
    }

    breakpoint_id debugger::matching_breakpoint(std::uint16_t pc) const {
        for (const auto& e : breakpoints_) {
            if (e.spec.enabled && e.spec.address == pc &&
                (!e.spec.condition || e.spec.condition())) {
                return e.id;
            }
        }
        return 0U;
    }

    void debugger::advance_one_instruction() {
        // Step master cycles until the CPU sits at an instruction boundary again.
        // The do/while guarantees forward progress even when starting on a boundary;
        // with no boundary probe it degenerates to a single master cycle.
        do {
            scheduler_.run_master_cycles(1U);
        } while (probe_.at_instruction_boundary && !probe_.at_instruction_boundary());
    }

    stop_event debugger::step_instruction() {
        advance_one_instruction();
        return {.reason = halt_reason::step_complete,
                .breakpoint = 0U,
                .pc = current_pc(),
                .master_cycle = scheduler_.master_cycle()};
    }

    std::uint64_t debugger::step_frame() { return scheduler_.run_frame(); }

    stop_event debugger::run(std::uint64_t max_instructions) {
        for (std::uint64_t n = 0; n < max_instructions; ++n) {
            advance_one_instruction();
            const std::uint16_t pc = current_pc();
            if (const breakpoint_id id = matching_breakpoint(pc); id != 0U) {
                return {.reason = halt_reason::breakpoint,
                        .breakpoint = id,
                        .pc = pc,
                        .master_cycle = scheduler_.master_cycle()};
            }
        }
        return {.reason = halt_reason::budget_exhausted,
                .breakpoint = 0U,
                .pc = current_pc(),
                .master_cycle = scheduler_.master_cycle()};
    }

} // namespace mnemos::instrumentation
