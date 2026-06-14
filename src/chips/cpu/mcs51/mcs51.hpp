#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Intel MCS-51 (8051 family; the i8751 is the EPROM part Irem ships as
    // the M72 protection MCU).
    //
    // Programming model: ACC, B, 16-bit DPTR and PC, SP, PSW
    // (CY/AC/F0/RS1/RS0/OV/P with hardware ACC parity), four R0-R7 banks in
    // the 128-byte internal RAM, and the SFR space (ports P0-P3, the two
    // timers, IE/IP, SCON/SBUF stored). The full 255-opcode instruction set
    // is implemented, including the bit-addressable space (IRAM 0x20-0x2F and
    // the bit-addressable SFRs), MUL/DIV/DA, and MOVC program-memory reads.
    //
    // Memory spaces: program memory is an attached non-owning span (the
    // MCU's internal EPROM); external data memory (MOVX) routes through the
    // abstract ibus -- on a board, that is the MCU's window onto shared
    // latches/RAM. Port reads/writes route through optional per-port
    // callbacks (unset: reads return the latch, writes only update it).
    //
    // Interrupts: external INT0/INT1 (IT0/IT1 edge or level sense) and the
    // timer 0/1 overflows through IE, vectoring to 0x03/0x0B/0x13/0x1B with
    // a single in-service level (nested priority via IP is deferred, as are
    // the serial port and timer mode 3).
    //
    // Instruction-stepped: step_instruction() returns the machine-cycle cost
    // (one machine cycle = 12 oscillator clocks on the classic part);
    // tick(cycles) catches up by whole instructions and advances the timers
    // per machine cycle.
    class mcs51 final : public icpu {
      public:
        // PSW bits.
        static constexpr std::uint8_t psw_cy = 0x80U;
        static constexpr std::uint8_t psw_ac = 0x40U;
        static constexpr std::uint8_t psw_f0 = 0x20U;
        static constexpr std::uint8_t psw_rs = 0x18U; // register-bank select
        static constexpr std::uint8_t psw_ov = 0x04U;
        static constexpr std::uint8_t psw_p = 0x01U;

        using port_in_fn = std::function<std::uint8_t(int port)>;
        using port_out_fn = std::function<void(int port, std::uint8_t value)>;

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint8_t acc{};
            std::uint8_t b{};
            std::uint8_t psw{};
            std::uint8_t sp{};
            std::uint16_t dptr{};
            std::uint16_t pc{};
        };

        mcs51() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_trace(instrumentation::pc_trace_installer(
                    trace_callback_, [this] { return elapsed_cycles(); }));
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override; // machine cycles
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // icpu: the external data space MOVX reads/writes (16-bit addresses).
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Program memory (internal EPROM image); non-owning, fetches beyond
        // the span read 0.
        void attach_program(std::span<const std::uint8_t> program) noexcept { program_ = program; }

        // Port pin callbacks (ports 0-3). Reads return the callback's pins
        // ANDed with the latch (a written 0 always reads 0); unset reads the
        // latch alone. Writes always update the latch, then notify.
        void set_port_in(port_in_fn handler) noexcept { port_in_ = std::move(handler); }
        void set_port_out(port_out_fn handler) noexcept { port_out_ = std::move(handler); }

        // External interrupt pins (TCON.IT0/IT1 select edge or level sense).
        void set_int0_line(bool asserted) noexcept;
        void set_int1_line(bool asserted) noexcept;

        // Execute exactly one instruction (servicing a pending interrupt
        // first); returns the machine cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Direct-address space access for tests/boards (IRAM below 0x80,
        // SFRs at and above).
        [[nodiscard]] std::uint8_t peek_direct(std::uint8_t address) noexcept {
            return read_direct(address);
        }
        void poke_direct(std::uint8_t address, std::uint8_t value) noexcept {
            write_direct(address, value);
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:

        // ---- spaces ----
        [[nodiscard]] std::uint8_t fetch8() noexcept;
        [[nodiscard]] std::uint8_t code_read(std::uint16_t address) const noexcept {
            return address < program_.size() ? program_[address] : 0U;
        }
        [[nodiscard]] std::uint8_t read_direct(std::uint8_t address) noexcept;
        void write_direct(std::uint8_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_indirect(std::uint8_t address) noexcept;
        void write_indirect(std::uint8_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_bit_source(std::uint8_t bit) noexcept;
        [[nodiscard]] bool read_bit(std::uint8_t bit) noexcept;
        void write_bit(std::uint8_t bit, bool value) noexcept;
        [[nodiscard]] std::uint8_t reg_r(int n) noexcept;
        void set_reg_r(int n, std::uint8_t value) noexcept;
        void push8(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t pop8() noexcept;

        // ---- flags / ALU ----
        [[nodiscard]] bool flag(std::uint8_t mask) const noexcept { return (psw_ & mask) != 0U; }
        void assign_flag(std::uint8_t mask, bool set) noexcept {
            psw_ = set ? static_cast<std::uint8_t>(psw_ | mask)
                       : static_cast<std::uint8_t>(psw_ & ~mask);
        }
        void do_add(std::uint8_t value, bool with_carry) noexcept;
        void do_subb(std::uint8_t value) noexcept;

        // ---- execution ----
        void interrupt(std::uint16_t vector) noexcept;
        [[nodiscard]] bool service_interrupts() noexcept;
        int exec_one();
        void timers_tick(std::uint32_t machine_cycles) noexcept;

        std::span<const std::uint8_t> program_{};
        std::array<std::uint8_t, 128> iram_{};
        std::array<std::uint8_t, 128> sfr_{}; // direct 0x80-0xFF, index addr-0x80

        std::uint8_t acc_{};
        std::uint8_t b_{};
        std::uint8_t psw_{};
        std::uint8_t sp_{};
        std::uint16_t dptr_{};
        std::uint16_t pc_{};

        bool int0_line_{};
        bool int1_line_{};
        bool in_interrupt_{}; // single in-service level (RETI clears)

        int step_cycles_{};
        std::int64_t cycle_debt_{};
        std::uint64_t elapsed_{};

        ibus* bus_{};
        port_in_fn port_in_{};
        port_out_fn port_out_{};
        std::function<void(std::uint32_t pc)> trace_callback_{};

        std::array<register_descriptor, 6> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
