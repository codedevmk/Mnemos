#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"
#include "cpu_fetch_span.hpp"
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
    // Built in phases (see docs/plans/2026-06-09-sega-32x-port.md) against the
    // Hitachi SH7604 hardware manual. Implemented: the programming model,
    // reset (PC + SP loaded big-endian from the vector table at VBR=0), the full
    // instruction set (data transfer, ALU, logical, shift/rotate, multiply,
    // divide-step, control flow with delay slots, system-register ops, all
    // addressing modes), illegal-instruction exceptions, SLEEP, on-chip
    // FRT/WDT/DMAC/DIVU/INTC interrupt delivery, and the TRAPA/RTE +
    // external-interrupt model (set_irq / an accept callback, with a
    // one-instruction inhibit after SR loads), address-error exceptions, and
    // opt-in timing models for the load-use interlock, shared-bus contention, and
    // the unified cache hit/miss shadow (instruction fetch + operand read). Still
    // deferred: the cache TW two-way mode and cross-CPU bus arbitration.
    //
    // Instruction-stepped like the m68000: step_instruction() runs one
    // instruction and returns its cycle cost; tick(cycles) catches up by running
    // whole instructions. Memory is the attached ibus (byte-addressed; 16/32-bit
    // accesses are assembled big-endian).
    class sh2 final : public icpu, public cpu_fetch_span<sh2> {
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

        sh2() {
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

        // icpu: the memory address space the CPU executes against. The on-chip
        // DMAC moves data over this same bus, so hand it the same handle. The
        // fetch fast path caches a direct span; the bus's invalidation listener
        // drops it on any remap / bank retarget / observer install.
        void attach_bus(ibus& bus) noexcept override;

        // Board-supplied DREQ level for module-request DMAC channels (the 32X
        // 68000-to-SH-2 FIFO asserts it while words are queued).
        void set_dmac_dreq_query(std::function<bool(int channel)> query) noexcept {
            peripherals_.set_dreq_query(std::move(query));
        }

        // Board-supplied external wait states. The CPU core reports candidate
        // memory accesses with their kind (read/write/rmw, or the locked atomic
        // TAS.B); manifests decide which addresses are shared hardware and how many
        // cycles to add. The on-chip DMAC uses the same hook (currently `read`).
        void set_bus_wait_callback(
            std::function<int(std::uint32_t address, std::uint8_t bytes, data_access_kind kind)>
                cb) {
            bus_wait_ = std::move(cb);
            peripherals_.set_bus_wait_callback(
                [this](std::uint32_t address, std::uint8_t bytes, data_access_kind kind) {
                    return bus_wait_ ? bus_wait_(address, bytes, kind) : 0;
                });
        }

        // The on-chip peripheral block (board glue + tests program the DMAC /
        // timers directly; CPU code reaches it through the $FFFFFE00 window).
        [[nodiscard]] sh2_peripherals& peripherals() noexcept { return peripherals_; }

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
        // The currently-presented external IRQ (0 = none), for a system INTC that
        // arbitrates several sources against the CPU's pending slot.
        [[nodiscard]] int pending_irq_level() const noexcept { return pending_irq_level_; }
        [[nodiscard]] std::uint8_t pending_irq_vector() const noexcept {
            return pending_irq_vector_;
        }
        // Invoked when an external IRQ is accepted so the system can clear its
        // own source latch and present the next pending request. Not serialized.
        void set_irq_accept_callback(std::function<void(int level, std::uint8_t vector)> cb) {
            irq_accept_ = std::move(cb);
        }
        // Invoked just before a WDT-driven internal reset zeroes this core's
        // elapsed counter, so a board pacing its schedule on elapsed_cycles()
        // can absorb the discarded count (the same way an external /RES does).
        void set_self_reset_callback(std::function<void()> cb) { self_reset_ = std::move(cb); }

        // X3: when enabled, ordinary (non-locked) data loads/stores are logged and
        // charged shared-bus contention through the bus-wait callback. Off by
        // default so the hot path takes no per-access metering cost; the board
        // turns it on only when its opt-in contention model is enabled. Locked
        // TAS reservations are charged regardless of this flag.
        void set_shared_contention_metering(bool on) noexcept { meter_shared_contention_ = on; }

        // X2: when enabled, model the SH7604 load-use interlock -- a register
        // loaded from memory by one instruction and read as a source by the next
        // costs +1 cycle. Off by default (an opt-in, manual-grounded timing model
        // with no cycle-accurate reference to validate against); when off, the
        // load-use state is never touched, so the hot path is unchanged.
        void set_load_use_interlock(bool on) noexcept { model_load_use_ = on; }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();
        // Scheduler-facing credit API. `tick()` uses this internally; board
        // manifests can grant both CPUs credit, then interleave credited
        // instructions by elapsed-cycle position without discarding overshoot.
        void grant_cycles(std::uint64_t cycles) noexcept {
            cycle_debt_ += static_cast<std::int64_t>(cycles);
        }
        [[nodiscard]] bool has_cycle_credit() const noexcept { return cycle_debt_ > 0; }
        int step_credited_instruction() {
            const int consumed = step_instruction();
            cycle_debt_ -= consumed;
            return consumed;
        }

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

        // ---- raw memory (24/32-bit address, big-endian) ----
        [[nodiscard]] std::uint8_t rd8(std::uint32_t a) noexcept;
        void wr8(std::uint32_t a, std::uint8_t v) noexcept;
        [[nodiscard]] std::uint16_t rd16(std::uint32_t a) noexcept;
        void wr16(std::uint32_t a, std::uint16_t v) noexcept;
        [[nodiscard]] std::uint32_t rd32(std::uint32_t a) noexcept;
        void wr32(std::uint32_t a, std::uint32_t v) noexcept;

        // ---- status-register T bit ----
        void set_t(bool value) noexcept { sr_ = value ? (sr_ | sr_t) : (sr_ & ~sr_t); }
        [[nodiscard]] std::uint32_t t_in() const noexcept { return (sr_ & sr_t) != 0U ? 1U : 0U; }

        // ---- multiply-accumulate (memory operands; SR.S saturates) ----
        void mac_long(std::size_t rn, std::size_t rm) noexcept;
        void mac_word(std::size_t rn, std::size_t rm) noexcept;

        // ---- control transfer: run the delay-slot instruction, then redirect ----
        void branch_delayed(std::uint32_t target, int minimum_cycles = 2);
        // Cycle accounting comes in two flavours, kept distinct on purpose:
        //  - account_cycles raises a FLOOR (the instruction's base issue cost);
        //  - add_external_wait_cycles / account_onchip_access_wait ADD stall
        //    cycles on top, saturating at INT_MAX (never wrapping) like the
        //    free add_bounded_wait_cycles helper.
        void account_cycles(int minimum_cycles) noexcept {
            if (cycles_ < minimum_cycles) {
                cycles_ = minimum_cycles;
            }
        }
        void add_external_wait_cycles(std::uint32_t address, std::uint8_t bytes,
                                      data_access_kind kind);
        void account_onchip_access_wait(std::uint32_t address, bool is_read) noexcept;
        // X3 shared-bus contention: require_*_data_access records each permitted
        // data access here (pure -- no cycle side effect), and step_instruction
        // charges the board bus-wait for them AFTER exec applies the base-cycle
        // floors, so the contention wait adds on top instead of being swallowed by
        // account_cycles' max-floor. SH-2 ops touch memory at most twice (MAC).
        void record_data_access(std::uint32_t address, std::uint8_t bytes,
                                data_access_kind kind = data_access_kind::read) noexcept {
            // Locked (TAS) reservations are charged inline at the access site; only
            // ordinary accesses are logged here, and only when the board opted in.
            if (!meter_shared_contention_ || kind == data_access_kind::tas) {
                return;
            }
            if (shared_access_count_ < static_cast<int>(shared_accesses_.size())) {
                shared_accesses_[static_cast<std::size_t>(shared_access_count_)] = {address, bytes,
                                                                                    kind};
                ++shared_access_count_;
            }
        }

        // A store re-tags the access require_*_data_access just logged for this
        // address as a write, so the board charges write-specific region timing
        // (e.g. SDRAM 2 cycles/word vs a read). Address-matched: it never re-tags
        // an unrelated prior access (a TAS isn't logged; an exception push targets
        // a different address). No call-site tagging needed -- wr8/16/32 call this.
        void note_write_access(std::uint32_t address) noexcept {
            if (shared_access_count_ <= 0) {
                return;
            }
            shared_access& a = shared_accesses_[static_cast<std::size_t>(shared_access_count_ - 1)];
            if (a.address == address && a.kind == data_access_kind::read) {
                a.kind = data_access_kind::write;
            }
        }

        // ---- X3 cache timing shadow (SH7604: 4 KiB, 4-way, 64-set, UNIFIED) ----
        // A timing-ONLY model: tags + valid + LRU predict hit/miss so the board
        // charges the SDRAM line-fill burst on a miss and nothing on a hit. Data
        // still flows through the bus (correctness unchanged); the shadow holds no
        // data. Live only while metering is on (ccr_ stays 0 otherwise, so the
        // hit/miss gate never fires and the default path is bit-identical). The
        // cache is unified: instruction fetches and operand reads share one array;
        // the no-fill (replacement-disable) bit is ID for a fetch, OD for an
        // operand. Returns true on a hit (no SDRAM burst); a miss fills the line.
        [[nodiscard]] bool cache_lookup(std::uint32_t address, bool is_instruction) noexcept;
        void cache_purge() noexcept;                       // invalidate all lines, reset LRU
        void cache_write_ccr(std::uint8_t value) noexcept; // CCR store; CP self-clears

        // X3 fetch-cache (Z7b): an instruction fetch from cacheable SDRAM hits/
        // misses the same unified shadow. Fetches dominate loop timing (a 16-byte
        // line = 8 instructions: 1 miss + 7 hits). The lookup runs at the fetch
        // site (program order: IF before MA), but a miss's burst is logged here and
        // charged at end-of-step BEFORE the operand charges (the floor-safe ADD
        // discipline + bus order). Transient per step, so it needs no save-state.
        void record_fetch_access(std::uint32_t address) noexcept {
            if (!meter_shared_contention_ || (ccr_ & ccr_ce) == 0U || address >= cache_area_limit) {
                return;
            }
            if (!cache_lookup(address, /*is_instruction=*/true) &&
                fetch_access_count_ < static_cast<int>(fetch_accesses_.size())) {
                fetch_accesses_[static_cast<std::size_t>(fetch_access_count_)] = address;
                ++fetch_access_count_;
            }
        }

        // ---- exceptions + interrupts ----
        // Push SR then PC to @-R15 and vector through VBR + vector*4.
        void raise_exception(std::uint8_t vector, std::uint32_t saved_pc);
        bool raise_address_error(std::uint32_t saved_pc);
        bool signal_address_error();
        bool require_fetch_access(std::uint32_t address);
        bool require_byte_data_access(std::uint32_t address,
                                      data_access_kind kind = data_access_kind::read);
        bool require_word_data_access(std::uint32_t address, bool pc_relative = false,
                                      data_access_kind kind = data_access_kind::read);
        bool require_long_data_access(std::uint32_t address, bool pc_relative = false,
                                      data_access_kind kind = data_access_kind::read);
        [[nodiscard]] static constexpr bool
        fast_offchip_data_space(std::uint32_t address) noexcept {
            return address < 0x40000000U ||
                   (address >= 0x80000000U && address < sh2_peripherals::window_base);
        }
        bool require_byte_data_access_fast(std::uint32_t address,
                                           data_access_kind kind = data_access_kind::read) {
            if (fast_offchip_data_space(address)) {
                record_data_access(address, 1U, kind);
                return true;
            }
            return require_byte_data_access(address, kind);
        }
        bool require_word_data_access_fast(std::uint32_t address, bool pc_relative = false,
                                           data_access_kind kind = data_access_kind::read) {
            if ((address & 1U) == 0U && fast_offchip_data_space(address)) {
                record_data_access(address, 2U, kind);
                return true;
            }
            return require_word_data_access(address, pc_relative, kind);
        }
        bool require_long_data_access_fast(std::uint32_t address, bool pc_relative = false,
                                           data_access_kind kind = data_access_kind::read) {
            if ((address & 3U) == 0U && fast_offchip_data_space(address)) {
                record_data_access(address, 4U, kind);
                return true;
            }
            return require_long_data_access(address, pc_relative, kind);
        }
        // Accept the presented external IRQ if level > SR.IMASK (called at the
        // instruction boundary). Returns true if an interrupt was taken.
        bool try_service_irq();
        bool service_watchdog_reset();
        void reset_core(reset_kind kind, bool preserve_watchdog_status);
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
        bool deferred_address_error_{};       // transient: delay-slot address fault
        std::uint32_t delay_resume_target_{}; // transient: a delayed branch's target
        int pending_irq_level_{};             // 0 = none; else presented IRQ level (1-15)
        std::uint8_t pending_irq_vector_{};
        int interrupt_inhibit_{}; // >0: skip IRQ acceptance for that many boundaries

        int cycles_{};              // cycles of the instruction in flight
        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()
        std::uint64_t elapsed_{};   // total cycles executed

        // Shared data accesses this instruction made, charged for contention at
        // the end of the step (see record_data_access). Capacity 2 = the SH-2 max
        // (MAC.x reads two operands); further accesses are dropped (metering only).
        struct shared_access final {
            std::uint32_t address{};
            std::uint8_t bytes{};
            data_access_kind kind{data_access_kind::read};
        };
        std::array<shared_access, 2> shared_accesses_{};
        int shared_access_count_{};
        bool meter_shared_contention_{}; // board opt-in; gates ordinary-access logging

        // X3 operand-cache timing shadow (see cache_operand_lookup). CCR lives in
        // the on-chip register window ($FFFFFE92); the SH-2 owns it only while
        // metering, so the default path leaves it to the peripheral register file.
        static constexpr std::uint32_t cache_ccr_address = 0xFFFFFE92U;
        static constexpr std::uint8_t ccr_ce = 0x01U; // cache enable
        static constexpr std::uint8_t ccr_id = 0x02U; // instruction-replacement disable
        static constexpr std::uint8_t ccr_od = 0x04U; // operand-replacement disable
        static constexpr std::uint8_t ccr_cp = 0x10U; // cache purge (self-clearing)
        static constexpr std::uint32_t cache_area_limit = 0x20000000U; // A31-29=000 cacheable
        static constexpr std::size_t cache_sets = 64U;
        static constexpr std::size_t cache_ways = 4U;
        static constexpr std::size_t cache_lines = cache_sets * cache_ways;
        std::uint8_t ccr_{};                                 // cache-control register
        std::array<std::uint32_t, cache_lines> cache_tag_{}; // [set*ways + way]
        std::array<bool, cache_lines> cache_valid_{};        // line validity
        std::array<std::array<std::uint8_t, cache_ways>, cache_sets> cache_order_{}; // MRU->LRU
        // Fetch-miss bursts deferred to the end-of-step charge (Z7b). Capacity 2 =
        // a step's max fetches (a delayed branch + its delay slot). Transient.
        std::array<std::uint32_t, 2> fetch_accesses_{};
        int fetch_access_count_{};

        // X2 load-use interlock. last_exec_op_ is the most recently executed
        // opcode (the delay slot for a taken delayed branch), so its load
        // destination is the producer for the NEXT step. pending_load_reg_ is the
        // GPR producer (-1 = none); pending_load_t_ covers LDC.L @Rn+,SR loading
        // T from memory. Both are consumed once by the following instruction.
        bool model_load_use_{}; // board opt-in; gates all load-use tracking
        std::uint16_t last_exec_op_{};
        int pending_load_reg_ = -1;
        bool pending_load_t_{};

        std::function<void(std::uint32_t)> trace_callback_{};
        std::function<void(int, std::uint8_t)> irq_accept_{};
        std::function<int(std::uint32_t, std::uint8_t, data_access_kind)> bus_wait_{};
        std::function<void()> self_reset_{}; // board hook: WDT internal reset is imminent

        ibus* bus_{};
        sh2_peripherals peripherals_{}; // on-chip SH7604 peripherals ($FFFFFE00 window)

        // Instruction-fetch fast path (span state + refill) lives in
        // cpu_fetch_span. The SH-2 on-chip register window must never be served
        // from a span, so it shadows fetch_span_excluded.
        friend class cpu_fetch_span<sh2>;
        [[nodiscard]] ibus* fetch_bus() const noexcept { return bus_; }
        [[nodiscard]] bool fetch_span_excluded(std::uint32_t a) const noexcept {
            return sh2_peripherals::in_window(a) || sh2_peripherals::in_window(a + 1U);
        }

        std::array<register_descriptor, 23> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
