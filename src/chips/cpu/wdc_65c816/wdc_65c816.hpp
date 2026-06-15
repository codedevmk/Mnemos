#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // WDC 65C816 16-bit CPU (a 6502 superset; the main CPU of a 16-bit
    // home console).
    //
    // The part is mode-switchable: an emulation mode that behaves as a 6502
    // (8-bit A/X/Y, page-1 stack) and a native 16-bit mode reached via XCE.
    // The M flag selects the accumulator/memory width and the X flag the
    // index-register width independently; the 24-bit address space is reached
    // through the program-bank (PBR) and data-bank (DBR) registers, with the
    // direct-page register (D) relocating the 6502 zero page.
    //
    // The core is instruction-stepped: step_instruction() executes exactly one
    // instruction (servicing a pending interrupt first) and returns its cycle
    // cost. tick(cycles) catches up by running whole instructions until the
    // requested cycles are consumed. Memory is the attached ibus, addressed as
    // a flat 24-bit space (bank << 16 | offset).
    //
    // Ported from the Emu reference (chips/wdc_65c816); clean-room per the WDC
    // 65C816 datasheet. The integer behaviour of every opcode is preserved.
    class wdc_65c816 final : public icpu, public cpu_catch_up<wdc_65c816> {
      public:
        // P-register flag bits. M (accumulator/memory width) and X (index
        // width) take their native-mode meaning; in emulation mode they reuse
        // the 6502 unused/break semantics.
        static constexpr std::uint8_t flag_c = 0x01U; // carry
        static constexpr std::uint8_t flag_z = 0x02U; // zero
        static constexpr std::uint8_t flag_i = 0x04U; // IRQ disable
        static constexpr std::uint8_t flag_d = 0x08U; // decimal
        static constexpr std::uint8_t flag_x = 0x10U; // index width (native) / break (emu)
        static constexpr std::uint8_t flag_m = 0x20U; // accumulator width (native) / unused (emu)
        static constexpr std::uint8_t flag_v = 0x40U; // overflow
        static constexpr std::uint8_t flag_n = 0x80U; // negative

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint16_t a{};  // accumulator (full 16 bits; high byte hidden when M=1)
            std::uint16_t x{};  // index X
            std::uint16_t y{};  // index Y
            std::uint16_t d{};  // direct-page register
            std::uint16_t s{};  // stack pointer
            std::uint16_t pc{}; // program counter (within the program bank)
            std::uint8_t pbr{}; // program-bank register
            std::uint8_t dbr{}; // data-bank register
            std::uint8_t p{};   // status flags
            bool e{};           // emulation-mode flag (separate from P; toggled by XCE)
            bool halted{};
        };

        wdc_65c816() {
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

        // icpu: the 24-bit memory address space the CPU reads/writes.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one instruction (servicing a pending interrupt
        // first); returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // Instruction-stepped: the CPU is always between instructions on return.
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Interrupt inputs. /IRQ is level-sensitive (serviced while I=0 and the
        // line is asserted); /NMI is edge-sensitive (latched on the rising edge).
        void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
        void set_nmi_line(bool asserted) noexcept;

        // The /RESET pin: assert to reset-and-park, release to run from the
        // reset vector (boards holding the CPU while program RAM is uploaded).
        void set_reset_line(bool asserted) noexcept;
        [[nodiscard]] bool reset_line_held() const noexcept { return reset_line_; }

        // Width queries (test/introspection): true when the corresponding
        // register is 8 bits wide in the current mode.
        [[nodiscard]] bool a_is_8bit() const noexcept { return e_ || (p_ & flag_m) != 0U; }
        [[nodiscard]] bool xy_is_8bit() const noexcept { return e_ || (p_ & flag_x) != 0U; }
        [[nodiscard]] bool emulation() const noexcept { return e_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // ---- memory access (24-bit, byte-granular) ----
        [[nodiscard]] std::uint8_t bus_read(std::uint32_t addr24) noexcept;
        void bus_write(std::uint32_t addr24, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t fetch_pc_byte() noexcept;
        [[nodiscard]] std::uint16_t fetch_pc_word() noexcept;

        // ---- stack (bank 0; page-1 forced in emulation mode) ----
        void stack_push8(std::uint8_t v) noexcept;
        [[nodiscard]] std::uint8_t stack_pop8() noexcept;

        // ---- mode enforcement ----
        void enforce_emulation_invariants() noexcept;

        // ---- flag helpers ----
        void set_nz8(std::uint8_t v) noexcept;
        void set_nz16(std::uint16_t v) noexcept;

        // ---- addressing ----
        [[nodiscard]] std::uint32_t addr_abs_data() noexcept;
        [[nodiscard]] std::uint32_t addr_dp() noexcept;
        [[nodiscard]] std::uint32_t addr_abs_x() noexcept;
        [[nodiscard]] std::uint32_t addr_abs_y() noexcept;
        [[nodiscard]] std::uint32_t addr_dp_x() noexcept;
        [[nodiscard]] std::uint32_t addr_dp_indirect() noexcept;
        [[nodiscard]] std::uint32_t addr_dp_indirect_y() noexcept;

        // ---- A load/store sized by the M flag ----
        void load_a_from(std::uint32_t addr) noexcept;
        void store_a_to(std::uint32_t addr) noexcept;

        // ---- ALU bodies ----
        void adc_body8(std::uint8_t operand) noexcept;
        void adc_body16(std::uint16_t operand) noexcept;
        void sbc_body8(std::uint8_t operand) noexcept;
        void sbc_body16(std::uint16_t operand) noexcept;
        void adc_decimal8(std::uint8_t operand) noexcept;
        void adc_decimal16(std::uint16_t operand) noexcept;
        void sbc_decimal8(std::uint8_t operand) noexcept;
        void sbc_decimal16(std::uint16_t operand) noexcept;
        void cmp_body8(std::uint8_t operand) noexcept;
        void cmp_body16(std::uint16_t operand) noexcept;

        // ---- index inc/dec (sized by X flag) ----
        void inc_xy(std::uint16_t& reg) noexcept;
        void dec_xy(std::uint16_t& reg) noexcept;

        // ---- branch ----
        void branch_if(bool taken) noexcept;

        // ---- interrupts ----
        void push_interrupt_context(std::uint8_t p_to_push) noexcept;
        [[nodiscard]] std::uint16_t fetch_vector(std::uint16_t native_addr,
                                                 std::uint16_t emu_addr) noexcept;
        void service_nmi() noexcept;
        void service_irq() noexcept;

        // ---- decode ----
        void execute(std::uint8_t opcode);

        // Register file. A/X/Y are stored full-width; the high byte is hidden by
        // the mode when narrow.
        std::uint16_t a_{};
        std::uint16_t x_{};
        std::uint16_t y_{};
        std::uint16_t d_{};
        std::uint16_t s_{};
        std::uint16_t pc_{};
        std::uint8_t pbr_{};
        std::uint8_t dbr_{};
        std::uint8_t p_{};
        bool e_{};
        bool halted_{};

        bool irq_line_{};
        bool nmi_line_{};
        bool nmi_pending_{};
        bool reset_line_{}; // /RESET held: parked, no execution

        int step_cycles_{}; // cycles of the instruction in flight
        // tick()'s catch-up loop and cycle_debt_ live in cpu_catch_up.
        friend class cpu_catch_up<wdc_65c816>;
        std::uint64_t elapsed_{}; // total cycles executed

        ibus* bus_{};

        // Per-instruction trace hook installed via the introspection surface.
        std::function<void(std::uint32_t pc)> trace_callback_{};

        std::array<register_descriptor, 11> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
