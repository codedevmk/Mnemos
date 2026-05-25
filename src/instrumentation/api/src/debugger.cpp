#include <mnemos/instrumentation/debugger.hpp>

#include <mnemos/runtime/scheduler.hpp>

#include <utility>

namespace mnemos::instrumentation {

    debugger::debugger(runtime::scheduler& scheduler, cpu_probe probe, topology::bus* watch_bus)
        : scheduler_(scheduler), probe_(std::move(probe)), watch_bus_(watch_bus) {
        if (watch_bus_ != nullptr) {
            watch_bus_->set_access_observer(
                [this](const topology::access_event& e) { on_bus_access(e); });
        }
    }

    debugger::~debugger() {
        if (watch_bus_ != nullptr) {
            watch_bus_->set_access_observer({}); // never leave a dangling observer
        }
    }

    std::uint64_t debugger::master_cycle() const noexcept { return scheduler_.master_cycle(); }

    std::uint64_t debugger::frame_index() const noexcept { return scheduler_.frame_index(); }

    // ----- breakpoints -----

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

    // ----- watchpoints -----

    watchpoint_id debugger::add_watchpoint(watchpoint_spec spec) {
        const watchpoint_id id = next_id_++;
        watchpoints_.push_back({.id = id, .spec = std::move(spec)});
        return id;
    }

    bool debugger::remove_watchpoint(watchpoint_id id) {
        for (auto it = watchpoints_.begin(); it != watchpoints_.end(); ++it) {
            if (it->id == id) {
                watchpoints_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool debugger::set_watchpoint_enabled(watchpoint_id id, bool enabled) {
        for (auto& e : watchpoints_) {
            if (e.id == id) {
                e.spec.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    void debugger::clear_watchpoints() noexcept { watchpoints_.clear(); }

    std::size_t debugger::watchpoint_count() const noexcept { return watchpoints_.size(); }

    // ----- event subscription -----

    subscription_handle debugger::subscribe(event_filter filter, event_sink& sink) {
        const subscription_handle handle = next_subscription_++;
        subscriptions_.push_back({.handle = handle, .filter = filter, .sink = &sink});
        return handle;
    }

    bool debugger::unsubscribe(subscription_handle handle) {
        for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
            if (it->handle == handle) {
                subscriptions_.erase(it);
                return true;
            }
        }
        return false;
    }

    void debugger::emit(event_kind kind, breakpoint_id id, std::uint16_t pc) {
        if (subscriptions_.empty()) {
            return;
        }
        const event e{.kind = kind,
                      .id = id,
                      .pc = pc,
                      .master_cycle = scheduler_.master_cycle(),
                      .frame_index = scheduler_.frame_index()};
        for (const auto& s : subscriptions_) {
            if (s.sink != nullptr && s.filter.selects(kind)) {
                s.sink->on_event(e);
            }
        }
    }

    void debugger::on_bus_access(const topology::access_event& access) {
        if (watchpoints_.empty() || pending_watch_.has_value()) {
            return; // nothing to match, or a hit is already latched for this step
        }
        for (const auto& e : watchpoints_) {
            if (!e.spec.enabled) {
                continue;
            }
            const bool kind_ok = e.spec.kind == watch_kind::access ||
                                 (e.spec.kind == watch_kind::write && access.write) ||
                                 (e.spec.kind == watch_kind::read && !access.write);
            if (!kind_ok) {
                continue;
            }
            const std::uint32_t end = e.spec.address + e.spec.length;
            if (access.address < e.spec.address || access.address >= end) {
                continue;
            }
            if (e.spec.condition && !e.spec.condition(access)) {
                continue;
            }
            pending_watch_ = watch_hit{.id = e.id, .access = access};
            return;
        }
    }

    // ----- execution control -----

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
        pending_watch_.reset();
        advance_one_instruction();
        const std::uint16_t pc = current_pc();
        if (pending_watch_.has_value()) {
            emit(event_kind::watchpoint, pending_watch_->id, pc);
            return {.reason = halt_reason::watchpoint,
                    .breakpoint = 0U,
                    .watchpoint = pending_watch_->id,
                    .pc = pc,
                    .master_cycle = scheduler_.master_cycle()};
        }
        emit(event_kind::step, 0U, pc);
        return {.reason = halt_reason::step_complete,
                .breakpoint = 0U,
                .watchpoint = 0U,
                .pc = pc,
                .master_cycle = scheduler_.master_cycle()};
    }

    std::uint64_t debugger::step_frame() {
        const std::uint64_t frame = scheduler_.run_frame();
        emit(event_kind::frame, 0U, current_pc());
        return frame;
    }

    stop_event debugger::run(std::uint64_t max_instructions) {
        for (std::uint64_t n = 0; n < max_instructions; ++n) {
            pending_watch_.reset();
            advance_one_instruction();
            const std::uint16_t pc = current_pc();
            if (pending_watch_.has_value()) {
                emit(event_kind::watchpoint, pending_watch_->id, pc);
                return {.reason = halt_reason::watchpoint,
                        .breakpoint = 0U,
                        .watchpoint = pending_watch_->id,
                        .pc = pc,
                        .master_cycle = scheduler_.master_cycle()};
            }
            if (const breakpoint_id id = matching_breakpoint(pc); id != 0U) {
                emit(event_kind::breakpoint, id, pc);
                return {.reason = halt_reason::breakpoint,
                        .breakpoint = id,
                        .watchpoint = 0U,
                        .pc = pc,
                        .master_cycle = scheduler_.master_cycle()};
            }
        }
        return {.reason = halt_reason::budget_exhausted,
                .breakpoint = 0U,
                .watchpoint = 0U,
                .pc = current_pc(),
                .master_cycle = scheduler_.master_cycle()};
    }

} // namespace mnemos::instrumentation
