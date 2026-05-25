#pragma once

#include <mnemos/topology/bus.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace mnemos::runtime {
    class scheduler;
}

namespace mnemos::instrumentation {

    using breakpoint_id = std::uint32_t;
    using watchpoint_id = std::uint32_t; // shares the monotonic id space with breakpoints

    // Why a run/step stopped.
    enum class halt_reason : std::uint8_t {
        budget_exhausted, // ran the requested number of instructions without a hit
        breakpoint,       // a PC breakpoint matched
        watchpoint,       // a memory watchpoint matched
        step_complete,    // an explicit single step finished
    };

    // Which kind of bus access a watchpoint triggers on.
    enum class watch_kind : std::uint8_t { read, write, access };

    // A memory watchpoint over the inclusive range [address, address + length). The
    // optional condition receives the triggering access (address/value/write) so the
    // caller can gate on a value; an empty condition is unconditional.
    struct watchpoint_spec final {
        std::uint32_t address{};
        std::uint32_t length{1U};
        watch_kind kind{watch_kind::write};
        std::function<bool(const topology::access_event&)> condition{};
        bool enabled{true};
    };

    // A program-counter breakpoint: fire when the CPU is about to execute
    // `address`. The optional condition (evaluated against the live machine, which
    // the caller captures) gates it; an empty condition is unconditional.
    struct breakpoint_spec final {
        std::uint16_t address{};
        std::function<bool()> condition{};
        bool enabled{true};
    };

    // Push-based time-evolution events (TDS §12). A subscriber filters by an OR of
    // these bits; the debugger emits one when execution halts on a breakpoint /
    // watchpoint, after an explicit single step, and after a frame step.
    enum class event_kind : std::uint8_t {
        breakpoint = 0x01U,
        watchpoint = 0x02U,
        step = 0x04U,
        frame = 0x08U,
    };

    // A set of event kinds a subscriber wants. Implicitly built from a single kind
    // or an OR of kinds, e.g. `event_kind::breakpoint | event_kind::step`.
    struct event_filter final {
        std::uint8_t mask{};

        constexpr event_filter() noexcept = default;
        constexpr event_filter(event_kind kind) noexcept // NOLINT(*-explicit-*): ergonomic
            : mask(static_cast<std::uint8_t>(kind)) {}

        [[nodiscard]] constexpr bool selects(event_kind kind) const noexcept {
            return (mask & static_cast<std::uint8_t>(kind)) != 0U;
        }
    };

    [[nodiscard]] constexpr event_filter operator|(event_kind a, event_kind b) noexcept {
        event_filter f;
        f.mask =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
        return f;
    }
    [[nodiscard]] constexpr event_filter operator|(event_filter a, event_kind b) noexcept {
        a.mask = static_cast<std::uint8_t>(a.mask | static_cast<std::uint8_t>(b));
        return a;
    }

    inline constexpr event_filter all_events =
        event_kind::breakpoint | event_kind::watchpoint | event_kind::step | event_kind::frame;

    using subscription_handle = std::uint32_t;

    // A delivered event. `id` is the breakpoint/watchpoint id for those kinds (0
    // otherwise); pc/master_cycle/frame_index snapshot the machine at the event.
    struct event final {
        event_kind kind{};
        breakpoint_id id{};
        std::uint16_t pc{};
        std::uint64_t master_cycle{};
        std::uint64_t frame_index{};
    };

    // Receives events matching a subscription's filter. Implementors must outlive
    // their subscription.
    class event_sink {
      public:
        event_sink() = default;
        event_sink(const event_sink&) = delete;
        event_sink& operator=(const event_sink&) = delete;
        virtual ~event_sink() = default;
        virtual void on_event(const event& e) = 0;
    };

    // The outcome of run()/step_instruction().
    struct stop_event final {
        halt_reason reason{halt_reason::budget_exhausted};
        breakpoint_id breakpoint{}; // meaningful only when reason == breakpoint
        watchpoint_id watchpoint{}; // meaningful only when reason == watchpoint
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

        // Memory watchpoints (require a bus to observe; otherwise add_watchpoint
        // still records the spec but it can never fire).
        virtual watchpoint_id add_watchpoint(watchpoint_spec spec) = 0;
        virtual bool remove_watchpoint(watchpoint_id id) = 0;
        virtual bool set_watchpoint_enabled(watchpoint_id id, bool enabled) = 0;
        virtual void clear_watchpoints() noexcept = 0;
        [[nodiscard]] virtual std::size_t watchpoint_count() const noexcept = 0;

        // Event subscription: `sink` receives every event whose kind is in `filter`.
        virtual subscription_handle subscribe(event_filter filter, event_sink& sink) = 0;
        virtual bool unsubscribe(subscription_handle handle) = 0;

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
        // `watch_bus` is optional; supply it to enable memory watchpoints. The
        // scheduler, probe targets, and bus must all outlive the debugger.
        debugger(runtime::scheduler& scheduler, cpu_probe probe,
                 topology::bus* watch_bus = nullptr);
        ~debugger() override;

        debugger(const debugger&) = delete;
        debugger& operator=(const debugger&) = delete;
        debugger(debugger&&) = delete;
        debugger& operator=(debugger&&) = delete;

        [[nodiscard]] std::uint64_t master_cycle() const noexcept override;
        [[nodiscard]] std::uint64_t frame_index() const noexcept override;

        breakpoint_id add_breakpoint(breakpoint_spec spec) override;
        bool remove_breakpoint(breakpoint_id id) override;
        bool set_breakpoint_enabled(breakpoint_id id, bool enabled) override;
        void clear_breakpoints() noexcept override;
        [[nodiscard]] std::size_t breakpoint_count() const noexcept override;

        watchpoint_id add_watchpoint(watchpoint_spec spec) override;
        bool remove_watchpoint(watchpoint_id id) override;
        bool set_watchpoint_enabled(watchpoint_id id, bool enabled) override;
        void clear_watchpoints() noexcept override;
        [[nodiscard]] std::size_t watchpoint_count() const noexcept override;

        subscription_handle subscribe(event_filter filter, event_sink& sink) override;
        bool unsubscribe(subscription_handle handle) override;

        stop_event step_instruction() override;
        std::uint64_t step_frame() override;
        [[nodiscard]] stop_event run(std::uint64_t max_instructions) override;

      private:
        struct breakpoint_entry final {
            breakpoint_id id{};
            breakpoint_spec spec{};
        };
        struct watchpoint_entry final {
            watchpoint_id id{};
            watchpoint_spec spec{};
        };
        struct watch_hit final {
            watchpoint_id id{};
            topology::access_event access{};
        };
        struct subscription final {
            subscription_handle handle{};
            event_filter filter{};
            event_sink* sink{};
        };

        // Advance the scheduler until the CPU reaches its next instruction boundary.
        void advance_one_instruction();
        [[nodiscard]] std::uint16_t current_pc() const;
        // The id of the first enabled, matching breakpoint at `pc`, or 0 for none.
        [[nodiscard]] breakpoint_id matching_breakpoint(std::uint16_t pc) const;
        // Bus observer: records the first watchpoint a completed access trips.
        void on_bus_access(const topology::access_event& access);
        // Deliver an event to every subscription whose filter selects its kind.
        void emit(event_kind kind, breakpoint_id id, std::uint16_t pc);

        runtime::scheduler& scheduler_;
        cpu_probe probe_;
        topology::bus* watch_bus_{};
        std::vector<breakpoint_entry> breakpoints_;
        std::vector<watchpoint_entry> watchpoints_;
        std::vector<subscription> subscriptions_;
        std::optional<watch_hit> pending_watch_;
        breakpoint_id next_id_{1U};
        subscription_handle next_subscription_{1U};
    };

} // namespace mnemos::instrumentation
