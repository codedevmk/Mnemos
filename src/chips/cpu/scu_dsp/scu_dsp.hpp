#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Sega System Control Unit DSP: a custom 32-bit DSP that runs in parallel
    // with the host CPUs and drives DMA + fixed-point math (vector transforms,
    // mixing, projection). It executes an internal microprogram (256 x 32-bit
    // words of program RAM) over four banks of 64 x 32-bit data RAM. The host
    // loads the microprogram and controls execution through the program-control
    // port window ($..80 PPAF / $..84 PPD / $..88 PDA / $..8C PDD).
    //
    // Ported from the Emu reference (chips/scu_dsp); clean-room per the Sega SCU
    // DSP datasheet. The instruction decode/execute is integer-exact with the
    // reference.
    //
    // The DSP is instruction-stepped: step_instruction() executes exactly one
    // microinstruction (or idles when halted) and returns its cycle cost.
    // tick(cycles) catches up by running whole instructions until the requested
    // master cycles are consumed (the DSP issues one command per two master
    // cycles). The attached ibus is the D0-bus the DMA opcode transfers across;
    // the microprogram + data RAM are internal, so most execution never touches
    // it.
    class scu_dsp final : public icpu, public cpu_catch_up<scu_dsp> {
      public:
        static constexpr std::uint32_t program_words = 256U;
        static constexpr std::uint32_t ram_banks = 4U;
        static constexpr std::uint32_t ram_words = 64U;

        // Master cycles consumed per issued microinstruction (the DSP issues one
        // command per two master cycles).
        static constexpr int cycles_per_instruction = 2;

        // A snapshot / load image of the architectural register file.
        struct registers final {
            std::uint32_t rx{};
            std::uint32_t ry{};
            std::uint16_t ph{};  // high 16 of the 48-bit product
            std::uint32_t pl{};  // low 32 of the product
            std::uint16_t ach{}; // high 16 of accumulator A (ALU input)
            std::uint32_t acl{}; // low 32 of accumulator A (ALU input)
            std::uint16_t alu_h{};
            std::uint32_t alu_l{};
            std::array<std::uint8_t, ram_banks> ct{}; // 6-bit data-RAM counters
            std::uint16_t lop{};                      // 12-bit loop counter
            std::uint8_t top{};                       // 8-bit loop top
            std::uint8_t pc{};                        // 8-bit program counter
            std::uint32_t ra0{};                      // external-RAM read DMA byte address
            std::uint32_t wa0{};                      // external-RAM write DMA byte address
            bool s_flag{};
            bool z_flag{};
            bool c_flag{};
            bool t0_flag{};
            bool loop_active{};
            bool ex_flag{};
            bool ep_flag{};
            bool es_flag{};
            bool end_irq{};
        };

        scu_dsp() {
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

        // icpu: the D0-bus the DMA opcode transfers across.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one microinstruction (or idle when halted); returns the
        // master cycles it consumed.
        int step_instruction();

        // Execute up to max_steps microinstructions; stops early when execution
        // ends (EX cleared). Returns the number of instructions executed.
        int run(int max_steps);

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        // Instruction-stepped: the DSP is always between instructions on return.
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // Program / data RAM access for hosts that load the microprogram and
        // seed data without driving the port window byte by byte.
        void write_program(std::uint8_t address, std::uint32_t word) noexcept {
            program_[address] = word;
        }
        [[nodiscard]] std::uint32_t read_program(std::uint8_t address) const noexcept {
            return program_[address];
        }
        void write_data(std::uint8_t bank, std::uint8_t offset, std::uint32_t word) noexcept {
            ram_[bank & 0x3U][offset & 0x3FU] = word;
        }
        [[nodiscard]] std::uint32_t read_data(std::uint8_t bank,
                                              std::uint8_t offset) const noexcept {
            return ram_[bank & 0x3U][offset & 0x3FU];
        }

        // The program-control port window. The host routes its mapped MMIO
        // reads/writes here; the low nibble of the offset selects the port
        // (0=PPAF, 4=PPD, 8=PDA, C=PDD), matching the datasheet $..80-$..8F map.
        [[nodiscard]] std::uint32_t read_reg(std::uint32_t address) noexcept;
        void write_reg(std::uint32_t address, std::uint32_t value) noexcept;

        // The end-of-program IRQ latch (raised by ENDI). Hosts poll + clear it.
        [[nodiscard]] bool end_irq() const noexcept { return end_irq_; }
        void clear_end_irq() noexcept { end_irq_ = false; }
        [[nodiscard]] bool executing() const noexcept { return ex_flag_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // ---- data-RAM access ----
        [[nodiscard]] std::uint32_t ram_read(std::uint8_t bank, bool post_increment) noexcept;
        void ram_write_postincrement(std::uint8_t bank, std::uint32_t value) noexcept;

        // ---- bus source / dest decoders ----
        [[nodiscard]] std::uint32_t d1_bus_source(std::uint32_t src) noexcept;
        void d1_op_dest_write(std::uint32_t dst, std::uint32_t value) noexcept;
        void mvi_dest_write(std::uint32_t dst, std::uint32_t value) noexcept;

        // ---- execute ----
        void do_alu(std::uint32_t opcode) noexcept;
        void do_op_command(std::uint32_t op) noexcept;
        [[nodiscard]] bool eval_cond(std::uint32_t op) const noexcept;
        void do_mvi(std::uint32_t op) noexcept;
        void do_jmp(std::uint32_t op) noexcept;
        void do_loop(std::uint32_t op) noexcept;
        void do_end(std::uint32_t op) noexcept;
        void do_dma(std::uint32_t op) noexcept;
        [[nodiscard]] std::uint32_t dma_count_source(std::uint32_t source) noexcept;

        // ---- D0-bus access (DMA) ----
        [[nodiscard]] std::uint32_t bus_read32(std::uint32_t addr) noexcept;
        void bus_write32(std::uint32_t addr, std::uint32_t value) noexcept;

        // Program RAM (256 x 32-bit microprogram).
        std::array<std::uint32_t, program_words> program_{};
        // 4 x 64 x 32-bit data RAM banks.
        std::array<std::array<std::uint32_t, ram_words>, ram_banks> ram_{};

        // Register file.
        std::uint32_t rx_{};
        std::uint32_t ry_{};
        std::uint16_t ph_{};
        std::uint32_t pl_{};
        std::uint16_t ach_{};
        std::uint32_t acl_{};
        std::uint16_t alu_h_{};
        std::uint32_t alu_l_{};
        std::array<std::uint8_t, ram_banks> ct_{};
        std::uint16_t lop_{};
        std::uint8_t top_{};
        std::uint8_t pc_{};
        std::uint32_t ra0_{};
        std::uint32_t wa0_{};

        // Flags.
        bool s_flag_{};
        bool z_flag_{};
        bool c_flag_{};
        bool t0_flag_{};     // DMA in progress
        bool loop_active_{}; // a one-instruction loop (LPS) is armed

        // Two-stage fetch/execute pipeline. Each step executes next_instr_ and
        // pre-fetches program[pc]; a taken JMP updates pc immediately, so the
        // already-prefetched slot still executes once (the delay slot) before
        // the fetch lands on the target. pipeline_primed_ defers the first
        // pre-fetch so callers that set pc directly (the port window, tests)
        // execute program[pc] as their first real instruction.
        std::uint32_t next_instr_{};
        std::uint8_t next_pc_{}; // source address of next_instr_ (for trace)
        bool pipeline_primed_{};

        // Port-control state (set by host writes to the PPAF port).
        bool ex_flag_{}; // executing
        bool ep_flag_{}; // paused
        bool es_flag_{}; // single-step armed
        bool end_irq_{}; // E flag, raised by ENDI

        // Port-window latches.
        std::uint8_t prog_write_addr_{};
        std::uint32_t data_addr_reg_{}; // bits 7-6 = bank, 5-0 = offset

        int step_cycles_{}; // master cycles of the instruction in flight
        // tick()'s catch-up loop + cycle_debt_ live in cpu_catch_up.
        friend class cpu_catch_up<scu_dsp>;
        std::uint64_t elapsed_{}; // total master cycles executed

        ibus* bus_{};

        // Per-instruction trace hook installed via the introspection surface.
        std::function<void(std::uint32_t pc)> trace_callback_{};

        std::array<register_descriptor, 20> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
