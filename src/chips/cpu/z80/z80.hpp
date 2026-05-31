#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Zilog Z80 CPU.
    //
    // Complete instruction set:
    // the 256 unprefixed opcodes, the CB (rotate/shift/bit), ED (block transfer +
    // block I/O + 16-bit arithmetic), and DD/FD (IX/IY) prefixes including the
    // DDCB/FDCB indexed bit ops and the common undocumented opcodes (SLL, the
    // IX/IY half-byte ops), with the full flag model (the undocumented XF/YF bits).
    //
    // The Z80 is instruction-stepped: step_instruction() executes exactly one
    // instruction and returns its cycle cost. tick(cycles) catches up by running
    // whole instructions until the requested cycles are consumed. Memory is the
    // attached ibus; the Z80's separate 64K I/O space (IN/OUT) routes through
    // injected port callbacks (unset -> reads 0xFF, writes dropped).
    class z80 final : public icpu {
      public:
        // F-register flag bits.
        static constexpr std::uint8_t flag_c = 0x01U; // carry
        static constexpr std::uint8_t flag_n = 0x02U; // subtract
        static constexpr std::uint8_t flag_p = 0x04U; // parity / overflow
        static constexpr std::uint8_t flag_x = 0x08U; // undocumented bit 3
        static constexpr std::uint8_t flag_h = 0x10U; // half-carry
        static constexpr std::uint8_t flag_y = 0x20U; // undocumented bit 5
        static constexpr std::uint8_t flag_z = 0x40U; // zero
        static constexpr std::uint8_t flag_s = 0x80U; // sign

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint16_t af{};
            std::uint16_t bc{};
            std::uint16_t de{};
            std::uint16_t hl{};
            std::uint16_t af2{};
            std::uint16_t bc2{};
            std::uint16_t de2{};
            std::uint16_t hl2{};
            std::uint16_t ix{};
            std::uint16_t iy{};
            std::uint16_t sp{};
            std::uint16_t pc{};
            std::uint8_t i{};
            std::uint8_t r{};
            std::uint8_t im{};
            bool iff1{};
            bool iff2{};
            bool halted{};
        };

        using port_in_fn = std::function<std::uint8_t(std::uint16_t port)>;
        using port_out_fn = std::function<void(std::uint16_t port, std::uint8_t value)>;

        z80() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;
        void configure(const config_table& cfg, const callback_table& callbacks) override;

        // icpu: the memory address space the CPU reads/writes.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // The separate Z80 I/O space (IN/OUT). Optional; unset ports read 0xFF.
        void set_port_in(port_in_fn handler) noexcept { port_in_ = std::move(handler); }
        void set_port_out(port_out_fn handler) noexcept { port_out_ = std::move(handler); }

        // Execute exactly one instruction (servicing a pending interrupt first);
        // returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // Instruction-stepped: the CPU is always between instructions on return.
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Interrupt inputs.
        void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
        void set_nmi_line(bool asserted) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // Bridges the chip's diagnostic surface into the generic
        // `instrumentation::ichip_introspection`. The Z80 advertises a trace
        // target (per-instruction PC + cycles hook) and a register view
        // (`register_snapshot()`). Operational hooks (port_in/port_out, IRQ
        // line, NMI) stay on z80 itself because the system needs them set
        // for correct emulation.
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(z80& owner) noexcept;

            [[nodiscard]] instrumentation::trace_target* trace() override { return &trace_impl_; }
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }

          private:
            class trace_impl final : public instrumentation::trace_target {
              public:
                explicit trace_impl(z80& owner) noexcept : owner_(&owner) {}
                void install(callback cb) override;

              private:
                z80* owner_;
            };

            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(z80& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                z80* owner_;
            };

            trace_impl trace_impl_;
            registers_impl registers_impl_;
        };

        // ---- 8-bit halves of the 16-bit pair registers (little-endian pairs) ----
        [[nodiscard]] std::uint8_t a() const noexcept {
            return static_cast<std::uint8_t>(af_ >> 8U);
        }
        [[nodiscard]] std::uint8_t f() const noexcept { return static_cast<std::uint8_t>(af_); }
        [[nodiscard]] std::uint8_t b() const noexcept {
            return static_cast<std::uint8_t>(bc_ >> 8U);
        }
        [[nodiscard]] std::uint8_t c() const noexcept { return static_cast<std::uint8_t>(bc_); }
        [[nodiscard]] std::uint8_t d() const noexcept {
            return static_cast<std::uint8_t>(de_ >> 8U);
        }
        [[nodiscard]] std::uint8_t e() const noexcept { return static_cast<std::uint8_t>(de_); }
        [[nodiscard]] std::uint8_t h() const noexcept {
            return static_cast<std::uint8_t>(hl_ >> 8U);
        }
        [[nodiscard]] std::uint8_t l() const noexcept { return static_cast<std::uint8_t>(hl_); }

        void set_a(std::uint8_t v) noexcept {
            af_ = static_cast<std::uint16_t>((af_ & 0x00FFU) | (v << 8U));
        }
        void set_f(std::uint8_t v) noexcept {
            af_ = static_cast<std::uint16_t>((af_ & 0xFF00U) | v);
        }
        void set_b(std::uint8_t v) noexcept {
            bc_ = static_cast<std::uint16_t>((bc_ & 0x00FFU) | (v << 8U));
        }
        void set_c(std::uint8_t v) noexcept {
            bc_ = static_cast<std::uint16_t>((bc_ & 0xFF00U) | v);
        }
        void set_d(std::uint8_t v) noexcept {
            de_ = static_cast<std::uint16_t>((de_ & 0x00FFU) | (v << 8U));
        }
        void set_e(std::uint8_t v) noexcept {
            de_ = static_cast<std::uint16_t>((de_ & 0xFF00U) | v);
        }
        void set_h(std::uint8_t v) noexcept {
            hl_ = static_cast<std::uint16_t>((hl_ & 0x00FFU) | (v << 8U));
        }
        void set_l(std::uint8_t v) noexcept {
            hl_ = static_cast<std::uint16_t>((hl_ & 0xFF00U) | v);
        }

        // ---- memory + I/O access ----
        [[nodiscard]] std::uint8_t rb(std::uint16_t addr) noexcept;
        void wb(std::uint16_t addr, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t rw(std::uint16_t addr) noexcept;
        void ww(std::uint16_t addr, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t op_fetch8() noexcept;
        [[nodiscard]] std::uint8_t imm_fetch8() noexcept;
        [[nodiscard]] std::uint16_t fetch16() noexcept;
        [[nodiscard]] std::int8_t fetchd() noexcept;
        [[nodiscard]] std::uint8_t port_in8(std::uint16_t port);
        void port_out8(std::uint16_t port, std::uint8_t value);

        void push16(std::uint16_t v) noexcept;
        [[nodiscard]] std::uint16_t pop16() noexcept;
        [[nodiscard]] std::uint16_t ex_sp16(std::uint16_t reg) noexcept;
        void inc_r() noexcept {
            r_ = static_cast<std::uint8_t>((r_ & 0x80U) | ((r_ + 1U) & 0x7FU));
        }

        // ---- ALU + register helpers ----
        void do_add_a(std::uint8_t v) noexcept;
        void do_adc_a(std::uint8_t v) noexcept;
        void do_sub_a(std::uint8_t v) noexcept;
        void do_sbc_a(std::uint8_t v) noexcept;
        void do_cp(std::uint8_t v) noexcept;
        void do_and(std::uint8_t v) noexcept;
        void do_or(std::uint8_t v) noexcept;
        void do_xor(std::uint8_t v) noexcept;
        [[nodiscard]] std::uint8_t do_inc(std::uint8_t v) noexcept;
        [[nodiscard]] std::uint8_t do_dec(std::uint8_t v) noexcept;
        void do_add16(std::uint16_t& dst, std::uint16_t src) noexcept;
        void do_adc16(std::uint16_t src) noexcept;
        void do_sbc16(std::uint16_t src) noexcept;
        void do_alu(int op, std::uint8_t v) noexcept;

        [[nodiscard]] std::uint8_t get_reg8(int reg) noexcept;
        void set_reg8(int reg, std::uint8_t v) noexcept;
        [[nodiscard]] std::uint16_t* get_rp(int p) noexcept;
        [[nodiscard]] std::uint16_t* get_rp2(int p) noexcept;
        [[nodiscard]] bool test_cc(int cc) const noexcept;

        // ---- decode ----
        void exec_main(std::uint8_t op);
        void exec_cb();
        void exec_ed();
        void exec_dd_fd(bool use_iy);
        void exec_xcb(std::uint16_t idx);

        // Register file (16-bit pairs; halves via the accessors above).
        std::uint16_t af_{};
        std::uint16_t bc_{};
        std::uint16_t de_{};
        std::uint16_t hl_{};
        std::uint16_t af2_{};
        std::uint16_t bc2_{};
        std::uint16_t de2_{};
        std::uint16_t hl2_{};
        std::uint16_t ix_{};
        std::uint16_t iy_{};
        std::uint16_t sp_{};
        std::uint16_t pc_{};
        std::uint8_t i_{};
        std::uint8_t r_{};
        std::uint8_t im_{};
        bool iff1_{};
        bool iff2_{};
        bool halted_{};
        bool ei_pending_{};
        bool irq_line_{};
        bool nmi_pending_{};

        int step_cycles_{};         // cycles of the instruction in flight
        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()
        std::uint64_t elapsed_{};   // total cycles executed

        ibus* bus_{};
        port_in_fn port_in_{};
        port_out_fn port_out_{};

        // Per-instruction trace hook installed via the introspection surface.
        // The trace_impl bridges the generic trace_target callback (pc +
        // cycles trace_event) onto this PC-only slot; the chip queries its
        // own elapsed_cycles() at fire time.
        std::function<void(std::uint32_t pc)> trace_callback_{};

        friend class introspection_surface;

        std::array<register_descriptor, 16> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::cpu
